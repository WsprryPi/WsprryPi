// MIT License
// Copyright © 2025 - 2026 Lee C. Bussy (@LBussy)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the conditions in LICENSE.md.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr std::size_t WSPR_SYMBOL_COUNT = 162U;

struct PreparedWsprFrame
{
    std::array<std::uint8_t, WSPR_SYMBOL_COUNT> symbols{};
};

struct PreparedWsprTransmission
{
    std::string plan_type;
    std::vector<PreparedWsprFrame> frames;
    std::string callsign;
    std::string locator;
    std::string callsign_raw;
    std::string locator_raw;
    std::string callsign_normalized;
    std::string locator_normalized;
    std::vector<std::string> frame_callsigns;
    std::vector<std::string> frame_locators;
    std::size_t total_frame_count = 0;
    std::size_t current_frame = 0;
    std::string frame_callsign;
    std::string frame_locator;
    int power_dbm = 0;

    [[nodiscard]] bool empty() const noexcept { return frames.empty(); }
    [[nodiscard]] std::size_t frameCount() const noexcept { return frames.size(); }
    [[nodiscard]] std::size_t symbolCountPerFrame() const noexcept
    {
        return WSPR_SYMBOL_COUNT;
    }
    [[nodiscard]] std::size_t totalSymbolCount() const noexcept
    {
        return frames.size() * WSPR_SYMBOL_COUNT;
    }
};
