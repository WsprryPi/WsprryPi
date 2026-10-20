/**
 * @file web_server_admin_http.cpp
 * @brief Builds privileged-administration responses without HTTP transport templates.
 */

#include "web_server_admin_http.hpp"

#include "json.hpp"
#include "logging.hpp"
#include "privileged_network_admin.hpp"
#include "rp1_gpclk_route_service.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace web_server_routes
{
namespace
{
const char *network_mode_name(PrivilegedNetworkMode mode) noexcept
{
    return mode == PrivilegedNetworkMode::insecure_disabled
        ? "insecure-disabled" : "enforced";
}
} // namespace

RouteResponse build_network_safety_status(
    PrivilegedNetworkAdmin &privileged_network_admin)
{
    const auto state = privileged_network_admin.status();
    const nlohmann::json body = {
        {"configured_known", state.configured_known},
        {"active_known", state.active_known},
        {"setting_was_valid", state.setting_was_valid},
        {"setting_was_missing", state.setting_was_missing},
        {"configured", state.configured_known
            ? nlohmann::json(network_mode_name(state.configured))
            : nlohmann::json(nullptr)},
        {"active", state.active_known
            ? nlohmann::json(network_mode_name(state.active))
            : nlohmann::json(nullptr)},
        {"status", state.active_known &&
            state.active == PrivilegedNetworkMode::insecure_disabled
            ? "NETWORK SAFETY OFF" : state.active_known
            ? "NETWORK SAFETY ENFORCED"
            : "NETWORK SAFETY STATE UNKNOWN"}};
    return {200, body.dump(4), "application/json", false};
}

RouteResponse apply_network_safety(
    const std::string &request_body,
    PrivilegedNetworkAdmin &privileged_network_admin)
{
    try
    {
        const auto request = nlohmann::json::parse(request_body);
        const std::optional<std::string> mode =
            request.contains("mode") && request["mode"].is_string()
            ? std::optional<std::string>(request["mode"].get<std::string>())
            : std::nullopt;
        const auto result = privileged_network_admin.apply(mode);
        const auto state = result.state;
        const nlohmann::json body = {
            {"applied", result.applied()},
            {"result", privileged_network_transaction_status_name(result.status)},
            {"configured_known", state.configured_known},
            {"active_known", state.active_known},
            {"configured", state.configured_known
                ? nlohmann::json(network_mode_name(state.configured))
                : nlohmann::json(nullptr)},
            {"active", state.active_known
                ? nlohmann::json(network_mode_name(state.active))
                : nlohmann::json(nullptr)},
            {"status", result.status_text()},
            {"warning_defaulted_to_enforced",
             result.warning_defaulted_to_enforced}};
        if (result.applied())
        {
            llog.logS(
                result.status_text() == "NETWORK SAFETY OFF" ? WARN : INFO,
                result.status_text());
        }
        else
        {
            llog.logS(
                ERROR,
                "Privileged network safety apply failed: ",
                privileged_network_transaction_status_name(result.status));
        }
        const int status = result.applied() ? 200 :
            result.status == PrivilegedNetworkTransactionStatus::rollback_failed
            ? 500 : 409;
        return {status, body.dump(4), "application/json", false};
    }
    catch (const nlohmann::json::parse_error &)
    {
        return {
            400,
            R"({"error":"invalid_json","message":"Malformed JSON request."})",
            "application/json",
            false};
    }
    catch (const std::exception &error)
    {
        llog.logS(
            ERROR, "Privileged network safety apply failed: ",
            std::string(error.what()));
        return {
            500,
            R"({"error":"apply_failed","message":"The network safety transaction could not be completed."})",
            "application/json",
            false};
    }
}

RouteResponse build_rp1_gpclk_route_status(
    wsprrypi::Rp1GpclkRouteService &rp1_gpclk_route_service)
{
    const auto body = rp1_gpclk_route_service.query();
    return {200, body.dump(4), "application/json", false};
}

RouteResponse apply_rp1_gpclk_route(
    const std::string &request_body,
    wsprrypi::Rp1GpclkRouteService &rp1_gpclk_route_service)
{
    try
    {
        const auto request = nlohmann::json::parse(request_body);
        if (!request.contains("operation") ||
            !request["operation"].is_string() ||
            !request.contains("route") ||
            !request["route"].is_string() ||
            !request.contains("generation") ||
            !request["generation"].is_number_unsigned())
        {
            throw std::invalid_argument(
                "operation, route, and generation are required");
        }
        const auto body = rp1_gpclk_route_service.operate(
            request["operation"].get<std::string>(),
            request["route"].get<std::string>(),
            request["generation"].get<std::uint64_t>());
        return {
            body.value("ok", false) ? 200 : 409,
            body.dump(4),
            "application/json",
            false};
    }
    catch (const nlohmann::json::parse_error &)
    {
        return {
            400,
            R"({"error":"invalid_json","message":"Malformed JSON request."})",
            "application/json",
            false};
    }
    catch (const std::exception &)
    {
        return {
            400,
            R"({"error":"invalid_request","message":"A fixed operation, GPIO4 or GPIO20 route, and generation are required."})",
            "application/json",
            false};
    }
}
} // namespace web_server_routes
