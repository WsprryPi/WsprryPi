/**
 * @file web_server_routes.hpp
 * @brief Private registration boundaries for WsprryPi HTTP routes.
 */

#ifndef WEB_SERVER_ROUTES_HPP
#define WEB_SERVER_ROUTES_HPP

#include "support_request_guard.hpp"

#include <functional>
#include <string>

class PrivilegedNetworkAdmin;
namespace httplib
{
class Server;
struct Response;
}
namespace wsprrypi
{
class Rp1GpclkRouteService;
}

namespace web_server_routes
{
struct RouteResponse;
using CorsHeaderSetter = std::function<void(httplib::Response &)>;
using NetworkSnapshotProvider =
    std::function<SupportRequestGuardSnapshot()>;

void register_request_guard(
    httplib::Server &server,
    NetworkSnapshotProvider network_snapshot_provider);
void register_config(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers);
void register_control(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers);
void register_privileged_admin(
    httplib::Server &server,
    PrivilegedNetworkAdmin &privileged_network_admin,
    wsprrypi::Rp1GpclkRouteService &rp1_gpclk_route_service);
void register_version(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers);
void write_route_response(
    httplib::Response &response,
    RouteResponse result,
    const CorsHeaderSetter &set_cors_headers);
} // namespace web_server_routes

#endif // WEB_SERVER_ROUTES_HPP
