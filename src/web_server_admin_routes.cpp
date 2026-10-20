/**
 * @file web_server_admin_routes.cpp
 * @brief Registers privileged network and RP1 route administration endpoints.
 */

#include "web_server_routes.hpp"

#include "httplib.hpp"
#include "web_server_admin_http.hpp"

namespace web_server_routes
{
void register_privileged_admin(
    httplib::Server &server,
    PrivilegedNetworkAdmin &privileged_network_admin,
    wsprrypi::Rp1GpclkRouteService &rp1_gpclk_route_service)
{
    server.Get(
        "/api/network-safety",
        [&privileged_network_admin](
            const httplib::Request &, httplib::Response &response)
        {
            write_route_response(
                response,
                build_network_safety_status(privileged_network_admin),
                CorsHeaderSetter{});
        });

    server.Post(
        "/api/network-safety",
        [&privileged_network_admin](
            const httplib::Request &request, httplib::Response &response)
        {
            write_route_response(
                response,
                apply_network_safety(
                    request.body, privileged_network_admin),
                CorsHeaderSetter{});
        });

    server.Get(
        "/api/rp1-gpclk-route",
        [&rp1_gpclk_route_service](
            const httplib::Request &, httplib::Response &response)
        {
            write_route_response(
                response,
                build_rp1_gpclk_route_status(rp1_gpclk_route_service),
                CorsHeaderSetter{});
        });

    server.Post(
        "/api/rp1-gpclk-route",
        [&rp1_gpclk_route_service](
            const httplib::Request &request, httplib::Response &response)
        {
            write_route_response(
                response,
                apply_rp1_gpclk_route(
                    request.body, rp1_gpclk_route_service),
                CorsHeaderSetter{});
        });
}
} // namespace web_server_routes
