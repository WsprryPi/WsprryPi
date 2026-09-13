/**
 * @file non_wspr_request_builder.cpp
 * @brief Builds requests for QRSS, FSKCW, and DFCW scheduling.
 */

#include "non_wspr_request_builder.hpp"

#include "legacy_gpio_clock_model.hpp"
#include "execution_plan_compiler.hpp"
#include "version.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <optional>
#include <sstream>

namespace scheduling_detail
{
namespace
{
wsprrypi::BackendKind to_controller_backend(
    TransmitBackendKind backend) noexcept
{
    if (backend == TransmitBackendKind::WTP)
        return wsprrypi::BackendKind::WTP;
    if (backend == TransmitBackendKind::SI5351)
        return wsprrypi::BackendKind::SI5351;
    if (backend == TransmitBackendKind::SIMULATED)
        return wsprrypi::BackendKind::SIMULATED;
    if (backend == TransmitBackendKind::RP1_GPCLK)
        return wsprrypi::BackendKind::RP1_GPCLK;
    return wsprrypi::BackendKind::RPI_CLOCK_GPIO;
}
wsprrypi::ClockSource to_controller_clock_source(
    const ArgParserConfig &cfg) noexcept
{
    if (cfg.transmit_backend == TransmitBackendKind::WTP)
        return wsprrypi::ClockSource::UNSPECIFIED;
    if (cfg.transmit_backend != TransmitBackendKind::SI5351)
        return wsprrypi::ClockSource::GPIO_CLK;

    switch (cfg.si5351_tx_output)
    {
    case 1: return wsprrypi::ClockSource::SI5351_CLK1;
    case 2: return wsprrypi::ClockSource::SI5351_CLK2;
    default: return wsprrypi::ClockSource::SI5351_CLK0;
    }
}

std::optional<wsprrypi::LegacyGpioProcessorProfile>
resolve_legacy_gpio_processor_profile() noexcept
{
    const int generation = get_raspberry_pi_generation();
    if (generation == 1)
        return wsprrypi::LegacyGpioProcessorProfile::Bcm2835;
    if (generation == 2 || generation == 3)
        return wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837;
    if (generation == 4)
        return wsprrypi::LegacyGpioProcessorProfile::Bcm2711;
    return std::nullopt;
}

wsprrypi::HardwareProfile to_controller_profile(
    TransmitBackendKind backend) noexcept
{
    if (backend == TransmitBackendKind::SI5351)
        return wsprrypi::HardwareProfile::SI5351;
    if (backend == TransmitBackendKind::SIMULATED || backend == TransmitBackendKind::WTP)
        return wsprrypi::HardwareProfile::UNSPECIFIED;
    if (backend == TransmitBackendKind::RP1_GPCLK)
        return wsprrypi::HardwareProfile::RP1_GPCLK;
    const auto processor = resolve_legacy_gpio_processor_profile();
    if (processor.has_value())
        return wsprrypi::legacyHardwareProfile(*processor);
    return wsprrypi::HardwareProfile::UNSPECIFIED;
}
}

static std::chrono::nanoseconds seconds_to_nanoseconds(double seconds)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(seconds));
}

static wsprrypi::FadeShape cw_fade_shape_from_config(const std::string &shape)
{
    if (shape == "linear")
    {
        return wsprrypi::FadeShape::LINEAR;
    }

    if (shape == "raised_cosine")
    {
        return wsprrypi::FadeShape::RAISED_COSINE;
    }

    return wsprrypi::FadeShape::NONE;
}

static void validate_wtp_timing(double dot, const ArgParserConfig& cfg, bool dfcw) {
    if (cfg.transmit_backend != TransmitBackendKind::WTP) return;
    const double maximum = std::nextafter(
        static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 1e9, 0.0);
    const double values[] = {dot, dfcw ? dot : dot * 3,
        dot * (dfcw ? cfg.dfcw_intra_element_gap : cfg.cw_intra_element_gap),
        dot * (dfcw ? cfg.dfcw_inter_character_gap : cfg.cw_inter_character_gap),
        dot * (dfcw ? cfg.dfcw_inter_word_gap : cfg.cw_inter_word_gap)};
    for (double seconds : values)
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > maximum)
            throw std::runtime_error("Pico message timing exceeds finite nanosecond accounting.");
}

static wsprrypi::MorseTiming cw_timing_from_config(
    double dot_seconds,
    const ArgParserConfig &cfg)
{
    validate_wtp_timing(dot_seconds, cfg, false);
    wsprrypi::MorseTiming timing;
    timing.dot = seconds_to_nanoseconds(dot_seconds);
    timing.dash = timing.dot * 3;
    timing.intra_element_gap =
        seconds_to_nanoseconds(dot_seconds * cfg.cw_intra_element_gap);
    timing.inter_character_gap =
        seconds_to_nanoseconds(dot_seconds * cfg.cw_inter_character_gap);
    timing.inter_word_gap =
        seconds_to_nanoseconds(dot_seconds * cfg.cw_inter_word_gap);
    return timing;
}

static wsprrypi::MorseTiming dfcw_timing_from_config(
    double dot_seconds,
    const ArgParserConfig &cfg)
{
    validate_wtp_timing(dot_seconds, cfg, true);
    wsprrypi::MorseTiming timing;
    timing.dot = seconds_to_nanoseconds(dot_seconds);
    timing.dash = timing.dot;
    timing.intra_element_gap =
        seconds_to_nanoseconds(dot_seconds * cfg.dfcw_intra_element_gap);
    timing.inter_character_gap =
        seconds_to_nanoseconds(dot_seconds * cfg.dfcw_inter_character_gap);
    timing.inter_word_gap =
        seconds_to_nanoseconds(dot_seconds * cfg.dfcw_inter_word_gap);
    return timing;
}

static wsprrypi::EnvelopeSettings cw_envelope_from_config(
    const ArgParserConfig &cfg)
{
    wsprrypi::EnvelopeSettings envelope;
    envelope.fade_shape = cw_fade_shape_from_config(cfg.cw_fade_shape);
    envelope.fade_in = std::chrono::milliseconds(cfg.cw_fade_in_ms);
    envelope.fade_out = std::chrono::milliseconds(cfg.cw_fade_out_ms);
    envelope.fade_slice = std::chrono::milliseconds(cfg.cw_fade_slice_ms);
    return envelope;
}

static std::string format_policy_duration(
    std::chrono::nanoseconds duration)
{
    const double total_seconds =
        std::chrono::duration<double>(duration).count();
    const auto total_whole_seconds =
        static_cast<long long>(std::llround(total_seconds));
    if (std::fabs(total_seconds - static_cast<double>(total_whole_seconds)) <
        0.0005)
    {
        const long long minutes = total_whole_seconds / 60;
        const long long seconds = total_whole_seconds % 60;
        std::ostringstream oss;
        oss << minutes << "m " << std::setw(2) << std::setfill('0') << seconds
            << "s";
        return oss.str();
    }

    const long long minutes = static_cast<long long>(total_seconds / 60.0);
    const double seconds = total_seconds - static_cast<double>(minutes * 60);
    std::ostringstream oss;
    oss << minutes << "m "
        << std::fixed << std::setprecision(3) << std::setw(6)
        << std::setfill('0') << seconds << "s";
    return oss.str();
}

wsprrypi::TransmissionRequest make_qrss_controller_request(
    const ArgParserConfig &cfg,
    double committed_ppm)
{
    wsprrypi::TransmissionRequest request;
    request.id.value = 1;
    request.mode = wsprrypi::TransmissionMode::QRSS;
    request.output.backend = to_controller_backend(cfg.transmit_backend);
    request.output.output = to_controller_clock_source(cfg);
    request.output.gpio = cfg.tx_pin;
    request.calibration.ppm = committed_ppm;
    request.policy.allow_unqualified_frequency = cfg.allow_unqualified_frequency;
    request.policy.allow_non_amateur_frequency = cfg.allow_non_amateur_frequency;
    request.policy.hardware_profile = to_controller_profile(cfg.transmit_backend);
    request.metadata.label = "qrss-cli-test";
    request.metadata.origin = "cli";
    request.metadata.note = "temporary qrss test path";

    wsprrypi::QrssPayload payload;
    payload.message = cfg.qrss.message;
    payload.frequency_hz = cfg.qrss.frequency_hz;
    payload.timing = cw_timing_from_config(cfg.qrss.dot_seconds, cfg);
    payload.envelope = cw_envelope_from_config(cfg);
    request.payload = payload;
    return request;
}

TransmissionRequest make_qrss_legacy_request(
    const ArgParserConfig &cfg,
    double committed_ppm)
{
    TransmissionRequest request;
    request.mode = TransmissionMode::WSPR;
    request.dial_frequency_hz = cfg.qrss.frequency_hz;
    request.actual_rf_frequency_hz = cfg.qrss.frequency_hz;
    request.ppm = committed_ppm;
    request.power_level =
        cfg.transmit_backend == TransmitBackendKind::RP1_GPCLK
            ? cfg.rp1_gpio_drive_ma
            : cfg.power_level;
    request.tx_gpio = cfg.tx_pin;
    request.frequency_entry_label = "qrss-cli-test";
    return request;
}

wsprrypi::TransmissionRequest make_fskcw_controller_request(
    const ArgParserConfig &cfg,
    double committed_ppm)
{
    wsprrypi::TransmissionRequest request;
    request.id.value = 1;
    request.mode = wsprrypi::TransmissionMode::FSKCW;
    request.output.backend = to_controller_backend(cfg.transmit_backend);
    request.output.output = to_controller_clock_source(cfg);
    request.output.gpio = cfg.tx_pin;
    request.calibration.ppm = committed_ppm;
    request.policy.allow_unqualified_frequency = cfg.allow_unqualified_frequency;
    request.policy.allow_non_amateur_frequency = cfg.allow_non_amateur_frequency;
    request.policy.hardware_profile = to_controller_profile(cfg.transmit_backend);
    request.metadata.label = "fskcw-cli-test";
    request.metadata.origin = "cli";
    request.metadata.note = "temporary fskcw test path";

    wsprrypi::FskcwPayload payload;
    payload.message = cfg.fskcw.message;
    payload.mark_frequency_hz = cfg.fskcw.mark_frequency_hz;
    payload.space_frequency_hz = cfg.fskcw.space_frequency_hz;
    payload.timing = cw_timing_from_config(cfg.fskcw.dot_seconds, cfg);
    payload.envelope = cw_envelope_from_config(cfg);
    request.payload = payload;
    return request;
}

TransmissionRequest make_fskcw_legacy_request(
    const ArgParserConfig &cfg,
    double committed_ppm)
{
    TransmissionRequest request;
    request.mode = TransmissionMode::WSPR;
    request.dial_frequency_hz = cfg.fskcw.mark_frequency_hz;
    request.actual_rf_frequency_hz = cfg.fskcw.mark_frequency_hz;
    request.ppm = committed_ppm;
    request.power_level =
        cfg.transmit_backend == TransmitBackendKind::RP1_GPCLK
            ? cfg.rp1_gpio_drive_ma
            : cfg.power_level;
    request.tx_gpio = cfg.tx_pin;
    request.applied_offset_hz =
        cfg.fskcw.mark_frequency_hz - cfg.fskcw.space_frequency_hz;
    request.frequency_entry_label = "fskcw-cli-test";
    return request;
}

wsprrypi::TransmissionRequest make_dfcw_controller_request(
    const ArgParserConfig &cfg,
    double committed_ppm)
{
    wsprrypi::TransmissionRequest request;
    request.id.value = 1;
    request.mode = wsprrypi::TransmissionMode::DFCW;
    request.output.backend = to_controller_backend(cfg.transmit_backend);
    request.output.output = to_controller_clock_source(cfg);
    request.output.gpio = cfg.tx_pin;
    request.calibration.ppm = committed_ppm;
    request.policy.allow_unqualified_frequency = cfg.allow_unqualified_frequency;
    request.policy.allow_non_amateur_frequency = cfg.allow_non_amateur_frequency;
    request.policy.hardware_profile = to_controller_profile(cfg.transmit_backend);
    request.metadata.label = "dfcw-cli-test";
    request.metadata.origin = "cli";
    request.metadata.note = "temporary dfcw test path";

    wsprrypi::DfcwPayload payload;
    payload.message = cfg.dfcw.message;
    payload.dot_frequency_hz = cfg.dfcw.dot_frequency_hz;
    payload.dash_frequency_hz = cfg.dfcw.dash_frequency_hz;
    payload.timing = dfcw_timing_from_config(cfg.dfcw.dot_seconds, cfg);
    payload.envelope = cw_envelope_from_config(cfg);
    request.payload = payload;
    return request;
}

TransmissionRequest make_dfcw_legacy_request(
    const ArgParserConfig &cfg,
    double committed_ppm)
{
    TransmissionRequest request;
    request.mode = TransmissionMode::WSPR;
    request.dial_frequency_hz = cfg.dfcw.dot_frequency_hz;
    request.actual_rf_frequency_hz = cfg.dfcw.dot_frequency_hz;
    request.ppm = committed_ppm;
    request.power_level =
        cfg.transmit_backend == TransmitBackendKind::RP1_GPCLK
            ? cfg.rp1_gpio_drive_ma
            : cfg.power_level;
    request.tx_gpio = cfg.tx_pin;
    request.applied_offset_hz =
        cfg.dfcw.dash_frequency_hz - cfg.dfcw.dot_frequency_hz;
    request.frequency_entry_label = "dfcw-cli-test";
    return request;
}
}

namespace
{
bool is_non_wspr_runtime_mode(ModeType mode) noexcept
{
    return mode == ModeType::QRSS ||
        mode == ModeType::FSKCW ||
        mode == ModeType::DFCW;
}

const char *mode_type_name(ModeType mode) noexcept
{
    switch (mode)
    {
    case ModeType::WSPR: return "WSPR";
    case ModeType::TONE: return "TONE";
    case ModeType::QRSS: return "QRSS";
    case ModeType::FSKCW: return "FSKCW";
    case ModeType::DFCW: return "DFCW";
    }
    return "UNKNOWN";
}
}

bool compute_non_wspr_message_duration(
    const ArgParserConfig &cfg,
    std::chrono::nanoseconds &duration_out,
    std::string *error_message)
{
    try
    {
        wsprrypi::TransmissionRequest request;
        if (cfg.mode == ModeType::QRSS)
        {
            request = scheduling_detail::make_qrss_controller_request(cfg, cfg.ppm);
        }
        else if (cfg.mode == ModeType::FSKCW)
        {
            request = scheduling_detail::make_fskcw_controller_request(cfg, cfg.ppm);
        }
        else if (cfg.mode == ModeType::DFCW)
        {
            request = scheduling_detail::make_dfcw_controller_request(cfg, cfg.ppm);
        }
        else
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Timed-message duration is only available for QRSS, FSKCW, and DFCW modes.";
            }
            return false;
        }

        const wsprrypi::ExecutionPlanCompiler compiler;
        duration_out = compiler.compile(request).summary.total_duration;
        return true;
    }
    catch (const std::exception &e)
    {
        if (error_message != nullptr)
        {
            *error_message = e.what();
        }
        return false;
    }
}

bool validate_non_wspr_repeat_interval_policy(
    const ArgParserConfig &cfg,
    std::string *error_message)
{
    if (!is_non_wspr_runtime_mode(cfg.mode) || cfg.schedule_repeat_minutes <= 0)
    {
        return true;
    }

    std::chrono::nanoseconds message_duration{};
    if (!compute_non_wspr_message_duration(cfg, message_duration, error_message))
    {
        return false;
    }

    const auto repeat_interval =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::minutes(cfg.schedule_repeat_minutes));
    if (message_duration <= repeat_interval)
    {
        return true;
    }

    if (error_message != nullptr)
    {
        *error_message =
            "Configured " +
            std::string(mode_type_name(cfg.mode)) +
            " message duration of " +
            scheduling_detail::format_policy_duration(message_duration) +
            " exceeds repeat_every interval of " +
            scheduling_detail::format_policy_duration(repeat_interval) +
            ". Reduce the message length, shorten the unit length, or increase repeat_every.";
    }
    return false;
}
