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
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/discovery.hpp"
#include "wtp_integration/identity_probe.hpp"
#include "wtp_settings_json.hpp"
#include "transmitter_runtime_bridge.hpp"
#include <utility>
#include <mutex>
#include "web_server_config_http.hpp"

namespace {
std::optional<WtpSettings> selected_wtp() {
    const auto [snapshot, revision] = get_public_config_snapshot();
    (void)revision;
    if (snapshot.at("Operation").value("Transmit Backend", "") != "wtp") return {};
    return parse_wtp_settings(snapshot.at("WTP"), true);
}
wsprrypi::WtpCatalog &catalog() {
    static wsprrypi::WtpCatalog value(config.ini_filename + ".wtp-devices.json");
    return value;
}
std::uint64_t discovery_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
void require_probe_idle() {
    const auto [current, revision] = get_public_config_snapshot();
    (void)revision;
    const auto state = transmitter_state();
    if (current.at("Operation").value("Transmit", false) ||
        state == WsprTransmitState::TRANSMITTING ||
        state == WsprTransmitState::RECOVERING ||
        state == WsprTransmitState::HUNG)
        throw std::runtime_error("Stop and resolve transmitter work before identifying a device");
}
WtpSettings candidate_settings(const nlohmann::json &request, bool allow_blank_plain) {
    const auto id = request.at("discovery_id").get<std::string>();
    const auto candidate = wsprrypi::wtp_discovery().candidate(id, discovery_now());
    if (!candidate) throw std::runtime_error("Discovery candidate is unavailable or stale");
    auto settings = parse_wtp_settings(request.at("settings"), true);
    const std::string binding = settings.transport == "network" ? "tls" :
        settings.transport == "network_plain" ? "plain" : "usb";
    if (candidate->binding != binding || settings.hostname != candidate->target ||
        settings.tcp_port != static_cast<int>(candidate->port))
        throw std::runtime_error("Discovery binding or SRV endpoint changed; review the candidate");
    if (binding == "tls" && settings.tls_identity.empty())
        throw std::runtime_error("Discovered TLS requires a pinned certificate identity");
    if (binding == "plain" && !allow_blank_plain && settings.device_id.empty())
        throw std::runtime_error("Plain LAN needs a confirmed WTP device ID");
    return settings;
}
wsprrypi::PicoHttpResponse catalog_request(const httplib::Request &request) {
    try {
        static std::mutex mutation_mutex;
        std::unique_lock mutation_lock(mutation_mutex, std::defer_lock);
        if (request.method == "POST" &&
            (request.path == "/api/v1/host/devices" ||
             request.path == "/api/v1/host/devices/use")) mutation_lock.lock();
        const auto active = selected_wtp();
        if (request.path == "/api/v1/host/discovery" && request.method == "GET")
            return {200, wsprrypi::wtp_discovery().snapshot(discovery_now()).dump(), {}};
        if (request.path == "/api/v1/host/devices" && request.method == "GET") {
            auto [snapshot, revision] = catalog().snapshot(active);
            return {200, snapshot.dump(), revision};
        }
        if (request.path == "/api/v1/host/discovery/identify" && request.method == "POST") {
            const auto body = wsprrypi::strict_browser_json(request.body);
            if (!body.is_object() || body.size() != 2) throw std::runtime_error("Invalid identify request");
            const auto settings = candidate_settings(body, true);
            require_probe_idle();
            const auto observation = wtp_runtime_safe_probe([&] {
                return wsprrypi::wtp_probe_network_identity(settings);
            });
            return {200, observation.dump(), {}};
        }
        if (request.path == "/api/v1/host/devices" && request.method == "POST") {
            if (request.get_header_value("If-Match").empty())
                return {428, R"({"error":{"code":"revision_required"}})", {}};
            auto body = wsprrypi::strict_browser_json(request.body);
            if (!body.is_object()) throw std::runtime_error("Invalid catalog request");
            const auto operation = body.value("operation", "");
            if (operation == "add" && body.value("method", "") == "dns_sd") {
                const auto settings = candidate_settings(body, false);
                if (settings.transport == "network_plain" && body.value("consent_plain", false) != true)
                    throw std::runtime_error("Plain LAN requires explicit consent");
                require_probe_idle();
                const auto observed = wtp_runtime_safe_probe([&] {
                    return wsprrypi::wtp_probe_network_identity(settings);
                });
                if (observed.at("device_id") != settings.device_id)
                    throw std::runtime_error("Observed WTP identity changed");
            } else if (operation == "edit") {
                auto old = catalog().profile(body.at("id").get<std::string>(),
                    request.get_header_value("If-Match"), active);
                if (old.method == "dns_sd") {
                    nlohmann::json candidate = {{"discovery_id", old.discovery_id},
                        {"settings", body.at("settings")}};
                    const auto settings = candidate_settings(candidate, false);
                    require_probe_idle();
                    const auto observed = wtp_runtime_safe_probe([&] {
                        return wsprrypi::wtp_probe_network_identity(settings);
                    });
                    if (observed.at("device_id") != settings.device_id)
                        throw std::runtime_error("Observed WTP identity changed");
                }
            }
            auto [result, revision] = catalog().mutate(body,
                request.get_header_value("If-Match"), active);
            return {200, result.dump(), revision};
        }
        if (request.path == "/api/v1/host/devices/use" && request.method == "POST") {
            if (request.get_header_value("If-Match").empty())
                return {428, R"({"error":{"code":"revision_required"}})", {}};
            const auto body = wsprrypi::strict_browser_json(request.body);
            if (!body.is_object() || body.size() != 2 ||
                !body.contains("id") || !body.contains("catalog_revision"))
                throw std::runtime_error("Invalid selection request");
            const auto profile = catalog().profile(body.at("id").get<std::string>(),
                body.at("catalog_revision").get<std::string>(), active);
            const auto [current_config, current_revision] = get_public_config_snapshot();
            if (current_revision != request.get_header_value("If-Match"))
                throw std::runtime_error("revision_conflict");
            if (current_config.at("Operation").value("Transmit", false))
                throw std::runtime_error("Disable transmission before switching devices");
            if (profile.legacy_unverified || profile.settings.device_id.empty())
                throw std::runtime_error("Confirm the expected WTP device ID before selection");
            if (profile.method == "dns_sd") {
                const nlohmann::json candidate = {{"discovery_id", profile.discovery_id},
                    {"settings", wtp_settings_json(profile.settings)}};
                const auto settings = candidate_settings(candidate, false);
                require_probe_idle();
                const auto observed = wtp_runtime_safe_probe([&] {
                    return wsprrypi::wtp_probe_network_identity(settings);
                });
                if (observed.at("device_id") != settings.device_id)
                    throw std::runtime_error("Observed WTP identity changed");
            }
            const auto state = transmitter_state();
            if (!wtp_runtime_switch_allowed())
                throw std::runtime_error("Resolve Pico ownership and output before switching devices");
            if (state == WsprTransmitState::TRANSMITTING ||
                state == WsprTransmitState::RECOVERING || state == WsprTransmitState::HUNG)
                throw std::runtime_error("Stop and resolve current transmitter work before switching");
            const auto selection_error = wtp_runtime_selection_error(profile.settings);
            if (!selection_error.empty()) throw std::runtime_error(selection_error);
            // The active INI and runtime path are the sole endpoint authority.
            // A config failure leaves the catalog untouched and the old active
            // endpoint selected. No separate active-ID write can partially fail.
            std::string revision;
            try {
                revision = patch_all_from_web_revision(
                    {{"Operation", {{"Transmit Backend", "wtp"}}},
                     {"WTP", wtp_settings_json(profile.settings)}},
                    request.get_header_value("If-Match"));
            } catch (...) {
                const auto [after, after_revision] = get_public_config_snapshot();
                if (after.at("Operation").value("Transmit Backend", "") == "wtp" &&
                    after.at("WTP") == wtp_settings_json(profile.settings))
                    return {500, R"({"error":{"code":"runtime_apply_unconfirmed","message":"The active endpoint was saved, but runtime application was not confirmed. Review status before further work."}})", after_revision};
                throw;
            }
            return {200, nlohmann::json{{"ok", true}, {"active_id", profile.id}}.dump(), revision};
        }
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

namespace web_server_routes
{
void register_control(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers)
{
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
    const auto shared = [](const httplib::Request &request, httplib::Response &response) {
        if (request.target != request.path || request.body.size() > 32768 ||
            request.get_header_value_count("Host") != 1 ||
            request.get_header_value_count("Origin") > 1 ||
            request.get_header_value_count("Content-Type") > 1 ||
            request.get_header_value_count("If-Match") > 1 ||
            request.get_header_value_count("X-WsprryPico-Request") > 1) {
            response.status = 400;
            response.set_content(R"({"error":{"code":"invalid_request"}})", "application/json");
            return;
        }
        const auto fetch = request.get_header_value("Sec-Fetch-Site");
        const bool context = request.has_header("Origin") &&
            request.get_header_value("Content-Type") == "application/json" &&
            request.get_header_value("X-WsprryPico-Request") == "1" &&
            (fetch.empty() || fetch == "same-origin" || fetch == "none");
        wsprrypi::PicoHttpResponse result;
        if (request.path == "/api/v1/host/fleet") {
            result = request.method == "GET" || context
                ? wsprrypi::wtp_fleet_api(request.method, request.body, request.get_header_value("If-Match"))
                : wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}};
        } else if (request.path == "/api/v1/host/wtp-endpoint" && request.method == "GET") {
            result = {200, wsprrypi::wtp_pi_endpoint_status_json(), {}};
        } else if (request.path == "/api/v1/host/wtp-endpoint/recover" && request.method == "POST") {
            if (!context) result = {403, R"({"error":{"code":"origin_or_content_type"}})", {}};
            else try {
                if (wsprrypi::strict_browser_json(request.body) != nlohmann::json{{"confirmed", true}})
                    throw std::runtime_error("Recovery requires local confirmation");
                const bool ok = wsprrypi::wtp_pi_recover_remote_output();
                result = {ok ? 200U : 409U, wsprrypi::wtp_pi_endpoint_status_json(), {}};
            } catch (const std::exception &error) {
                result = {409, nlohmann::json{{"error", {{"message", error.what()}}}}.dump(), {}};
            }
        } else if (request.path == "/api/v1/host/wtp-endpoint/enable" && request.method == "POST") {
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
        } else if (request.path == "/api/v1/host/discovery" ||
            request.path == "/api/v1/host/discovery/identify" ||
            request.path == "/api/v1/host/devices" ||
            request.path == "/api/v1/host/devices/use") {
            result = context || request.method == "GET" ? catalog_request(request)
                : wsprrypi::PicoHttpResponse{403, R"({"error":{"code":"origin_or_content_type"}})", {}};
        } else if (request.path == "/api/v1/host/config") {
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
        } else result = api.handle({request.method, request.path, request.body, request.get_header_value("If-Match"), context});
        response.status = result.status;
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        if (!result.etag.empty()) response.set_header("ETag", result.etag);
        response.headers.erase("Access-Control-Allow-Origin");
        response.set_content(result.body, "application/json");
    };
    server.Get(R"(/api/v1/.*)", shared);
    server.Put(R"(/api/v1/.*)", shared);
    server.Post(R"(/api/v1/.*)", shared);
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
