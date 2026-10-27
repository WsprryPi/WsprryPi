// SPDX-License-Identifier: MIT
#pragma once
#include "network_http.hpp"
namespace wsprrypi {
void start_usb_logging(const std::string &ini_path);
void stop_usb_logging() noexcept;
PicoHttpResponse usb_logging_api(const std::string &method,
                                 const std::string &body,
                                 const std::string &revision);
// Read-only descriptor identities for filtering; collection still requires verified Fleet admission.
nlohmann::json usb_logging_attached_sources(const nlohmann::json &discovery);
// Bounded, device-bound decoding seam used by deterministic tests and
// collector.
nlohmann::json parse_usb_log(const std::string &line, const std::string &device,
                             const std::string &boot);
} // namespace wsprrypi
