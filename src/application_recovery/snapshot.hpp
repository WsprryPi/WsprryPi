// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// Backend-only capture into a new private job directory. Actual persistence
// owners remain authoritative. Refuses unsafe activity without stopping it.
// Returns a secret-free inventory for the shared archive helper, never paths
// to the live database/key stores. Failed staging is owned by the job cleanup.
nlohmann::json capture_application_recovery_snapshot(const std::string &private_staging);
}
