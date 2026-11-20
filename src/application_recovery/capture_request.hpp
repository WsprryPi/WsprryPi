// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// Administrator-only request from the maintenance worker. Only an admitted job
// identity crosses IPC; all paths derive from fixed backend-owned roots.
nlohmann::json recovery_capture_request(const nlohmann::json &,const std::string &backend_boot);
}
