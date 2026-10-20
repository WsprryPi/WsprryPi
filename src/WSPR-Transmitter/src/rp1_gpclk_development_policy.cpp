/* SPDX-License-Identifier: MIT */
#include "rp1_gpclk_development_policy.hpp"
#include <mutex>

namespace
{
using D = wsprrypi::Rp1GpclkDevelopmentDenial;
using R = wsprrypi::Rp1GpclkDevelopmentDecision;

R deny(D reason, const char* code, const char* explanation) noexcept
{
    return {false, reason, code, explanation,
        "Experimental development use only; this is not qualification or normal product eligibility."};
}

bool known_route(std::uint32_t route) noexcept
{
    return route == wsprrypi::kRp1GpclkDevelopmentRouteGpio4 ||
        route == wsprrypi::kRp1GpclkDevelopmentRouteGpio20;
}

std::mutex authorization_mutex;
std::optional<wsprrypi::Rp1GpclkDevelopmentPolicyInputs> armed_operation;
} // namespace

namespace wsprrypi
{
void armRp1GpclkDevelopmentOperation(Rp1GpclkDevelopmentPolicyInputs inputs)
{
    std::lock_guard<std::mutex> lock(authorization_mutex);
    armed_operation = std::move(inputs);
}

std::optional<Rp1GpclkDevelopmentPolicyInputs>
consumeRp1GpclkDevelopmentOperation(
    const std::string& operation_id,
    std::uint32_t requested_route,
    const Rp1GpclkProviderIdentity& identity)
{
    std::lock_guard<std::mutex> lock(authorization_mutex);
    if (!armed_operation)
        return std::nullopt;
    auto result = std::move(*armed_operation);
    armed_operation.reset();
    if (result.operation_id != operation_id ||
        result.requested_route != requested_route)
        return std::nullopt;
    result.identity = identity;
    result.module_route = identity.route;
    return result;
}

void invalidateRp1GpclkDevelopmentOperation() noexcept
{
    std::lock_guard<std::mutex> lock(authorization_mutex);
    armed_operation.reset();
}

bool rp1GpclkDevelopmentOperationArmedForRoute(
    std::uint32_t requested_route) noexcept
{
    std::lock_guard<std::mutex> lock(authorization_mutex);
    return armed_operation &&
        armed_operation->development_testing_enabled &&
        armed_operation->rp1_backend_selected &&
        known_route(requested_route) &&
        armed_operation->requested_route == requested_route;
}

Rp1GpclkDevelopmentDecision decideRp1GpclkDevelopmentUse(
    const Rp1GpclkDevelopmentPolicyInputs& i) noexcept
{
    if (!i.development_testing_enabled)
        return deny(D::development_testing_disabled, "development-testing-disabled", "RP1 GPCLK development testing requires a deliberate operation-scoped enablement.");
    if (!i.rp1_backend_selected)
        return deny(D::unsupported_backend, "unsupported-backend", "The operation is not using the RP1 GPCLK backend.");
    if (!known_route(i.requested_route) || !known_route(i.persisted_route))
        return deny(D::invalid_route, "invalid-route", "An explicit GPIO4 or GPIO20 route is required.");
    if (i.active_route_count == 0)
        return deny(D::zero_route_topology, "zero-route-topology", "No active RP1 GPCLK route was established.");
    if (i.active_route_count != 1)
        return deny(D::ambiguous_topology, "ambiguous-topology", "Exactly one RP1 GPCLK route must be active.");
    if (i.requested_route != i.persisted_route || i.persisted_route != i.configured_route ||
        i.configured_route != i.active_route || i.active_route != i.module_route)
        return deny(D::configured_active_mismatch, "configured-active-mismatch", "Requested, saved, boot-configured, active, and module-reported routes must agree exactly.");
    if (i.identity.route != i.requested_route)
        return deny(D::configured_active_mismatch, "configured-active-mismatch", "The provider-reported route must match the selected route.");
    if (!i.route_transaction_resolved)
        return deny(D::unresolved_route_transaction, "unresolved-route-transaction", "Route state is unresolved.");
    if (!i.scheduler_idle)
        return deny(D::scheduler_conflict, "scheduler-conflict", "Scheduling must be idle before development output.");
    if (!i.application_owns_operation)
        return deny(D::transmission_owner_conflict, "transmission-owner-conflict", "WsprryPi does not exclusively own the intended operation.");
    if (!i.endpoint_available)
        return deny(D::endpoint_unavailable, "endpoint-unavailable", "The canonical endpoint is unavailable or was replaced.");
    if (!i.endpoint_closed || !i.endpoint_exclusively_acquirable)
        return deny(D::endpoint_already_owned, "endpoint-already-owned", "The endpoint is open, busy, or cannot be acquired exclusively.");
    if (i.cleanup_fault)
        return deny(D::cleanup_fault, "cleanup-fault", "A cleanup fault or unsafe terminal state is latched.");
    if (!i.confirmation_current || i.operation_id.empty() ||
        i.confirmation_operation_id != i.operation_id || i.confirmation_route != i.requested_route ||
        i.route_transaction_generation == 0 ||
        i.confirmation_route_transaction_generation != i.route_transaction_generation)
        return deny(D::stale_operator_confirmation, "stale-operator-confirmation", "Operator confirmation is absent, stale, or bound to another route or operation.");
    if (!i.physical_connection_confirmed || !i.attenuation_and_load_confirmed ||
        !i.bounded_operation_confirmed || !i.non_radiating_topology_confirmed ||
        !i.experimental_status_acknowledged)
        return deny(D::physical_topology_unconfirmed, "physical-topology-unconfirmed", "Current physical routing, attenuation, load, and non-radiating topology confirmation is required.");
    return {true, D::allowed, "development-testing-allowed",
        "All guarded development gates passed for the exact selected route and operation.",
        "Experimental development use only; this is not qualification or normal product eligibility."};
}
} // namespace wsprrypi
