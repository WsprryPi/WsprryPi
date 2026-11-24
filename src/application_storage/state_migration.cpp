// SPDX-License-Identifier: MIT
#include "state_migration.hpp"
#include "generation_selection.hpp"
#include "persistent_configuration.hpp"
#include "owned_state_directory.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/browser_api.hpp"
#include <cerrno>
#include <filesystem>
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "windows_generation_admission.hpp"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <set>
#include <openssl/rand.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif

namespace wsprrypi {
namespace {
using Json=nlohmann::json;
using Journal=PiManagementJournal;
void require(bool condition){if(!condition)throw StateStorageError(StateStorageError::Code::Unavailable);}
bool hash(const std::string &value){return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string role_name(StateRole role){require(role==StateRole::Pi||role==StateRole::Controller);return role==StateRole::Pi?"pi":"controller";}
const char *name(StateRecord kind){
    switch(kind){
        case StateRecord::Configuration:return "configuration";
        case StateRecord::ManagedSettings:return "managed-settings";
        case StateRecord::Operations:return "operations";
        case StateRecord::Catalog:return "catalog";
        case StateRecord::RecurrenceAuthority:return "recurrence-authority";
        case StateRecord::RecurrenceInitialization:return "recurrence-initialization";
        case StateRecord::Initialization:return "initialization";
        case StateRecord::RecurrenceRecovery:return "recurrence-recovery";
    }
    throw StateStorageError(StateStorageError::Code::InvalidInput);
}
#ifdef _WIN32
#include "state_migration_windows.hpp"
#else
std::filesystem::path disk_path(const std::string &text){return std::filesystem::path(text);}
std::string path_text(const std::filesystem::path &path){return path.string();}
bool absolute_path(const std::string &text){return std::filesystem::path(text).is_absolute()&&text.find('\0')==std::string::npos;}
bool present(const std::string &text){struct stat info{};if(!::lstat(text.c_str(),&info))return true;require(errno==ENOENT);return false;}
std::string normalized_root(const std::string &text){const auto path=disk_path(text);return path_text(std::filesystem::canonical(path.parent_path())/path.filename());}
void make_directory(const std::string &path){require(!::mkdir(path.c_str(),0700));}
struct Admission {
    int fd{-1};std::string path;
    explicit Admission(const std::string &input){
        require(std::filesystem::path(input).is_absolute()&&input.find('\0')==std::string::npos);
        struct stat before{};require(!::lstat(input.c_str(),&before)&&S_ISDIR(before.st_mode)&&before.st_uid==geteuid()&&(before.st_mode&0777)==0700);
        path=std::filesystem::canonical(input).string();
        fd=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0)throw StateStorageError(StateStorageError::Code::Unavailable);
        struct stat opened{};
        if(::fstat(fd,&opened)||before.st_dev!=opened.st_dev||before.st_ino!=opened.st_ino||::flock(fd,LOCK_EX|LOCK_NB)) {
            ::close(fd);fd=-1;throw StateStorageError(StateStorageError::Code::Unavailable);
        }
    }
    ~Admission(){if(fd>=0)::close(fd);}
    void sync(){require(!::fsync(fd));}
};
void sync_parent(const std::string &path){
    const auto parent=std::filesystem::path(path).parent_path().string();
    const int fd=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0);
    const auto result=::fsync(fd),closed=::close(fd);require(!result&&!closed);
}
bool move_no_replace(const std::string &from,const std::string &to){
#ifdef __APPLE__
    return !::renamex_np(from.c_str(),to.c_str(),RENAME_EXCL);
#elif defined(__linux__)
    return !::syscall(SYS_renameat2,AT_FDCWD,from.c_str(),AT_FDCWD,to.c_str(),RENAME_NOREPLACE);
#else
    (void)from;(void)to;return false;
#endif
}
#endif
Json capture_description(const StateMigrationCapture &capture,StateRole role){
    require(hash(capture.source_revision)&&absolute_path(capture.source_namespace)&&capture.original_records.contains(StateRecord::Configuration));
    Json records=Json::object(),originals=Json::object();
    for(const auto &record:capture.records) {
        require(record.kind!=StateRecord::Initialization&&record.bytes&&record.bytes->size()<=SqliteStateStore::maximum_record_bytes&&
            !records.contains(name(record.kind)));
        records[name(record.kind)]=Journal::digest(*record.bytes);
        if(record.kind==StateRecord::Configuration)(void)decode_persistent_configuration(*record.bytes,role);
    }
    require(records.contains("configuration")&&records.contains("catalog")&&capture.records.size()<=7);
    if(role==StateRole::Pi){
        require(records.contains("recurrence-authority"));
        require((records.contains("managed-settings")&&records.contains("operations"))||
            (!records.contains("managed-settings")&&!records.contains("operations")&&!records.contains("recurrence-initialization")&&!records.contains("recurrence-recovery")&&!capture.progress_bytes));
    }
    else require(!records.contains("managed-settings")&&!records.contains("recurrence-authority")&&!records.contains("recurrence-initialization")&&!records.contains("recurrence-recovery")&&!capture.progress_bytes);
    for(const auto &[kind,bytes]:capture.original_records) {
        require(kind!=StateRecord::Initialization&&bytes.size()<=SqliteStateStore::maximum_record_bytes);
        originals[name(kind)]=Journal::digest(bytes);
    }
    if(capture.progress_bytes)require(capture.progress_bytes->size()<=SqliteStateStore::maximum_record_bytes);
    return {{"records",records},{"originals",originals},{"progress",capture.progress_bytes?Json(Journal::digest(*capture.progress_bytes)):Json(nullptr)},
        {"companions",companion_manifest(capture.companions)}};
}
Json marker(const std::string &bytes,const std::string &identity,StateRole role){
    const auto value=strict_browser_json(bytes);
    const std::set<std::string> keys{"schema","identity","role","state","generation","source","source_revision","images"};
    require(value.is_object()&&value.size()==keys.size());
    for(const auto &key:keys)require(value.contains(key));
    require(value["schema"]=="application-state/1"&&value["identity"]==identity&&value["role"]==role_name(role)&&
        (value["state"]=="preparing"||value["state"]=="retiring"||value["state"]=="ready")&&value["generation"]=="1"&&
        value["source"].is_string()&&value["source_revision"].is_string()&&hash(value["source_revision"].get<std::string>())&&
        value["images"].is_object());
    require(value["images"].size()==4&&value["images"].contains("records")&&value["images"]["records"].is_object()&&
        value["images"].contains("originals")&&value["images"]["originals"].is_object()&&value["images"].contains("progress")&&value["images"].contains("companions"));
    (void)parse_companion_manifest(value["images"]["companions"]);
    return value;
}
Json initialization(const Json &selection){
    return {{"schema","state-initialization/1"},{"identity",selection["identity"]},{"role",selection["role"]},
        {"origin",selection["source"]=="fresh"?"fresh":"migration"},
        {"source_namespace",selection["source"]},
        {"generation",selection["generation"]},{"source_revision",selection["source_revision"]},
        {"images_digest",Journal::digest(selection["images"].dump())}};
}
std::shared_ptr<SqliteStateStore> selected_database(const std::shared_ptr<Admission> &admission,
    const Json &selection,const std::string &identity,StateRole role){
    auto database=std::shared_ptr<SqliteStateStore>(new SqliteStateStore(admission->path+"/generation-1",identity,role,StateOpen::Existing),
        [admission](SqliteStateStore *value){delete value;});
    require(database->record(StateRecord::Initialization).bytes==initialization(selection).dump());
    return database;
}
void backup(const std::string &path,const std::string &bytes){
    const auto existing=Journal::read_private_record(path);
    if(existing){require(existing->bytes==bytes);return;}
    (void)Journal::publish_private_record(path,{},bytes);
}
void verify_candidate(const std::shared_ptr<SqliteStateStore> &database,const StateMigrationCapture &image,const Json &selection){
    auto expected=std::map<StateRecord,std::string>{};
    for(const auto &record:image.records){require(record.bytes.has_value());expected.emplace(record.kind,*record.bytes);}
    expected.emplace(StateRecord::Initialization,initialization(selection).dump());
    const auto saved=database->snapshot();
    require(saved.revision=="1"&&saved.records==expected);
    for(const auto &[kind,revision]:saved.record_revisions){(void)kind;require(revision=="1");}
    const auto progress=Journal::read_private_record(database->directory()+"/configuration.recurrence-progress.json");
    require(bool(progress)==bool(image.progress_bytes));
    if(progress)require(progress->bytes==*image.progress_bytes);
    for(const auto &file:image.companions)verify_companion_file(database->directory()+"/"+companion_relative_path(file),file);
}
void clear_unpublished_candidate(const std::string &path,const std::vector<CompanionFile> &companions={}){
    if(!present(path))return;
#ifndef _WIN32
    auto admission=std::make_shared<Admission>(path);
#endif
    std::set<std::string> files{"state.sqlite3","state.sqlite3-wal","state.sqlite3-shm","state.sqlite3-journal","configuration.recurrence-progress.json"};
    std::set<std::string> directories;
    for(const auto &file:companions){
        const auto relative=companion_relative_path(file);files.insert(relative);files.insert(relative+".writing");
        if(file.kind==CompanionKind::TargetManagement)directories.insert("configuration.management");
    }
#ifdef _WIN32
    remove_candidate(path,files,directories);
#else
    std::vector<std::filesystem::path> remove_files,remove_directories;
    // Validate the complete inventory before removing any candidate file.
    for(const auto &entry:std::filesystem::recursive_directory_iterator(path)) {
        const auto relative=entry.path().lexically_relative(path).string();
        struct stat file{};require(!::lstat(entry.path().c_str(),&file)&&file.st_uid==geteuid());
        if(S_ISDIR(file.st_mode)){
            require(directories.contains(relative)&&(file.st_mode&0777)==0700);remove_directories.push_back(entry.path());
        }else{
            require(files.contains(relative)&&S_ISREG(file.st_mode)&&file.st_nlink>=1&&file.st_nlink<=2&&
                (file.st_mode&0777)==0600&&file.st_size>=0&&file.st_size<=64*1048576);
            remove_files.push_back(entry.path());
        }
    }
    for(const auto &file:remove_files)require(!::unlink(file.c_str()));
    for(auto directory=remove_directories.rbegin();directory!=remove_directories.rend();++directory)require(!::rmdir(directory->c_str()));
    admission->sync();require(!::rmdir(path.c_str()));sync_parent(path);
#endif
}
void retire_source(const StateMigrationCapture &capture){
    const auto &original=capture.original_records.at(StateRecord::Configuration);
    const auto backup_path=capture.source_namespace+".sqlite-migrated";
    const bool exists=present(backup_path);
    const auto retired=exists?std::optional<Journal::Observation>(Journal::read_ini_input(backup_path)):std::nullopt;
    if(!present(capture.source_namespace)){require(retired&&retired->bytes==original);return;}
    require(!retired&&Journal::read_ini_input(capture.source_namespace).bytes==original);
    require(move_no_replace(capture.source_namespace,backup_path));sync_parent(backup_path);
    require(Journal::read_ini_input(backup_path).bytes==original);
}
}

nlohmann::json parse_initial_application_selection(const std::string &bytes,const std::string &identity,StateRole role){return marker(bytes,identity,role);}
nlohmann::json initial_application_selection_provenance(const nlohmann::json &selection){return initialization(selection);}

std::shared_ptr<SqliteStateStore> open_application_state(const std::string &root,const std::string &identity,StateRole role){
    const auto admission=std::make_shared<Admission>(root);
    // A confirmed restore must finish its held-selector preparation through the
    // independent maintenance owner before any normal backend owner can open.
    require(!Journal::read_private_record(admission->path+"/restore-preparation.json"));
    const auto saved=Journal::read_private_record(admission->path+"/authority.json");require(saved.has_value());
    const auto document=strict_browser_json(saved->bytes);
    if(document.is_object()&&document.value("schema",Json(nullptr))=="application-generation/1"){
        const auto selection=parse_generation_selection(saved->bytes,identity,role);
        const auto generation=selection["generation"].get<std::string>();
        const auto hold=Journal::read_private_record(admission->path+"/restore-hold.json");require(hold.has_value());
        require(parse_restore_hold(hold->bytes,identity,role,generation)["job_id"]==selection["restore_job"]);
        auto database=std::shared_ptr<SqliteStateStore>(new SqliteStateStore(admission->path+"/generation-"+generation,identity,role,StateOpen::Existing),
            [admission](SqliteStateStore *value){delete value;});
        require(database->record(StateRecord::Initialization).bytes==selection["initialization"].dump());
        return database;
    }
    const auto selection=marker(saved->bytes,identity,role);require(selection["state"]=="ready");
    if(const auto hold=Journal::read_private_record(admission->path+"/restore-hold.json"))
        (void)parse_restore_hold(hold->bytes,identity,role,"1");
    return selected_database(admission,selection,identity,role);
}

std::shared_ptr<SqliteStateStore> migrate_application_state(const std::string &root,const std::string &identity,StateRole role,
    const std::string &source_namespace,
    const std::function<StateMigrationCapture()> &capture,
    const std::function<void(std::shared_ptr<SqliteStateStore>)> &validate,
    const std::function<void(StateMigrationPoint)> &checkpoint){
    require(bool(capture)&&bool(validate));
    require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos&&
        absolute_path(root));
    // The trusted invoker supplies the source namespace separately. A marker
    // read from disk never chooses a path on which to perform retirement.
    require(absolute_path(source_namespace));
    std::optional<StateMigrationCapture> captured;
    if(!present(root)) {
        captured=capture();
        require(captured->source_namespace==source_namespace);
        const auto description=capture_description(*captured,role);
        const Json initial{{"schema","application-state/1"},{"identity",identity},{"role",role_name(role)},
            {"state","preparing"},{"generation","1"},{"source",source_namespace},
            {"source_revision",captured->source_revision},{"images",description}};
        (void)prepare_owned_state_directory(root,[&]{return initial.dump();},
            [&](const auto &bytes){require(marker(bytes,identity,role)==initial);});
    }
    const auto admission=std::make_shared<Admission>(root);
    require(!Journal::read_private_record(admission->path+"/restore-preparation.json"));
    const auto marker_path=admission->path+"/authority.json";
    auto observed=Journal::read_private_record(marker_path);require(observed.has_value());
    auto selection=marker(observed->bytes,identity,role);
    require(selection["source"]==source_namespace);
    if(selection["state"]=="ready")return selected_database(admission,selection,identity,role);
    StateMigrationCapture image;
    if(selection["state"]=="retiring") {
        // Source retirement may already have succeeded. Reconstruct the exact
        // admitted image from private backups and the unopened-to-runtime DB;
        // do not require or reimport an active INI during this phase.
        auto database=selected_database(admission,selection,identity,role);
        const auto saved=database->snapshot();
        for(const auto &[kind,bytes]:saved.records)if(kind!=StateRecord::Initialization)image.records.push_back({kind,bytes});
        image.source_namespace=source_namespace;image.source_revision=selection["source_revision"].get<std::string>();
        image.companions=parse_companion_manifest(selection["images"].at("companions"));
        for(const auto kind:{StateRecord::Configuration,StateRecord::ManagedSettings,StateRecord::Operations,
                StateRecord::Catalog,StateRecord::RecurrenceAuthority,StateRecord::RecurrenceInitialization,StateRecord::RecurrenceRecovery})
            if(selection["images"].at("originals").contains(name(kind))) {
                const auto original=Journal::read_private_record(admission->path+"/source-"+name(kind));
                require(original.has_value());image.original_records[kind]=original->bytes;
            }
        if(!selection["images"].at("progress").is_null()) {
            const auto progress=Journal::read_private_record(admission->path+"/source-execution-progress");
            require(progress.has_value());image.progress_bytes=progress->bytes;
        }
    } else image=captured?*captured:capture();
    const auto description=capture_description(image,role);
    require(image.source_namespace==source_namespace&&selection["source_revision"]==image.source_revision&&selection["images"]==description);
    const auto notify=[&](StateMigrationPoint point){if(checkpoint)checkpoint(point);};
    if(selection["state"]=="preparing") {
        for(const auto &[kind,bytes]:image.original_records)backup(admission->path+"/source-"+name(kind),bytes);
        if(image.progress_bytes)backup(admission->path+"/source-execution-progress",*image.progress_bytes);
        if(!image.companions.empty())prepare_companion_directory(admission->path+"/source-companions");
        for(const auto &file:image.companions)
            copy_companion_file(companion_source_path(source_namespace,file),admission->path+"/source-companions/"+companion_backup_name(file),file,true);
        notify(StateMigrationPoint::SourceBackedUp);
        const auto staging=admission->path+"/generation-1.prepare",final=admission->path+"/generation-1";
        if(!present(final)) {
            clear_unpublished_candidate(staging,image.companions);make_directory(staging);
            {
                auto database=std::make_shared<SqliteStateStore>(staging,identity,role,StateOpen::CreateNew);
                auto records=image.records;records.push_back({StateRecord::Initialization,initialization(selection).dump()});
                database->commit("0",records);
                if(image.progress_bytes)backup(staging+"/configuration.recurrence-progress.json",*image.progress_bytes);
                for(const auto &file:image.companions){
                    const auto destination=staging+"/"+companion_relative_path(file);
                    if(file.kind==CompanionKind::TargetManagement)prepare_companion_directory(staging+"/configuration.management");
                    copy_companion_file(admission->path+"/source-companions/"+companion_backup_name(file),destination,file,false);
                }
                verify_candidate(database,image,selection);validate(database);verify_candidate(database,image,selection);
                notify(StateMigrationPoint::DatabaseCommitted);
                const auto current=capture();
                require(current.source_namespace==source_namespace&&capture_description(current,role)==description&&current.source_revision==image.source_revision);
            }
            require(move_no_replace(staging,final));admission->sync();
        }
        auto database=selected_database(admission,selection,identity,role);
        verify_candidate(database,image,selection);validate(database);verify_candidate(database,image,selection);
        const auto current=capture();
        require(current.source_namespace==source_namespace&&capture_description(current,role)==description&&current.source_revision==image.source_revision);
        notify(StateMigrationPoint::DatabasePublished);
        selection["state"]="retiring";
        observed=Journal::publish_private_record(marker_path,observed,selection.dump());
    }
    auto database=selected_database(admission,selection,identity,role);
    verify_candidate(database,image,selection);validate(database);verify_candidate(database,image,selection);
    retire_source(image);notify(StateMigrationPoint::SourceRetired);
    selection["state"]="ready";
    (void)Journal::publish_private_record(marker_path,observed,selection.dump());
    notify(StateMigrationPoint::Initialized);
    return database;
}

std::shared_ptr<SqliteStateStore> initialize_application_state(const std::string &root,const std::string &identity,StateRole role,
    const std::function<std::vector<StateChange>(const std::string &,const std::string &)> &seed,
    const std::function<void(std::shared_ptr<SqliteStateStore>)> &validate,
    const std::function<void(StateMigrationPoint)> &checkpoint){
    require(bool(seed)&&bool(validate)&&identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos&&
        absolute_path(root));
    const auto directory=normalized_root(root);
    const auto make_image=[&](const std::string &nonce){
        require(hash(nonce));StateMigrationCapture image;
        image.source_namespace=directory+"/generation-1/configuration";
        image.source_revision=nonce;image.records=seed(image.source_namespace,nonce.substr(0,32));
        std::set<StateRecord> kinds;
        Json digests=Json::object();
        for(const auto &record:image.records) {
            require(record.kind!=StateRecord::Initialization&&record.bytes&&kinds.insert(record.kind).second&&
                record.bytes->size()<=SqliteStateStore::maximum_record_bytes);
            digests[name(record.kind)]=Journal::digest(*record.bytes);
            if(record.kind==StateRecord::Configuration)(void)decode_persistent_configuration(*record.bytes,role);
        }
        require(kinds.contains(StateRecord::Configuration)&&kinds.contains(StateRecord::Catalog)&&
            !kinds.contains(StateRecord::ManagedSettings)&&!kinds.contains(StateRecord::Operations)&&!kinds.contains(StateRecord::RecurrenceInitialization)&&!kinds.contains(StateRecord::RecurrenceRecovery));
        require(role==StateRole::Pi?kinds.contains(StateRecord::RecurrenceAuthority):!kinds.contains(StateRecord::RecurrenceAuthority));
        return std::pair{image,Json{{"records",digests},{"originals",Json::object()},{"progress",nullptr},{"companions",Json::array()}}};
    };
    if(!present(directory)) {
        unsigned char random[32];require(RAND_bytes(random,sizeof(random))==1);
        std::string nonce;for(auto byte:random){nonce+="0123456789abcdef"[byte>>4];nonce+="0123456789abcdef"[byte&15];}
        const auto [image,images]=make_image(nonce);(void)image;
        const Json first{{"schema","application-state/1"},{"identity",identity},{"role",role_name(role)},
            {"state","preparing"},{"generation","1"},{"source","fresh"},{"source_revision",nonce},{"images",images}};
        // A crash in the root preparation retains its nonce. The deterministic
        // seed is rebuilt below from that admitted record, never a new event.
        (void)prepare_owned_state_directory(directory,[&]{return first.dump();},[&](const auto &bytes){
            const auto value=marker(bytes,identity,role);require(value["source"]=="fresh"&&value["state"]=="preparing");
            require(value["images"]==make_image(value["source_revision"].template get<std::string>()).second);
        });
    }
    const auto admission=std::make_shared<Admission>(directory);
    require(!Journal::read_private_record(admission->path+"/restore-preparation.json"));
    const auto path=directory+"/authority.json";
    auto observed=Journal::read_private_record(path);require(observed.has_value());
    auto selection=marker(observed->bytes,identity,role);require(selection["source"]=="fresh");
    if(selection["state"]=="ready")return selected_database(admission,selection,identity,role);
    require(selection["state"]=="preparing");
    const auto [image,images]=make_image(selection["source_revision"].get<std::string>());
    require(selection["images"]==images);
    const auto staging=directory+"/generation-1.prepare",final=directory+"/generation-1";
    if(!present(final)) {
        clear_unpublished_candidate(staging);make_directory(staging);
        {
            auto database=std::make_shared<SqliteStateStore>(staging,identity,role,StateOpen::CreateNew);
            auto records=image.records;records.push_back({StateRecord::Initialization,initialization(selection).dump()});
            database->commit("0",records);verify_candidate(database,image,selection);
            if(checkpoint)checkpoint(StateMigrationPoint::DatabaseCommitted);
        }
        require(move_no_replace(staging,final));admission->sync();
    }
    auto database=selected_database(admission,selection,identity,role);
    verify_candidate(database,image,selection);validate(database);verify_candidate(database,image,selection);
    if(checkpoint)checkpoint(StateMigrationPoint::DatabasePublished);
    selection["state"]="ready";
    (void)Journal::publish_private_record(path,observed,selection.dump());
    if(checkpoint)checkpoint(StateMigrationPoint::Initialized);
    return database;
}
} // namespace wsprrypi
