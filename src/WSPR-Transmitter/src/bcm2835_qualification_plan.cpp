// SPDX-License-Identifier: MIT
#include "bcm2835_qualification_plan.hpp"
#include "gpio_band_policy.hpp"
#include "legacy_gpio_clock_model.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wsprrypi
{
Bcm2835QualificationPlan planBcm2835Qualification(
    int pin, int drive, int carrier_hz, double additional_ppm)
{
    if ((pin != 4 && pin != 20) || drive < 0 || drive > 7)
        throw std::invalid_argument("Unsupported BCM2835 qualification route or drive.");
    const auto band = evaluate_frequency_policy(BackendKind::RPI_CLOCK_GPIO,
        TransmissionMode::TONE, carrier_hz, false, false, HardwareProfile::BCM2835);
    if (!band.allowed)
        throw std::invalid_argument(band.error);

    ::TransmissionRequest request;
    request.mode = ::TransmissionMode::TONE;
    request.actual_rf_frequency_hz = carrier_hz;
    request.power_level = drive;
    request.ppm = additional_ppm;
    request.tx_gpio = pin;
    const auto tone = makeLegacyGpioTransmissionPlan(request);
    const auto clock = selectLegacyGpioClockForAdditionalCorrection(
        LegacyGpioProcessorProfile::Bcm2835, carrier_hz, carrier_hz, additional_ppm);
    const auto dividers = gpioPlanDividerTable(clock.corrected_rate_hz, tone);
    const double pwm = clock.model.nominal_rate_hz * (1 + additional_ppm / 1e6) / 2;
    const double low = clock.corrected_rate_hz * 4096 / dividers.words[0];
    const double high = clock.corrected_rate_hz * 4096 / dividers.words[1];
    const double ratio = std::clamp((high - carrier_hz) / (high - low), 0.0, 1.0);
    const auto clocks = bcm2835_qualification_dither_clocks;
    const auto lower = gpioDitherLowerClockCount(ratio, clocks, 0, 0);
    const double mean = (low * lower + high * (clocks - lower)) / clocks;

    // The repeated ring distributes its integer duty over equally sized
    // blocks. Within a block the cumulative phase-count discrepancy is at
    // most one block plus one sample. Two such endpoints bound any window
    // of at least one modeled second after the PWM FIFO and ring settle.
    const double block = clocks / bcm2835_qualification_dither_blocks;
    const double edge = 2 * (block + 1) * 1024 / pwm * (high - low);
    constexpr double rounding_margin_hz = 0.000001;
    return {tone, dividers, clock.corrected_rate_hz, pwm,
        clock.correction.intrinsic_ppm, clock.correction.effective_ppm, lower,
        static_cast<std::int64_t>(std::floor((mean - edge - rounding_margin_hz) * 1e9)),
        static_cast<std::int64_t>(std::ceil((mean + edge + rounding_margin_hz) * 1e9))};
}
}
