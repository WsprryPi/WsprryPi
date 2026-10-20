/**
 * @file web_server_request_guard.cpp
 * @brief Registers the backend HTTP request-authorization boundary.
 */

#include "web_server_routes.hpp"

#include "backend_http_guard.hpp"
#include "httplib.hpp"
#include "logging.hpp"
#include "privileged_network_runtime.hpp"
#include "wtp_runtime_bridge.hpp"
#include "pico_usb_installer_state.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace web_server_routes
{
void register_request_guard(
    httplib::Server &server,
    NetworkSnapshotProvider network_snapshot_provider)
{
    server.set_pre_routing_handler(
        [network_snapshot_provider](
            const httplib::Request &request,
            httplib::Response &response) {
            const std::optional<std::string> origin =
                request.has_header("Origin")
                ? std::optional<std::string>(
                    request.get_header_value("Origin"))
                : std::nullopt;
            std::vector<std::string> trusted_proxy_identities;
            const std::string trusted_header(
                WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER);
            const auto trusted_count =
                request.get_header_value_count(trusted_header);
            for (std::size_t index = 0; index < trusted_count; ++index)
                trusted_proxy_identities.push_back(
                    request.get_header_value(trusted_header, "", index));
            SupportRequestGuardDecision rejection_reason =
                SupportRequestGuardDecision::allowed;
            if (evaluate_backend_http_request(
                    request.method, request.path, request.remote_addr,
                    request.get_header_value("Host"), origin,
                    network_snapshot_provider(),
                    active_privileged_network_mode(),
                    trusted_proxy_identities, &rejection_reason) ==
                BackendHttpGuardDecision::allowed) {
                if(wtp_runtime_firmware_busy() && wsprrypi::pico_usb_http_mutation(request.method,request.path)) {
                    response.status=409;
                    response.set_content(R"({"error":{"code":"pico_firmware_maintenance","message":"Pico firmware maintenance is active. Continue or recover the update before making changes."}})","application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
                return httplib::Server::HandlerResponse::Unhandled;
            }
            switch (rejection_reason) {
            case SupportRequestGuardDecision::invalid_trusted_proxy_identity:
                llog.logS(
                    WARN,
                    "HTTP request rejected: invalid trusted-proxy identity.");
                break;
            case SupportRequestGuardDecision::rejected_host:
                llog.logS(DEBUG, "HTTP request rejected: invalid Host.");
                break;
            case SupportRequestGuardDecision::rejected_origin:
                llog.logS(DEBUG, "HTTP request rejected: invalid Origin.");
                break;
            case SupportRequestGuardDecision::invalid_request:
                llog.logS(
                    DEBUG,
                    "HTTP request rejected: invalid method or route.");
                break;
            case SupportRequestGuardDecision::no_eligible_network:
                llog.logS(
                    DEBUG,
                    "HTTP request rejected: enforced safety has no eligible LAN.");
                break;
            case SupportRequestGuardDecision::interface_discovery_unavailable:
                llog.logS(
                    DEBUG,
                    "HTTP request rejected: current network discovery failed.");
                break;
            default:
                llog.logS(
                    DEBUG,
                    "HTTP request rejected: client network is not eligible.");
                break;
            }
            response.status = 403;
            response.headers.erase("Access-Control-Allow-Origin");
            response.set_content("Forbidden", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        });
}
} // namespace web_server_routes
