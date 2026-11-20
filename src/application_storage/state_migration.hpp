// SPDX-License-Identifier: MIT
#pragma once
#include "sqlite_state_store.hpp"
#include "companion_files.hpp"

namespace wsprrypi {
struct StateMigrationCapture {
    std::vector<StateChange> records;
    std::optional<std::string> progress_bytes;
    std::string source_revision;
    std::string source_namespace;
    std::map<StateRecord,std::string> original_records;
    std::vector<CompanionFile> companions;
};
enum class StateMigrationPoint {SourceBackedUp,DatabaseCommitted,DatabasePublished,SourceRetired,Initialized};
// Trusted, local, quiescent backend operation. The capture and validator use
// the actual role's owners; neither callback may initialize output authority.
// The selected database retains the root admission lock for its lifetime.
std::shared_ptr<SqliteStateStore> migrate_application_state(const std::string &root,
    const std::string &identity,StateRole,const std::string &source_namespace,
    const std::function<StateMigrationCapture()> &capture,
    const std::function<void(std::shared_ptr<SqliteStateStore>)> &validate,
    const std::function<void(StateMigrationPoint)> &checkpoint = {});
// Pure current initialization-selector validation, shared by admitted migration
// and recovery staging. This does not open state or create authority.
nlohmann::json parse_initial_application_selection(const std::string &,const std::string &identity,StateRole);
nlohmann::json initial_application_selection_provenance(const nlohmann::json &);
// Normal startup never creates a root, imports source, or resumes preparation.
std::shared_ptr<SqliteStateStore> open_application_state(const std::string &root,
    const std::string &identity,StateRole);
// Explicit first-install operation. The deterministic role-specific seed is
// bound to a persisted installation ID and final namespace before publication.
std::shared_ptr<SqliteStateStore> initialize_application_state(const std::string &root,
    const std::string &identity,StateRole,
    const std::function<std::vector<StateChange>(const std::string &final_namespace,const std::string &installation_id)> &seed,
    const std::function<void(std::shared_ptr<SqliteStateStore>)> &validate,
    const std::function<void(StateMigrationPoint)> &checkpoint = {});
} // namespace wsprrypi
