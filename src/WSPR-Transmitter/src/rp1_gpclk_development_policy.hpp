/* SPDX-License-Identifier: MIT */
#pragma once

#include "rp1_gpclk_backend.hpp"

#include <cstdint>
#include <string>
#include <optional>

namespace wsprrypi
{
inline constexpr std::uint32_t kRp1GpclkDevelopmentRouteGpio4 = 1;
inline constexpr std::uint32_t kRp1GpclkDevelopmentRouteGpio20 = 2;
enum class Rp1GpclkDevelopmentDenial
{
    allowed,
    development_testing_disabled,
    unsupported_backend,
    invalid_route,
    zero_route_topology,
    ambiguous_topology,
    configured_active_mismatch,
    unresolved_route_transaction,
    scheduler_conflict,
    transmission_owner_conflict,
    endpoint_unavailable,
    endpoint_already_owned,
    stale_operator_confirmation,
    physical_topology_unconfirmed,
    cleanup_fault,
    internal_state_invalid
};

struct Rp1GpclkDevelopmentPolicyInputs
{
    bool development_testing_enabled{false};
    bool rp1_backend_selected{false};
    std::uint32_t requested_route{0};
    std::uint32_t persisted_route{0};
    std::uint32_t configured_route{0};
    std::uint32_t active_route{0};
    std::uint32_t module_route{0};
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
    std::uint32_t confirmation_route{0};
    Rp1GpclkProviderIdentity identity;
};

struct Rp1GpclkDevelopmentDecision
{
    bool allowed{false};
    Rp1GpclkDevelopmentDenial reason{
        Rp1GpclkDevelopmentDenial::development_testing_disabled};
    const char* code{"development-testing-disabled"};
    const char* explanation{"RP1 GPCLK development testing is disabled."};
    const char* warning{
        "Experimental development use only; this is not qualification or normal product eligibility."};
};

Rp1GpclkDevelopmentDecision decideRp1GpclkDevelopmentUse(
    const Rp1GpclkDevelopmentPolicyInputs& inputs) noexcept;
void armRp1GpclkDevelopmentOperation(
    Rp1GpclkDevelopmentPolicyInputs inputs);
std::optional<Rp1GpclkDevelopmentPolicyInputs>
consumeRp1GpclkDevelopmentOperation(
    const std::string& operation_id,
    std::uint32_t requested_route,
    const Rp1GpclkProviderIdentity& identity);
void invalidateRp1GpclkDevelopmentOperation() noexcept;
bool rp1GpclkDevelopmentOperationArmedForRoute(
    std::uint32_t requested_route) noexcept;
} // namespace wsprrypi
