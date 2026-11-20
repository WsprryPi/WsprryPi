// SPDX-License-Identifier: MIT
#include "candidate_builder.hpp"
#include "paths.hpp"
#include "definition_adaptation.hpp"
#include "private_json.hpp"
#include "restore_hold.hpp"
#include "evidence.hpp"
#include "application_storage/generation_selection.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/companion_capture.hpp"
#include "application_storage/companion_files.hpp"
#include "application_storage/migration_source.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "application_storage/state_migration.hpp"
#include "config_handler.hpp"
#include "recurrence_authority.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/pi_managed_store.hpp"
#include "wtp_settings_json.hpp"
#include <cerrno>
#include <array>
#include <fcntl.h>
#include <openssl/evp.h>
#include <filesystem>
#include <limits>
#include <set>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;using Journal=PiManagementJournal;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
std::string read(const std::filesystem::path &path){const auto record=Journal::read_private_record(path.string());require(bool(record));return record->bytes;}
void directory(const std::filesystem::path &path){struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0700&&std::filesystem::canonical(path)==path);}
void optional_progress(const std::string &from,const std::string &to){
    const auto value=Journal::read_private_record(from+".recurrence-progress.json");if(value){require(value->bytes.size()<=16384);(void)Journal::publish_private_record(to+".recurrence-progress.json",{},value->bytes);}
}
Json internal_settings(const PersistentConfiguration &saved){
    auto value=initial_configuration_draft();for(const auto &[section,fields]:saved.configuration.items())value[section]=fields;
    value["Meta"]["debug_logging"]=saved.private_settings.debug_logging;value["Meta"]["Loop TX"]=saved.private_settings.startup_loop_tx;
    value["Meta"]["TX Iterations"]=saved.private_settings.startup_tx_iterations;
    value["Experimental"]["Allow Unqualified Frequency"]=saved.private_settings.allow_unqualified_frequency;
    value["Experimental"]["Allow Non-Amateur Frequency"]=saved.private_settings.allow_non_amateur_frequency;return value;
}
Json import_definitions(WtpCatalog &catalog,const std::string &source_bytes,bool moving){
    WtpCatalog source(CatalogStorage{[source_bytes]{return std::optional<std::string>(source_bytes);},[](const auto &,const auto &){throw LocalCommandRefused(LocalRefusal::InvalidRequest);}});
    const auto wanted=source.snapshot({}).first;auto unresolved=Json::array();
    for(const auto &row:wanted.at("profiles")){
        const auto settings=parse_wtp_settings(row.at("settings"),true);const auto current=catalog.snapshot({});
        const auto *match=recovery_definition_match(row,current.first.at("profiles"));
        const auto resolution=recovery_definition_resolution(row,current.first.at("profiles"),moving);
        // Engineering credential paths and unverified historical definitions
        // are retained in inactive source evidence. No missing key is replaced,
        // no old credential is reauthorized, and no carrier is substituted.
        if(resolution=="destination_connection_required"||resolution=="unverified_definition"){
            unresolved.push_back({{"source_id",row.at("id")},{"name",row.at("name")},{"reason",resolution}});continue;
        }
        auto adapted=settings;
        if(match){
            const auto local=parse_wtp_settings(match->at("settings"),true);adapted.path=local.path;
            adapted.tls_ca=local.tls_ca;adapted.tls_certificate=local.tls_certificate;adapted.tls_key=local.tls_key;
        }else if(moving&&adapted.transport=="usb"){
            adapted.path="/dev/wsprrypi-unresolved-"+row.at("id").get<std::string>();
            unresolved.push_back({{"source_id",row.at("id")},{"name",row.at("name")},{"reason",resolution}});
        }
        Json request{{"operation",match?"edit":"add"},{"name",row.at("name")},{"settings",management_profile_settings_json(adapted)},{"consent_plain",true}};
        if(match)request["id"]=match->at("id");
        else{request["method"]=row.at("method");request["discovery_id"]=row.at("discovery_id");}
        (void)catalog.mutate(request,current.second,{});
    }return unresolved;
}
}
Json RecoveryCandidateBuilder::build(const std::string &id){
#ifdef __linux__
    require(::geteuid()==0||!recovery_paths().software_backend_command.empty());
#endif
    require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);
    const auto paths=recovery_paths();const auto root=std::filesystem::path(paths.root),jobs=root/"jobs",job=jobs/id;directory(root);directory(jobs);directory(job);
    const auto active_job=parse_recovery_json(read(job/"job.json"),1048576,16),preview=parse_recovery_json(read(job/"preview.json"),65536,16);
    const auto identity=wtp_pi_device_id();require(active_job.value("schema",Json(nullptr))=="recovery-job/1"&&active_job.value("job_id",Json(nullptr))==id&&
        active_job.value("identity",Json(nullptr))==identity&&active_job.value("kind",Json(nullptr))=="restore"&&active_job.value("state",Json(nullptr))=="applying"&&
        preview.value("job_id",Json(nullptr))==id&&preview.value("destination_identity",Json(nullptr))==identity&&
        (preview.value("destination_role",Json(nullptr))=="pi"||preview.value("destination_role",Json(nullptr))=="controller"));
    const auto role=preview["destination_role"]=="pi"?StateRole::Pi:StateRole::Controller;
    // Opening retains the application's root flock for this entire offline
    // operation. A live backend, pending selector preparation, or missing state
    // refuses before any candidate creation and never selects a fallback DB.
    auto destination=open_application_state(paths.application_root,identity,role);ApplicationStateSelection selection(destination);
    const auto hold=application_restore_hold_view();require(restore_maintenance_active()&&local_restore_output_held()&&restored_remote_authority_held()&&hold["job_id"]==id);
    const auto stopped=parse_recovery_json(read(job/"stop-result.json"),8192);require(stopped.value("schema",Json(nullptr))=="recovery-stop-result/1"&&
        stopped.value("job_id",Json(nullptr))==id&&stopped.value("state",Json(nullptr))=="held"&&stopped.value("backend_boot",Json(nullptr))==preview["backend_boot"]);
    const auto saved_bytes=read(job/"proposed-settings.json");require(preview.contains("settings_sha256")&&Journal::digest(saved_bytes)==preview["settings_sha256"].get<std::string>());
    const auto saved=decode_persistent_configuration(saved_bytes,role);const auto generation_name=std::filesystem::path(destination->directory()).filename().string();
    require(generation_name.starts_with("generation-"));const auto previous=generation_name.substr(11);const auto number=std::stoull(previous);require(number<std::numeric_limits<std::uint64_t>::max());
    const auto generation=std::to_string(number+1);require(!std::filesystem::exists(std::filesystem::path(paths.application_root)/("generation-"+generation)));
    const auto working=job/"candidate-working",candidate=job/"candidate";require(!std::filesystem::exists(working)&&!std::filesystem::exists(candidate));
    prepare_companion_directory(working.string());prepare_companion_directory(candidate.string());
    WtpCatalog destination_catalog(sqlite_catalog_storage(destination));
    const auto companions=capture_application_companions(destination->directory()+"/configuration",identity,destination_catalog);
    destination->snapshot_to(working.string());
    for(const auto &file:companions){if(file.kind==CompanionKind::TargetManagement)prepare_companion_directory((working/"configuration.management").string());
        copy_companion_file(companion_source_path(destination->directory()+"/configuration",file),(working/companion_relative_path(file)).string(),file,false);}
    optional_progress(destination->directory()+"/configuration",(working/"configuration").string());
    auto staged=std::make_shared<SqliteStateStore>(working.string(),identity,role,StateOpen::Existing);
    Json unresolved;
    {
        WtpCatalog catalog(sqlite_catalog_storage(staged));
        const auto source_catalog=read(job/"source-records/state-catalog.json");unresolved=import_definitions(catalog,source_catalog,preview["purpose"]=="move");
        if(role==StateRole::Pi){
            const auto before=staged->snapshot();
            if(!before.records.contains(StateRecord::ManagedSettings)){
                const auto raw=staged->record(StateRecord::RecurrenceAuthority);require(raw.bytes.has_value());const auto authority=parse_recovery_json(*raw.bytes,1048576,16);
                if(authority.value("schema",Json(nullptr))=="pi-recurrence-bootstrap/1"){
                    require(authority.contains("source_binding")&&authority["source_binding"].is_string());
                    const auto bootstrap=parse_recurrence_storage_record(*raw.bytes,identity,authority["source_binding"].get<std::string>());
                    (void)staged->commit(before.revision,{{StateRecord::RecurrenceAuthority,recurrence_bootstrap_record(identity,Journal::digest((working/"configuration").string()),bootstrap["installation_id"].get<std::string>()).dump()}});
                }
            }
            PiManagedStore owner(staged,catalog.pi_management_anchor(identity,{}));owner.bind_recurrence_storage({},true);owner.recover();
            Json scheduling=nullptr;const auto source_record=Journal::read_private_record((job/"source-records/state-managed-settings.json").string());
            if(source_record){const auto source=parse_recovery_json(source_record->bytes,1048576,32);require(source.contains("scheduling"));scheduling=source["scheduling"];}
            owner.restore_configuration(internal_settings(saved),saved_bytes,scheduling,id);
        }else{
            const auto before=staged->snapshot();(void)staged->commit(before.revision,{{StateRecord::Configuration,saved_bytes}});
        }
    }
    validate_application_storage(staged);require(staged->record(StateRecord::Configuration).bytes==std::optional<std::string>(saved_bytes));
    WtpCatalog final_catalog(sqlite_catalog_storage(staged));const auto final_companions=capture_application_companions((working/"configuration").string(),identity,final_catalog);
    staged->snapshot_to(candidate.string());
    for(const auto &file:final_companions){if(file.kind==CompanionKind::TargetManagement)prepare_companion_directory((candidate/"configuration.management").string());
        copy_companion_file(companion_source_path((working/"configuration").string(),file),(candidate/companion_relative_path(file)).string(),file,false);}
    optional_progress((working/"configuration").string(),(candidate/"configuration").string());
    const Json proof{{"schema","recovery-candidate-built/1"},{"job_id",id},{"identity",identity},{"role",preview["destination_role"]},
        {"previous_generation",previous},{"generation",generation},{"settings_sha256",preview["settings_sha256"]},{"archive_sha256",preview["archive_sha256"]},
        {"preview_digest",preview["digest"]},{"unresolved_definitions",unresolved}};
    (void)Journal::publish_private_record((job/"candidate-built.json").string(),{},proof.dump());return {{"job_id",id},{"state","candidate_built"}};
}
Json RecoveryCandidateBuilder::seal(const std::string &id){
#ifdef __linux__
    require(::geteuid()==0||!recovery_paths().software_backend_command.empty());
#endif
    require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);
    const auto paths=recovery_paths();const auto root=std::filesystem::path(paths.root),jobs=root/"jobs",job=jobs/id,candidate=job/"candidate";
    directory(root);directory(jobs);directory(job);directory(candidate);
    const auto built=parse_recovery_json(read(job/"candidate-built.json"),65536,16),preview=parse_recovery_json(read(job/"preview.json"),65536,16);
    const auto active=parse_recovery_json(read(job/"job.json"),1048576,16);const auto identity=wtp_pi_device_id();
    require(active.value("schema",Json(nullptr))=="recovery-job/1"&&active.value("job_id",Json(nullptr))==id&&active.value("state",Json(nullptr))=="applying"&&
        active.value("kind",Json(nullptr))=="restore"&&active.value("identity",Json(nullptr))==identity&&built.value("schema",Json(nullptr))=="recovery-candidate-built/1"&&
        built.value("job_id",Json(nullptr))==id&&built.value("identity",Json(nullptr))==identity&&built.value("preview_digest",Json(nullptr))==preview["digest"]&&
        (built.value("role",Json(nullptr))=="pi"||built.value("role",Json(nullptr))=="controller"));
    const auto role=built["role"]=="pi"?StateRole::Pi:StateRole::Controller;
    auto destination=open_application_state(paths.application_root,identity,role);ApplicationStateSelection selected(destination);
    require(restore_maintenance_active()&&local_restore_output_held()&&restored_remote_authority_held()&&application_restore_hold_view()["job_id"]==id);
    const auto previous=std::filesystem::path(destination->directory()).filename().string().substr(11);
    require(built["previous_generation"]==previous&&built["generation"].is_string()&&std::stoull(built["generation"].get<std::string>())==std::stoull(previous)+1);
    const auto preparation=parse_recovery_json(read(job/"hold-prepared.json"),65536,16);
    auto selection=parse_generation_selection(read(std::filesystem::path(paths.application_root)/"authority.json"),identity,role);
    auto hold=parse_restore_hold(read(std::filesystem::path(paths.application_root)/"restore-hold.json"),identity,role,previous);
    require(selection==preparation["selection"]&&hold==preparation["hold"]&&selection["restore_job"]==id&&hold["job_id"]==id&&hold["stage"]=="maintenance");
    std::map<std::string,std::string> files{{"state.sqlite3",(candidate/"state.sqlite3").string()}};
    std::set<std::string> history_keep;
    {
        auto database=std::make_shared<SqliteStateStore>(candidate.string(),identity,role,StateOpen::Existing);
        validate_application_storage(database);const auto configuration=database->record(StateRecord::Configuration);require(configuration.bytes.has_value()&&
            Journal::digest(*configuration.bytes)==built["settings_sha256"].get<std::string>()&&configuration.bytes==std::optional<std::string>(read(job/"proposed-settings.json")));
        WtpCatalog catalog(sqlite_catalog_storage(database));
        history_keep=recovery_history_dependencies(catalog.snapshot({}).first.at("profiles"),paths.application_root);
        for(const auto &file:capture_application_companions((candidate/"configuration").string(),identity,catalog))files[companion_relative_path(file)]=companion_source_path((candidate/"configuration").string(),file);
        if(const auto progress=Journal::read_private_record((candidate/"configuration.recurrence-progress.json").string())){
            require(role==StateRole::Pi&&progress->bytes.size()<=16384);files["configuration.recurrence-progress.json"]=(candidate/"configuration.recurrence-progress.json").string();
        }
        for(const auto &[logical,path]:capture_recovery_evidence(candidate.string()))files[logical.substr(std::string("application/").size())]=path;
    }
    // Some SQLite builds retain an empty WAL and its reconstruction index
    // after close. They carry no snapshot data. Reacquire the candidate's owner
    // lock and remove only these verified empty-WAL sidecars before sealing the
    // closed logical inventory; a nonempty WAL is never discarded.
    const int candidate_lock=::open(candidate.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(candidate_lock>=0);
    struct CandidateLock {int value;~CandidateLock(){::close(value);}} lock{candidate_lock};require(!::flock(candidate_lock,LOCK_EX|LOCK_NB));
    const auto wal=candidate/"state.sqlite3-wal",shm=candidate/"state.sqlite3-shm";struct stat wal_info{},shm_info{};
    const bool has_wal=!::lstat(wal.c_str(),&wal_info);require(has_wal||errno==ENOENT);
    if(has_wal)require(S_ISREG(wal_info.st_mode)&&wal_info.st_uid==::geteuid()&&(wal_info.st_mode&0777)==0600&&wal_info.st_nlink==1&&wal_info.st_size==0);
    const bool has_shm=!::lstat(shm.c_str(),&shm_info);require(has_shm||errno==ENOENT);
    if(has_shm)require(S_ISREG(shm_info.st_mode)&&shm_info.st_uid==::geteuid()&&(shm_info.st_mode&0777)==0600&&shm_info.st_nlink==1&&shm_info.st_size>=0&&shm_info.st_size<=1048576);
    if(has_wal)require(!::unlink(wal.c_str()));
    if(has_shm)require(!::unlink(shm.c_str()));
    require(!::fsync(candidate_lock));
    const auto progress_lock=candidate/"configuration.pi-recurrence-progress";
    if(std::filesystem::exists(progress_lock)){directory(progress_lock);require(std::filesystem::is_empty(progress_lock));require(::rmdir(progress_lock.c_str())==0);}
    std::set<std::filesystem::path> directories;
    for(const auto &[name,path]:files){(void)path;auto parent=std::filesystem::path(name).parent_path();while(!parent.empty()){directories.insert(parent);parent=parent.parent_path();}}
    std::set<std::string> actual;
    for(const auto &file:std::filesystem::recursive_directory_iterator(candidate)){
        const auto relative=std::filesystem::relative(file.path(),candidate);require(!file.is_symlink());
        if(file.is_directory()){require(directories.contains(relative));directory(file.path());}
        else{require(files.contains(relative.generic_string()));actual.insert(relative.generic_string());}
    }
    require(actual.size()==files.size()&&files.size()<=1024);auto inventory=Json::array();std::uint64_t total{};
    for(const auto &[name,path]:files){
        const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);require(fd>=0);struct Close{int value;~Close(){::close(value);}}close{fd};
        struct stat before{},after{};require(!::fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_uid==::geteuid()&&(before.st_mode&0777)==0600&&before.st_nlink==1&&before.st_size>=0);
        const auto size=static_cast<std::uint64_t>(before.st_size);total+=size;require(total<=2ULL*1024*1024*1024-2*1024*1024);
        std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(digest&&EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)==1);
        std::array<char,65536> buffer{};std::uint64_t copied{};
        while(true){const auto count=::read(fd,buffer.data(),buffer.size());if(count<0&&errno==EINTR)continue;require(count>=0);if(!count)break;copied+=count;require(copied<=size&&EVP_DigestUpdate(digest.get(),buffer.data(),count)==1);}
        require(copied==size&&!::fstat(fd,&after)&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&!::fsync(fd));
#ifdef __APPLE__
        require(before.st_mtimespec.tv_sec==after.st_mtimespec.tv_sec&&before.st_mtimespec.tv_nsec==after.st_mtimespec.tv_nsec&&before.st_ctimespec.tv_sec==after.st_ctimespec.tv_sec&&before.st_ctimespec.tv_nsec==after.st_ctimespec.tv_nsec);
#else
        require(before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec&&before.st_ctim.tv_sec==after.st_ctim.tv_sec&&before.st_ctim.tv_nsec==after.st_ctim.tv_nsec);
#endif
        std::array<unsigned char,32> hash{};unsigned length{};require(EVP_DigestFinal_ex(digest.get(),hash.data(),&length)==1&&length==32);std::string text;
        for(const auto byte:hash){text+="0123456789abcdef"[byte>>4];text+="0123456789abcdef"[byte&15];}
        inventory.push_back({{"name",name},{"size",size},{"sha256",text}});
    }
    const auto generation=built["generation"].get<std::string>();selection["generation"]=generation;hold["generation"]=generation;hold["stage"]="review";
    const auto revision=std::stoull(hold["revision"].get<std::string>());require(revision<std::numeric_limits<std::uint64_t>::max());hold["revision"]=std::to_string(revision+1);
    const Json proof{{"schema","recovery-generation-prepared/1"},{"job_id",id},{"identity",identity},{"role",built["role"]},
        {"archive_sha256",preview["archive_sha256"]},{"previous_generation",previous},{"generation",generation},{"selection",selection},{"hold",hold},
        {"files",inventory},{"history_keep",history_keep},{"destination_digest",preview["destination_digest"]},{"backend_boot",preview["backend_boot"]}};
    (void)Journal::publish_private_record((job/"prepared.json").string(),{},proof.dump());return {{"job_id",id},{"state","candidate_prepared"}};
}

}
