// SPDX-License-Identifier: MIT
#pragma once
#include "state_migration.hpp"
#include <memory>
namespace wsprrypi {
class StateMigrationSource {
public:
    StateMigrationSource(std::string source,std::string identity,StateRole);
    ~StateMigrationSource();
    StateMigrationCapture capture();
    const std::vector<std::string> &warnings() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Actual role-specific validation for initialized/migrated generations. Fresh
// seeds remain unconfirmed and are not converted into settings authority here.
void validate_application_storage(std::shared_ptr<SqliteStateStore>);
} // namespace wsprrypi
