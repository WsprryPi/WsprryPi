// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
namespace wsprrypi {
// Translate the transport's conservative origin into the publication domain.
// The order of these samples must remain conservative across preemption.
template<class ControllerNow,class TransportNow>
std::optional<std::uint64_t> wtp_fleet_observation_origin(
    ControllerNow controller_now,TransportNow transport_now,std::uint64_t origin) {
    const auto controller=controller_now();
    const auto transport=transport_now();
    if(transport<origin || transport-origin>controller)return std::nullopt;
    return controller-(transport-origin);
}
}
