#include "rp1_gpclk_reconciliation.hpp"
#include "rp1_gpclk_uapi.h"

namespace wsprrypi
{
Rp1GpclkRoute routeFromGpio(int gpio) noexcept
{
    return gpio == 4 ? Rp1GpclkRoute::gpio4 :
        gpio == 20 ? Rp1GpclkRoute::gpio20 : Rp1GpclkRoute::unavailable;
}

std::uint32_t routeToUapi(Rp1GpclkRoute route) noexcept
{
    return route == Rp1GpclkRoute::gpio4 ? RP1_GPCLK_ROUTE_GPIO4 :
        route == Rp1GpclkRoute::gpio20 ? RP1_GPCLK_ROUTE_GPIO20 :
        RP1_GPCLK_ROUTE_INVALID;
}

bool reconcileRp1GpclkRoute(
    const Rp1GpclkRouteState& state,
    std::string& error) noexcept
{
    if (state.requested == Rp1GpclkRoute::unavailable ||
        state.persisted == Rp1GpclkRoute::unavailable ||
        state.configured == Rp1GpclkRoute::unavailable ||
        state.active == Rp1GpclkRoute::unavailable)
    {
        error = "RP1 GPCLK route reconciliation is incomplete; requested, persisted, configured, and active routes are required.";
        return false;
    }
    if (state.requested != state.persisted ||
        state.persisted != state.configured ||
        state.configured != state.active)
    {
        error = "RP1 GPCLK route mismatch; requested, persisted, configured, and active routes must match exactly.";
        return false;
    }
    if (!state.identity_matches)
    {
        error = "RP1 GPCLK provider UAPI or module identity does not match the selected provider identity.";
        return false;
    }
    if (!state.eligible)
    {
        error = "RP1 GPCLK active route is not live-eligible for this exact compatibility identity.";
        return false;
    }
    error.clear();
    return true;
}
} // namespace wsprrypi
