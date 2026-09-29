// SPDX-License-Identifier: MIT
#pragma once
#include "network_http.hpp"
#include <string>
namespace wsprrypi {
// Explicit identity supports hardware-free typed integration tests. Production
// callers omit it and use the same machine identity as the inbound endpoint.
void start_wtp_fleet(const std::string& ini_path, const std::string& self_id = {});
void stop_wtp_fleet() noexcept;
PicoHttpResponse wtp_fleet_api(const std::string& method, const std::string& body,
                              const std::string& revision);
}
