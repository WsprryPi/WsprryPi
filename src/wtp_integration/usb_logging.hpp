// SPDX-License-Identifier: MIT
#pragma once
#include "network_http.hpp"
#include <functional>
namespace wsprrypi {
using UsbLoggingCatalogSnapshot=std::function<nlohmann::json()>;
void start_usb_logging(const std::string &ini_path,UsbLoggingCatalogSnapshot catalog = {});
void stop_usb_logging() noexcept;
// Read-only validation through the collector's actual saved-policy validator.
void validate_saved_usb_logging_policy(const std::string &bytes,const std::string &collector_id);
namespace testing {
// Policy-only manager with synthetic identity; never inventories or opens USB.
void start_usb_logging_policy(const std::string &ini_path, const std::string &machine_id_path,UsbLoggingCatalogSnapshot catalog = {});
}
PicoHttpResponse usb_logging_api(const std::string &method,
                                 const std::string &body,
                                 const std::string &revision);
// Read-only descriptor identities for filtering; collection still requires verified Fleet admission.
nlohmann::json usb_logging_attached_sources(const nlohmann::json &discovery);
// Bounded, device-bound decoding seam used by deterministic tests and
// collector.
// Closed USB-Logging/1 handshake validators, also used by paired fixtures.
nlohmann::json parse_usb_logging_status(const std::string &line, const std::string &device,
                                      const std::string &boot = "");
void validate_usb_logging_start(const std::string &line, const std::string &device,
                                const std::string &boot);
nlohmann::json parse_usb_log(const std::string &line, const std::string &device,
                             const std::string &boot);
} // namespace wsprrypi
