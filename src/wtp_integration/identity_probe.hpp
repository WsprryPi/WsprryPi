// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "json.hpp"
#include "wtp_settings.hpp"
namespace wsprrypi {
// Explicit read-only HTTPS management identification; no execution session.
nlohmann::json wtp_probe_network_identity(const WtpSettings &);
// Explicit interface-4 management identification. No clock or execution writes.
nlohmann::json wtp_probe_usb_identity(const WtpSettings &);
}
