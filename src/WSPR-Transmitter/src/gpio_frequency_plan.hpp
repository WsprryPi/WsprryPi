// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#ifndef WSPRRYPI_GPIO_FREQUENCY_PLAN_HPP
#define WSPRRYPI_GPIO_FREQUENCY_PLAN_HPP

#include "wspr_transmit_types.hpp"
#include <array>

std::uint32_t gpioBuildDividerWord(
    double source_hz,
    double tone_hz,
    bool round_up_one_lsb);

struct GpioDividerTablePlan
{
    double applied_frequency_hz;
    std::array<std::uint32_t, 8> words;
};

// Pure arithmetic shared by DMA setup and hardware-free regression tests.
GpioDividerTablePlan gpioPlanDividerTable(
    double source_hz,
    const WsprTransmissionPlan& plan);

std::int64_t gpioDitherLowerClockCount(
    double lower_ratio,
    std::int64_t block_clocks,
    std::int64_t clocks_scheduled,
    std::int64_t lower_clocks_scheduled);

#endif
