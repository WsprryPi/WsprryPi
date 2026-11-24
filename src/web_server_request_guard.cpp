/**
 * @file web_server_request_guard.cpp
 * @brief Registers the backend HTTP request-authorization boundary.
 */

#include "web_server_routes.hpp"
#include "application_recovery/restore_hold.hpp"

#include "backend_http_guard.hpp"
#include "httplib.hpp"
#include "logging.hpp"
#include "privileged_network_runtime.hpp"
#include "wtp_runtime_bridge.hpp"
#include "pico_usb_runtime_ownership.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "json.hpp"
#include "wtp_integration/controller_contract.hpp"

#include <optional>
#include <charconv>
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
            if(wsprrypi::restore_maintenance_active()&&request.method!="GET"&&request.method!="HEAD"&&request.method!="OPTIONS"&&request.path!="/control/stop"){
                response.status=409;response.set_content(R"({"error":{"code":"restore_maintenance","message":"A confirmed restore is holding application changes."}})","application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
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
            // Legacy GET /config is ordinarily read-only. During protected
            // recovery its replacement document requires the same peer/proxy
            // authority as the new recovery route, before reading any draft.
            const bool host_held=wsprrypi::pi_managed_host_held();
            const auto protected_path=request.path=="/config"&&host_held
                ?std::string("/api/control/v1/host/configuration-recovery"):request.path;
            if (evaluate_backend_http_request(
                    request.method, protected_path, request.remote_addr,
                    request.get_header_value("Host"), origin,
                    network_snapshot_provider(),
                    host_held||wsprrypi::pi_managed_host_protected_recovery_path(request.path)
                        ?PrivilegedNetworkMode::enforced:active_privileged_network_mode(),
                    trusted_proxy_identities, &rejection_reason) ==
                BackendHttpGuardDecision::allowed) {
                if ((request.path.starts_with("/api/control/v1/member/") && request.path != "/api/control/v1/member/firmware") ||
                    request.path == "/api/control/v1/network") {
                    response.status = 404;
                    response.set_content(wsprrypi::controller_target_error(404,"unsupported_resource").body,"application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
                if (!controller_http_allowed(request.method, request.path)) {
                    response.status = 409;
                    response.set_content(nlohmann::json{{"error", {{"code", "controller_only"},
                        {"message", "This process has no local transmitter. The requested operation is unavailable."}}},
                        {"controller", controller_only_status()}}.dump(), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
                const bool management=request.path=="/api/control/v1/host/retirements"||request.path=="/api/control/v1/host/management-session"||
                    request.path=="/api/control/v1/host/readiness"||request.path=="/api/control/v1/host/configuration-recovery"||
                    request.path=="/api/control/v1/targets"||request.path.starts_with("/api/control/v1/targets/")||
                    (request.path=="/config"&&wsprrypi::pi_managed_host_held());
                if(management){
                    std::size_t bytes=0;for(const auto &[name,value]:request.headers)bytes+=name.size()+value.size()+4;
                    unsigned status=0;const char *code="invalid_shape";
                    if(bytes>8192||request.path.size()>256){status=431;code="payload_too_large";}
                    else if(request.get_header_value_count("Content-Length")>1||
                        (request.has_header("Content-Length")&&request.has_header("Transfer-Encoding")))status=400;
                    else if(request.has_header("Content-Length")){
                        const auto value=request.get_header_value("Content-Length");std::uint64_t length{};
                        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),length);
                        if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())status=400;
                        else if(length>32768){status=413;code="payload_too_large";}
                        else if(request.method=="GET"&&length)status=400;
                    }
                    else if(request.method=="GET"&&request.has_header("Transfer-Encoding"))status=400;
                    if(status){
                        response.status=status;response.set_header("Cache-Control","no-store");
                        response.set_content(wsprrypi::controller_target_error(status,code).body,"application/json");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                }
                if(!wsprrypi::pi_managed_host_http_allowed(request.method,request.path)) {
                    response.status=503;
                    response.set_header("Cache-Control","no-store");
                    response.set_content(wsprrypi::controller_target_error(503,"authority_pending").body,"application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
                if(request.method=="GET"&&request.path=="/config"&&wsprrypi::pi_managed_host_held()) {
                    if(!host_held&&evaluate_backend_http_request(request.method,
                        "/api/control/v1/host/configuration-recovery",request.remote_addr,
                        request.get_header_value("Host"),origin,network_snapshot_provider(),
                        PrivilegedNetworkMode::enforced,trusted_proxy_identities)!=BackendHttpGuardDecision::allowed){
                        // History can become unverifiable after the first
                        // request snapshot. Recheck before replacing a public
                        // legacy read with a protected recovery document.
                        response.status=403;response.set_header("Cache-Control","no-store");
                        response.set_content(wsprrypi::controller_target_error(403,"unauthorized").body,"application/json");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                    const auto prefix=request.path+"?_=";
                    const auto cache_value=request.target.starts_with(prefix)?request.target.substr(prefix.size()):std::string{};
                    const bool cache_buster=request.params.size()==1&&request.has_param("_")&&
                        request.get_param_value("_")==cache_value&&!cache_value.empty()&&cache_value.size()<=20&&
                        cache_value.find_first_not_of("0123456789")==std::string::npos;
                    const auto result=(request.target==request.path||cache_buster)
                        ?wsprrypi::pi_managed_host_recovery({"GET","/api/control/v1/host/configuration-recovery",request.body},true)
                        :wsprrypi::controller_target_error(400,"invalid_shape");
                    response.status=result.status==200?409:result.status;
                    response.set_header("Cache-Control","no-store");
                    response.set_content(result.body,"application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
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
            if(request.path=="/api/control/v1/host/retirements"||request.path=="/api/control/v1/host/management-session"||request.path=="/api/control/v1/host/readiness"||request.path=="/api/control/v1/host/configuration-recovery"||
                request.path=="/api/control/v1/targets"||request.path.starts_with("/api/control/v1/targets/"))
                response.set_content(wsprrypi::controller_target_error(403,"unauthorized").body,"application/json");
            else response.set_content("Forbidden", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        });
}
} // namespace web_server_routes
