// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "WSPR-Transmitter/src/transmission_request.hpp"
#include <cstdint>
namespace wsprrypi {
// Explicit per-output schedule, independent of the host's local configuration.
TransmissionRequest wtp_fleet_request(const nlohmann::json& schedule);
std::uint64_t wtp_fleet_next_slot(const nlohmann::json& schedule,
    std::uint64_t earliest_ns, std::uint64_t last_start_ns);
void validate_wtp_fleet_schedule(const nlohmann::json& schedule);
}
