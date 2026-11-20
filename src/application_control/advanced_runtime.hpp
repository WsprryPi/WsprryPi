// SPDX-License-Identifier: MIT
#pragma once
#include "local_transport.hpp"
#include "application_storage/sqlite_state_store.hpp"
namespace wsprrypi {
nlohmann::json advanced_settings_view(std::shared_ptr<SqliteStateStore>);
nlohmann::json backend_local_request(std::shared_ptr<SqliteStateStore>,LocalPeer,const LocalRequest &,const std::string &boot);
}
