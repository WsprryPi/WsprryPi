#include "wspr_transmit_backend_si5351.hpp"

#include "wspr_transmit.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <cerrno>
#include <chrono>

#include <time.h>

namespace
{
    static constexpr double kFrequencyMatchToleranceHz = 0.000001;

    static bool is_frequency_event(const wsprrypi::RfEvent& event)
    {
        return event.frequency_hz > 0.0 &&
            (event.rf_on ||
             event.type == wsprrypi::RfEventType::SET_FREQUENCY);
    }

    static std::size_t invalid_tone_index() noexcept
    {
        return std::numeric_limits<std::size_t>::max();
    }

    static void log_si5351(
        IControllerBridge& owner,
        WsprTransmitLogLevel level,
        const std::string& message)
    {
        owner.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            level,
            message,
            0.0);
    }

    static const char *mode_name(wsprrypi::TransmissionMode mode) noexcept
    {
        switch (mode)
        {
            case wsprrypi::TransmissionMode::WSPR:
                return "WSPR";
            case wsprrypi::TransmissionMode::QRSS:
                return "QRSS";
            case wsprrypi::TransmissionMode::FSKCW:
                return "FSKCW";
            case wsprrypi::TransmissionMode::DFCW:
                return "DFCW";
            case wsprrypi::TransmissionMode::CW:
                return "CW";
            case wsprrypi::TransmissionMode::TONE:
                return "TONE";
            case wsprrypi::TransmissionMode::STANDARD_FELD:
                return "STANDARD_FELD";
        }

        return "UNKNOWN";
    }

    static std::string format_frequency(double frequency_hz)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3)
               << frequency_hz << " Hz";
        return stream.str();
    }

    static const char *drive_strength_name(
        Si5351Device::DriveStrength strength) noexcept
    {
        switch (strength)
        {
            case Si5351Device::DriveStrength::MA_2:
                return "2 mA";
            case Si5351Device::DriveStrength::MA_4:
                return "4 mA";
            case Si5351Device::DriveStrength::MA_6:
                return "6 mA";
            case Si5351Device::DriveStrength::MA_8:
                return "8 mA";
        }

        return "unknown";
    }

    static std::string device_error_or(
        const Si5351Device& device,
        const std::string& fallback)
    {
        return device.getLastError().empty() ?
            fallback :
            device.getLastError();
    }

    static timespec add_ns(timespec time, std::int64_t ns)
    {
        const std::int64_t sec = ns / 1000000000LL;
        const std::int64_t rem = ns % 1000000000LL;
        time.tv_sec += sec;
        time.tv_nsec += rem;

        if (time.tv_nsec >= 1000000000L)
        {
            time.tv_sec += 1;
            time.tv_nsec -= 1000000000L;
        }
        else if (time.tv_nsec < 0)
        {
            time.tv_sec -= 1;
            time.tv_nsec += 1000000000L;
        }

        return time;
    }

    static std::int64_t diff_ns(
        const timespec& lhs,
        const timespec& rhs) noexcept
    {
        return (lhs.tv_sec - rhs.tv_sec) * 1000000000LL +
            (lhs.tv_nsec - rhs.tv_nsec);
    }

    static bool same_event(
        const wsprrypi::RfEvent& lhs,
        const wsprrypi::RfEvent& rhs) noexcept
    {
        return lhs.offset_from_start == rhs.offset_from_start &&
            lhs.duration == rhs.duration &&
            lhs.type == rhs.type &&
            lhs.rf_on == rhs.rf_on &&
            lhs.envelope.fade_shape == rhs.envelope.fade_shape &&
            lhs.envelope.fade_in == rhs.envelope.fade_in &&
            lhs.envelope.fade_out == rhs.envelope.fade_out &&
            lhs.envelope.fade_slice == rhs.envelope.fade_slice &&
            std::fabs(lhs.frequency_hz - rhs.frequency_hz) <=
                kFrequencyMatchToleranceHz;
    }

    static double envelope_level_at(
        const wsprrypi::EnvelopeSettings& envelope,
        std::chrono::nanoseconds event_duration,
        std::chrono::nanoseconds offset) noexcept
    {
        if (envelope.fade_shape == wsprrypi::FadeShape::NONE)
            return 1.0;

        auto ramp_level = [&](std::chrono::nanoseconds elapsed,
                              std::chrono::nanoseconds ramp_duration) noexcept
        {
            if (ramp_duration <= std::chrono::nanoseconds::zero())
                return 1.0;

            const double x = std::clamp(
                static_cast<double>(elapsed.count()) /
                    static_cast<double>(ramp_duration.count()),
                0.0,
                1.0);
            return envelope.fade_shape == wsprrypi::FadeShape::RAISED_COSINE
                       ? 0.5 - 0.5 * std::cos(3.14159265358979323846 * x)
                       : x;
        };

        double level = 1.0;
        if (offset < envelope.fade_in)
            level = std::min(level, ramp_level(offset, envelope.fade_in));

        const auto remaining = event_duration - offset;
        if (remaining < envelope.fade_out)
            level = std::min(level, ramp_level(remaining, envelope.fade_out));

        return std::clamp(level, 0.0, 1.0);
    }
}

/**
 * @brief Construct the backend
 *
 * @param owner Controller bridge used for callbacks and stop requests.
 * @param config Backend configuration
 */
WsprSi5351Backend::WsprSi5351Backend(
    IControllerBridge& owner,
    const Config& config)
    : owner_(owner),
      config_(config),
      device_(config.device, config.device_adapter),
      current_plan_(),
      unique_tone_frequencies_(),
      event_tone_indexes_(),
      si5351_plan_(),
      configured_(false),
      execution_cleanup_completed_(false),
      execution_cleanup_result_(),
      stop_requested_(false),
      active_power_level_(1),
      active_drive_strength_(Si5351Device::DriveStrength::MA_2),
      current_tone_index_(invalid_tone_index())
{
    resetActiveDriveStrengthFromConfig();
}

/**
 * @brief Destroy the backend
 */
WsprSi5351Backend::~WsprSi5351Backend()
{
    cleanup();
}

wsprrypi::BackendInfo WsprSi5351Backend::info() const
{
    return wsprrypi::BackendInfo{
        wsprrypi::BackendKind::SI5351,
        "si5351",
        "Si5351A I2C clock-generator backend"};
}

wsprrypi::BackendCapabilities WsprSi5351Backend::capabilities() const
{
    wsprrypi::BackendCapabilities caps;
    caps.output_class = wsprrypi::BackendOutputClass::EXTERNAL_CLOCK_RF;
    caps.supported_modes =
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::WSPR) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::TONE) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::QRSS) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::FSKCW) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::DFCW);
    caps.supports_frequency_switching = true;
    caps.supports_rf_gating = true;
    caps.supports_fade_shape = true;
    caps.supports_precomputed_execution = true;
    caps.min_frequency_hz = 0.0;
    caps.max_frequency_hz = 0.0;
    caps.nominal_frequency_resolution_hz = 0.0;
    return caps;
}

wsprrypi::BackendCompileResult WsprSi5351Backend::configure(
    const wsprrypi::ExecutionPlan& plan,
    const wsprrypi::BackendExecutionInputs& inputs)
{
    resetState();
    current_plan_ = plan;

    {
        std::ostringstream stream;
        stream << "Si5351 configure: mode=" << mode_name(plan.mode)
               << ", events=" << plan.events.size() << ".";
        log_si5351(owner_, WsprTransmitLogLevel::DEBUG, stream.str());
    }

    wsprrypi::BackendCompileResult result;
    result.ok = false;

    const int requested_power_level =
        (inputs.power_level == 0) ? config_.power_level : inputs.power_level;
    if (!mapPowerLevelToDriveStrength(
            requested_power_level,
            active_drive_strength_))
    {
        result.error = "Si5351 power level must be in the range 1..4.";
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }
    active_power_level_ = requested_power_level;

    if (plan.backend != wsprrypi::BackendKind::SI5351)
    {
        result.error = "Execution plan is not targeted for the Si5351 "
                       "backend.";
        return result;
    }

    Si5351Planner::Mode planner_mode = Si5351Planner::Mode::TONE;
    if (!mapPlannerMode(plan.mode, planner_mode))
    {
        result.error = "Execution plan mode is not supported by the "
                       "Si5351 backend.";
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }

    std::string error;
    if (!extractToneFrequencies(
            plan,
            unique_tone_frequencies_,
            event_tone_indexes_,
            error))
    {
        result.error = error;
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }

    const std::size_t expected_tones = expectedToneCount(planner_mode);
    if (unique_tone_frequencies_.size() != expected_tones)
    {
        std::ostringstream stream;
        stream << "Execution plan contains "
               << unique_tone_frequencies_.size()
               << " unique Si5351 tone frequencies, expected "
               << expected_tones << ".";
        result.error = stream.str();
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }

    std::vector<Si5351Planner::ToneEntry> tones;
    tones.reserve(unique_tone_frequencies_.size());
    for (const double frequency_hz : unique_tone_frequencies_)
    {
        tones.push_back(Si5351Planner::ToneEntry{frequency_hz});
    }

    Si5351Planner::Config planner_config = config_.planner;
    planner_config.calibration_ppm = plan.calibration.ppm;
    si5351_plan_ = Si5351Planner(planner_config).buildPlan(
        planner_mode,
        tones);

    {
        std::ostringstream stream;
        stream << "Si5351 calibration: ppm=" << std::fixed
               << std::setprecision(6) << si5351_plan_.calibration_ppm
               << ", nominal reference="
               << format_frequency(planner_config.reference_hz)
               << ", effective reference="
               << format_frequency(si5351_plan_.effective_reference_hz)
               << ".";
        log_si5351(owner_, WsprTransmitLogLevel::DEBUG, stream.str());
    }

    if (!validatePlannerOutput(si5351_plan_, expected_tones, error))
    {
        result.error = error;
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }

    for (std::size_t i = 0; i < si5351_plan_.tone_sets.size(); ++i)
    {
        const Si5351Planner::ToneRegisterSet& tone =
            si5351_plan_.tone_sets[i];
        std::ostringstream stream;
        stream << "Si5351 planner tone " << i << ": requested RF="
               << format_frequency(tone.requested_hz)
               << ", calculated output="
               << format_frequency(tone.actual_hz) << ".";
        log_si5351(owner_, WsprTransmitLogLevel::DEBUG, stream.str());
    }

    log_si5351(
        owner_,
        WsprTransmitLogLevel::DEBUG,
        "Si5351 configure complete.");
    {
        std::ostringstream stream;
        stream << "Si5351 power level " << active_power_level_
               << " selected (" << drive_strength_name(active_drive_strength_)
               << " drive strength).";
        log_si5351(owner_, WsprTransmitLogLevel::DEBUG, stream.str());
    }

    configured_ = true;
    result.ok = true;
    return result;
}

wsprrypi::StartupQuiesceResult WsprSi5351Backend::quiesceForStartup()
{
    wsprrypi::StartupQuiesceResult result;

    if (config_.dry_run)
    {
        log_si5351(
            owner_, WsprTransmitLogLevel::INFO,
            "Si5351 dry-run: startup quiesce skipped without I2C access.");
        result.ok = true;
        return result;
    }

    // Startup quiescence is a direct all-outputs-off operation.  It must not
    // apply planner, drive-strength, output-enable, or PLL programming.
    if (!device_.open())
    {
        result.error = device_error_or(
            device_, "Could not open Si5351 device for startup quiesce.");
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        device_.close();
        return result;
    }

    const bool disabled = device_.disableAllOutputs();
    if (!disabled)
    {
        result.error = device_error_or(
            device_, "Could not disable Si5351 outputs for startup quiesce.");
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
    }
    device_.close();

    result.ok = disabled;
    if (result.ok)
    {
        log_si5351(
            owner_, WsprTransmitLogLevel::DEBUG,
            "Si5351 startup quiesce complete: all outputs disabled.");
    }
    return result;
}

wsprrypi::ExecutionResult WsprSi5351Backend::execute(
    const wsprrypi::ExecutionPlan& plan)
{
    wsprrypi::ExecutionResult result;
    if (!configured_)
    {
        result.error = "Si5351 backend is not configured.";
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }

    if (!planMatchesConfigured(plan))
    {
        result.error = "Execution plan does not match the configured "
                       "Si5351 plan.";
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }

    auto idle_device = [this]() {
        const bool disabled = disableTransmitOutput();
        const bool idle_programmed = applyIdleProgramming();
        device_.close();
        execution_cleanup_completed_ = true;
        execution_cleanup_result_.ok = disabled && idle_programmed;
        execution_cleanup_result_.error.clear();
        if (!disabled)
            execution_cleanup_result_.error =
                "Could not disable Si5351 output.";
        if (!idle_programmed)
        {
            if (!execution_cleanup_result_.error.empty())
                execution_cleanup_result_.error += " ";
            execution_cleanup_result_.error +=
                "Could not apply Si5351 idle programming.";
        }
        log_si5351(
            owner_,
            WsprTransmitLogLevel::DEBUG,
            "Si5351 idle cleanup complete.");
    };

    try
    {
        log_si5351(
            owner_,
            WsprTransmitLogLevel::DEBUG,
            "Si5351 execution starting.");

        if (config_.dry_run)
        {
            log_si5351(
                owner_,
                WsprTransmitLogLevel::INFO,
                "Si5351 dry-run: skipping I2C open and initialization.");
        }
        else if (!device_.open())
        {
            result.error = device_error_or(
                device_,
                "Could not open Si5351 device.");
            log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
            return result;
        }
        else
        {
            log_si5351(
                owner_,
                WsprTransmitLogLevel::DEBUG,
                "Si5351 device opened.");
        }

        if (!config_.dry_run && !device_.initialize())
        {
            result.error = device_error_or(
                device_,
                "Could not initialize Si5351 device.");
            log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
            idle_device();
            return result;
        }
        if (!config_.dry_run)
        {
            log_si5351(
                owner_,
                WsprTransmitLogLevel::DEBUG,
                "Si5351 device initialized.");
        }

        if (!applyStartupProgramming())
        {
            result.error = device_error_or(
                device_,
                "Could not apply Si5351 startup programming.");
            log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
            idle_device();
            return result;
        }
        log_si5351(
            owner_,
            WsprTransmitLogLevel::DEBUG,
            "Si5351 startup programming applied.");

        if (config_.dry_run)
        {
            log_si5351(
                owner_,
                WsprTransmitLogLevel::INFO,
                "Si5351 dry-run: skipping drive-strength programming.");
        }
        else if (!device_.setDriveStrength(
                config_.planner.tx_output,
                active_drive_strength_))
        {
            result.error = device_error_or(
                device_,
                "Could not set Si5351 drive strength.");
            log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
            idle_device();
            return result;
        }
        else
        {
            log_si5351(
                owner_,
                WsprTransmitLogLevel::DEBUG,
                "Si5351 drive strength applied.");
        }

        struct timespec start_time{};
        if (::clock_gettime(CLOCK_MONOTONIC, &start_time) != 0)
        {
            result.error = "Could not read monotonic clock for Si5351 "
                           "execution.";
            log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
            idle_device();
            return result;
        }

        bool rf_enabled = false;
        bool execution_interrupted = false;
        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            if (stop_requested_ || owner_.backendShouldStop())
                break;

            const wsprrypi::RfEvent& event = plan.events[i];
            if (i > 0)
            {
                const timespec target = add_ns(
                    start_time,
                    event.offset_from_start.count());

                while (!stop_requested_ && !owner_.backendShouldStop())
                {
                    timespec now{};
                    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0)
                    {
                        throw std::system_error(
                            errno,
                            std::generic_category(),
                            "clock_gettime");
                    }

                    const std::int64_t remaining_ns = diff_ns(target, now);
                    if (remaining_ns <= 0)
                        break;

                    if (!owner_.backendWaitInterruptableFor(
                            std::chrono::nanoseconds{remaining_ns}))
                    {
                        execution_interrupted = true;
                        break;
                    }
                }
            }

            if (execution_interrupted ||
                stop_requested_ ||
                owner_.backendShouldStop())
                break;

            owner_.backendReportExecutionProgress(i);

            if (i < event_tone_indexes_.size() &&
                event_tone_indexes_[i] != invalid_tone_index() &&
                event_tone_indexes_[i] != current_tone_index_)
            {
                if (!applyTone(event_tone_indexes_[i], rf_enabled))
                {
                    if (stop_requested_ || owner_.backendShouldStop())
                    {
                        execution_interrupted = true;
                        break;
                    }

                    result.error = device_error_or(
                        device_,
                        "Could not apply Si5351 tone programming.");
                    log_si5351(
                        owner_,
                        WsprTransmitLogLevel::ERROR,
                        result.error);
                    idle_device();
                    return result;
                }
            }

            if (!runEnvelopeEvent(event, rf_enabled, result.error))
            {
                log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
                idle_device();
                return result;
            }
        }

        if (!execution_interrupted &&
            !stop_requested_ &&
            !owner_.backendShouldStop() &&
            !plan.events.empty())
        {
            const wsprrypi::RfEvent& last = plan.events.back();
            const timespec end_target = add_ns(
                start_time,
                (last.offset_from_start + last.duration).count());

            while (!stop_requested_ && !owner_.backendShouldStop())
            {
                timespec now{};
                if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0)
                {
                    throw std::system_error(
                        errno,
                        std::generic_category(),
                        "clock_gettime");
                }

                const std::int64_t remaining_ns = diff_ns(end_target, now);
                if (remaining_ns <= 0)
                    break;

                if (!owner_.backendWaitInterruptableFor(
                        std::chrono::nanoseconds{remaining_ns}))
                {
                    execution_interrupted = true;
                    break;
                }
            }
        }

        if (execution_interrupted ||
            stop_requested_ ||
            owner_.backendShouldStop())
        {
            log_si5351(
                owner_,
                WsprTransmitLogLevel::DEBUG,
                "Si5351 execution stopped early.");
        }

        idle_device();
        result.ok = true;
        result.stopped = execution_interrupted ||
            stop_requested_ ||
            owner_.backendShouldStop();
        return result;
    }
    catch (const std::exception& e)
    {
        idle_device();
        result.faulted = true;
        result.error = e.what();
        log_si5351(owner_, WsprTransmitLogLevel::ERROR, result.error);
        return result;
    }
}

bool WsprSi5351Backend::runEnvelopeEvent(
    const wsprrypi::RfEvent& event,
    bool& rf_enabled,
    std::string& error)
{
    auto enable_output = [&]() -> bool
    {
        if (rf_enabled)
            return true;

        if (!enableTransmitOutput())
        {
            error = device_error_or(
                device_,
                "Could not enable Si5351 transmit output.");
            return false;
        }

        rf_enabled = true;
        return true;
    };

    auto disable_output = [&]() -> bool
    {
        if (!rf_enabled)
            return true;

        if (!disableTransmitOutput())
        {
            error = device_error_or(
                device_,
                "Could not disable Si5351 transmit output.");
            return false;
        }

        rf_enabled = false;
        return true;
    };

    if (!event.rf_on)
    {
        return disable_output();
    }

    if (event.envelope.fade_shape == wsprrypi::FadeShape::NONE ||
        (event.envelope.fade_in <= std::chrono::nanoseconds::zero() &&
         event.envelope.fade_out <= std::chrono::nanoseconds::zero()))
    {
        return enable_output();
    }

    const auto slice_limit =
        event.envelope.fade_slice > std::chrono::nanoseconds::zero()
            ? event.envelope.fade_slice
            : std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::milliseconds(5));
    std::chrono::nanoseconds elapsed{0};
    while (elapsed < event.duration &&
           !stop_requested_ &&
           !owner_.backendShouldStop())
    {
        const auto remaining = event.duration - elapsed;
        const auto slice = std::min(remaining, slice_limit);
        const auto midpoint = elapsed + slice / 2;
        const double level =
            envelope_level_at(event.envelope, event.duration, midpoint);
        const auto on_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double, std::nano>(
                static_cast<double>(slice.count()) * level));
        const auto off_duration = slice - on_duration;

        if (on_duration > std::chrono::nanoseconds::zero())
        {
            if (!enable_output())
                return false;

            if (!owner_.backendWaitInterruptableFor(on_duration))
                return true;
        }

        if (off_duration > std::chrono::nanoseconds::zero())
        {
            if (!disable_output())
                return false;

            if (!owner_.backendWaitInterruptableFor(off_duration))
                return true;
        }

        elapsed += slice;
    }

    return true;
}

void WsprSi5351Backend::stop() noexcept
{
    stop_requested_ = true;
    log_si5351(
        owner_,
        WsprTransmitLogLevel::DEBUG,
        "Si5351 stop requested.");
    owner_.backendRequestStopTxNoJoin();
}

wsprrypi::CleanupResult WsprSi5351Backend::cleanup() noexcept
{
    if (execution_cleanup_completed_)
    {
        const wsprrypi::CleanupResult result = execution_cleanup_result_;
        resetState();
        return result;
    }

    if (!configured_)
    {
        device_.close();
        return {true, {}};
    }

    const bool disabled = disableTransmitOutput();
    device_.close();
    wsprrypi::CleanupResult result{
        disabled, disabled ? std::string{} : "Could not disable Si5351 output."};
    if (configured_)
    {
        log_si5351(
            owner_,
            WsprTransmitLogLevel::DEBUG,
            "Si5351 cleanup requested.");
    }
    resetState();
    return result;
}

const WsprSi5351Backend::Config&
WsprSi5351Backend::getConfig() const noexcept
{
    return config_;
}

bool WsprSi5351Backend::mapPlannerMode(
    wsprrypi::TransmissionMode mode,
    Si5351Planner::Mode& planner_mode) const
{
    switch (mode)
    {
        case wsprrypi::TransmissionMode::TONE:
            planner_mode = Si5351Planner::Mode::TONE;
            return true;
        case wsprrypi::TransmissionMode::QRSS:
            planner_mode = Si5351Planner::Mode::QRSS;
            return true;
        case wsprrypi::TransmissionMode::FSKCW:
            planner_mode = Si5351Planner::Mode::FSKCW;
            return true;
        case wsprrypi::TransmissionMode::DFCW:
            planner_mode = Si5351Planner::Mode::DFCW;
            return true;
        case wsprrypi::TransmissionMode::WSPR:
            planner_mode = Si5351Planner::Mode::WSPR;
            return true;
        case wsprrypi::TransmissionMode::CW:
        case wsprrypi::TransmissionMode::STANDARD_FELD:
            return false;
    }

    return false;
}

bool WsprSi5351Backend::extractToneFrequencies(
    const wsprrypi::ExecutionPlan& plan,
    std::vector<double>& frequencies,
    std::vector<std::size_t>& event_tone_indexes,
    std::string& error) const
{
    frequencies.clear();
    event_tone_indexes.assign(plan.events.size(), invalid_tone_index());

    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const wsprrypi::RfEvent& event = plan.events[i];
        if (!is_frequency_event(event))
            continue;

        if (!std::isfinite(event.frequency_hz))
        {
            std::ostringstream stream;
            stream << "Execution plan event " << i
                   << " has an invalid Si5351 frequency.";
            error = stream.str();
            return false;
        }

        std::size_t tone_index = invalid_tone_index();
        for (std::size_t j = 0; j < frequencies.size(); ++j)
        {
            const double delta_hz =
                std::fabs(frequencies[j] - event.frequency_hz);
            if (delta_hz <= kFrequencyMatchToleranceHz)
            {
                tone_index = j;
                break;
            }
        }

        if (tone_index == invalid_tone_index())
        {
            tone_index = frequencies.size();
            frequencies.push_back(event.frequency_hz);
        }

        event_tone_indexes[i] = tone_index;
    }

    if (frequencies.empty())
    {
        error = "Execution plan contains no usable Si5351 tone "
                "frequencies.";
        return false;
    }

    return true;
}

bool WsprSi5351Backend::validatePlannerOutput(
    const Si5351Planner::Plan& plan,
    std::size_t expected_tones,
    std::string& error) const
{
    if (!std::isfinite(plan.calibration_ppm) ||
        plan.effective_reference_hz <= 0.0 ||
        !std::isfinite(plan.effective_reference_hz))
    {
        error = "Si5351 calibration PPM is non-finite or outside the "
                "usable range.";
        return false;
    }

    if (plan.startup_writes.empty())
    {
        error = "Si5351 planner produced no startup register writes.";
        return false;
    }

    if (plan.tone_sets.size() != expected_tones)
    {
        std::ostringstream stream;
        stream << "Si5351 planner produced " << plan.tone_sets.size()
               << " tone register sets, expected " << expected_tones
               << ".";
        error = stream.str();
        return false;
    }

    for (std::size_t i = 0; i < plan.tone_sets.size(); ++i)
    {
        const Si5351Planner::ToneRegisterSet& tone = plan.tone_sets[i];
        if (tone.requested_hz <= 0.0 || tone.actual_hz <= 0.0)
        {
            std::ostringstream stream;
            stream << "Si5351 planner produced unusable frequency data "
                   << "for tone " << i << ".";
            error = stream.str();
            return false;
        }

        if (tone.writes.empty())
        {
            std::ostringstream stream;
            stream << "Si5351 planner produced no register writes for "
                   << "tone " << i << ".";
            error = stream.str();
            return false;
        }
    }

    return true;
}

bool WsprSi5351Backend::planMatchesConfigured(
    const wsprrypi::ExecutionPlan& plan) const noexcept
{
    if (plan.id.value != current_plan_.id.value ||
        plan.request_id.value != current_plan_.request_id.value ||
        plan.mode != current_plan_.mode ||
        plan.backend != current_plan_.backend ||
        plan.calibration.ppm != current_plan_.calibration.ppm ||
        plan.events.size() != current_plan_.events.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        if (!same_event(plan.events[i], current_plan_.events[i]))
            return false;
    }

    return true;
}

bool WsprSi5351Backend::mapPowerLevelToDriveStrength(
    int power_level,
    Si5351Device::DriveStrength& drive_strength) const noexcept
{
    switch (power_level)
    {
        case 1:
            drive_strength = Si5351Device::DriveStrength::MA_2;
            return true;
        case 2:
            drive_strength = Si5351Device::DriveStrength::MA_4;
            return true;
        case 3:
            drive_strength = Si5351Device::DriveStrength::MA_6;
            return true;
        case 4:
            drive_strength = Si5351Device::DriveStrength::MA_8;
            return true;
    }

    return false;
}

void WsprSi5351Backend::resetActiveDriveStrengthFromConfig() noexcept
{
    active_power_level_ = config_.power_level;
    if (!mapPowerLevelToDriveStrength(
            active_power_level_,
            active_drive_strength_))
    {
        active_drive_strength_ = Si5351Device::DriveStrength::MA_2;
    }
}

std::size_t WsprSi5351Backend::expectedToneCount(
    Si5351Planner::Mode mode) const noexcept
{
    switch (mode)
    {
        case Si5351Planner::Mode::TONE:
        case Si5351Planner::Mode::QRSS:
            return 1;
        case Si5351Planner::Mode::FSKCW:
        case Si5351Planner::Mode::DFCW:
            return 2;
        case Si5351Planner::Mode::WSPR:
            return 4;
    }

    return 0;
}

void WsprSi5351Backend::resetState()
{
    current_plan_ = wsprrypi::ExecutionPlan{};
    unique_tone_frequencies_.clear();
    event_tone_indexes_.clear();
    si5351_plan_ = Si5351Planner::Plan{};
    configured_ = false;
    execution_cleanup_completed_ = false;
    execution_cleanup_result_ = wsprrypi::CleanupResult{};
    stop_requested_ = false;
    resetActiveDriveStrengthFromConfig();
    current_tone_index_ = invalid_tone_index();
}

bool WsprSi5351Backend::applyStartupProgramming()
{
    if (config_.dry_run)
    {
        log_si5351(
            owner_,
            WsprTransmitLogLevel::INFO,
            "Si5351 dry-run: startup register writes skipped.");
        return true;
    }

    return device_.writeRegisters(si5351_plan_.startup_writes);
}

bool WsprSi5351Backend::applyIdleProgramming()
{
    if (config_.dry_run)
        return true;

    return device_.writeRegisters(si5351_plan_.idle_writes);
}

bool WsprSi5351Backend::applyTone(
    std::size_t tone_index,
    bool rf_enabled)
{
    if (tone_index >= si5351_plan_.tone_sets.size())
        return false;

    const Si5351Planner::ToneRegisterSet& tone =
        si5351_plan_.tone_sets[tone_index];
    if (tone.writes.empty())
        return false;

    if (stop_requested_ || owner_.backendShouldStop())
        return false;

    if (!config_.dry_run)
    {
        if (tone.requires_output_inhibit && !disableTransmitOutput())
            return false;

        if (stop_requested_ || owner_.backendShouldStop())
            return false;

        for (const Si5351Device::RegisterWrite& write : tone.writes)
        {
            if (stop_requested_ || owner_.backendShouldStop())
                return false;

            if (!device_.writeRegister(write.address, write.value))
                return false;

            if (stop_requested_ || owner_.backendShouldStop())
                return false;
        }

        if (tone.requires_output_inhibit &&
            rf_enabled &&
            !enableTransmitOutput())
        {
            return false;
        }
    }

    current_tone_index_ = tone_index;
    {
        std::ostringstream stream;
        stream << "Si5351 tone " << tone_index << ": requested RF="
               << format_frequency(tone.requested_hz)
               << ", calculated output="
               << format_frequency(tone.actual_hz) << ".";
        log_si5351(owner_, WsprTransmitLogLevel::DEBUG, stream.str());
    }
    return true;
}

bool WsprSi5351Backend::enableTransmitOutput()
{
    if (config_.dry_run)
        return true;

    return device_.enableOutput(config_.planner.tx_output);
}

bool WsprSi5351Backend::disableTransmitOutput()
{
    if (config_.dry_run)
        return true;

    return device_.disableOutput(config_.planner.tx_output);
}
