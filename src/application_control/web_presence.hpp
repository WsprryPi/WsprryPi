// SPDX-License-Identifier: MIT
#pragma once
#include "local_transport.hpp"
#include "application_storage/sqlite_state_store.hpp"
#include <optional>
namespace wsprrypi {
struct WebListeners {std::optional<int> http,websocket;};
WebListeners web_service_listeners();
void prepare_backend_web_bootstrap(std::shared_ptr<SqliteStateStore>,const std::string &boot);
nlohmann::json web_service_bootstrap(std::shared_ptr<SqliteStateStore>,const std::string &boot);
nlohmann::json backend_web_presence(const LocalRequest &);
void backend_web_disconnected(const std::string &) noexcept;
}
