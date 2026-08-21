/**
 * @file wspr_transmit.cpp
 * @brief Transmitter implementation for executing committed requests.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// C++ standard library headers
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

// POSIX and system headers
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

// Project headers
#include "wspr_transmit.hpp" // Class Declarations
#include "backend_capabilities.hpp"
#include "gpio_band_policy.hpp"
#include "thread_affinity.hpp"
#if WSPRRYPI_BACKEND_RP1_GPCLK
#include "rp1_gpclk_transmit_backend.hpp"
#endif
#if WSPRRYPI_BACKEND_RPI_GPIO
#include "wspr_transmit_backend_rpi.hpp"
#endif
#if WSPRRYPI_BACKEND_SI5351
#include "wspr_transmit_backend_si5351.hpp"
#endif
#if WSPRRYPI_BACKEND_SIMULATED
#include "simulated_transmit_backend.hpp"
#endif

// Helper classes and functions in anonymous namespace
namespace
{
    std::string current_cw_message_for_payload(
        const wsprrypi::TransmissionPayload &payload)
    {
        return std::visit(
            [](const auto &variant_payload) -> std::string
            {
                using PayloadT = std::decay_t<decltype(variant_payload)>;

                if constexpr (std::is_same_v<PayloadT, wsprrypi::QrssPayload> ||
                              std::is_same_v<PayloadT, wsprrypi::FskcwPayload> ||
                              std::is_same_v<PayloadT, wsprrypi::DfcwPayload> ||
                              std::is_same_v<PayloadT, wsprrypi::CwPayload>)
                {
                    return variant_payload.message;
                }

                return std::string{};
            },
            payload);
    }

    const char *backend_kind_name(wsprrypi::BackendKind backend) noexcept
    {
        switch (backend)
        {
        case wsprrypi::BackendKind::RPI_CLOCK_GPIO:
            return "GPIO";
        case wsprrypi::BackendKind::RP1_GPCLK:
            return "RP1 GPCLK";
        case wsprrypi::BackendKind::SI5351:
            return "SI5351";
        case wsprrypi::BackendKind::SIMULATED:
            return "simulated";
        }

        return "unknown";
    }

    static inline int cpu_count() noexcept
    {
        long n = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1)
            return 1;
        if (n > INT32_MAX)
            return INT32_MAX;
        return static_cast<int>(n);
    }

    static inline int clamp_cpu(int cpu, int ncpu) noexcept
    {
        if (ncpu <= 1)
            return 0;
        if (cpu < 0)
            return 0;
        if (cpu >= ncpu)
            return ncpu - 1;
        return cpu;
    }

#if WSPRRYPI_BACKEND_SI5351
    static Si5351Device::Output si5351_output_from_index(int output) noexcept
    {
        switch (output)
        {
            case 1:
                return Si5351Device::Output::CLK1;
            case 2:
                return Si5351Device::Output::CLK2;
            case 0:
            default:
                return Si5351Device::Output::CLK0;
        }
    }
#endif

    bool backend_is_compiled(wsprrypi::BackendKind backend) noexcept
    {
        switch (backend)
        {
        case wsprrypi::BackendKind::RPI_CLOCK_GPIO:
            return WSPRRYPI_BACKEND_RPI_GPIO;
        case wsprrypi::BackendKind::RP1_GPCLK:
            return WSPRRYPI_BACKEND_RP1_GPCLK;
        case wsprrypi::BackendKind::SI5351:
            return WSPRRYPI_BACKEND_SI5351;
        case wsprrypi::BackendKind::SIMULATED:
            return WSPRRYPI_BACKEND_SIMULATED;
        }
        return false;
    }

    wsprrypi::BackendKind initial_compiled_backend() noexcept
    {
#if WSPRRYPI_BACKEND_RPI_GPIO
        return wsprrypi::BackendKind::RPI_CLOCK_GPIO;
#elif WSPRRYPI_BACKEND_RP1_GPCLK
        return wsprrypi::BackendKind::RP1_GPCLK;
#elif WSPRRYPI_BACKEND_SI5351
        return wsprrypi::BackendKind::SI5351;
#else
        static_assert(WSPRRYPI_BACKEND_SIMULATED, "at least one backend is required");
        return wsprrypi::BackendKind::SIMULATED;
#endif
    }

    static wsprrypi::ClockSource si5351_clock_source_from_index(int output) noexcept
    {
        switch (output)
        {
            case 1:
                return wsprrypi::ClockSource::SI5351_CLK1;
            case 2:
                return wsprrypi::ClockSource::SI5351_CLK2;
            case 0:
            default:
                return wsprrypi::ClockSource::SI5351_CLK0;
        }
    }

    static inline int64_t diff_ns(const timespec &a, const timespec &b)
    {
        return (a.tv_sec - b.tv_sec) * 1'000'000'000LL + (a.tv_nsec - b.tv_nsec);
    }

} // end anonymous namespace

WsprTransmitter::TransmissionScheduler::TransmissionScheduler(
    WsprTransmitter *parent)
    : parent_{parent}
{
}

WsprTransmitter::TransmissionScheduler::~TransmissionScheduler()
{
    stop();
}

void WsprTransmitter::TransmissionScheduler::start()
{
    if (thread_.joinable())
        return;

    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread(&TransmissionScheduler::run, this);
}

void WsprTransmitter::TransmissionScheduler::stop()
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_requested_.store(true, std::memory_order_release);
    }
    cv_.notify_all();

    if (thread_.joinable() &&
        thread_.get_id() != std::this_thread::get_id())
    {
        thread_.join();
    }
}

void WsprTransmitter::TransmissionScheduler::notify() noexcept
{
    cv_.notify_all();
}

std::chrono::system_clock::time_point
WsprTransmitter::TransmissionScheduler::nextEvent() const
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto secs = duration_cast<seconds>(now.time_since_epoch()).count();

    const int cycle = 2 * 60;

    auto idx = secs / cycle;

    auto base = idx * cycle;
    seconds target_secs;
    if (secs < base + 1)
    {
        target_secs = seconds{base + 1};
    }
    else
    {
        target_secs = seconds{(idx + 1) * cycle + 1};
    }

    return system_clock::time_point{target_secs};
}

void WsprTransmitter::TransmissionScheduler::run()
{
    std::chrono::system_clock::time_point last_when{};

    while (!stop_requested_.load(std::memory_order_acquire) &&
           !parent_->soft_off_.load(std::memory_order_acquire))
    {
        if (parent_->external_stop_flag_ &&
            parent_->external_stop_flag_->load(std::memory_order_acquire))
        {
            break;
        }

        auto when = nextEvent();

        // Never reuse the same WSPR window twice. This matters most for
        // zero-frequency skip windows, which complete almost immediately at
        // the boundary. Without remembering the last scheduled boundary, the
        // scheduler can loop fast enough to compute the same window again and
        // emit a second completion for the same slot.
        if (last_when.time_since_epoch().count() != 0 &&
            when <= last_when)
        {
            when = last_when + std::chrono::seconds(2 * 60);
        }

        // Be conservative about late or ambiguous scheduling.
        //
        // WSPR frames must start exactly on the window boundary. If
        // the computed boundary is effectively "now" or in the past
        // (for example due to clock adjustments or coarse rounding),
        // do not start late. Skip to the next window instead.
        const auto now_check = std::chrono::system_clock::now();
        constexpr auto kLateTolerance = std::chrono::milliseconds(50);
        if (now_check + kLateTolerance >= when)
        {
            when += std::chrono::seconds(2 * 60);
        }

        // Spawn the TX thread slightly before the window boundary so it
        // can apply affinity/scheduling and then sleep until the exact
        // boundary.
        constexpr auto kLead = std::chrono::seconds(2);

        const auto pre = when - kLead;

        std::unique_lock<std::mutex> lk(mtx_);
        while (!stop_requested_.load(std::memory_order_acquire) &&
               !parent_->soft_off_.load(std::memory_order_acquire) &&
               std::chrono::system_clock::now() < pre)
        {
            cv_.wait_until(
                lk,
                pre,
                [this]
                {
                    return stop_requested_.load(std::memory_order_acquire);
                });
        }

        if (stop_requested_.load(std::memory_order_acquire) ||
            parent_->soft_off_.load(std::memory_order_acquire))
        {
            break;
        }

        // If we missed the boundary, skip this cycle. This prevents
        // "starting late" when the daemon is launched too late or the
        // system is heavily loaded.
        const auto now = std::chrono::system_clock::now();
        if (now > when + kLateTolerance)
        {
            continue;
        }

        // If a stop was requested while we were evaluating timing,
        // do not schedule another transmission.
        if (stop_requested_.load(std::memory_order_acquire) ||
            parent_->soft_off_.load(std::memory_order_acquire))
        {
            break;
        }

        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            when.time_since_epoch())
                            .count();
        parent_->scheduled_start_rt_ns_.store(
            ns,
            std::memory_order_release);

        // Synchronize with stop()/shutdown() so we don't
        // race a join/start with a shutdown request.
        std::lock_guard<std::mutex> tx_lk(parent_->tx_thread_mtx_);

        if (stop_requested_.load(std::memory_order_acquire) ||
            parent_->shouldStop())
        {
            break;
        }

        // Join any prior TX thread before launching a new one.
        if (parent_->tx_thread_.joinable())
        {
            parent_->tx_thread_.join();
        }

        // If we waited for a prior transmission to finish and are now
        // past the target window, do not start late. Instead, skip to
        // the next computed window.
        const auto now_post_join = std::chrono::system_clock::now();
        if (now_post_join > when + kLateTolerance)
        {
            continue;
        }

        // Clear the parent stop flag only immediately before launch.
        parent_->stop_requested_.store(false, std::memory_order_release);

        parent_->tx_thread_ = std::thread(
            &WsprTransmitter::thread_entry,
            parent_);

        last_when = when;

        if (parent_->one_shot_.load(std::memory_order_acquire))
        {
            break;
        }
    }
}

WsprTransmitter wsprTransmitter;

/* Public Methods */

WsprTransmitter::WsprTransmitter()
{
    const int ncpu = cpu_count();
    tx_cpu_ = clamp_cpu(tx_cpu_, ncpu);

    if (ncpu <= 1)
    {
        spin_ns_ = 0; // or 50'000 if you want a tiny spin
    }
    selectBackend(initial_compiled_backend());
    callback_thread_ = std::thread(&WsprTransmitter::callback_worker_loop, this);
}

WsprTransmitter::~WsprTransmitter()
{
    shutdown();
    const auto cleanup_result = cleanupTransmissionBackend();
    if (!cleanup_result.ok)
    {
        std::fprintf(
            stderr,
            "WsprTransmitter destructor cleanup failed: %s\n",
            cleanup_result.error.c_str());
    }
    stop_callback_worker();
}

void WsprTransmitter::selectBackend(wsprrypi::BackendKind backend_kind)
{
    selectBackend(
        backend_kind,
        Si5351RuntimeConfig{},
        SimulatedRuntimeConfig{});
}

void WsprTransmitter::selectBackend(
    wsprrypi::BackendKind backend_kind,
    const Si5351RuntimeConfig &runtime_config)
{
    selectBackend(backend_kind, runtime_config, SimulatedRuntimeConfig{});
}

void WsprTransmitter::selectBackend(
    wsprrypi::BackendKind backend_kind,
    const Si5351RuntimeConfig &runtime_config,
    const SimulatedRuntimeConfig &simulated_config)
{
    if (!backend_is_compiled(backend_kind))
    {
        throw std::invalid_argument(
            std::string("Transmission backend ") + backend_kind_name(backend_kind) +
            " is unavailable in this build. Compiled backends: " +
            WSPRRYPI_COMPILED_BACKENDS + ".");
    }

    if (backend_ &&
        selected_backend_ == backend_kind &&
        (backend_kind != wsprrypi::BackendKind::SI5351 ||
         selected_si5351_config_ == runtime_config) &&
        (backend_kind != wsprrypi::BackendKind::SIMULATED ||
         selected_simulated_config_ == simulated_config))
    {
        return;
    }

    shutdown();
    requireBackendCleanup("backend replacement");

    rpi_backend_ = nullptr;
    selected_backend_ = backend_kind;
    selected_si5351_config_ = runtime_config;
    selected_simulated_config_ = simulated_config;

    backend_ = createBackend(backend_kind, runtime_config, simulated_config);

    transmission_controller_ =
        std::make_unique<wsprrypi::TransmissionController>(
            execution_plan_compiler_,
            *backend_);
}

std::unique_ptr<wsprrypi::ITransmissionBackend> WsprTransmitter::createBackend(
    wsprrypi::BackendKind backend_kind,
    const Si5351RuntimeConfig& runtime_config,
    const SimulatedRuntimeConfig& simulated_config)
{
    switch (backend_kind)
    {
        case wsprrypi::BackendKind::RPI_CLOCK_GPIO:
        {
#if WSPRRYPI_BACKEND_RPI_GPIO
            auto rpi_backend = std::make_unique<WsprRpiBackend>(*this);
            rpi_backend_ = rpi_backend.get();
            return rpi_backend;
#else
            break;
#endif
        }
        case wsprrypi::BackendKind::RP1_GPCLK:
#if WSPRRYPI_BACKEND_RP1_GPCLK
            return std::make_unique<WsprRp1GpclkBackend>(*this);
#else
            break;
#endif
        case wsprrypi::BackendKind::SI5351:
        {
#if WSPRRYPI_BACKEND_SI5351
            WsprSi5351Backend::Config si5351_config;
            si5351_config.device.i2c_bus = runtime_config.i2c_bus;
            si5351_config.device.i2c_address =
                static_cast<std::uint8_t>(runtime_config.i2c_address);
            si5351_config.device.reference_hz =
                static_cast<std::uint32_t>(runtime_config.reference_hz);
            si5351_config.device.reference_source =
                runtime_config.reference_source ==
                        Si5351RuntimeConfig::ReferenceSource::CRYSTAL
                    ? Si5351Device::ReferenceSource::CRYSTAL
                    : Si5351Device::ReferenceSource::EXTERNAL_TCXO;
            si5351_config.device.crystal_load_capacitance_pf =
                runtime_config.crystal_load_capacitance_pf;
            si5351_config.planner.reference_hz =
                static_cast<std::uint32_t>(runtime_config.reference_hz);
            si5351_config.planner.tx_output =
                si5351_output_from_index(runtime_config.tx_output);
            if (runtime_config.app_managed)
            {
                si5351_config.planner.park_unused_outputs = true;
            }
            si5351_config.power_level = runtime_config.power_level;
            si5351_config.dry_run = false;
            return std::make_unique<WsprSi5351Backend>(
                *this,
                si5351_config);
#else
            break;
#endif
        }
        case wsprrypi::BackendKind::SIMULATED:
        {
#if WSPRRYPI_BACKEND_SIMULATED
            wsprrypi::SimulatedBackendConfig config;
            config.virtual_time = simulated_config.virtual_time;
            config.trace_path = simulated_config.trace_path;
            config.fail_startup_quiesce = simulated_config.fail_startup_quiesce;
            config.fail_configure = simulated_config.fail_configure;
            config.fail_event = simulated_config.fail_event;
            config.cancel_event = simulated_config.cancel_event;
            config.fail_cleanup = simulated_config.fail_cleanup;
            return std::make_unique<wsprrypi::SimulatedTransmitBackend>(*this, config);
#else
            break;
#endif
        }
    }
    throw std::invalid_argument(
        std::string("Transmission backend ") + backend_kind_name(backend_kind) +
        " is unavailable in this build. Compiled backends: " +
        WSPRRYPI_COMPILED_BACKENDS + ".");
}

wsprrypi::StartupQuiesceResult WsprTransmitter::quiesceForStartup()
{
    if (transmission_controller_ == nullptr || backend_ == nullptr)
    {
        return {false, "Startup quiesce is unavailable because no transmission backend is selected."};
    }

    if (state_.load(std::memory_order_acquire) != State::DISABLED ||
        current_request_.actual_rf_frequency_hz != 0.0 ||
        !current_execution_plan_.events.empty() ||
        transmission_controller_->prepared_plan() != nullptr)
    {
        return {false, "Startup quiesce is only valid before transmission configuration or scheduling."};
    }

    return transmission_controller_->quiesceForStartup();
}

void WsprTransmitter::setTransmissionCallbacks(TransmissionCallback cb)
{
    on_transmit_cb_ = std::move(cb);
}

std::string WsprTransmitter::formatFrequencyMHz(double frequency_hz)
{
    const auto hz_rounded =
        static_cast<std::int64_t>(std::llround(frequency_hz));

    const double mhz = static_cast<double>(hz_rounded) / 1.0e6;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << mhz;
    return oss.str();
}

bool WsprTransmitter::activeExecutionIsTone() const noexcept
{
    return current_execution_mode_ == wsprrypi::TransmissionMode::TONE;
}

bool WsprTransmitter::activeExecutionIsWspr() const noexcept
{
    return current_execution_mode_ == wsprrypi::TransmissionMode::WSPR;
}

WsprTransmitter::RuntimeExecutionStatus
WsprTransmitter::runtimeExecutionStatusSnapshot() const
{
    RuntimeExecutionStatus snapshot;
    snapshot.mode = current_execution_mode_;
    snapshot.cw_message = current_cw_message_;
    snapshot.cw_active_char_index =
        current_cw_active_char_index_.load(std::memory_order_acquire);
    return snapshot;
}

std::string WsprTransmitter::reloadDeferDebugState() const
{
    auto mode_name =
        [](wsprrypi::TransmissionMode mode) noexcept
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
        default:
            return "UNKNOWN";
        }
    };

    auto committed_mode_name =
        [](TransmissionMode mode) noexcept
    {
        switch (mode)
        {
        case TransmissionMode::WSPR:
            return "WSPR";
        case TransmissionMode::TONE:
            return "TONE";
        default:
            return "UNKNOWN";
        }
    };

    std::ostringstream oss;
    const State state = state_.load(std::memory_order_acquire);
    std::string state_name = wsprTransmitStateToString(state);
    std::transform(
        state_name.begin(),
        state_name.end(),
        state_name.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    oss << "state=" << state_name
        << ", stop_requested=" << (stop_requested_.load(std::memory_order_acquire) ? "true" : "false")
        << ", soft_off=" << (soft_off_.load(std::memory_order_acquire) ? "true" : "false")
        << ", active_execution_mode=" << mode_name(current_execution_mode_)
        << ", active_execution_is_tone=" << (activeExecutionIsTone() ? "true" : "false")
        << ", active_execution_is_wspr=" << (activeExecutionIsWspr() ? "true" : "false")
        << ", current_request_mode=" << committed_mode_name(current_request_.mode)
        << ", current_request_rf_hz=" << current_request_.actual_rf_frequency_hz
        << ", current_request_dial_hz=" << current_request_.dial_frequency_hz
        << ", current_request_skip=" << (current_request_.isSkipWindow() ? "true" : "false")
        << ", current_plan_events=" << current_execution_plan_.events.size()
        << ", scheduled_start_rt_ns=" << scheduled_start_rt_ns_.load(std::memory_order_acquire);
    return oss.str();
}

void WsprTransmitter::clearExecutionStateAfterStop() noexcept
{
    current_request_ = TransmissionRequest{};
    current_execution_plan_ = wsprrypi::ExecutionPlan{};
    current_execution_mode_ = wsprrypi::TransmissionMode::WSPR;
    current_cw_message_.clear();
    current_cw_active_char_index_.store(-1, std::memory_order_release);
    scheduled_start_rt_ns_.store(0, std::memory_order_release);
    transmission_controller_->reset();
}

void WsprTransmitter::configureExecution(
    const TransmissionRequest &request)
{
    if (!request.isSkipWindow())
    {
        const auto policy = wsprrypi::evaluate_gpio_band_policy(
            selected_backend_,
            request.actual_rf_frequency_hz,
            request.isTone() ? wsprrypi::TransmissionMode::TONE
                             : wsprrypi::TransmissionMode::WSPR,
            request.allow_unqualified_frequency,
            request.allow_non_amateur_frequency,
            request.hardware_profile);
        if (!policy.allowed)
            throw std::invalid_argument(policy.error);
    }

    if (request.isTone() && selected_backend_ == wsprrypi::BackendKind::SI5351)
    {
        wsprrypi::TransmissionRequest controller_request;
        controller_request.mode = wsprrypi::TransmissionMode::TONE;
        controller_request.output.backend = selected_backend_;
        controller_request.output.output =
            si5351_clock_source_from_index(selected_si5351_config_.tx_output);
        controller_request.output.gpio = request.tx_gpio;
        controller_request.calibration.ppm = request.ppm;
        controller_request.policy.allow_unqualified_frequency =
            request.allow_unqualified_frequency;
        controller_request.policy.allow_non_amateur_frequency =
            request.allow_non_amateur_frequency;
        controller_request.policy.hardware_profile = request.hardware_profile;
        controller_request.id.value = 1;

        wsprrypi::TonePayload payload;
        payload.frequency_hz = request.actual_rf_frequency_hz;
        controller_request.payload = payload;

        configureExecution(controller_request, request);
        return;
    }

    if (!request.isTone() && !request.isSkipWindow())
    {
        wsprrypi::TransmissionRequest controller_request;
        controller_request.mode = wsprrypi::TransmissionMode::WSPR;
        controller_request.output.backend = selected_backend_;
        controller_request.output.output =
            selected_backend_ == wsprrypi::BackendKind::SI5351
                ? si5351_clock_source_from_index(selected_si5351_config_.tx_output)
                : wsprrypi::ClockSource::GPIO_CLK;
        controller_request.output.gpio = request.tx_gpio;
        controller_request.calibration.ppm = request.ppm;
        controller_request.policy.allow_unqualified_frequency =
            request.allow_unqualified_frequency;
        controller_request.policy.allow_non_amateur_frequency =
            request.allow_non_amateur_frequency;
        controller_request.policy.hardware_profile = request.hardware_profile;
        controller_request.id.value = 1;

        wsprrypi::WsprPayload payload;
        payload.prepared = request.payload;
        payload.base_frequency_hz = request.actual_rf_frequency_hz;
        controller_request.payload = payload;

        configureExecution(controller_request, request);
        return;
    }

    // Reconfiguration is only safe when the transmit thread is not actively
    // feeding DMA. If a transmission is in progress, stop it first.
    if (state_.load(std::memory_order_acquire) == State::TRANSMITTING)
    {
        requestStopTx();
    }

    shutdown();
    requireBackendCleanup("legacy execution reconfiguration");

    stop_requested_.store(false);

    if (!request.isTone() &&
        !request.isSkipWindow() &&
        request.payload.frames.empty())
    {
        throw std::invalid_argument(
            "WSPR transmission request contains no frames.");
    }

    // Store the committed execution snapshot exactly as provided by the
    // orchestration layer. Recovery paths reuse this request verbatim.
    current_request_ = request;
    current_execution_plan_ = wsprrypi::ExecutionPlan{};
    current_execution_mode_ =
        request.isTone() ? wsprrypi::TransmissionMode::TONE
                         : wsprrypi::TransmissionMode::WSPR;
    current_cw_message_.clear();
    current_cw_active_char_index_.store(-1, std::memory_order_release);
    transmission_controller_->reset();

    if (current_request_.isSkipWindow())
    {
        // The scheduler explicitly marked this as a skipped window.
        // Do not initialize DMA or mailbox resources for this cycle.
        scheduled_start_rt_ns_.store(0, std::memory_order_release);
        state_.store(State::ENABLED, std::memory_order_release);
        return;
    }

    if (activeExecutionIsWspr() && current_request_.actual_rf_frequency_hz == 0.0)
    {
        throw std::invalid_argument(
            "WSPR execution request missing actual RF frequency.");
    }

    try
    {
        prepareTransmissionBackend();

        const auto configure_result = configureTransmissionBackend();

        if (current_request_.actual_rf_frequency_hz != 0.0)
            current_request_.actual_rf_frequency_hz = configure_result.applied_frequency_hz;

        state_.store(State::ENABLED, std::memory_order_release);
    }
    catch (...)
    {
        const std::exception_ptr original = std::current_exception();
        shutdown();
        rethrowWithCleanupResult(
            original,
            "legacy execution configuration failure");
    }
}

void WsprTransmitter::configureExecution(
    const wsprrypi::TransmissionRequest& request,
    const TransmissionRequest& legacy_request)
{
    if (request.mode == wsprrypi::TransmissionMode::TONE &&
        request.output.backend != wsprrypi::BackendKind::SI5351)
    {
        throw std::invalid_argument(
            std::string(
                "Controller tone execution is only supported for the SI5351 backend; received ") +
            backend_kind_name(request.output.backend) + ".");
    }

    if (request.mode == wsprrypi::TransmissionMode::WSPR &&
        legacy_request.isTone())
    {
        throw std::invalid_argument(
            "Canonical WSPR configuration received tone legacy context.");
    }

    if (request.mode == wsprrypi::TransmissionMode::WSPR &&
        !legacy_request.isSkipWindow() &&
        legacy_request.payload.frames.empty())
    {
        throw std::invalid_argument(
            "WSPR transmission request contains no frames.");
    }

    // Compile and enforce the GPIO band policy before stopping or preparing
    // any transmission hardware. TransmissionController repeats this check
    // immediately before backend configuration as defense in depth.
    const auto policy = wsprrypi::evaluate_gpio_band_policy(
        execution_plan_compiler_.compile(request));
    if (!policy.allowed)
        throw std::invalid_argument(policy.error);

    // Reconfiguration is only safe when the transmit thread is not actively
    // feeding DMA. If a transmission is in progress, stop it first.
    if (state_.load(std::memory_order_acquire) == State::TRANSMITTING)
    {
        requestStopTx();
    }

    shutdown();
    requireBackendCleanup("canonical execution reconfiguration");

    stop_requested_.store(false);

    current_request_ = legacy_request;
    current_execution_plan_ = wsprrypi::ExecutionPlan{};
    standard_feld_progress_.clear();
    current_execution_mode_ = request.mode;
    current_cw_message_ = current_cw_message_for_payload(request.payload);
    current_cw_active_char_index_.store(-1, std::memory_order_release);
    transmission_controller_->reset();

    if (current_request_.isSkipWindow())
    {
        scheduled_start_rt_ns_.store(0, std::memory_order_release);
        state_.store(State::ENABLED, std::memory_order_release);
        return;
    }

    if (current_request_.actual_rf_frequency_hz == 0.0)
    {
        throw std::invalid_argument(
            "Execution request missing actual RF frequency.");
    }

    try
    {
        prepareTransmissionBackend();

        const auto configure_result =
            transmission_controller_->prepare(
                request,
                wsprrypi::TransmissionPrepareOptions{
                    current_request_.power_level});

        if (!configure_result.ok)
        {
            throw std::runtime_error(
                configure_result.error.empty()
                    ? "Execution-plan backend configuration failed."
                    : configure_result.error);
        }

        const wsprrypi::ExecutionPlan* prepared_plan =
            transmission_controller_->prepared_plan();
        if (prepared_plan == nullptr)
        {
            throw std::runtime_error(
                "Execution-plan controller did not retain the prepared plan.");
        }

        current_execution_plan_ = *prepared_plan;
        current_request_.actual_rf_frequency_hz =
            current_execution_plan_.reference_frequency_hz;

        state_.store(State::ENABLED, std::memory_order_release);
    }
    catch (...)
    {
        const std::exception_ptr original = std::current_exception();
        shutdown();
        rethrowWithCleanupResult(
            original,
            "canonical execution configuration failure");
    }
}

void WsprTransmitter::setThreadScheduling(int policy, int priority)
{
    thread_policy_ = policy;
    thread_priority_ = priority;
}

void WsprTransmitter::setOneShot(bool enable) noexcept
{
    one_shot_.store(enable, std::memory_order_release);
}

void WsprTransmitter::setTransmitNow(bool enable) noexcept
{
    transmit_now_.store(enable, std::memory_order_release);
}

void WsprTransmitter::requestSoftOff() noexcept
{
    soft_off_.store(true, std::memory_order_release);
    scheduler_.notify();
}

void WsprTransmitter::clearSoftOff() noexcept
{
    soft_off_.store(false, std::memory_order_release);
}

void WsprTransmitter::startAsync()
{
    stop_requested_.store(false, std::memory_order_release);

    if (!activeExecutionIsTone() && current_request_.actual_rf_frequency_hz == 0.0)
    {
        // Only explicit skip-window requests are allowed to use this path.
        // Zero RF frequency alone is not sufficient because ordinary waiting
        // and debug logging are not scheduling outcomes.
        if (!current_request_.isSkipWindow())
        {
            throw std::logic_error(
                "Non-skip non-tone request reached zero-frequency startAsync() path.");
        }

        const State prior = state_.load(std::memory_order_acquire);
        if (prior == State::DISABLED || prior == State::COMPLETE ||
            prior == State::CANCELLED || prior == State::FAILED)
        {
            state_.store(State::ENABLED, std::memory_order_release);
        }

        const bool immediate = transmit_now_.load(std::memory_order_acquire);
        if (immediate)
        {
            scheduled_start_rt_ns_.store(0, std::memory_order_release);

            std::lock_guard<std::mutex> lk(tx_thread_mtx_);
            if (tx_thread_.joinable())
            {
                tx_thread_.join();
            }

            tx_thread_ = std::thread(&WsprTransmitter::thread_entry, this);
        }
        else
        {
            scheduler_.start();
        }
        return;
    }

    // If the application has requested a soft-off, do not start scheduling.
    if (activeExecutionIsWspr() && soft_off_.load(std::memory_order_acquire))
    {
        return;
    }

    // The application may poll getState() immediately after startAsync().
    // Transition to ENABLED here so callers do not misinterpret the initial
    // DISABLED state as an early abort before the TX thread has a chance to
    // run and advance the state machine.
    {
        const State prior = state_.load(std::memory_order_acquire);
        if (prior == State::DISABLED || prior == State::COMPLETE ||
            prior == State::CANCELLED || prior == State::FAILED)
        {
            state_.store(State::ENABLED, std::memory_order_release);
        }
    }

    const bool immediate = !activeExecutionIsWspr() ||
                           transmit_now_.load(std::memory_order_acquire);

    if (immediate)
    {
        // For WSPR "--now" runs, align the TX start to the next 50 ms
        // boundary. This emulates the final timer stage used by the normal
        // window scheduler (which sleeps to an absolute CLOCK_REALTIME
        // boundary).
        if (activeExecutionIsWspr() &&
            transmit_now_.load(std::memory_order_acquire))
        {
            struct timespec now_rt{};
            ::clock_gettime(CLOCK_REALTIME, &now_rt);

            const std::int64_t now_ns =
                static_cast<std::int64_t>(now_rt.tv_sec) * 1000000000LL +
                static_cast<std::int64_t>(now_rt.tv_nsec);

            constexpr std::int64_t kTickNs = 50000000LL;   // 50 ms
            constexpr std::int64_t kMinLeadNs = 5000000LL; // 5 ms

            std::int64_t next_ns = ((now_ns / kTickNs) + 1) * kTickNs;
            if (next_ns - now_ns < kMinLeadNs)
            {
                next_ns += kTickNs;
            }

            scheduled_start_rt_ns_.store(next_ns, std::memory_order_release);
        }
        else
        {
            scheduled_start_rt_ns_.store(0, std::memory_order_release);
        }

        std::lock_guard<std::mutex> lk(tx_thread_mtx_);
        if (tx_thread_.joinable())
        {
            tx_thread_.join();
        }

        tx_thread_ = std::thread(&WsprTransmitter::thread_entry, this);
        return;
    }

    scheduler_.start();
}

void WsprTransmitter::shutdown()
{
    // Set the stop flag first so a newly spawned transmit thread
    // will abort before it touches DMA/PWM state.
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ == nullptr || !rpi_backend_->publishStandardFeldStop())
#endif
        stop_requested_.store(true, std::memory_order_release);
    stop_cv_.notify_all();

    // Stop the scheduler thread. Note: do not set soft_off_ here.
    //
    // soft_off_ is an application-level "no new scheduling" latch (used
    // for Ctrl-C / graceful shutdown). shutdown() is also used
    // internally during reconfiguration (e.g., configureExecution()), and
    // must not permanently prevent future enableTransmission() calls.
    scheduler_.stop();

    // Join the transmit thread under a mutex so the scheduler cannot
    // race with us and start a new thread while we are joining.
    {
        std::lock_guard<std::mutex> lk(tx_thread_mtx_);
        if (tx_thread_.joinable() &&
            tx_thread_.get_id() != std::this_thread::get_id())
        {
            tx_thread_.join();
        }
    }

    // The execution thread owns reporting until it has joined above.  Clear
    // the internal Standard Feld history only after that quiescence point.
    standard_feld_progress_.clear();

    stopFaultMonitoring();

    // Return to DISABLED when the transmitter is shut down, unless a
    // watchdog recovery is in progress or the transmitter is latched HUNG.
#if WSPRRYPI_BACKEND_RPI_GPIO
    const bool recovery_in_progress =
        rpi_backend_ != nullptr && rpi_backend_->recoveryInProgress();
#else
    constexpr bool recovery_in_progress = false;
#endif
    if (!recovery_in_progress)
    {
        const State prior = state_.load(std::memory_order_acquire);
        if (prior != State::HUNG && prior != State::RECOVERING)
        {
            state_.store(State::DISABLED, std::memory_order_release);
        }
    }
}

void WsprTransmitter::requestStopTx()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ == nullptr || !rpi_backend_->publishStandardFeldStop())
#endif
        stop_requested_.store(true, std::memory_order_release);
    stop_cv_.notify_all();

    // Synchronize with the scheduler so it cannot race a join/start while
    // we are waiting for the transmit thread to unwind.
    {
        std::lock_guard<std::mutex> lk(tx_thread_mtx_);
        if (tx_thread_.joinable() &&
            tx_thread_.get_id() != std::this_thread::get_id())
        {
            tx_thread_.join();
        }
    }
}

void WsprTransmitter::requestStopTxNoJoin() noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ == nullptr || !rpi_backend_->publishStandardFeldStop())
#endif
        stop_requested_.store(true, std::memory_order_release);
    stop_cv_.notify_all();
}

void WsprTransmitter::force_dma_reset_sequence() noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->resetTransmissionOutput();
    }
#endif
}


bool WsprTransmitter::watchdogFaulted() const noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    return rpi_backend_ != nullptr && rpi_backend_->faulted();
#else
    return false;
#endif
}

void WsprTransmitter::clearWatchdogFault() noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->clearFault();
    }
#endif
}

void WsprTransmitter::setWatchdogAutoRecover(bool enable) noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->setAutoRecover(enable);
    }
#else
    (void)enable;
#endif
}

bool WsprTransmitter::watchdogAutoRecoverEnabled() const noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    return rpi_backend_ != nullptr && rpi_backend_->autoRecoverEnabled();
#else
    return false;
#endif
}

bool WsprTransmitter::recoverFromWatchdogFault()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    return rpi_backend_ != nullptr && rpi_backend_->recoverFromFault();
#else
    return false;
#endif
}

void WsprTransmitter::request_watchdog_recovery() noexcept
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->recoverFromFault();
    }
#endif
}

void WsprTransmitter::recovery_worker()
{
}

bool WsprTransmitter::recover_from_watchdog_fault_locked()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    return rpi_backend_ != nullptr && rpi_backend_->recoverFromFault();
#else
    return false;
#endif
}

void WsprTransmitter::stopAndJoin()
{
    shutdown();
    observeBackendCleanup("explicit stop");
}

void WsprTransmitter::shutdownForProcessExit()
{
    shutdown();
    observeBackendCleanup("process shutdown");
    backend_.reset();
    rpi_backend_ = nullptr;
    stop_callback_worker();
}

WsprTransmitState WsprTransmitter::getState() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

void WsprTransmitter::dumpParameters()
{
    auto log_line =
        [this](const std::string &line)
    {
        fire_transmit_cb(
            TransmissionCallbackEvent::LOGGING,
            LogLevel::DEBUG,
            line,
            0.0);
    };

    std::ostringstream oss;

    oss << "Call Sign:         "
        << (activeExecutionIsWspr() ? current_request_.payload.callsign : "N/A");
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "Grid Square:       "
        << (activeExecutionIsWspr() ? current_request_.payload.locator : "N/A");
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "Actual RF Freq:    "
        << formatFrequencyMHz(current_request_.actual_rf_frequency_hz)
        << " MHz";
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "GPIO Power:        "
        << std::fixed
        << std::setprecision(1)
        << convert_mw_dbm(getOutputPowerMilliwatts(current_request_.power_level))
        << " dBm";
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "Transmit GPIO:     "
        << current_request_.tx_gpio;
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "Test Tone:         "
        << (activeExecutionIsTone() ? "True" : "False");
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "WSPR Symbol Time:  "
        << (!activeExecutionIsWspr()
                ? "N/A"
                : (std::to_string(WSPR_SYMTIME) + " s"));
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "WSPR Tone Spacing: "
        << (!activeExecutionIsWspr()
                ? "N/A"
                : (std::to_string(1.0 / WSPR_SYMTIME) + " Hz"));
    log_line(oss.str());
    oss.str("");
    oss.clear();

    oss << "DMA Table Size:    "
        << 1024;
    log_line(oss.str());
    oss.str("");
    oss.clear();

    if (!activeExecutionIsWspr())
    {
        log_line("WSPR Symbols:      N/A");
    }
    else
    {
        log_line("WSPR Symbols: ");

        const int frame_count =
            static_cast<int>(current_request_.payload.frameCount());
        const int symbols_per_frame =
            static_cast<int>(current_request_.payload.symbolCountPerFrame());
        const int symbol_count =
            static_cast<int>(current_request_.payload.totalSymbolCount());

        std::string line;
        line.reserve(128);

        for (int i = 0; i < symbol_count; ++i)
        {
            const int frame_index = i / symbols_per_frame;
            const int symbol_index = i % symbols_per_frame;
            line += std::to_string(
                static_cast<int>(
                    current_request_.payload.frames[frame_index]
                        .symbols[symbol_index]));

            if (i < symbol_count - 1)
            {
                line += ", ";
            }

            if ((i + 1) % 18 == 0 || i == symbol_count - 1)
            {
                log_line(line);
                line.clear();
            }
        }

        oss << "WSPR Frames:       " << frame_count;
        log_line(oss.str());
    }
}

/* Private Methods */

inline void WsprTransmitter::fire_transmit_cb(
    TransmissionCallbackEvent event,
    LogLevel level,
    const std::string &msg,
    double value)
{
    {
        std::lock_guard<std::mutex> lk(callback_mtx_);
        if (!on_transmit_cb_ || callback_stop_)
        {
            return;
        }

        callback_queue_.push_back(PendingTransmitCallback{
            event,
            level,
            msg,
            value});
    }

    callback_cv_.notify_one();
}

void WsprTransmitter::callback_worker_loop()
{
    for (;;)
    {
        PendingTransmitCallback pending{};
        TransmissionCallback cb;

        {
            std::unique_lock<std::mutex> lk(callback_mtx_);
            callback_cv_.wait(lk,
                              [this]
                              {
                                  return callback_stop_ || !callback_queue_.empty();
                              });

            if (callback_stop_ && callback_queue_.empty())
            {
                return;
            }

            pending = std::move(callback_queue_.front());
            callback_queue_.pop_front();
            cb = on_transmit_cb_;
        }

        if (cb)
        {
            cb(pending.event, pending.level, pending.msg, pending.value);
        }
    }
}

void WsprTransmitter::stop_callback_worker()
{
    {
        std::lock_guard<std::mutex> lk(callback_mtx_);
        callback_stop_ = true;
    }
    callback_cv_.notify_all();

    if (callback_thread_.joinable() &&
        callback_thread_.get_id() != std::this_thread::get_id())
    {
        callback_thread_.join();
    }
}

WsprTransmitState WsprTransmitter::backendStateValue() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

void WsprTransmitter::backendSetStateValue(WsprTransmitState state) noexcept
{
    state_.store(state, std::memory_order_release);
}

bool WsprTransmitter::backendShouldStop() const noexcept
{
    return shouldStop();
}

void WsprTransmitter::backendSignalStopRequest() noexcept
{
    stop_requested_.store(true, std::memory_order_release);
    stop_cv_.notify_all();
}

void WsprTransmitter::backendRequestStopTxNoJoin() noexcept
{
    requestStopTxNoJoin();
}

bool WsprTransmitter::backendWaitInterruptableFor(std::chrono::nanoseconds duration)
{
    return waitInterruptableFor(duration);
}

void WsprTransmitter::backendThrowIfStopRequested(const char *context)
{
    throwIfStopRequested(context);
}

void WsprTransmitter::backendReportExecutionProgress(
    std::size_t event_index) noexcept
{
    if (current_execution_mode_ != wsprrypi::TransmissionMode::QRSS &&
        current_execution_mode_ != wsprrypi::TransmissionMode::FSKCW &&
        current_execution_mode_ != wsprrypi::TransmissionMode::DFCW)
    {
        return;
    }

    if (event_index >= current_execution_plan_.events.size())
    {
        return;
    }

    const int message_char_index =
        current_execution_plan_.events[event_index].message_char_index;
    const int prior =
        current_cw_active_char_index_.exchange(
            message_char_index,
            std::memory_order_acq_rel);
    if (prior == message_char_index)
    {
        return;
    }

    fire_transmit_cb(
        TransmissionCallbackEvent::PROGRESS,
        LogLevel::DEBUG,
        "",
        static_cast<double>(message_char_index));
}

bool WsprTransmitter::backendReportRasterProgress(
    const std::uint64_t generation,
    std::size_t event_index,
    const wsprrypi::RfEvent::RasterProgress& progress)
{
    // The store owns an immutable plan snapshot installed before execution;
    // this bridge therefore never reads mutable execution state on the event path.
    return standard_feld_progress_.report(generation, event_index, progress);
}

bool WsprTransmitter::backendActivateRasterProgress(
    const wsprrypi::ExecutionPlan& plan, const std::uint64_t generation)
{
    return standard_feld_progress_.prepare(plan, generation);
}

bool WsprTransmitter::backendFinalizeRasterProgress(
    const std::uint64_t generation,
    const wsprrypi::RpiStandardFeldExecutionTerminal terminal,
    const bool watchdog_faulted)
{
    wsprrypi::RpiStandardFeldExecutionResult result;
    result.terminal = terminal;
    result.watchdog_faulted = watchdog_faulted;
    // A completed terminal supplied by the backend is already classified;
    // otherwise the bridge conservatively records failure rather than success.
    result.safe_idle_confirmed = terminal ==
        wsprrypi::RpiStandardFeldExecutionTerminal::COMPLETED;
    return standard_feld_progress_.finalize(generation, result);
}

void WsprTransmitter::backendFireTransmitCallback(
    WsprTransmissionCallbackEvent event,
    WsprTransmitLogLevel level,
    const std::string &msg,
    double value)
{
    fire_transmit_cb(event, level, msg, value);
}

bool WsprTransmitter::backendRestartCurrentConfiguration()
{
    const TransmissionRequest request = current_request_;

    shutdown();
    requireBackendCleanup("watchdog recovery");

    configureExecution(request);
    startAsync();
    return true;
}

WsprTransmissionPlan WsprTransmitter::buildTransmissionPlan() const noexcept
{
    // Reduce the committed request to the hardware-facing fields consumed by
    // the backend. Policy and scheduler metadata stay out of the backend.
    return WsprTransmissionPlan{
        current_request_.actual_rf_frequency_hz,
        1.0 / WSPR_SYMTIME,
        current_request_.power_level,
        current_request_.ppm,
        current_request_.tx_gpio,
        current_request_.totalSymbolCount()};
}

bool WsprTransmitter::shouldStop() const noexcept
{
    if (stop_requested_.load(std::memory_order_acquire))
        return true;

    const std::atomic<bool> *ext = external_stop_flag_;
    if (ext && ext->load(std::memory_order_acquire))
        return true;

    return false;
}

void WsprTransmitter::startFaultMonitoring()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->startFaultMonitoring();
    }
#endif
}

void WsprTransmitter::stopFaultMonitoring()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->stopFaultMonitoring();
    }
#endif
}

bool WsprTransmitter::waitInterruptableFor(std::chrono::nanoseconds duration)
{
    std::unique_lock<std::mutex> lk(stop_mtx_);
    const bool interrupted = stop_cv_.wait_for(
        lk,
        duration,
        [this]
        {
            return shouldStop() || soft_off_.load(std::memory_order_acquire);
        });

    return !interrupted;
}

bool WsprTransmitter::sleepUntilAbsTightInterruptible(
    clockid_t clk_id,
    const timespec &ts_target,
    int64_t spin_ns)
{
    if (spin_ns < 0)
    {
        spin_ns = 0;
    }

    for (;;)
    {
        if (shouldStop() || soft_off_.load(std::memory_order_acquire))
        {
            return false;
        }

        timespec now{};
        ::clock_gettime(clk_id, &now);

        const int64_t remaining_ns = diff_ns(ts_target, now);
        if (remaining_ns <= 0)
        {
            break;
        }

        if (remaining_ns > spin_ns)
        {
            const auto sleep_ns =
                std::chrono::nanoseconds{remaining_ns - spin_ns};

            // Use an interruptible condition-variable wait for the bulk
            // of the sleep so requestStopTx() can wake us promptly.
            if (!waitInterruptableFor(sleep_ns))
            {
                return false;
            }
            continue;
        }

        // Final precision tail: busy-wait until the exact deadline.
        while (!shouldStop())
        {
            ::clock_gettime(clk_id, &now);
            if (diff_ns(now, ts_target) >= 0)
            {
                break;
            }
        }
        break;
    }

    return !(shouldStop() || soft_off_.load(std::memory_order_acquire));
}

void WsprTransmitter::throwIfStopRequested(const char *context)
{
    if (!shouldStop() && !soft_off_.load(std::memory_order_acquire))
        return;

    std::string msg = "Stop requested";
    if (context && *context)
    {
        msg += " while ";
        msg += context;
    }
    msg += '.';

    throw std::runtime_error(msg);
}

void WsprTransmitter::transmit()
{
    if (!activeExecutionIsTone() && current_request_.actual_rf_frequency_hz == 0.0)
    {
        if (!current_request_.isSkipWindow())
        {
            throw std::logic_error(
                "Non-skip non-tone request reached zero-frequency transmit() path.");
        }

        const std::int64_t start_rt_ns =
            scheduled_start_rt_ns_.load(std::memory_order_acquire);
        if (start_rt_ns != 0)
        {
            struct timespec start_rt{};
            start_rt.tv_sec = start_rt_ns / 1000000000LL;
            start_rt.tv_nsec = static_cast<long>(start_rt_ns % 1000000000LL);

            if (!sleepUntilAbsTightInterruptible(CLOCK_REALTIME, start_rt, spin_ns_))
            {
                const bool canceled = shouldStop();
                state_.store(canceled ? State::CANCELLED : State::COMPLETE,
                             std::memory_order_release);
                fire_transmit_cb(canceled
                                     ? TransmissionCallbackEvent::CANCELLED
                                     : TransmissionCallbackEvent::SKIPPED,
                                 LogLevel::INFO,
                                 canceled ? "" : "Skipping transmission",
                                 0.0);
                return;
            }
        }

        const bool canceled = shouldStop();
        state_.store(canceled ? State::CANCELLED : State::COMPLETE,
                     std::memory_order_release);
        fire_transmit_cb(canceled
                             ? TransmissionCallbackEvent::CANCELLED
                             : TransmissionCallbackEvent::SKIPPED,
                         LogLevel::INFO,
                         canceled ? "" : "Skipping transmission",
                         0.0);
        return;
    }

    if (shouldStop())
    {
        {
            std::ostringstream oss;
            oss << "transmit() aborted before start.";
            fire_transmit_cb(
                TransmissionCallbackEvent::LOGGING,
                LogLevel::DEBUG,
                oss.str(),
                0.0);
        }

        if (one_shot_.load(std::memory_order_acquire))
        {
            state_.store(State::COMPLETE, std::memory_order_release);
        }

        return;
    }

    // RAII guard that guarantees TX is turned off no matter how we exit.
    struct TxOffGuard
    {
        WsprTransmitter *self;
        bool enabled;

        explicit TxOffGuard(WsprTransmitter *s)
            : self{s}, enabled{true}
        {
        }

        void dismiss()
        {
            enabled = false;
        }

        ~TxOffGuard()
        {
            if (enabled && self)
            {
                self->endTransmissionOutput();
            }
        }
    };

    if (activeExecutionIsTone())
    {
        if (selected_backend_ == wsprrypi::BackendKind::SI5351)
        {
            state_.store(State::TRANSMITTING, std::memory_order_release);
            fire_transmit_cb(TransmissionCallbackEvent::STARTING,
                             LogLevel::INFO,
                             "",
                             current_request_.actual_rf_frequency_hz);

            const auto t0_chrono = std::chrono::steady_clock::now();
            const auto execute_result =
                transmission_controller_->execute_prepared();
            const bool canceled =
                execute_result.stopped || shouldStop();
            if (!execute_result.ok)
            {
                throw std::runtime_error(
                    execute_result.error.empty()
                        ? "Execution-plan backend fault."
                        : execute_result.error);
            }

            const auto t_end_chrono = std::chrono::steady_clock::now();
            current_cw_active_char_index_.store(-1, std::memory_order_release);
            state_.store(canceled ? State::CANCELLED : State::COMPLETE,
                         std::memory_order_release);

            const double actual =
                std::chrono::duration<double>(t_end_chrono - t0_chrono).count();
            fire_transmit_cb(canceled
                                 ? TransmissionCallbackEvent::CANCELLED
                                 : TransmissionCallbackEvent::COMPLETE,
                             LogLevel::INFO,
                             "",
                             actual);
            return;
        }

        // Fire callback as close to the first symbol as possible.
        state_.store(State::TRANSMITTING, std::memory_order_release);
        fire_transmit_cb(TransmissionCallbackEvent::STARTING,
                         LogLevel::INFO,
                         "",
                         current_request_.actual_rf_frequency_hz);

        const auto t0_chrono = std::chrono::steady_clock::now();

        beginTransmissionOutput();
        TxOffGuard tx_guard(this);

        if (!shouldStop())
        {
            emitSymbol(
                0,
                0.0,
                -1);

            if (!shouldStop())
            {
                startFaultMonitoring();
            }
        }

        while (!shouldStop())
        {
            emitSymbol(
                0,
                0.0,
                -1);
        }

        const auto t_end_chrono = std::chrono::steady_clock::now();

        endTransmissionOutput();
        tx_guard.dismiss();

        const bool canceled = shouldStop();

        current_cw_active_char_index_.store(-1, std::memory_order_release);
        state_.store(canceled ? State::CANCELLED : State::COMPLETE,
                     std::memory_order_release);

        const double actual =
            std::chrono::duration<double>(t_end_chrono - t0_chrono).count();
        fire_transmit_cb(canceled
                             ? TransmissionCallbackEvent::CANCELLED
                             : TransmissionCallbackEvent::COMPLETE,
                         LogLevel::INFO,
                         "",
                         actual);
    }
    else
    {
        // Align to the scheduler-provided realtime boundary before starting TX.
        const std::int64_t start_rt_ns =
            scheduled_start_rt_ns_.load(std::memory_order_acquire);
        if (start_rt_ns != 0)
        {
            struct timespec start_rt{};
            start_rt.tv_sec = start_rt_ns / 1000000000LL;
            start_rt.tv_nsec = static_cast<long>(start_rt_ns % 1000000000LL);

            if (!sleepUntilAbsTightInterruptible(CLOCK_REALTIME, start_rt, spin_ns_))
            {
                {
                    std::ostringstream oss;
                    oss << "TX start aborted before window boundary.";
                    fire_transmit_cb(
                        TransmissionCallbackEvent::LOGGING,
                        LogLevel::DEBUG,
                        oss.str(),
                        0.0);
                }

                if (one_shot_.load(std::memory_order_acquire))
                {
                    state_.store(State::COMPLETE, std::memory_order_release);
                }

                return;
            }

            {
                struct timespec now_rt{};
                clock_gettime(CLOCK_REALTIME, &now_rt);
                std::tm tm_rt{};
                gmtime_r(&now_rt.tv_sec, &tm_rt);

                const long usec = now_rt.tv_nsec / 1000;

                {
                    std::ostringstream oss;
                    oss
                        << "TX start realtime = "
                        << std::setw(2) << std::setfill('0') << tm_rt.tm_hour << ":"
                        << std::setw(2) << std::setfill('0') << tm_rt.tm_min << ":"
                        << std::setw(2) << std::setfill('0') << tm_rt.tm_sec << "."
                        << std::setw(6) << std::setfill('0') << usec
                        << std::setfill(' ')
                        << "Z";
                    fire_transmit_cb(
                        TransmissionCallbackEvent::LOGGING,
                        LogLevel::DEBUG,
                        oss.str(),
                        0.0);
                }
            }
        }

        // Fire callback as close to the first symbol as possible.
        state_.store(State::TRANSMITTING, std::memory_order_release);
        fire_transmit_cb(TransmissionCallbackEvent::STARTING, LogLevel::INFO, "", current_request_.actual_rf_frequency_hz);

        const int symbol_count =
            static_cast<int>(current_execution_plan_.events.size());

        if (activeExecutionIsWspr())
        {
            const int frame_count =
                static_cast<int>(current_request_.payload.frameCount());

            if (frame_count > 1)
            {
                std::ostringstream oss;
                oss << "Transmitting " << frame_count
                    << " WSPR frames back-to-back.";
                fire_transmit_cb(
                    TransmissionCallbackEvent::LOGGING,
                    LogLevel::DEBUG,
                    oss.str(),
                    0.0);
            }
        }

        auto t0_chrono = std::chrono::steady_clock::now();

        if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        {
            std::ostringstream oss;
            oss << "mlockall failed: "
                << std::strerror(errno);
            fire_transmit_cb(
                TransmissionCallbackEvent::LOGGING,
                LogLevel::DEBUG,
                oss.str(),
                0.0);
        }

        const auto execute_result = transmission_controller_->execute_prepared();
        const bool canceled = execute_result.stopped ||
                              (shouldStop() && symbol_count > 0);
        if (!execute_result.ok)
        {
            const auto t_end_chrono = std::chrono::steady_clock::now();
            const double actual =
                std::chrono::duration<double>(t_end_chrono - t0_chrono).count();
            const std::string error = execute_result.error.empty()
                ? "Execution-plan backend failed."
                : execute_result.error;
            current_cw_active_char_index_.store(-1, std::memory_order_release);
            state_.store(State::FAILED, std::memory_order_release);
            fire_transmit_cb(
                TransmissionCallbackEvent::FAILED,
                LogLevel::ERROR,
                error,
                actual);
            return;
        }

        // Capture the end time immediately after the symbol-period drain.
        // transmit_off() can take non-trivial time on some platforms, and that
        // shutdown overhead should not be counted against the on-air duration.
        const auto t_end_chrono = std::chrono::steady_clock::now();

        current_cw_active_char_index_.store(-1, std::memory_order_release);
        state_.store(canceled ? State::CANCELLED : State::COMPLETE,
                     std::memory_order_release);

        const double actual =
            std::chrono::duration<double>(t_end_chrono - t0_chrono).count();
        fire_transmit_cb(canceled
                             ? TransmissionCallbackEvent::CANCELLED
                             : TransmissionCallbackEvent::COMPLETE,
                         LogLevel::INFO,
                         "",
                         actual);
    }
}

void WsprTransmitter::join_transmission()
{
    if (tx_thread_.joinable())
    {
        tx_thread_.join();
    }
}

wsprrypi::CleanupResult WsprTransmitter::cleanupTransmissionBackend() noexcept
{
    last_cleanup_result_ = backend_ != nullptr
        ? backend_->cleanup()
        : wsprrypi::CleanupResult{true, {}};
    return last_cleanup_result_;
}

void WsprTransmitter::requireBackendCleanup(const char* context)
{
    if (observeBackendCleanup(context))
        return;

    std::string error = std::string(context) + " cleanup failed";
    if (!last_cleanup_result_.error.empty())
        error += ": " + last_cleanup_result_.error;
    throw std::runtime_error(error);
}

bool WsprTransmitter::observeBackendCleanup(const char* context)
{
    const auto result = cleanupTransmissionBackend();
    if (result.ok)
        return true;

    state_.store(State::FAILED, std::memory_order_release);
    std::string error = std::string(context) + " cleanup failed";
    if (!result.error.empty())
        error += ": " + result.error;
    fire_transmit_cb(
        TransmissionCallbackEvent::FAILED,
        LogLevel::ERROR,
        error,
        0.0);
    return false;
}

[[noreturn]] void WsprTransmitter::rethrowWithCleanupResult(
    std::exception_ptr original,
    const char* context)
{
    const auto cleanup_result = cleanupTransmissionBackend();
    if (cleanup_result.ok)
        std::rethrow_exception(original);

    state_.store(State::FAILED, std::memory_order_release);
    std::string original_error = "Unknown configuration failure.";
    try
    {
        std::rethrow_exception(original);
    }
    catch (const std::exception& e)
    {
        original_error = e.what();
    }
    catch (...)
    {
    }

    std::string error = original_error + " " + context + " cleanup failed";
    if (!cleanup_result.error.empty())
        error += ": " + cleanup_result.error;
    fire_transmit_cb(
        TransmissionCallbackEvent::FAILED,
        LogLevel::ERROR,
        error,
        0.0);
    throw std::runtime_error(error);
}

int WsprTransmitter::getOutputPowerMilliwatts(int level)
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        return rpi_backend_->getOutputPowerMilliwatts(level);
    }
#else
    (void)level;
#endif

    return 0;
}

inline double WsprTransmitter::convert_mw_dbm(double mw)
{
    if (mw <= 0.0)
    {
        throw std::domain_error(
            "WsprTransmitter::convert_mw_dbm: Input power (mW) must "
            "be > 0 to compute logarithm");
    }
    return 10.0 * std::log10(mw);
}

void WsprTransmitter::thread_entry()
{
    const int ncpu = cpu_count();

    if (ncpu > 1)
    {
        const ThreadAffinityResult affinity_result =
            pin_current_thread_to_cpu(tx_cpu_);

        if (affinity_result.status == ThreadAffinityStatus::Failed)
        {
            {
                std::ostringstream oss;
                oss << "thread_entry(): failed to set CPU affinity: "
                    << std::strerror(affinity_result.error_number);
                fire_transmit_cb(
                    TransmissionCallbackEvent::LOGGING,
                    LogLevel::DEBUG,
                    oss.str(),
                    0.0);
            }
        }
        else if (affinity_result.status == ThreadAffinityStatus::Unsupported)
        {
            fire_transmit_cb(
                TransmissionCallbackEvent::LOGGING,
                LogLevel::DEBUG,
                "thread_entry(): CPU affinity is unavailable on this platform; continuing without CPU pinning.",
                0.0);
        }
    }

    try
    {
        if (selected_backend_ != wsprrypi::BackendKind::SIMULATED)
            set_thread_priority();
    }
    catch (const std::system_error &e)
    {
        throw std::domain_error(
            std::string(
                "WsprTransmitter::thread_entry(): Error setting thread "
                "priority: ") +
            e.what());
    }
    catch (const std::exception &e)
    {
        throw std::domain_error(
            std::string("WsprTransmitter::thread_entry(): Unexpected error: ") + e.what());
    }
    transmit();
}

void WsprTransmitter::set_thread_priority()
{
    sched_param sch{};
    sch.sched_priority = thread_priority_;
    int ret = pthread_setschedparam(pthread_self(), thread_policy_, &sch);

    if (ret != 0)
    {
        throw std::runtime_error(
            std::string("WsprTransmitter::set_thread_priority(): pthread_setschedparam failed: ") +
            std::strerror(ret));
    }
}
void WsprTransmitter::emitSymbol(
    const std::uint32_t &sym_num,
    const double &tsym,
    int symbol_index)
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ == nullptr)
    {
        throw std::runtime_error(
            "Legacy symbol emission requires the GPIO backend.");
    }

    rpi_backend_->emitSymbol(
        buildTransmissionPlan(),
        sym_num,
        tsym,
        symbol_index);
#else
    (void)sym_num;
    (void)tsym;
    (void)symbol_index;
    throw std::runtime_error(
        "Legacy symbol emission is unavailable because the GPIO backend was not compiled.");
#endif
}

void WsprTransmitter::prepareTransmissionBackend()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->prepareTransmission();
    }
#endif
}

WsprTransmissionConfigureResult WsprTransmitter::configureTransmissionBackend()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ == nullptr)
    {
        throw std::runtime_error(
            "Legacy transmission path requires the GPIO backend.");
    }

    return rpi_backend_->configureTransmission(buildTransmissionPlan());
#else
    throw std::runtime_error(
        "Legacy transmission path is unavailable because the GPIO backend was not compiled.");
#endif
}

void WsprTransmitter::beginTransmissionOutput()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ == nullptr)
    {
        throw std::runtime_error(
            "Legacy transmission output requires the GPIO backend.");
    }

    rpi_backend_->beginTransmissionOutput(buildTransmissionPlan());
#else
    throw std::runtime_error(
        "Legacy transmission output is unavailable because the GPIO backend was not compiled.");
#endif
}

void WsprTransmitter::endTransmissionOutput()
{
#if WSPRRYPI_BACKEND_RPI_GPIO
    if (rpi_backend_ != nullptr)
    {
        rpi_backend_->endTransmissionOutput();
    }
#endif
}

std::string WsprTransmitter::stateToStringLower(State state)
{
    std::string s = wsprTransmitStateToString(state);
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c)
        { return std::tolower(c); });
    return s;
}
