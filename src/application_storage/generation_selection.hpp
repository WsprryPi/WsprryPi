// SPDX-License-Identifier: MIT
#pragma once
#include "sqlite_state_store.hpp"
#include "json.hpp"
namespace wsprrypi {
// Private publication selectors, independent of SQL and shared control formats.
nlohmann::json parse_generation_selection(const std::string &,const std::string &identity,StateRole);
nlohmann::json parse_restore_hold(const std::string &,const std::string &identity,StateRole,const std::string &generation);
}
