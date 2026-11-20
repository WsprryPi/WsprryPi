// SPDX-License-Identifier: MIT
#pragma once
#include "sqlite_state_store.hpp"
#include "json.hpp"
namespace wsprrypi {
// Existing fixed-route maintenance, exclusively admitted with the backend
// stopped. Never constructs output hardware or modifies saved boot policy.
nlohmann::json route_maintenance_state(std::shared_ptr<SqliteStateStore>,
    const std::optional<std::string> &configure_route = {});
}
