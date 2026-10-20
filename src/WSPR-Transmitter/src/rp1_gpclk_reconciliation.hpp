#pragma once

#include <cstdint>
#include <string>

namespace wsprrypi
{
enum class Rp1GpclkRoute : std::uint32_t
{
    unavailable = 0,
    gpio4 = 4,
    gpio20 = 20
};

struct Rp1GpclkRouteExpectation
{
    Rp1GpclkRoute requested{Rp1GpclkRoute::unavailable};
    Rp1GpclkRoute persisted{Rp1GpclkRoute::unavailable};
    Rp1GpclkRoute configured{Rp1GpclkRoute::unavailable};
};

struct Rp1GpclkRouteState
{
    Rp1GpclkRoute requested{Rp1GpclkRoute::unavailable};
    Rp1GpclkRoute persisted{Rp1GpclkRoute::unavailable};
    Rp1GpclkRoute configured{Rp1GpclkRoute::unavailable};
    Rp1GpclkRoute active{Rp1GpclkRoute::unavailable};
    bool eligible{false};
    bool identity_matches{false};
    std::uint32_t compatibility_state{0};
    std::string module_id;
    std::string build_id;
    std::string compatibility_id;
};

Rp1GpclkRoute routeFromGpio(int gpio) noexcept;
std::uint32_t routeToUapi(Rp1GpclkRoute route) noexcept;
bool reconcileRp1GpclkRoute(
    const Rp1GpclkRouteState& state,
    std::string& error) noexcept;
} // namespace wsprrypi
