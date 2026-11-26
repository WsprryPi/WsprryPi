// SPDX-License-Identifier: MIT
#pragma once

#include "gpio_frequency_plan.hpp"
#include <cstdint>

namespace wsprrypi
{
struct Bcm2835QualificationPlan
{
    WsprTransmissionPlan tone;
    GpioDividerTablePlan dividers;
    double rf_clock_hz;
    double pwm_clock_hz;
    double intrinsic_ppm;
    double effective_ppm;
    std::int64_t lower_clocks;
    std::int64_t lower_nhz;
    std::int64_t upper_nhz;
};

// The immutable ring emits this many PWM DREQ samples per cycle.
inline constexpr std::int64_t bcm2835_qualification_dither_clocks = 4096;
inline constexpr unsigned bcm2835_qualification_dither_blocks = 64;

Bcm2835QualificationPlan planBcm2835Qualification(
    int pin, int drive, int carrier_hz, double additional_ppm);
}
