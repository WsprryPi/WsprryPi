/**
 * @file web_server_admin_http.hpp
 * @brief HTTP-independent privileged-administration response builders.
 */

#ifndef WEB_SERVER_ADMIN_HTTP_HPP
#define WEB_SERVER_ADMIN_HTTP_HPP

#include "web_server_http_response.hpp"

#include <string>

class PrivilegedNetworkAdmin;
namespace wsprrypi
{
class Rp1GpclkRouteService;
}

namespace web_server_routes
{
RouteResponse build_network_safety_status(
    PrivilegedNetworkAdmin &privileged_network_admin);
RouteResponse apply_network_safety(
    const std::string &request_body,
    PrivilegedNetworkAdmin &privileged_network_admin);
RouteResponse build_rp1_gpclk_route_status(
    wsprrypi::Rp1GpclkRouteService &rp1_gpclk_route_service);
RouteResponse apply_rp1_gpclk_route(
    const std::string &request_body,
    wsprrypi::Rp1GpclkRouteService &rp1_gpclk_route_service);
} // namespace web_server_routes

#endif // WEB_SERVER_ADMIN_HTTP_HPP
