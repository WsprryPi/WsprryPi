// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
namespace wsprrypi {
nlohmann::json recovery_prepare_request(const nlohmann::json &,const std::string &backend_boot);
}
