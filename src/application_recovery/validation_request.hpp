// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "application_storage/sqlite_state_store.hpp"
namespace wsprrypi {
std::string recovery_destination_context(std::shared_ptr<SqliteStateStore>);
nlohmann::json recovery_validation_request(const nlohmann::json &,const std::string &boot);
}
