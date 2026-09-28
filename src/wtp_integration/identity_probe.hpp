// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "json.hpp"
#include "wtp_settings.hpp"
namespace wsprrypi {
// Explicit host action: TLS/Plain LAN HELLO, STATUS and CAPS only. No CLAIM.
nlohmann::json wtp_probe_network_identity(const WtpSettings &);
}
