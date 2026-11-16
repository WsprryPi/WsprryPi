// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee C. Bussy
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace wsprrypi {
inline constexpr std::uint64_t solar_first_utc_ns=1'735'689'600'000'000'000ULL;
inline constexpr std::uint64_t solar_end_utc_ns=4'102'444'800'000'000'000ULL;
struct SolarSite {
    double latitude_degrees{},longitude_degrees{};
    bool operator==(const SolarSite&) const = default;
};
struct SolarPosition {
    double elevation_degrees{},hour_angle_radians{};
    bool morning{};
    double daily_minimum_degrees{},daily_maximum_degrees{};
};
struct SolarWindow {
    bool enabled=false;
    std::int32_t lower_mdeg=-6000,upper_mdeg=6000;
    bool valid() const noexcept;
    bool qualifies(double elevation_degrees) const noexcept;
};
enum class SolarPeriod { Day,Night,Morning,Evening };
struct SolarPeriodObservation {
    SolarPeriod period;
    // Daily extrema at this observation's declination, not a predicted crossing.
    bool prolonged;
};
std::optional<SolarSite> solar_locator_center(std::string_view locator) noexcept;
// Pure geometric Sun-center position; UTC only. No clock, GPS, or I/O acquisition.
std::optional<SolarPosition> solar_position(SolarSite,std::uint64_t utc_ns) noexcept;
bool solar_morning(double hour_angle_radians) noexcept;
std::optional<SolarPeriodObservation> solar_period(const SolarPosition&,
    const SolarWindow &morning,const SolarWindow &evening) noexcept;
}
