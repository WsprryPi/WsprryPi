// SPDX-License-Identifier: MIT
#pragma once
#include "local_transport.hpp"
namespace wsprrypi {
// The transport supplies connection identity. No request can select another
// session, serialize its principal, or acknowledge another session's effects.
nlohmann::json backend_browser_request(const LocalRequest &);
void backend_browser_disconnected(const std::string &) noexcept;
}
