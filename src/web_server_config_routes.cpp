/**
 * @file web_server_config_routes.cpp
 * @brief Registers configuration HTTP routes.
 */

#include "web_server_routes.hpp"

#include "httplib.hpp"
#include "config_handler.hpp"
#include "web_server_config_http.hpp"

#include <utility>

namespace web_server_routes
{
void write_route_response(
    httplib::Response &response,
    RouteResponse result,
    const CorsHeaderSetter &set_cors_headers)
{
    if (result.allow_cors)
        set_cors_headers(response);
    else
        response.headers.erase("Access-Control-Allow-Origin");
    response.status = result.status;
    response.set_content(std::move(result.body), result.content_type);
}

void register_config(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers)
{
    server.Options(
        R"(/(.*))",
        [set_cors_headers](
            const httplib::Request &, httplib::Response &response)
        {
            set_cors_headers(response);
            response.set_content("", "text/plain");
        });

    server.Get(
        "/",
        [set_cors_headers](
            const httplib::Request &, httplib::Response &response)
        {
            set_cors_headers(response);
            response.set_content(
                "Wsprry Pi webserver is running.", "text/plain");
        });

    server.Get(
        "/config",
        [set_cors_headers](
            const httplib::Request &, httplib::Response &response)
        {
            auto [config, revision] = get_public_config_snapshot();
            response.set_header("ETag", revision);
            write_route_response(response, {200, config.dump(4), "application/json", false}, set_cors_headers);
        });

    server.Get(
        "/config/si5351-addresses",
        [set_cors_headers](
            const httplib::Request &request, httplib::Response &response)
        {
            const std::string bus = request.has_param("bus")
                ? request.get_param_value("bus")
                : std::string();
            write_route_response(
                response,
                build_si5351_addresses_response(bus),
                set_cors_headers);
        });

    const auto handle_put_patch =
        [set_cors_headers](
            const httplib::Request &request, httplib::Response &response)
        {
            if (request.has_header("If-Match")) {
                if (request.get_header_value("If-Match").empty()) {
                    response.status = 428;
                    response.set_content(R"({"error":"revision_required"})", "application/json");
                    return;
                }
                try {
                    const auto revision = patch_all_from_web_revision(nlohmann::json::parse(request.body), request.get_header_value("If-Match"));
                    response.set_header("ETag", revision);
                    response.set_content("Ok", "text/plain");
                } catch (const std::exception &error) {
                    const bool conflict = std::string(error.what()) == "revision_conflict";
                    response.status = conflict ? 412 : 400;
                    response.set_content(nlohmann::json{{"error", conflict ? "revision_conflict" : "invalid_config"},
                        {"message", conflict ? "Settings changed elsewhere. Your draft is preserved; reload saved settings before retrying." : error.what()}}.dump(), "application/json");
                }
            } else write_route_response(response, apply_config_update(request.body), set_cors_headers);
        };
    server.Put("/config", handle_put_patch);
    server.Patch("/config", handle_put_patch);

    server.Post(
        "/config/repair",
        [set_cors_headers](
            const httplib::Request &request, httplib::Response &response)
        {
            write_route_response(
                response, apply_config_repair(request.body), set_cors_headers);
        });
}
} // namespace web_server_routes
