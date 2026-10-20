/**
 * @file web_server_http_response.hpp
 * @brief HTTP-independent response data shared by private web routes.
 */

#ifndef WEB_SERVER_HTTP_RESPONSE_HPP
#define WEB_SERVER_HTTP_RESPONSE_HPP

#include <string>

namespace web_server_routes
{
struct RouteResponse
{
    int status = 200;
    std::string body;
    std::string content_type;
    bool allow_cors = false;
};
} // namespace web_server_routes

#endif // WEB_SERVER_HTTP_RESPONSE_HPP
