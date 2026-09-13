/**
 * @file wspr_transmit_types.hpp
 * @brief Shared transmission state and backend-neutral configuration types.
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

#ifndef WSPR_TRANSMIT_TYPES_HPP
#define WSPR_TRANSMIT_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "band_gpio.hpp"
#include "prepared_wspr_transmission.hpp"
#include "rp1_gpclk_development_inputs.hpp"
#include "transmission_request.hpp"

/**
 * @enum WsprTransmitState
 * @brief High-level runtime state for the transmitter controller.
 *
 * @details
 * The state machine is owned by the controller and observed by the active
 * backend. Values describe operational readiness and fault handling rather
 * than low-level hardware state.
 */
enum class WsprTransmitState
{
    DISABLED,
    ENABLED,
    TRANSMITTING,
    RECOVERING,
    COMPLETE,
    CANCELLED,
    FAILED,
    HUNG
};

constexpr const char *wsprTransmitStateToString(WsprTransmitState state) noexcept
{
    switch (state)
    {
    case WsprTransmitState::DISABLED:
        return "DISABLED";
    case WsprTransmitState::ENABLED:
        return "ENABLED";
    case WsprTransmitState::TRANSMITTING:
        return "TRANSMITTING";
    case WsprTransmitState::RECOVERING:
        return "RECOVERING";
    case WsprTransmitState::COMPLETE:
        return "COMPLETE";
    case WsprTransmitState::CANCELLED:
        return "CANCELLED";
    case WsprTransmitState::FAILED:
        return "FAILED";
    case WsprTransmitState::HUNG:
        return "HUNG";
    default:
        return "UNKNOWN";
    }
}

enum class WsprTransmitLogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

enum class WsprTransmissionCallbackEvent
{
    STARTING,
    PROGRESS,
    COMPLETE,
    CANCELLED,
    FAILED,
    SKIPPED,
    LOGGING
};

enum class TransmissionMode
{
    WSPR, ///< Prepared WSPR frame execution for one committed slot.
    TONE  ///< Direct RF tone execution for one committed runtime request.
};

inline constexpr int kWsprRandomOffsetHz = 80;

/**
 * @struct TransmissionRequest
 * @brief Complete execution-time request for one transmitter run.
 *
 * @details
 * Built by the orchestration layer and executed by the transmitter without
 * further policy decisions. It intentionally carries both transmitter-facing
 * data and orchestration metadata for the currently selected slot so callers
 * can keep one coherent snapshot for scheduling, logging, and GPIO control.
 */
struct TransmissionRequest
{
    /**
     * @brief Execution mode already chosen by the scheduler.
     *
     * This is an execution-time choice, not a backend or transmitter policy
     * decision. Tone mode is transient runtime behavior.
     */
    TransmissionMode mode = TransmissionMode::WSPR;

    /**
     * @brief Prepared WSPR frame data for this committed slot.
     *
     * This is empty for tone mode. For paired WSPR, the scheduler commits
     * one slot at a time even if the saved scheduler plan spans two slots.
     */
    PreparedWsprTransmission payload{};

    /**
     * @brief Scheduler-selected dial frequency in hertz (Hz).
     *
     * This remains the user-facing dial frequency even when the actual RF
     * execution frequency differs because of audio offset or random offset.
     */
    double dial_frequency_hz = 0.0;

    /**
     * @brief Committed RF frequency to realize in hertz (Hz).
     *
     * This is the execution frequency after scheduler-side audio-offset and
     * optional random-offset handling.
     */
    double actual_rf_frequency_hz = 0.0;
    std::optional<std::chrono::nanoseconds> tone_duration{};

    /**
     * @brief PPM correction committed for this execution.
     *
     * Backends consume this value directly and must not fetch PPM through a
     * separate policy path.
     */
    double ppm = 0.0;
    int power_level = 0;

    /**
     * @brief BCM GPIO used for RF output.
     *
     * This is the transmit-output GPIO consumed by the backend, not the
     * scheduler-owned band-selector GPIO.
     */
    int tx_gpio = 4;
    wsprrypi::Rp1GpclkDevelopmentInputs rp1_development{};

    /**
     * @brief Whether the scheduler enabled random WSPR offset for this slot.
     *
     * This is informative metadata describing how the committed RF frequency
     * was chosen. It is not a request for the backend to add offset.
     */
    bool use_offset = false;

    /**
     * @brief Scheduler-applied WSPR random offset in hertz (Hz).
     *
     * Zero means no random offset was committed for this request.
     */
    double applied_offset_hz = 0.0;

    /**
     * @brief Original user-facing frequency token for logs and diagnostics.
     */
    std::string frequency_entry_label{};
    bool allow_unqualified_frequency = false;
    bool allow_non_amateur_frequency = false;
    wsprrypi::HardwareProfile hardware_profile =
        wsprrypi::HardwareProfile::UNSPECIFIED;

    /**
     * @brief Whether the scheduler prepared a selector GPIO for this request.
     *
     * This snapshot is scheduler metadata used to recover selector state
     * from the committed request without re-running policy resolution.
     */
    bool selector_gpio_enabled = false;

    /**
     * @brief True only when the scheduler intentionally commits a skipped slot.
     *
     * This must not be inferred from a zero RF frequency because waiting and
     * logging paths are not scheduling skips.
     */
    bool skip_window = false;

    /**
     * @brief Scheduler-selected amateur band for LPF control.
     */
    HamBand selector_band = HamBand::BAND_2200M;

    /**
     * @brief Scheduler-selected GPIO configuration for LPF control.
     */
    BandGPIOConfig selector_gpio_config{};

    bool isTone() const noexcept
    {
        return mode == TransmissionMode::TONE;
    }

    bool isSkipWindow() const noexcept
    {
        return !isTone() && skip_window;
    }

    bool hasSelectorGPIO() const noexcept
    {
        return selector_gpio_enabled &&
               selector_gpio_config.enabled &&
               selector_gpio_config.gpio >= 0;
    }

    std::size_t totalSymbolCount() const noexcept
    {
        return isTone() ? 0U : payload.totalSymbolCount();
    }
};

/**
 * @struct WsprTransmissionPlan
 * @brief Backend-neutral snapshot of transmission intent and configuration.
 *
 * @details
 * The transmitter constructs this lightweight plan from the committed
 * execution request and passes it to a backend when preparing, configuring,
 * or emitting a transmission. It contains only the data a backend should
 * need to render RF, without exposing scheduler metadata or backend-private
 * runtime state such as DMA cursors, watchdog state, mailbox handles, or
 * recovery bookkeeping.
 */
struct WsprTransmissionPlan
{
    /**
     * @brief Requested RF center frequency in hertz (Hz).
     */
    double frequency_hz = 0.0;

    /**
     * @brief Tone spacing in hertz (Hz); zero selects a single RF frequency.
     */
    double tone_spacing_hz = 0.0;

    /**
     * @brief Logical output power level index used by the active backend.
     *
     * @details
     * This is not a calibrated power value in milliwatts (mW) or dBm. Each
     * backend interprets the level according to its hardware-specific output
     * control model.
     */
    int power_level = 0;

    /**
     * @brief Applied frequency correction in parts per million (PPM).
     *
     * @details
     * The controller commits this as part of the execution request so the
     * backend can derive hardware clocking for the active transmission
     * without relying on an out-of-band mutation path.
     */
    double ppm = 0.0;

    /**
     * @brief BCM GPIO used to emit the GPCLK0-based RF output.
     */
    int tx_gpio = 4;

    /**
     * @brief Total number of symbols that will be emitted for this transmission.
     *
     * @details
     * Tone mode sets this to zero because it is open-ended. Prepared WSPR mode
     * reports the total symbol count across all encoded frames.
     */
    std::size_t total_symbol_count = 0;

    std::size_t symbolCount() const noexcept
    {
        return total_symbol_count;
    }

    double symbolFrequencyHz(std::uint32_t symbol) const noexcept
    {
        return frequency_hz - 1.5 * tone_spacing_hz +
            static_cast<double>(symbol) * tone_spacing_hz;
    }
};

// The legacy TONE emitter holds symbol zero. A zero-spacing table keeps that
// symbol at the committed carrier without changing request/status frequency.
// Configuration, emission, and restart must all use this same adaptation.
inline WsprTransmissionPlan makeLegacyGpioTransmissionPlan(
    const TransmissionRequest& request) noexcept
{
    return WsprTransmissionPlan{
        request.actual_rf_frequency_hz,
        request.isTone() ? 0.0 : 12000.0 / 8192.0,
        request.power_level,
        request.ppm,
        request.tx_gpio,
        request.totalSymbolCount()};
}

/**
 * @struct WsprTransmissionConfigureResult
 * @brief Result returned by a backend after applying transmission setup.
 *
 * @details
 * This structure reports the small amount of backend-neutral information the
 * controller may need after hardware-specific configuration has completed.
 * It intentionally avoids exposing backend internals while still allowing the
 * controller to reflect adjusted transmit parameters back to callers.
 */
struct WsprTransmissionConfigureResult
{
    /**
     * @brief Actual RF center frequency applied by the backend in hertz (Hz).
     *
     * @details
     * This may differ from the requested plan frequency when hardware tuning
     * resolution requires quantization or a nearby realizable value.
     */
    double applied_frequency_hz = 0.0;
};

#endif
