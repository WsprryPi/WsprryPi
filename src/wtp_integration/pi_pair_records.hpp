// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// Private storage validation only. These helpers neither initialize history nor
// grant access. Returned documents contain secrets and belong only to owners.
bool valid_pi_pair_catalog(const nlohmann::json &,const std::string &self) noexcept;
nlohmann::json parse_pi_pair_catalog(const std::string &,const std::string &self);
} // namespace wsprrypi
