// SPDX-License-Identifier: MIT
#include "prepare_request.hpp"
#include "private_json.hpp"
#include "paths.hpp"
#include "restore_hold.hpp"
#include "validation_request.hpp"
#include "application_storage/application_state.hpp"
#include "config_handler.hpp"
#include "controller_runtime.hpp"
#include "native_recurrence.hpp"
#include "scheduling.hpp"
#include "wspr_transmit.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <filesystem>
#include <mutex>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;using Journal=PiManagementJournal;
std::mutex preparation;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
void fields(const Json &value,std::initializer_list<const char *> keys){require(value.is_object()&&value.size()==keys.size());for(const auto *key:keys)require(value.contains(key));}
bool hex(const Json &value,std::size_t size){return value.is_string()&&value.get_ref<const std::string &>().size()==size&&value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
void directory(const std::filesystem::path &path){struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0700&&std::filesystem::canonical(path)==path);}
Json read(const std::filesystem::path &path,std::size_t maximum=65536){const auto source=Journal::read_private_record(path.string());require(bool(source));return parse_recovery_json(source->bytes,maximum,16);}
}
Json recovery_prepare_request(const Json &request,const std::string &boot){
    fields(request,{"job_id"});require(hex(request["job_id"],32));const auto id=request["job_id"].get<std::string>();
    std::unique_lock admitted(preparation,std::try_to_lock);if(!admitted.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
    const auto paths=recovery_paths();const auto root=std::filesystem::path(paths.root),jobs=root/"jobs",job=jobs/id;directory(root);directory(jobs);directory(job);
    const int job_lock=::open(job.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(job_lock>=0);
    struct JobLock {int value;~JobLock(){::close(value);}} lock_job{job_lock};
    if(::flock(job_lock,LOCK_EX|LOCK_NB))throw LocalCommandRefused(LocalRefusal::Busy);
    const auto database=application_database();require(bool(database));
    const auto confirmation=read(job/"confirmation-admission.json");fields(confirmation,{"schema","job_id","identity","backend_boot","preview_digest","archive_sha256"});
    require(confirmation["schema"]=="recovery-confirmation/1"&&confirmation["job_id"]==id&&confirmation["identity"]==database->identity()&&
        confirmation["backend_boot"]==boot&&hex(confirmation["preview_digest"],64)&&hex(confirmation["archive_sha256"],64));
    const auto terminal=(job/"stop-result.json").string();
    if(const auto previous=Journal::read_private_record(terminal)){
        const auto proof=parse_recovery_json(previous->bytes,8192);fields(proof,{"schema","job_id","backend_boot","state","generation","error"});
        require(proof["schema"]=="recovery-stop-result/1"&&proof["job_id"]==id&&proof["backend_boot"]==boot&&
            ((proof["state"]=="held"&&proof["generation"].is_string()&&proof["error"].is_null())||(proof["state"]=="failed"&&proof["error"]=="cleanup_unconfirmed")));
        return {{"job_id",id},{"state",proof["state"]}};
    }
    const auto active=read(job/"job.json",1048576);
    require(active.value("schema",Json(nullptr))=="recovery-job/1"&&active.value("job_id",Json(nullptr))==id&&active.value("identity",Json(nullptr))==database->identity()&&
        active.value("kind",Json(nullptr))=="restore"&&active.value("state",Json(nullptr))=="applying"&&active.contains("confirmation_sha256")&&
        active["confirmation_sha256"].is_string()&&active["confirmation_sha256"]==Journal::digest(confirmation.dump()));
    auto preview=read(job/"preview.json");require(preview.contains("digest")&&preview["digest"]==confirmation["preview_digest"]&&
        preview.value("schema",Json(nullptr))=="recovery-preview/1"&&preview.value("job_id",Json(nullptr))==id&&
        preview.value("backend_boot",Json(nullptr))==boot&&preview.value("destination_identity",Json(nullptr))==database->identity()&&
        preview.value("archive_sha256",Json(nullptr))==confirmation["archive_sha256"]&&preview.contains("destination_digest")&&hex(preview["destination_digest"],64));
    const auto digest=preview["digest"].get<std::string>();preview.erase("digest");require(Journal::digest(preview.dump())==digest);
    const auto proposed=Journal::read_private_record((job/"proposed-settings.json").string());require(proposed&&preview.contains("settings_sha256")&&
        hex(preview["settings_sha256"],64)&&Journal::digest(proposed->bytes)==preview["settings_sha256"].get<std::string>());
    (void)decode_persistent_configuration(proposed->bytes,database->role());
    {
        std::unique_lock configuration(config_update_mutex(),std::try_to_lock);if(!configuration.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
        if(recovery_destination_context(database)!=preview["destination_digest"].get<std::string>())throw LocalCommandRefused(LocalRefusal::RevisionConflict);
        // The closed preview and current destination context are checked before
        // holding output or changing persistent selection. Saved startup intent
        // remains in SQL; maintenance is a separate durable admission decision.
        (void)prepare_application_restore_hold(database,job.string(),id,boot);
    }
    Json proof{{"schema","recovery-stop-result/1"},{"job_id",id},{"backend_boot",boot},{"state","failed"},{"generation",nullptr},{"error","cleanup_unconfirmed"}};
    try{
        if(database->role()==StateRole::Controller)require(controller_only_ready());
        else{
            (void)stop_transmission_by_user_request(false);
            require(transmitter_output_inactive_confirmed()&&!native_occurrence_active()&&!current_test_tone_status().owned);
        }
        proof["state"]="held";proof["generation"]=std::filesystem::path(database->directory()).filename().string().substr(11);proof["error"]=nullptr;
    }catch(...){/* Cleanup uncertainty retains both durable holds and its proof. */}
    (void)Journal::publish_private_record(terminal,{},proof.dump());return {{"job_id",id},{"state",proof["state"]}};
}
}
