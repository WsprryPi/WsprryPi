#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wsprrypi::standard_feld
{

inline constexpr std::string_view kProfileId = "standard-feld-wsprry-v1";
inline constexpr std::string_view kAssetId =
    "wsprry-standard-feld-radiolib-5x5-v1";
inline constexpr std::string_view kCanonicalAssetSha256 =
    "025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227";
inline constexpr std::string_view kUpstreamCommit =
    "0795caa41c6350a2f862137cfc22528c2aaad2bc";
inline constexpr std::string_view kUpstreamFileSha256 =
    "44e1e4fd22d130d018e8e02745845fe2cf059eb6730a813190f8a2b30486c3cb";

inline constexpr std::uint32_t kPositionsPerSecond = 245;
inline constexpr std::size_t kColumnsPerCell = 7;
inline constexpr std::size_t kPhysicalPositionsPerColumn = 14;
inline constexpr std::size_t kPositionsPerCell =
    kColumnsPerCell * kPhysicalPositionsPerColumn;
inline constexpr unsigned char kFirstCodePoint = 0x20;
inline constexpr unsigned char kLastCodePoint = 0x5f;

// Five stored image rows, top to bottom. Bit 6 is the leftmost column and
// bit 0 the rightmost. Generated from the canonical JSON named above.
inline constexpr std::array<std::array<std::uint8_t, 5>, 64> kStoredRows{{
    {0, 0, 0, 0, 0}, {8, 8, 8, 0, 8}, {20, 20, 0, 0, 0},
    {20, 62, 20, 62, 20}, {62, 40, 62, 10, 62}, {50, 52, 8, 22, 38},
    {16, 40, 16, 40, 52}, {8, 8, 0, 0, 0}, {4, 8, 8, 8, 4},
    {16, 8, 8, 8, 16}, {20, 8, 20, 0, 0}, {8, 8, 62, 8, 8},
    {0, 0, 0, 8, 16}, {0, 0, 62, 0, 0}, {0, 0, 0, 0, 8},
    {2, 4, 8, 16, 32}, {28, 38, 42, 50, 28}, {24, 8, 8, 8, 8},
    {24, 36, 8, 16, 60}, {60, 4, 28, 4, 60}, {36, 36, 60, 4, 4},
    {28, 32, 60, 4, 60}, {60, 32, 60, 36, 60}, {60, 4, 8, 16, 32},
    {60, 36, 24, 36, 60}, {60, 36, 60, 4, 60}, {0, 8, 0, 0, 8},
    {0, 8, 0, 8, 8}, {4, 8, 16, 8, 4}, {0, 62, 0, 62, 0},
    {16, 8, 4, 8, 16}, {28, 4, 8, 0, 8}, {28, 34, 46, 42, 12},
    {62, 34, 62, 34, 34}, {60, 18, 30, 18, 60}, {30, 48, 32, 48, 30},
    {60, 34, 34, 34, 60}, {62, 32, 60, 32, 62}, {62, 32, 60, 32, 32},
    {62, 32, 46, 34, 62}, {34, 34, 62, 34, 34}, {28, 8, 8, 8, 28},
    {60, 8, 8, 40, 56}, {36, 40, 48, 40, 36}, {32, 32, 32, 32, 60},
    {34, 54, 42, 34, 34}, {34, 50, 42, 38, 34}, {28, 34, 34, 34, 28},
    {62, 34, 62, 32, 32}, {62, 34, 34, 38, 62}, {62, 34, 62, 36, 34},
    {62, 32, 62, 2, 62}, {62, 8, 8, 8, 8}, {34, 34, 34, 34, 62},
    {34, 34, 20, 20, 8}, {34, 34, 42, 54, 34}, {34, 20, 8, 20, 34},
    {34, 20, 8, 8, 8}, {62, 4, 8, 16, 62}, {12, 8, 8, 8, 12},
    {32, 16, 8, 4, 2}, {24, 8, 8, 8, 24}, {8, 20, 0, 0, 0},
    {0, 0, 0, 0, 62}
}};

constexpr bool stored_pixel(unsigned char code_point,
                            std::size_t image_row,
                            std::size_t column) noexcept
{
    if (code_point < kFirstCodePoint || code_point > kLastCodePoint ||
        image_row >= 5 || column >= kColumnsPerCell)
        return false;

    const auto row = kStoredRows[code_point - kFirstCodePoint][image_row];
    return (row & (std::uint8_t{1} << (6U - column))) != 0;
}

constexpr bool physical_pixel(unsigned char code_point,
                              std::size_t column,
                              std::size_t position_bottom_to_top) noexcept
{
    if (position_bottom_to_top >= kPhysicalPositionsPerColumn)
        return false;

    const std::size_t logical_row_bottom_to_top = position_bottom_to_top / 2U;
    if (logical_row_bottom_to_top == 0U || logical_row_bottom_to_top == 6U)
        return false;

    const std::size_t image_row_top_to_bottom = 5U - logical_row_bottom_to_top;
    return stored_pixel(code_point, image_row_top_to_bottom, column);
}

} // namespace wsprrypi::standard_feld
