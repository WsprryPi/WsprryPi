/**
 * @file web_server_config_http.hpp
 * @brief HTTP-independent configuration and control response builders.
 */

#ifndef WEB_SERVER_CONFIG_HTTP_HPP
#define WEB_SERVER_CONFIG_HTTP_HPP

#include "web_server_http_response.hpp"

#include <string>

namespace web_server_routes
{
RouteResponse build_config_response();
RouteResponse build_si5351_addresses_response(const std::string &i2c_bus);
RouteResponse apply_config_update(const std::string &request_body);
RouteResponse apply_config_repair(const std::string &request_body);
RouteResponse stop_transmission(const std::string &request_body);
} // namespace web_server_routes

#endif // WEB_SERVER_CONFIG_HTTP_HPP
