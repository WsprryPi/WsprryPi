// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "wtp_settings.hpp"
#include <filesystem>

namespace wsprrypi {
// Metadata only: enumeration never opens a tty or changes configuration.
nlohmann::json wtp_usb_discovery(
    const std::filesystem::path &sysfs_root = "/sys",
    const std::filesystem::path &dev_root = "/dev");
// Resolve a fresh observation and reject changed or disconnected endpoints.
void validate_discovered_usb(const nlohmann::json &snapshot,
                             const std::string &id, const WtpSettings &);
} // namespace wsprrypi
