// SPDX-License-Identifier: MIT
#pragma once
#include "application_storage/persistent_configuration.hpp"
namespace wsprrypi {
struct RecoverySource {
    nlohmann::json manifest,profiles;
    StateSnapshot state;
    PersistentConfiguration settings;
};
// Untrusted decoded members are copied with bounded size/hash/stat fencing into
// private validation staging. Actual SQL, native/controller, Fleet, pairing,
// retirement and credential parsers run there; active owners are never selected.
RecoverySource validate_staged_recovery_source(const std::string &job_directory);
}
