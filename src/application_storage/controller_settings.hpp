// SPDX-License-Identifier: MIT
#pragma once
#include "persistent_configuration.hpp"
#include <mutex>
namespace wsprrypi {
class ControllerSettings {
public:
    explicit ControllerSettings(std::shared_ptr<SqliteStateStore>);
    struct Snapshot {PersistentConfiguration settings;std::string revision;};
    Snapshot snapshot();
    std::string persist_network_safety(const std::string &expected_revision,SavedNetworkSafety);
    std::string persist_advanced(const std::string &expected_revision,const nlohmann::json &changes);
private:
    std::shared_ptr<SqliteStateStore> database_;
    std::mutex mutex_;
};
} // namespace wsprrypi
