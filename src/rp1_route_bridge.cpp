#include "rp1_route_bridge.hpp"

#include "rp1_gpclk_route_service.hpp"
#include "scheduling_rp1_test_support.hpp"

#include <stdexcept>
#include <utility>

namespace
{
Rp1DevelopmentReconcileInvokerForTest development_reconcile_invoker;

int route_gpio(const std::string &value)
{
    return value == "GPIO4" ? 4 : value == "GPIO20" ? 20 : 0;
}

nlohmann::json reconcile_development(const std::string &route)
{
    return development_reconcile_invoker
        ? development_reconcile_invoker(route)
        : wsprrypi::productionRp1GpclkRouteService()
              .reconcileDevelopmentStartup(route);
}

void apply_observed_route(
    const nlohmann::json &observed,
    int requested_gpio,
    const std::string &operation_id,
    TransmissionRequest &request)
{
    if (!observed.value("ok", false))
        throw std::runtime_error(observed.value(
            "message", std::string("RP1 route reconciliation failed.")));

    auto &development = request.rp1_development;
    development.enabled = true;
    development.persisted_gpio = route_gpio(observed.value("persisted", std::string{}));
    development.active_gpio = route_gpio(observed.value("active", std::string{}));
    development.module_gpio = requested_gpio;
    development.active_route_count = development.active_gpio == 0 ? 0U : 1U;
    development.route_transaction_resolved =
        observed.value("reconciled", false) &&
        observed.value("journal", std::string{}) == "none";
    development.scheduler_idle = true;
    development.application_owns_operation = true;
    development.endpoint_available = observed.value("ok", false);
    development.endpoint_closed = !observed.value("endpointOpen", true);
    development.endpoint_exclusively_acquirable =
        observed.value("endpointOwned", false) && development.endpoint_closed;
    development.cleanup_fault =
        observed.value("state", std::string{}) == "rollback_required";
    development.confirmation_current = true;
    development.route_transaction_generation = observed.value("generation", 0ULL);
    development.confirmation_route_transaction_generation =
        development.route_transaction_generation;
    development.operation_id = operation_id;
    development.confirmation_operation_id = operation_id;
    development.confirmation_gpio = requested_gpio;
}
}

bool apply_direct_rp1_development_confirmation_bridge(
    const ArgParserConfig &cfg,
    TransmissionRequest &request,
    std::string *error_message)
{
    if (cfg.transmit_backend != TransmitBackendKind::RP1_GPCLK)
        return true;
    try
    {
        const auto confirmation = nlohmann::json::parse(
            cfg.rp1_development_confirmation_json);
        const std::string route = confirmation.at("route").get<std::string>();
        const int requested_gpio = route_gpio(route);
        const std::string operation_id =
            confirmation.at("operation_id").get<std::string>();
        if (!confirmation.at("enabled").get<bool>() || requested_gpio == 0 ||
            requested_gpio != cfg.gpio_tx_pin || operation_id.size() < 8 ||
            operation_id.size() > 64 ||
            !confirmation.at("physical_connection_confirmed").get<bool>() ||
            !confirmation.at("attenuation_and_load_confirmed").get<bool>() ||
            !confirmation.at("bounded_operation_confirmed").get<bool>() ||
            !confirmation.at("non_radiating_topology_confirmed").get<bool>() ||
            !confirmation.at("experimental_status_acknowledged").get<bool>())
            throw std::runtime_error(
                "RP1 direct-CLI development confirmation is incomplete or mismatched.");

        apply_observed_route(
            reconcile_development(route), requested_gpio, operation_id, request);
        auto &development = request.rp1_development;
        development.physical_connection_confirmed = true;
        development.attenuation_and_load_confirmed = true;
        development.bounded_operation_confirmed = true;
        development.non_radiating_topology_confirmed = true;
        development.experimental_status_acknowledged = true;
        return true;
    }
    catch (const std::exception &error)
    {
        if (error_message != nullptr)
            *error_message = error.what();
        return false;
    }
}

void apply_test_tone_rp1_development_confirmation_bridge(
    const ParsedTestToneRequest::Rp1DevelopmentConfirmation &confirmation,
    const ArgParserConfig &cfg,
    TransmissionRequest &request)
{
    if (cfg.transmit_backend != TransmitBackendKind::RP1_GPCLK ||
        confirmation.route_gpio != cfg.gpio_tx_pin)
        throw std::runtime_error(
            "RP1 development confirmation does not match the selected backend and route.");

    const std::string route = confirmation.route_gpio == 4 ? "GPIO4" : "GPIO20";
    apply_observed_route(
        reconcile_development(route), confirmation.route_gpio,
        confirmation.operation_id, request);
    auto &development = request.rp1_development;
    development.physical_connection_confirmed = confirmation.physical_connection;
    development.attenuation_and_load_confirmed = confirmation.attenuation_and_load;
    development.bounded_operation_confirmed = confirmation.bounded_operation;
    development.non_radiating_topology_confirmed = confirmation.non_radiating_topology;
    development.experimental_status_acknowledged = confirmation.experimental_acknowledged;
}

Rp1IdleReconciliation reconcile_rp1_idle_startup(int gpio)
{
    const auto value = wsprrypi::productionRp1GpclkRouteService()
        .reconcileIdleStartup(gpio == 4 ? "GPIO4" : "GPIO20");
    return {value.value("ok", false),
            value.value("message", std::string("unknown route state")),
            value.value("policyDomain", std::string{})};
}

bool acknowledge_rp1_restoration(const std::string &token, bool transmit)
{
    return wsprrypi::productionRp1GpclkRouteService()
        .acknowledgeRestoration(token, transmit);
}

Rp1RouteStatus query_rp1_route_status()
{
    const auto value = wsprrypi::productionRp1GpclkRouteService().query(false);
    return {value.value("requested", std::string("Unavailable")),
            value.value("persisted", std::string("Unavailable")),
            value.value("configured", std::string("Unavailable")),
            value.value("active", std::string("Unavailable")),
            value.value("eligible", false),
            value.value("journal", std::string("unknown"))};
}

void set_rp1_development_reconcile_invoker_for_test(
    Rp1DevelopmentReconcileInvokerForTest invoker)
{
    development_reconcile_invoker = std::move(invoker);
}

void reset_rp1_development_reconcile_invoker_bridge() noexcept
{
    development_reconcile_invoker = {};
}
