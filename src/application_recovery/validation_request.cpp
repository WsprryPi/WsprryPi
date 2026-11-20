// SPDX-License-Identifier: MIT
#include "validation_request.hpp"
#include "private_json.hpp"
#include "staged_source.hpp"
#include "destination_settings.hpp"
#include "definition_adaptation.hpp"
#include "paths.hpp"
#include "application_control/local_transport.hpp"
#include "application_control/web_presence.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/companion_capture.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "application_storage/companion_files.hpp"
#include "config_handler.hpp"
#include "recurrence_authority.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
#include <filesystem>
#include <mutex>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;using Journal=PiManagementJournal;
std::mutex validation_mutex;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
void directory(const std::filesystem::path &path){struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0700&&std::filesystem::canonical(path)==path);}
void fields(const Json &value,std::initializer_list<const char *> names){require(value.is_object()&&value.size()==names.size());for(const auto *name:names)require(value.contains(name));}
bool hex(const Json &value,std::size_t size){return value.is_string()&&value.get_ref<const std::string &>().size()==size&&value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
}
std::string recovery_destination_context(std::shared_ptr<SqliteStateStore> database){
    std::unique_lock configuration(config_update_mutex(),std::try_to_lock);if(!configuration.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
    require(database&&database==application_database()&&!database->held());const auto pair=pi_pair_owner();require(bool(pair));std::string result;
    pair->with_backup_files([&](const auto &pairs){wtp_fleet_with_backup_files([&](const auto &retirement){
        Json context{{"identity",database->identity()},{"role",database->role()==StateRole::Pi?"pi":"controller"},{"revision",database->snapshot().revision}};
        const auto state_root=std::filesystem::path(database->directory()).parent_path();
        const auto selector=Journal::read_private_record((state_root/"authority.json").string());require(bool(selector));context["selector"]=Journal::digest(selector->signature);
        const auto hold=Journal::read_private_record((state_root/"restore-hold.json").string());context["hold"]=hold?Json(Journal::digest(hold->signature)):Json(nullptr);
        for(const auto &[logical,path]:pairs){const auto record=Journal::read_private_record(path);require(bool(record));context["pairing"][logical]=Journal::digest(record->signature);}
        for(const auto &[logical,path]:retirement){const auto record=Journal::read_private_record(path);require(bool(record));context["retirement"][logical]=Journal::digest(record->signature);}
        const auto companions=capture_application_companions(database->directory()+"/configuration",database->identity(),application_catalog());
        context["companions"]=companion_manifest(companions);result=Journal::digest(context.dump());
    });});return result;
}
Json recovery_validation_request(const Json &request,const std::string &boot){
    fields(request,{"job_id"});require(hex(request["job_id"],32));const auto id=request["job_id"].get<std::string>();
    std::unique_lock lock(validation_mutex,std::try_to_lock);if(!lock.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
    const auto paths=recovery_paths();const auto root=std::filesystem::path(paths.root),jobs=root/"jobs",job=jobs/id;directory(root);directory(jobs);directory(job);
    const int job_lock=::open(job.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(job_lock>=0);
    struct JobLock {int value;~JobLock(){::close(value);}} lock_job{job_lock};
    if(::flock(job_lock,LOCK_EX|LOCK_NB))throw LocalCommandRefused(LocalRefusal::Busy);
    const auto active_record=Journal::read_private_record((job/"job.json").string());require(bool(active_record));
    const auto active=parse_recovery_json(active_record->bytes,1048576,32);
    require(active.value("schema",Json(nullptr))=="recovery-job/1"&&active.value("job_id",Json(nullptr))==id&&
        active.value("state",Json(nullptr))=="validating"&&active.value("confirmation_sha256",Json(nullptr)).is_null());
    const auto database=application_database();require(bool(database));
    const auto admitted=Journal::read_private_record((job/"validation-admission.json").string());require(bool(admitted));const auto admission=strict_browser_json(admitted->bytes);
    fields(admission,{"schema","job_id","backend_boot","identity","role","purpose","archive_sha256","manifest_sha256"});
    require(admission["schema"]=="recovery-validation-admission/1"&&admission["job_id"]==id&&admission["backend_boot"]==boot&&
        admission["identity"]==database->identity()&&admission["role"]==(database->role()==StateRole::Pi?"pi":"controller")&&
        (admission["purpose"]=="recover"||admission["purpose"]=="move")&&hex(admission["archive_sha256"],64)&&hex(admission["manifest_sha256"],64));
    const auto result_path=(job/"validation-result.json").string();
    if(const auto existing=Journal::read_private_record(result_path)){
        const auto proof=strict_browser_json(existing->bytes);fields(proof,{"schema","job_id","backend_boot","state","preview_digest","error"});
        require(proof["schema"]=="recovery-validation-result/1"&&proof["job_id"]==id&&proof["backend_boot"]==boot&&
            ((proof["state"]=="ready"&&hex(proof["preview_digest"],64)&&proof["error"].is_null())||
            (proof["state"]=="failed"&&proof["preview_digest"].is_null()&&(proof["error"]=="busy"||proof["error"]=="invalid_source"||proof["error"]=="destination_recovery_required"))));
        return {{"job_id",id},{"state",proof["state"]}};
    }
    Json proof{{"schema","recovery-validation-result/1"},{"job_id",id},{"backend_boot",boot},{"state","failed"},{"preview_digest",nullptr},{"error",nullptr}};
    try{
        const auto raw_manifest=Journal::read_private_record((job/"archive-manifest.json").string());require(raw_manifest&&Journal::digest(raw_manifest->bytes)==admission["manifest_sha256"].get<std::string>());
        const auto source=validate_staged_recovery_source(job.string());const bool move=admission["purpose"]=="move";
        require(source.settings.role==database->role()&&(move||source.manifest["source"]["identity"]==database->identity()));
        std::unique_lock configuration(config_update_mutex(),std::try_to_lock);if(!configuration.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
        const auto destination=configuration_from_snapshot(database->snapshot(),database->role());const auto proposed=recovery_destination_settings(source.settings,destination,move);
        if(database->role()==StateRole::Pi)require(prepare_saved_recovery_configuration(proposed).valid);
        if(database->role()==StateRole::Pi&&source.state.records.contains(StateRecord::ManagedSettings)){
            const auto source_settings=parse_recovery_json(source.state.records.at(StateRecord::ManagedSettings),1048576,32);
            if(source_settings.at("scheduling").at("solar").at("enabled")==true){
                const auto state=database->snapshot();require(state.records.contains(StateRecord::RecurrenceAuthority));
                const auto authority=parse_recovery_json(state.records.at(StateRecord::RecurrenceAuthority),1048576,32);
                const auto status=authority.value("state",Json(nullptr));
                const bool first_setup=!state.records.contains(StateRecord::ManagedSettings)&&authority.contains("clean_proof")&&!authority["clean_proof"].is_null();
                if(status!="native"&&status!="staged"&&!first_setup)throw LocalCommandRefused(LocalRefusal::SetupRequired);
            }
        }
        const auto context=recovery_destination_context(database);const auto listeners=web_service_bootstrap(database,boot);
        const auto changes=recovery_configuration_changes(destination,proposed);const auto retained=move?recovery_configuration_changes(proposed,source.settings):Json::array();
        const auto current_catalog=application_catalog().snapshot({});
        auto definitions=Json::array();for(const auto &row:source.profiles.at("profiles"))definitions.push_back({{"id",row.at("id")},{"name",row.at("name")},
            {"destination_status",recovery_definition_resolution(row,current_catalog.first.at("profiles"),move)}});
        const auto records=job/"source-records";prepare_companion_directory(records.string());auto record_files=Json::array();
        const std::map<StateRecord,std::string> record_names{{StateRecord::Configuration,"configuration"},{StateRecord::ManagedSettings,"managed-settings"},
            {StateRecord::Operations,"operations"},{StateRecord::Catalog,"catalog"},{StateRecord::RecurrenceAuthority,"recurrence-authority"},
            {StateRecord::RecurrenceInitialization,"recurrence-initialization"},{StateRecord::Initialization,"initialization"},{StateRecord::RecurrenceRecovery,"recurrence-recovery"}};
        for(const auto &[kind,bytes]:source.state.records){const auto name="state-"+record_names.at(kind)+".json";
            (void)Journal::publish_private_record((records/name).string(),{},bytes);record_files.push_back({{"name",name},{"size",bytes.size()},{"sha256",Journal::digest(bytes)}});
        }
        (void)Journal::publish_private_record((records/"index.json").string(),{},Json{{"schema","recovery-source-records/1"},
            {"identity",source.manifest["source"]["identity"]},{"role",source.manifest["source"]["role"]},{"files",record_files}}.dump());
        const auto settings=encode_persistent_configuration(proposed.configuration,proposed.private_settings,proposed.role);
        (void)Journal::publish_private_record((job/"proposed-settings.json").string(),{},settings);
        auto existing_definitions=Json::array();for(const auto &row:current_catalog.first.at("profiles"))
            existing_definitions.push_back({{"id",row.at("id")},{"name",row.at("name")}});
        Json preview{{"fleet_policy","merge_preserving_destination_authority"},{"destination_fleet_definitions",existing_definitions},
            {"settings_sha256",Journal::digest(settings)},{"schema","recovery-preview/1"},{"job_id",id},{"purpose",admission["purpose"]},{"archive_sha256",admission["archive_sha256"]},
            {"source",source.manifest["source"]},{"created_utc",source.manifest["created_utc"]},{"destination_identity",database->identity()},
            {"destination_role",database->role()==StateRole::Pi?"pi":"controller"},{"backend_boot",boot},{"destination_digest",context},
            {"changes",changes},{"retained_destination_settings",retained},{"fleet_definitions",definitions},
            {"listeners",{{"current_web_port",listeners["web_port"]},{"current_socket_port",listeners["socket_port"]},
                {"restored_web_port",proposed.configuration["Operation"]["Web Port"]},{"restored_socket_port",proposed.configuration["Operation"]["Socket Port"]}}},
            {"local_output_held",true},{"remote_authority_held",true},{"remote_jobs_adopted",false}};
        preview["digest"]=Journal::digest(preview.dump());require(preview.dump().size()<=65536);
        (void)Journal::publish_private_record((job/"preview.json").string(),{},preview.dump());proof["state"]="ready";proof["preview_digest"]=preview["digest"];
    }catch(const LocalCommandRefused &error){proof["error"]=error.reason()==LocalRefusal::Busy?"busy":error.reason()==LocalRefusal::SetupRequired?"destination_recovery_required":"invalid_source";}
    catch(...){proof["error"]="invalid_source";}
    const auto after=Journal::read_private_record((job/"validation-admission.json").string());require(after&&after->signature==admitted->signature&&after->bytes==admitted->bytes);
    (void)Journal::publish_private_record(result_path,{},proof.dump());return {{"job_id",id},{"state",proof["state"]}};
}
}
