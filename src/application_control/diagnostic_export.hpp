// SPDX-License-Identifier: MIT
#pragma once
#include "application_storage/sqlite_state_store.hpp"
#include "json.hpp"
#include <optional>
namespace wsprrypi {
nlohmann::json diagnostic_settings_export(std::shared_ptr<SqliteStateStore>,const std::string &boot);
nlohmann::json diagnostic_settings_chunk(std::shared_ptr<SqliteStateStore>,const std::string &boot,const nlohmann::json &request);
std::optional<int> diagnostic_export_main(int argc,char *argv[]);
}
