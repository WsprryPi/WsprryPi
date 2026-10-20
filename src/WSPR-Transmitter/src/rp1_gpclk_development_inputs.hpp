/* SPDX-License-Identifier: MIT */
#pragma once

#include <cstdint>
#include <string>

namespace wsprrypi
{
struct Rp1GpclkDevelopmentInputs
{
    bool enabled{false};
    int persisted_gpio{0};
    int active_gpio{0};
    int module_gpio{0};
    unsigned active_route_count{0};
    bool route_transaction_resolved{false};
    bool scheduler_idle{false};
    bool application_owns_operation{false};
    bool endpoint_available{false};
    bool endpoint_closed{false};
    bool endpoint_exclusively_acquirable{false};
    bool cleanup_fault{false};
    bool physical_connection_confirmed{false};
    bool attenuation_and_load_confirmed{false};
    bool bounded_operation_confirmed{false};
    bool non_radiating_topology_confirmed{false};
    bool experimental_status_acknowledged{false};
    bool confirmation_current{false};
    std::uint64_t route_transaction_generation{0};
    std::uint64_t confirmation_route_transaction_generation{0};
    std::string operation_id;
    std::string confirmation_operation_id;
    int confirmation_gpio{0};
};
} // namespace wsprrypi
