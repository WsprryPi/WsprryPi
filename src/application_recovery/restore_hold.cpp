// SPDX-License-Identifier: MIT
#include "restore_hold.hpp"
#include "application_storage/generation_selection.hpp"
#include "application_storage/state_migration.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <limits>
namespace wsprrypi {
namespace {
std::atomic_bool local_hold{false},remote_hold{false},maintenance{false};
std::mutex mutex;bool selected{};nlohmann::json record;
}
void select_application_restore_hold(std::shared_ptr<SqliteStateStore> database){
    std::lock_guard lock(mutex);
    if(selected||!database)throw StateStorageError(StateStorageError::Code::InvalidInput);
    const auto directory=std::filesystem::path(database->directory());const auto name=directory.filename().string();
    // Standalone storage fixtures have no application-generation authority.
    // Production state always enters through open_application_state first.
    if(name.starts_with("generation-")){
        if(const auto saved=PiManagementJournal::read_private_record((directory.parent_path()/"restore-hold.json").string())){
            record=parse_restore_hold(saved->bytes,database->identity(),database->role(),name.substr(11));
            local_hold.store(record.at("local_output").get<bool>(),std::memory_order_release);
            remote_hold.store(record.at("remote_authority").get<bool>(),std::memory_order_release);
            maintenance.store(record.at("stage")=="maintenance",std::memory_order_release);
        }
    }
    selected=true;
}
void retire_application_restore_hold() noexcept{
    local_hold.store(true,std::memory_order_release);remote_hold.store(true,std::memory_order_release);
}
bool local_restore_output_held() noexcept{return local_hold.load(std::memory_order_acquire);}
bool restored_remote_authority_held() noexcept{return remote_hold.load(std::memory_order_acquire);}
bool restore_maintenance_active() noexcept{return maintenance.load(std::memory_order_acquire);}
void release_application_restore_hold(std::shared_ptr<SqliteStateStore> database,const std::string &expected_revision,
    bool release_local,bool release_remote){
    std::lock_guard lock(mutex);
    if(!selected||!database||database->held()||!record.is_object()||record.at("stage")!="review"||
        record.at("revision")!=expected_revision||(!release_local&&!release_remote))
        throw StateStorageError(StateStorageError::Code::InvalidInput);
    const auto path=(std::filesystem::path(database->directory()).parent_path()/"restore-hold.json").string();
    const auto previous=PiManagementJournal::read_private_record(path);
    if(!previous||parse_restore_hold(previous->bytes,database->identity(),database->role(),record.at("generation").get<std::string>())!=record)
        throw StateStorageError(StateStorageError::Code::Unavailable);
    auto next=record;const auto revision=std::stoull(expected_revision);
    if(revision==std::numeric_limits<std::uint64_t>::max())throw StateStorageError(StateStorageError::Code::InvalidInput);
    next["revision"]=std::to_string(revision+1);
    if(release_local)next["local_output"]=false;
    if(release_remote)next["remote_authority"]=false;
    (void)PiManagementJournal::publish_private_record(path,previous,next.dump());record=next;
    local_hold.store(next.at("local_output").get<bool>(),std::memory_order_release);
    remote_hold.store(next.at("remote_authority").get<bool>(),std::memory_order_release);
}
nlohmann::json prepare_application_restore_hold(std::shared_ptr<SqliteStateStore> database,const std::string &job_directory,
    const std::string &job_id,const std::string &backend_boot,const std::function<void(RestoreHoldPoint)> &checkpoint){
    using Json=nlohmann::json;using Journal=PiManagementJournal;
    const auto require=[](bool value){if(!value)throw StateStorageError(StateStorageError::Code::Unavailable);};
    std::lock_guard lock(mutex);require(selected&&database&&!database->held()&&job_id.size()==32&&
        job_id.find_first_not_of("0123456789abcdef")==std::string::npos&&backend_boot.size()==32&&backend_boot.find_first_not_of("0123456789abcdef")==std::string::npos);
    const auto directory=std::filesystem::path(database->directory()),root=directory.parent_path(),job=std::filesystem::path(job_directory);
    require(directory.filename().string().starts_with("generation-")&&job.filename()==job_id&&std::filesystem::canonical(job)==job);
    require(!Journal::read_private_record((job/"hold-prepared.json").string())&&!Journal::read_private_record((root/"restore-preparation.json").string()));
    const auto generation=directory.filename().string().substr(11);const auto name=database->role()==StateRole::Pi?"pi":"controller";
    const auto selection=Journal::read_private_record((root/"authority.json").string());require(bool(selection));
    const auto previous_selection=nlohmann::json::parse(selection->bytes);const auto initial=database->record(StateRecord::Initialization);require(initial.bytes.has_value());
    const Json next_selection{{"schema","application-generation/1"},{"identity",database->identity()},{"role",name},{"generation",generation},
        {"initialization",Json::parse(*initial.bytes)},{"restore_job",job_id}};
    (void)parse_generation_selection(next_selection.dump(),database->identity(),database->role());
    if(previous_selection.value("schema",Json(nullptr))=="application-generation/1"){
        const auto current=parse_generation_selection(selection->bytes,database->identity(),database->role());
        require(current["generation"]==generation&&current["initialization"]==next_selection["initialization"]);
    }else{
        const auto current=parse_initial_application_selection(selection->bytes,database->identity(),database->role());
        require(current["state"]=="ready"&&generation=="1"&&initial_application_selection_provenance(current)==next_selection["initialization"]);
    }
    const auto previous_hold=Journal::read_private_record((root/"restore-hold.json").string());std::uint64_t revision{};
    Json old_hold=nullptr;
    if(previous_hold){old_hold=parse_restore_hold(previous_hold->bytes,database->identity(),database->role(),generation);require(old_hold["stage"]!="maintenance");revision=std::stoull(old_hold.at("revision").get<std::string>());}
    require(revision<std::numeric_limits<std::uint64_t>::max());
    const Json next_hold{{"schema","application-restore-hold/1"},{"identity",database->identity()},{"role",name},{"generation",generation},{"job_id",job_id},
        {"local_output",true},{"remote_authority",true},{"revision",std::to_string(revision+1)},{"stage","maintenance"}};
    (void)parse_restore_hold(next_hold.dump(),database->identity(),database->role(),generation);
    const Json intent{{"schema","recovery-hold-prepared/1"},{"job_id",job_id},{"identity",database->identity()},{"role",name},{"generation",generation},
        {"previous_selection",previous_selection},{"previous_hold",old_hold},{"selection",next_selection},{"hold",next_hold},{"backend_boot",backend_boot}};
    // Once this trusted admission starts, live gates stay held even if a later
    // durable write fails. The owner reports failure; it never resumes output.
    local_hold.store(true,std::memory_order_release);remote_hold.store(true,std::memory_order_release);maintenance.store(true,std::memory_order_release);
    (void)Journal::publish_private_record((root/"restore-preparation.json").string(),{},intent.dump());
    if(checkpoint)checkpoint(RestoreHoldPoint::IntentRecorded);
    (void)Journal::publish_private_record((job/"hold-prepared.json").string(),{},intent.dump());
    if(checkpoint)checkpoint(RestoreHoldPoint::JobRecorded);
    (void)Journal::publish_private_record((root/"restore-hold.json").string(),previous_hold,next_hold.dump());record=next_hold;
    if(checkpoint)checkpoint(RestoreHoldPoint::HoldPublished);
    (void)Journal::publish_private_record((root/"authority.json").string(),selection,next_selection.dump());
    if(checkpoint)checkpoint(RestoreHoldPoint::SelectorPublished);
    return intent;
}
nlohmann::json application_restore_hold_view(){
    std::lock_guard lock(mutex);
    return {{"schema","restore-hold-status/1"},{"local_output_held",local_restore_output_held()},
        {"remote_authority_held",restored_remote_authority_held()},{"maintenance_active",restore_maintenance_active()},
        {"job_id",record.is_object()?record.at("job_id"):nlohmann::json(nullptr)},
        {"revision",record.is_object()?record.at("revision"):nlohmann::json(nullptr)}};
}
}
