// SPDX-License-Identifier: MIT
#pragma once
#include "wtp_integration/network_http.hpp"
namespace wsprrypi {
PicoHttpResponse udp_logging_installation();
void start_udp_logging_policy(const std::string &ini);
PicoHttpResponse udp_logging_api(const std::string &method,
                                 const std::string &body,
                                 const std::string &revision,
                                 bool capabilities = false, bool test = false);
} // namespace wsprrypi
