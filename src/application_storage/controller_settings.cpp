// SPDX-License-Identifier: MIT
#include "controller_settings.hpp"
#include "application_control/advanced_settings.hpp"
#include "application_control/local_transport.hpp"
namespace wsprrypi {
ControllerSettings::ControllerSettings(std::shared_ptr<SqliteStateStore> database):database_(std::move(database)){
    if(!database_||database_->role()!=StateRole::Controller)
        throw StateStorageError(StateStorageError::Code::InvalidInput);
    (void)snapshot();
}
ControllerSettings::Snapshot ControllerSettings::snapshot(){
    std::lock_guard lock(mutex_);
    const auto saved=database_->record(StateRecord::Configuration);
    if(!saved.bytes||!saved.record_revision)throw StateStorageError(StateStorageError::Code::Unavailable);
    return {decode_persistent_configuration(*saved.bytes,StateRole::Controller),*saved.record_revision};
}
std::string ControllerSettings::persist_network_safety(const std::string &expected,SavedNetworkSafety mode){
    std::lock_guard lock(mutex_);
    if(mode!=SavedNetworkSafety::Enforced&&mode!=SavedNetworkSafety::InsecureDisabled)
        throw StateStorageError(StateStorageError::Code::InvalidInput);
    const auto before=database_->record(StateRecord::Configuration);
    if(!before.bytes||!before.record_revision)throw StateStorageError(StateStorageError::Code::Unavailable);
    if(*before.record_revision!=expected)throw StateStorageError(StateStorageError::Code::Conflict);
    auto proposed=decode_persistent_configuration(*before.bytes,StateRole::Controller);
    if(proposed.private_settings.network_safety==mode)return expected;
    proposed.private_settings.network_safety=mode;
    const auto bytes=encode_persistent_configuration(proposed.configuration,proposed.private_settings,StateRole::Controller);
    const auto committed=database_->commit(before.revision,{{StateRecord::Configuration,bytes}});
    return committed;
}
std::string ControllerSettings::persist_advanced(const std::string &expected,const nlohmann::json &changes){
    std::lock_guard lock(mutex_);
    const auto before=database_->record(StateRecord::Configuration);
    if(!before.bytes||!before.record_revision)throw LocalCommandRefused(LocalRefusal::StorageUnavailable);
    if(*before.record_revision!=expected)throw LocalCommandRefused(LocalRefusal::RevisionConflict);
    const auto previous=decode_persistent_configuration(*before.bytes,StateRole::Controller);
    const auto proposed=apply_advanced_changes(previous,changes);
    if(proposed==previous)return expected;
    return database_->commit(before.revision,{{StateRecord::Configuration,
        encode_persistent_configuration(proposed.configuration,proposed.private_settings,StateRole::Controller)}});
}
} // namespace wsprrypi
