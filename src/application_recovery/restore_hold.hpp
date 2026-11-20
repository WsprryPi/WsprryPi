// SPDX-License-Identifier: MIT
#pragma once
#include "application_storage/sqlite_state_store.hpp"
#include "json.hpp"
namespace wsprrypi {
// Selected before backend parsing/output admission. Execution reads are atomic;
// no SQLite, filesystem, or coordinator mutex is used by these two gates.
void select_application_restore_hold(std::shared_ptr<SqliteStateStore>);
void retire_application_restore_hold() noexcept;
bool local_restore_output_held() noexcept;
bool restored_remote_authority_held() noexcept;
bool restore_maintenance_active() noexcept;
nlohmann::json application_restore_hold_view();
// Only an explicit, current destination review may release these extra gates.
// Existing local cleanup and remote pairing/authority checks remain separate.
void release_application_restore_hold(std::shared_ptr<SqliteStateStore>,const std::string &expected_revision,
    bool release_local,bool release_remote);
// Confirmed maintenance only, while the backend owns the selected root and the
// caller holds configuration admission. Persist intent outside the generation
// before requiring a new held selector; never grants output or remote authority.
enum class RestoreHoldPoint {IntentRecorded,JobRecorded,HoldPublished,SelectorPublished};
nlohmann::json prepare_application_restore_hold(std::shared_ptr<SqliteStateStore>,const std::string &job_directory,
    const std::string &job_id,const std::string &backend_boot,const std::function<void(RestoreHoldPoint)> &checkpoint = {});
}
