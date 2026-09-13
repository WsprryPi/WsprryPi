// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "gpio_frequency_plan.hpp"
#include "legacy_gpio_clock_model.hpp"

#include <cmath>
#include <stdexcept>

namespace
{
constexpr std::uint32_t kGpclkDividerMask = 0x00FFFFFFu;
constexpr double kGpclkDividerScale = 4096.0;
}

std::uint32_t gpioBuildDividerWord(
    double source_hz,
    double tone_hz,
    bool round_up_one_lsb)
{
    if (!wsprrypi::legacyGpioClockCanRepresent(source_hz, tone_hz, tone_hz))
    {
        throw std::out_of_range(
            "GPIO RF tuning word is outside the GPCLK 12.12 divisor range.");
    }
    const double scaled = source_hz / tone_hz * kGpclkDividerScale;
    const double word = std::floor(scaled) + (round_up_one_lsb ? 1.0 : 0.0);
    if (word < 0.0 || word > static_cast<double>(kGpclkDividerMask))
    {
        throw std::out_of_range(
            "GPIO RF tuning word exceeds the GPCLK divider field.");
    }
    return static_cast<std::uint32_t>(word);
}

GpioDividerTablePlan gpioPlanDividerTable(
    double source_hz,
    const WsprTransmissionPlan& plan)
{
    if (!std::isfinite(plan.tone_spacing_hz) || plan.tone_spacing_hz < 0.0)
        throw std::invalid_argument("GPIO tone spacing must be finite and non-negative.");

    const auto low_word = gpioBuildDividerWord(source_hz, plan.symbolFrequencyHz(0), true);
    const auto high_word = gpioBuildDividerWord(
        source_hz, plan.frequency_hz + 1.5 * plan.tone_spacing_hz, false);
    WsprTransmissionPlan applied = plan;
    if ((low_word >> 12) != (high_word >> 12))
    {
        // Changing the integer divisor during dithering is unsafe. Preserve
        // the existing modulated-table adjustment, but never move an exact
        // carrier request to a nearby frequency without the caller's consent.
        if (plan.tone_spacing_hz == 0.0)
            throw std::out_of_range(
                "GPIO test tone cannot preserve the requested RF frequency at this divider boundary.");
        applied.frequency_hz = source_hz / static_cast<double>(low_word >> 12) -
            1.6 * plan.tone_spacing_hz;
    }

    GpioDividerTablePlan result{applied.frequency_hz, {}};
    for (std::size_t symbol = 0; symbol < 4; ++symbol)
    {
        const double frequency = applied.symbolFrequencyHz(symbol);
        result.words[2 * symbol] = gpioBuildDividerWord(source_hz, frequency, true);
        result.words[2 * symbol + 1] = gpioBuildDividerWord(source_hz, frequency, false);
        if ((result.words[2 * symbol] >> 12) != (result.words[2 * symbol + 1] >> 12))
            throw std::out_of_range("GPIO tone divider pair crosses an integer boundary.");
    }
    return result;
}
