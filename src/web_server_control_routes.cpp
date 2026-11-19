/**
 * @file web_server_control_routes.cpp
 * @brief Registers transmission-control HTTP routes.
 */

#include "web_server_routes.hpp"

#include "httplib.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "json.hpp"
#include "config_handler.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/browser_http.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/discovery.hpp"
#include "wtp_integration/usb_discovery.hpp"
#include "wtp_integration/identity_probe.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_integration/usb_logging.hpp"
#include "udp_logging/host.hpp"
#include "transmitter_runtime_bridge.hpp"
#include <utility>
#include <mutex>
#include "web_server_config_http.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/pico_target_controller.hpp"
#include "wtp_integration/pi_pair_machine_backend.hpp"
#include "wtp_integration/pi_pair_local_service.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "privileged_network_runtime.hpp"
#include <filesystem>

namespace {
std::optional<WtpSettings> selected_wtp() {
    return {}; // Retired execution selection is never an authority for management.

}
wsprrypi::WtpCatalog &catalog() {
    // The main-owned host outlives every HTTP worker. Reuse its mutex and
    // atomic anchor owner; a second catalog instance could lose concurrent
    // management history updates even though both name the same file.
    if(const auto host=wsprrypi::pi_managed_host())return host->catalog();
    static wsprrypi::WtpCatalog value(config.ini_filename + ".wtp-devices.json");
    return value;
}
struct TargetControllerSlot {
    std::mutex mutex;
    std::shared_ptr<wsprrypi::PicoTargetController> production,fixture;
};
TargetControllerSlot &target_controller_slot(){static TargetControllerSlot value;return value;}
std::shared_ptr<wsprrypi::PicoTargetController> target_controller() {
    auto &slot=target_controller_slot();std::lock_guard lock(slot.mutex);
    if(slot.fixture)return slot.fixture;
    if(!slot.production) {
        const auto identity=controller_only_mode()?controller_only_status().at("device_id").get<std::string>():wsprrypi::wtp_pi_device_id();
        slot.production=std::make_shared<wsprrypi::PicoTargetController>(catalog(),selected_wtp,
            config.ini_filename+".management",identity,wsprrypi::PicoTargetController::Fixtures{},
            controller_only_mode()?controller_only_process_boot():wsprrypi::pi_managed_host_process_boot());
    }
    return slot.production;
}
void catalog_management_changed() {
    auto &slot=target_controller_slot();std::shared_ptr<wsprrypi::PicoTargetController> existing;
    {std::lock_guard lock(slot.mutex);existing=slot.fixture?slot.fixture:slot.production;}
    if(existing){existing->catalog_changed();return;}
    // Ordinary catalog use does not initialize a management service or acquire
    // native sessions. A previous process's history still needs safe retirement.
    const auto directory=config.ini_filename+".management";
    if(!std::filesystem::exists(std::filesystem::symlink_status(directory)))return;
    const auto identity=controller_only_mode()?controller_only_status().at("device_id").get<std::string>():wsprrypi::wtp_pi_device_id();
    wsprrypi::PicoManagementJournal recovery(directory,identity,identity,catalog().management_history_authority(selected_wtp()));
    recovery.prune_retired();
}
std::uint64_t discovery_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
void require_probe_idle() {
    if (controller_only_mode()) return;
    const auto [current, revision] = get_public_config_snapshot();
    (void)revision;
    const auto state = transmitter_state();
    if (current.at("Operation").value("Transmit", false) ||
        state == WsprTransmitState::TRANSMITTING ||
        state == WsprTransmitState::RECOVERING ||
        state == WsprTransmitState::HUNG)
        throw std::runtime_error("Stop and resolve transmitter work before identifying a device");
}
WtpSettings candidate_settings(const nlohmann::json &request, bool) {
    const auto id = request.at("discovery_id").get<std::string>();
    if (id.starts_with("usb-")) {
        auto settings = parse_management_profile_settings(request.at("settings"), true);
        wsprrypi::validate_discovered_usb(wsprrypi::pico_management_usb_discovery(), id, settings);
        return settings;
    }
    const auto candidate = wsprrypi::wtp_discovery().candidate(id, discovery_now());
    if (!candidate) throw std::runtime_error("Discovery candidate is unavailable or stale");
    auto settings = parse_management_profile_settings(request.at("settings"), true);
    const std::string binding = settings.transport == "network" ? "tls" :
        settings.transport == "network_plain" ? "consumer" : "usb";
    if (candidate->binding != binding || settings.hostname != candidate->target ||
        settings.tcp_port != static_cast<int>(candidate->port))
        throw std::runtime_error("Discovery binding or SRV endpoint changed; review the candidate");
    if (binding == "tls" && settings.tls_identity.empty())
        throw std::runtime_error("Discovered TLS requires a pinned certificate identity");
    if (binding == "consumer" && settings.device_id.empty())
        throw std::runtime_error("Consumer Wi-Fi needs an expected immutable device ID");
    return settings;
}
wsprrypi::PicoHttpResponse catalog_request(const httplib::Request &request) {
    try {
        std::unique_lock profile_lock(wsprrypi::pico_management_catalog_gate(),std::defer_lock);
        if(request.method=="POST"&&request.path=="/api/control/v1/host/devices"&&!profile_lock.try_lock())
            return {409,R"({"error":{"code":"management_busy"}})",{}};
        static std::mutex mutation_mutex;
        std::unique_lock mutation_lock(mutation_mutex, std::defer_lock);
        if (request.method == "POST" &&
            (request.path == "/api/control/v1/host/devices" ||
             request.path == "/api/control/v1/host/devices/use")) mutation_lock.lock();
        const auto active = selected_wtp();
        if (request.path == "/api/control/v1/host/discovery" && request.method == "GET") {
            auto result = wsprrypi::wtp_discovery().snapshot(discovery_now());
            result["usb"] = wsprrypi::pico_management_usb_discovery();
            return {200, result.dump(), {}};
        }
        if (request.path == "/api/control/v1/host/devices" && request.method == "GET") {
            auto [snapshot, revision] = catalog().snapshot(active);
            return {200, snapshot.dump(), revision};
        }
        if (request.path == "/api/control/v1/host/discovery/identify" && request.method == "POST") {
            const auto body = wsprrypi::strict_browser_json(request.body);
            if (!body.is_object() || body.size() != 2) throw std::runtime_error("Invalid identify request");
            const auto settings = candidate_settings(body, true);
            require_probe_idle();
            const auto observation = wtp_runtime_safe_probe([&] {
                return settings.transport == "usb" ? wsprrypi::wtp_probe_usb_identity(settings)
                    : wsprrypi::wtp_probe_network_identity(settings);
            });
            return {200, observation.dump(), {}};
        }
        if (request.path == "/api/control/v1/host/devices" && request.method == "POST") {
            if (request.get_header_value("If-Match").empty())
                return {428, R"({"error":{"code":"revision_required"}})", {}};
            auto body = wsprrypi::strict_browser_json(request.body);
            if (!body.is_object()) throw std::runtime_error("Invalid catalog request");
            const auto operation = body.value("operation", "");
            if (operation == "add" && body.value("method", "") == "dns_sd") {
                const auto settings = candidate_settings(body, false);
                if (settings.transport == "network_plain" && body.value("consent_plain", false) != true)
                    throw std::runtime_error("Consumer Wi-Fi requires an explicit connection choice");
                require_probe_idle();
                if(settings.transport=="network"){
                        const auto observed = wtp_runtime_safe_probe([&] {
                            return wsprrypi::wtp_probe_network_identity(settings);
                        });
                        if (observed.at("device_id") != settings.device_id)
                            throw std::runtime_error("Observed management identity changed");
                }
            } else if (operation == "edit") {
                auto old = catalog().profile(body.at("id").get<std::string>(),
                    request.get_header_value("If-Match"), active);
                if (old.method == "dns_sd") {
                    nlohmann::json candidate = {{"discovery_id", old.discovery_id},
                        {"settings", body.at("settings")}};
                    const auto settings = candidate_settings(candidate, false);
                    require_probe_idle();
                    if(settings.transport=="network"){
                        const auto observed = wtp_runtime_safe_probe([&] {
                            return wsprrypi::wtp_probe_network_identity(settings);
                        });
                        if (observed.at("device_id") != settings.device_id)
                            throw std::runtime_error("Observed management identity changed");
                    }
                }
            }
            auto [result, revision] = catalog().mutate(body,
                request.get_header_value("If-Match"), active);
            try {catalog_management_changed();}
            catch(...){return {503,R"({"error":{"code":"management_history_unavailable"}})",{}};}
            revision=catalog().snapshot(active).second;
            return {200, result.dump(), revision};
        }
        if (request.path == "/api/control/v1/host/devices/use")
            return {404,R"({"error":{"code":"not_found"}})",{}};
        return {404, R"({"error":{"code":"not_found"}})", {}};
    } catch (const std::exception &error) {
        const std::string reason = error.what();
        const bool conflict = reason == "revision_conflict";
        return {conflict ? 412U : 409U,
            nlohmann::json{{"error", {{"code", conflict ? "revision_conflict" : "catalog_request_rejected"},
                                      {"message", reason}}}}.dump(), {}};
    }
}
}

void wsprrypi::testing::set_pico_target_controller(std::shared_ptr<PicoTargetController> controller) {
    auto &slot=target_controller_slot();std::lock_guard lock(slot.mutex);slot.fixture=std::move(controller);
}

namespace web_server_routes
{
std::shared_ptr<wsprrypi::PiPairMachineBackend> pairing_machine_backend(){
    const auto owner=wsprrypi::pi_pair_owner();
    if(!owner)throw std::runtime_error("pairing_owner_unavailable");
    const auto host=wsprrypi::pi_managed_host();
    if(!controller_only_mode()&&!host)throw std::runtime_error("pairing_native_owner_unavailable");
    return std::make_shared<wsprrypi::PiPairMachineBackend>([owner]{return owner->history();},catalog(),target_controller(),selected_wtp,host);
}
std::shared_ptr<wsprrypi::PiPairRemoteController> pairing_remote_controller(std::shared_ptr<wsprrypi::PiPairHistory> history){
    const auto owner=wsprrypi::pi_pair_owner();if(!owner||!history)throw std::runtime_error("pairing_owner_unavailable");
    auto remote=std::make_shared<wsprrypi::PiPairRemoteController>([history]{return history;},wsprrypi::pi_pair_admission(),
        owner->self(),catalog(),config.ini_filename+".management",selected_wtp);
    remote->authority_source([]{return wsprrypi::wtp_fleet_api("GET",{},{},"retirements");});
    return remote;
}
void retire_control_management(){
    std::shared_ptr<wsprrypi::PicoTargetController> retiring;
    {auto &slot=target_controller_slot();std::lock_guard lock(slot.mutex);retiring=std::move(slot.production);}
}
void register_control(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers,
    std::function<std::shared_ptr<wsprrypi::PiPairLocalService>()> pairing,
    NetworkSnapshotProvider snapshot)
{
    server.Get("/status", [](const httplib::Request &, httplib::Response &response) {
        if (!controller_only_mode()) { response.status = 404; return; }
        response.set_header("Cache-Control", "no-store");
        response.set_content(controller_only_status().dump(), "application/json");
    });
    wsprrypi::start_wtp_discovery();
    static wsprrypi::WtpBrowserApi api(
        [] { return nlohmann::json::parse(wtp_runtime_json()); },
        wtp_runtime_management,
        [](const std::string &job) {
            const auto result = wtp_runtime_cancel_job(job);
            return wsprrypi::PicoHttpResponse{result.ok ? 200U : 409U,
                result.ok ? R"({"cleanup_ok":true})" :
                    nlohmann::json{{"error", {{"code", "not_owner_or_cleanup_unresolved"}}}}.dump(), {}};
        });
    const auto shared = [pairing=std::move(pairing),snapshot=std::move(snapshot)](const httplib::Request &request, httplib::Response &response) {
        const auto browser_request = wsprrypi::wtp_browser_http_request(request);
        if (!browser_request) {
            response.status = 400;
            const bool target=request.path=="/api/control/v1/host/retirements"||request.path=="/api/control/v1/host/management-session"||request.path=="/api/control/v1/host/readiness"||request.path=="/api/control/v1/host/configuration-recovery"||
                request.path=="/api/control/v1/targets"||request.path.starts_with("/api/control/v1/targets/");
            response.set_content(target?wsprrypi::controller_target_error(400,"invalid_shape").body:
                R"({"error":{"code":"invalid_request"}})", "application/json");
            return;
        }
        const bool context = browser_request->mutation_context;
        wsprrypi::PicoHttpResponse result;
        if(request.path=="/api/control/v1/host/retirements"||request.path=="/api/control/v1/host/management-session"||request.path=="/api/control/v1/host/readiness"||request.path=="/api/control/v1/host/configuration-recovery"||request.path=="/api/control/v1/targets"||request.path.starts_with("/api/control/v1/targets/")) {
            auto &sessions=wsprrypi::management_browser_sessions();
            const auto failure=[](unsigned status,const char *code){return wsprrypi::controller_target_error(status,code);};
            std::size_t header_bytes=0;for(const auto &[name,value]:request.headers)header_bytes+=name.size()+value.size()+4;
            if(active_privileged_network_mode()!=PrivilegedNetworkMode::enforced&&
                !wsprrypi::pi_managed_host_protected_recovery_path(request.path))result=failure(403,"unauthorized");
            else if(header_bytes>8192||request.path.size()>256)result=failure(431,"payload_too_large");
            else if(!sessions.within_rate(request))result=failure(429,"rate_limited");
            else if(request.path=="/api/control/v1/host/management-session") {
                try {
                    if(request.body.size()>256||!wsprrypi::valid_controller_document(wsprrypi::strict_browser_json(request.body),"controller-browser-session"))result=failure(400,"invalid_shape");
                    else if(!sessions.establish(request,response))result=failure(403,"csrf_failed");
                    else result={200,R"({"contract":"Wsprry-Control/1","schema":"controller-browser-session"})",{}};
                }catch(...){result=failure(400,"invalid_json");}
            } else if(request.method!="GET"&&!sessions.permits(request))result=failure(403,"csrf_failed");
            else if(request.path=="/api/control/v1/host/retirements")
                result=wsprrypi::wtp_fleet_api(request.method,request.body,request.get_header_value("If-Match"),"retirements");
            else if(request.path=="/api/control/v1/host/readiness"||request.path=="/api/control/v1/host/configuration-recovery")
                result=wsprrypi::pi_managed_host_recovery(*browser_request,true);
            else if(wsprrypi::pi_managed_host_target_path(request.path)){
                if(!request.get_header_value("X-WsprryPi-Profile-ID").empty()||!request.get_header_value("X-WsprryPi-Target-Context").empty()||
                    !request.get_header_value("X-WsprryPi-Operation-ID").empty())result=failure(400,"invalid_shape");
                else if(const auto host=wsprrypi::pi_managed_host()){
                    const auto principal=request.method!="GET"&&snapshot?sessions.authorize_local_mutation(request,snapshot()):std::optional<wsprrypi::LocalBrowserPrincipal>{};
                    const auto service=pairing?pairing():nullptr;
                    if(principal&&service)result=service->native(*browser_request,*principal,*host);
                    else result=host->management(*browser_request,true);
                }
                else result=failure(503,"storage_unavailable");
            }
            else try {
                std::optional<wsprrypi::PiPairHttpReply> remote;
                if(pairing&&snapshot)if(const auto owner=pairing())remote=owner->remote(request,snapshot());
                // A selected remote failure is final. It cannot fall through
                // into a local host or independently held Pico context.
                if(remote&&request.path!="/api/control/v1/targets"){
                    response.status=static_cast<int>(remote->payload.status);
                    response.set_header("Cache-Control","no-store");
                    if(!remote->payload.etag.empty())response.set_header("ETag",remote->payload.etag);
                    if(!remote->context.empty())response.set_header("X-WsprryPi-Target-Context",remote->context);
                    if(!remote->operation.empty())response.set_header("X-WsprryPi-Operation-ID",remote->operation);
                    if(!remote->location.empty())response.set_header("Location",remote->location);
                    if(!remote->readback_revision.empty())response.set_header("X-WsprryPi-Readback-Revision",remote->readback_revision);
                    if(!remote->recovery_resource.empty())response.set_header("X-WsprryPi-Recovery-Resource",remote->recovery_resource);
                    response.set_content(remote->payload.body,"application/json");return;
                }
                const auto reply=target_controller()->handle(*browser_request,request.get_header_value("X-WsprryPi-Profile-ID"),
                    request.get_header_value("X-WsprryPi-Target-Context"),request.get_header_value("X-WsprryPi-Operation-ID"));
                result=wsprrypi::normalize_controller_error(reply.response);
                if(request.method=="GET"&&request.path=="/api/control/v1/targets"&&result.status==200){
                    if(const auto host=wsprrypi::pi_managed_host()){
                        auto targets=wsprrypi::strict_browser_json(result.body);
                        if(!wsprrypi::valid_controller_document(targets,"targets"))throw std::runtime_error("invalid_targets");
                        const auto local=host->target();
                        for(const auto &target:targets["targets"])if(target["device_id"]==local["device_id"])
                            throw std::runtime_error("duplicate_target_identity");
                        targets["targets"].push_back(local);
                        result=wsprrypi::valid_controller_document(targets,"targets")&&targets.dump().size()<=32768
                            ?wsprrypi::PicoHttpResponse{200,targets.dump(),{}}:failure(429,"capacity");
                    }
                }
                if(remote&&request.method=="GET"&&request.path=="/api/control/v1/targets"){
                    if(remote->payload.status!=200)result=remote->payload;
                    else if(result.status==200){
                        auto combined=wsprrypi::strict_browser_json(result.body),extra=wsprrypi::strict_browser_json(remote->payload.body);
                        if(!wsprrypi::valid_controller_document(combined,"targets")||!wsprrypi::valid_controller_document(extra,"targets"))throw std::runtime_error("invalid_targets");
                        for(const auto &row:extra["targets"])combined["targets"].push_back(row);
                        result=wsprrypi::valid_controller_document(combined,"targets")&&combined.dump().size()<=32768?
                            wsprrypi::PicoHttpResponse{200,combined.dump(),{}}:failure(409,"target_mismatch");
                    }
                }
                if(!reply.context.empty())response.set_header("X-WsprryPi-Target-Context",reply.context);
                if(!reply.operation.empty())response.set_header("X-WsprryPi-Operation-ID",reply.operation);
                if(!reply.location.empty())response.set_header("Location",reply.location);
                if(!reply.readback_revision.empty())response.set_header("X-WsprryPi-Readback-Revision",reply.readback_revision);
                if(!reply.recovery_resource.empty())response.set_header("X-WsprryPi-Recovery-Resource",reply.recovery_resource);
            }catch(...){result=failure(503,"transport_unavailable");}
        } else if (request.path == "/api/control/v1/member/firmware") {
            result = request.method == "GET" || context
                ? wtp_runtime_firmware(request.method, request.body)
                : wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}};
        } else if (request.path.starts_with("/api/control/v1/member/") || request.path == "/api/control/v1/network") {
            // Settings use enrolled target contexts. Keep the explicit firmware
            // lifecycle endpoint separate from settings and WTP execution.
            result = wsprrypi::controller_target_error(404,"unsupported_resource");
        } else if(request.path=="/api/control/v1/host/udp-logging/installation") {
            result=request.method=="GET"?wsprrypi::udp_logging_installation():wsprrypi::PicoHttpResponse{405,R"({"error":{"code":"method_not_allowed"}})",{}};
        } else if (request.path == "/api/control/v1/host/udp-logging" || request.path == "/api/control/v1/udp-logging/capabilities" || request.path == "/api/control/v1/host/udp-logging/test" || request.path == "/api/control/v1/host/udp-logging/test/capabilities") {
            result = request.method != "GET" && !context
                ? wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}}
                : wsprrypi::udp_logging_api(request.method, request.body, request.get_header_value("If-Match"),request.path.ends_with("/capabilities"),request.path.find("/test")!=std::string::npos);
        } else if (request.path == "/api/control/v1/host/logging") {
            result = request.method != "GET" && !context
                ? wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}}
                : wsprrypi::usb_logging_api(request.method, request.body, request.get_header_value("If-Match"));
        } else if (request.path == "/api/control/v1/host/fleet" ||
                   request.path == "/api/control/v1/host/fleet/operations" ||
                   request.path == "/api/control/v1/host/fleet/actions") {
            const bool actions=request.path.ends_with("/actions");
            const bool operations=request.path.ends_with("/operations");
            const auto query=request.target.find('?');
            const auto action_ids=wsprrypi::wtp_fleet_action_query(request);
            const bool valid_query=actions? action_ids.has_value():query==std::string::npos && request.params.empty();
            if(!valid_query)result={400,R"({"error":{"code":"invalid_fleet_query"}})",{}};
            else if((actions || operations) && request.method!="GET")result={405,R"({"error":{"code":"method_not_allowed"}})",{}};
            else result = request.method == "GET" || context
                ? wsprrypi::wtp_fleet_api(request.method, request.body, request.get_header_value("If-Match"),
                    actions?"actions":operations?"operations":"fleet",actions?*action_ids:"")
                : wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}};
        } else if (request.path == "/api/control/v1/host/wtp-endpoint" && request.method == "GET") {
            result = {200, wsprrypi::wtp_pi_endpoint_status_json(), {}};
        } else if (request.path == "/api/control/v1/host/wtp-endpoint/recover" && request.method == "POST") {
            if (!context) result = {403, R"({"error":{"code":"origin_or_content_type"}})", {}};
            else try {
                if (wsprrypi::strict_browser_json(request.body) != nlohmann::json{{"confirmed", true}})
                    throw std::runtime_error("Recovery requires local confirmation");
                const bool ok = wsprrypi::wtp_pi_recover_remote_output();
                result = {ok ? 200U : 409U, wsprrypi::wtp_pi_endpoint_status_json(), {}};
            } catch (const std::exception &error) {
                result = {409, nlohmann::json{{"error", {{"message", error.what()}}}}.dump(), {}};
            }
        } else if (request.path == "/api/control/v1/host/wtp-endpoint/enable" && request.method == "POST") {
            if (!context) result = {403, R"({"error":{"code":"origin_or_content_type"}})", {}};
            else if (request.get_header_value("If-Match").empty())
                result = {428, R"({"error":{"code":"revision_required"}})", {}};
            else try {
                const auto body = wsprrypi::strict_browser_json(request.body);
                if (!body.is_object() || body.size() != 4 ||
                    !body.contains("choice") || !body.contains("confirmed") ||
                    !body.at("confirmed").is_boolean())
                    throw std::runtime_error("invalid_enable_request");
                const auto choice = body.at("choice").get<std::string>();
                if (choice != "end_now" && choice != "finish_current")
                    throw std::runtime_error("invalid_enable_choice");
                const auto revision = patch_all_from_web_revision(
                    {{"Operation", {{"Transmit", true}}}}, request.get_header_value("If-Match"),
                    choice == "end_now" ? LocalEnableAction::EndNow : LocalEnableAction::FinishCurrent,
                    body.at("confirmed").get<bool>(),
                    body.at("observed_owner").get<std::string>(), body.at("observed_job").get<std::string>());
                result = {200, wsprrypi::wtp_pi_endpoint_status_json(), revision};
            } catch (const std::exception &error) {
                const std::string reason = error.what();
                result = {reason == "revision_conflict" ? 412U : 409U,
                    nlohmann::json{{"error", {{"code", reason}}},
                        {"status", nlohmann::json::parse(wsprrypi::wtp_pi_endpoint_status_json())}}.dump(), {}};
            }
        } else if (request.path == "/api/control/v1/host/discovery" ||
            request.path == "/api/control/v1/host/discovery/identify" ||
            request.path == "/api/control/v1/host/devices" ||
            request.path == "/api/control/v1/host/devices/use") {
            result = context || request.method == "GET" ? catalog_request(request)
                : wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}};
        } else if (request.path == "/api/control/v1/host/config") {
            try {
                if (request.method == "PUT") {
                    if (!context) result = {403, R"({"error":{"code":"origin_or_content_type"}})", {}};
                    else if (request.get_header_value("If-Match").empty()) result = {428, R"({"error":{"code":"revision_required"}})", {}};
                    else {
                        const auto revision = patch_all_from_web_revision(wsprrypi::strict_browser_json(request.body), request.get_header_value("If-Match"));
                        result = {200, R"({"ok":true})", revision};
                    }
                } else if (request.method == "GET") {
                    auto [config, revision] = get_public_config_snapshot();
                    result = {200, nlohmann::json{{"config", config}, {"scope", "wsprrypi-host-config/1"}}.dump(), revision};
                } else result = {404, R"({"error":{"code":"not_found"}})", {}};
            } catch (const std::exception &error) {
                const bool conflict = std::string(error.what()) == "revision_conflict";
                result = {conflict ? 412U : 400U,
                    nlohmann::json{{"error", {{"code", conflict ? "revision_conflict" : "invalid_config"}}}}.dump(), {}};
            }
        } else result = api.handle(*browser_request);
        response.status = result.status;
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        if (!result.etag.empty()) response.set_header("ETag", result.etag);
        response.headers.erase("Access-Control-Allow-Origin");
        response.set_content(result.body, "application/json");
    };
    const auto bounded_shared=[shared](const httplib::Request &request,httplib::Response &response,const httplib::ContentReader &reader){
        auto bounded=request;bool oversized=false;
        const auto started=std::chrono::steady_clock::now();
        const bool read=reader([&](const char *data,std::size_t size){
            if(size>32768-bounded.body.size()){oversized=true;return false;}
            if(std::chrono::steady_clock::now()-started>std::chrono::seconds(24))return false;
            bounded.body.append(data,size);return true;
        });
        if(!read){
            oversized=oversized||response.status==413;
            const auto result=wsprrypi::controller_target_error(oversized?413:400,oversized?"payload_too_large":"invalid_shape");
            response.status=result.status;response.set_header("Cache-Control","no-store");
            response.set_content(result.body,"application/json");return;
        }
        shared(bounded,response);
    };
    server.Put(R"(/api/control/v1/.*)",bounded_shared);
    server.Post(R"(/api/control/v1/.*)",bounded_shared);
    server.Delete(R"(/api/control/v1/.*)",bounded_shared);
    // Keep bodyless requests on the same closed adapter; ContentReader handlers
    // are selected only when the HTTP parser sees request content.
    server.Get(R"(/api/control/v1/.*)", shared);
    server.Put(R"(/api/control/v1/.*)", shared);
    server.Post(R"(/api/control/v1/.*)", shared);
    server.Delete(R"(/api/control/v1/.*)", shared);
    server.Get("/api/wtp", [](const httplib::Request &, httplib::Response &response) {
        response.set_header("Cache-Control", "no-store");
        response.set_content(wtp_runtime_json(), "application/json");
    });
    server.Post("/api/wtp/recover", [](const httplib::Request &request, httplib::Response &response) {
        response.set_header("Cache-Control", "no-store");
        try {
            const auto type = request.get_header_value("Content-Type");
            if ((type != "application/json" && !type.starts_with("application/json;")) || request.body.size() > 256)
                throw std::runtime_error("Expected a bounded JSON reconciliation request");
            bool key_seen = false;
            const auto body = nlohmann::json::parse(request.body, [&](int, nlohmann::json::parse_event_t event, nlohmann::json &) {
                if (event == nlohmann::json::parse_event_t::key && std::exchange(key_seen, true))
                    throw std::runtime_error("Expected one reconciliation operation");
                return true;
            });
            if (!body.is_object() || body.size() != 1 || body.value("operation", "") != "reconcile")
                throw std::runtime_error("Expected operation reconcile");
            auto result = wtp_runtime_recover();
            response.status = result.ok ? 200 : 409;
            response.set_content(nlohmann::json{{"ok", result.ok}, {"error", result.error},
                {"status", nlohmann::json::parse(wtp_runtime_json())}}.dump(), "application/json");
        } catch (const std::exception &error) {
            response.status = 400;
            response.set_content(nlohmann::json{{"ok",false},{"error",error.what()}}.dump(), "application/json");
        }
    });
    server.Post(
        "/control/stop",
        [set_cors_headers](
            const httplib::Request &request, httplib::Response &response)
        {
            write_route_response(
                response, stop_transmission(request.body), set_cors_headers);
        });
}
} // namespace web_server_routes
