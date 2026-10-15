// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "httplib.hpp"
#include <optional>
namespace wsprrypi {
// Shared by the installed HTTP routes and in-process protected-route fixtures.
// Peer/Host/Origin authority is checked separately by BackendHttpGuard.
inline std::optional<BrowserRequest> wtp_browser_http_request(const httplib::Request &r) {
  if (r.target != r.path || r.body.size() > 32768 || r.get_header_value_count("Host") != 1) return {};
  for (const auto name : {"Origin","Content-Type","If-Match","X-WsprryPico-Request","X-WsprryPi-Member-Context","Sec-Fetch-Site"})
    if (r.get_header_value_count(name) > 1) return {};
  const auto fetch = r.get_header_value("Sec-Fetch-Site");
  return BrowserRequest{r.method,r.path,r.body,r.get_header_value("If-Match"),
      r.has_header("Origin") && r.get_header_value("Content-Type") == "application/json" &&
      r.get_header_value("X-WsprryPico-Request") == "1" && (fetch.empty() || fetch == "same-origin" || fetch == "none"),
      r.get_header_value("X-WsprryPi-Member-Context")};
}
}
