// SPDX-License-Identifier: MIT
#include "staged_source.hpp"
#include "evidence.hpp"
#include "private_json.hpp"
#include "application_control/local_transport.hpp"
#include "application_storage/companion_files.hpp"
#include "application_storage/generation_selection.hpp"
#include "application_storage/migration_source.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "recurrence_authority.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/pi_pair_history.hpp"
#include "wtp_integration/pi_pair_history_record.hpp"
#include "wtp_integration/tls.hpp"
#include "wtp_settings_json.hpp"
#include <array>
#include <cerrno>
#include <filesystem>
#ifdef _WIN32
#include "windows_file_io.hpp"
#include "application_storage/windows_sqlite_owner_lock.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <map>
#include <openssl/evp.h>
#include <regex>
#include <set>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;using Journal=PiManagementJournal;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
bool hex(const Json &value,std::size_t count){return value.is_string()&&value.get_ref<const std::string &>().size()==count&&value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
void fields(const Json &value,std::initializer_list<const char *> keys){require(value.is_object()&&value.size()==keys.size());for(const auto *key:keys)require(value.contains(key));}
#ifdef _WIN32
std::filesystem::path disk_path(const std::string &path){return windows_file::path_from_utf8(path);}
std::string text(const std::filesystem::path &path){return recovery_windows::text(path);}
void directory(const std::filesystem::path &path){(void)recovery_windows::private_directory(path);}
#else
std::filesystem::path disk_path(const std::string &path){return path;}
std::string text(const std::filesystem::path &path){return path.string();}
void directory(const std::filesystem::path &path){struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0700&&std::filesystem::canonical(path)==path);}
#endif
std::uint64_t maximum(const std::string &name){
    if(const auto bound=recovery_evidence_member_maximum(name))return *bound;
    if(name=="application/state.sqlite3")return 64ULL*1024*1024;
    if(name=="application/authority.json"||name=="application/configuration.wtp-assignments.json"||name=="application/configuration.wtp-fleet-contexts.json"||
        name=="pairing/selector.json"||name=="pairing/authority.json"||name=="pairing/operations.json"||name=="pairing/peers.json"||
        name=="retirement/authority.json"||name=="retirement/established.json")return 1048576;
    if(name=="application/configuration.recurrence-progress.json")return 16384;
    if(name=="application/configuration.udp-logs.json"||name=="application/configuration.usb-logs.json")return 65536;
    static const std::regex management("application/configuration\\.management/[0-9a-f]{32}\\.json"),credential("credentials/[0-9a-f]{32}/(ca\\.crt|client\\.crt|client\\.key)");
    if(std::regex_match(name,management))return 16ULL*1024*1024;
    if(std::regex_match(name,credential))return 1048576;
    throw LocalCommandRefused(LocalRefusal::InvalidRequest);
}
#ifdef _WIN32
void copy(const std::filesystem::path &source,const std::filesystem::path &destination,std::uint64_t size,const std::string &expected){
    directory(source.parent_path());directory(destination.parent_path());recovery_windows::copy(source,destination,size,expected);
}
void duplicate(const std::filesystem::path &from,const std::filesystem::path &to){
    std::uint64_t size{};{recovery_windows::Input input(from,1048576);size=static_cast<std::uint64_t>(input.observed().standard.EndOfFile.QuadPart);}copy(from,to,size,{});
}
#else
struct Fd {int value{-1};~Fd(){if(value>=0)::close(value);}};
bool same(const struct stat &a,const struct stat &b){
#ifdef __APPLE__
    const bool time=a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
#else
    const bool time=a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
#endif
    return time&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_uid==b.st_uid&&a.st_mode==b.st_mode&&a.st_nlink==b.st_nlink;
}
void copy(const std::filesystem::path &source,const std::filesystem::path &destination,std::uint64_t size,const std::string &expected){
    directory(source.parent_path());directory(destination.parent_path());
    Fd input{::open(source.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)},output;
    struct stat before{},after{},named{};require(input.value>=0&&!::fstat(input.value,&before)&&S_ISREG(before.st_mode)&&before.st_uid==::geteuid()&&
        before.st_nlink==1&&(before.st_mode&0777)==0600&&before.st_size>=0&&static_cast<std::uint64_t>(before.st_size)==size);
    output.value=::open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(output.value>=0);
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(hash&&EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)==1);
    std::array<char,65536> buffer{};std::uint64_t total{};
    while(true){const auto count=::read(input.value,buffer.data(),buffer.size());if(count<0&&errno==EINTR)continue;require(count>=0);if(!count)break;
        total+=count;require(total<=size&&EVP_DigestUpdate(hash.get(),buffer.data(),count)==1);std::size_t offset{};
        while(offset<static_cast<std::size_t>(count)){const auto written=::write(output.value,buffer.data()+offset,count-offset);if(written<0&&errno==EINTR)continue;require(written>0);offset+=written;}
    }
    std::array<unsigned char,32> digest{};unsigned length{};require(EVP_DigestFinal_ex(hash.get(),digest.data(),&length)==1&&length==digest.size());std::string actual;
    for(const auto byte:digest){actual+="0123456789abcdef"[byte>>4];actual+="0123456789abcdef"[byte&15];}
    require(total==size&&(expected.empty()||actual==expected)&&!::fstat(input.value,&after)&&same(before,after)&&!::lstat(source.c_str(),&named)&&same(before,named)&&!::fsync(output.value));
}
void duplicate(const std::filesystem::path &from,const std::filesystem::path &to){struct stat info{};require(!::lstat(from.c_str(),&info)&&info.st_size>=0&&info.st_size<=1048576);copy(from,to,info.st_size,{});}
#endif
std::string read(const std::filesystem::path &path){const auto record=Journal::read_private_record(text(path));require(bool(record));return record->bytes;}
void source_initialization(const Json &authority,const Json &initial,const std::string &self,StateRole role){
    const auto name=role==StateRole::Pi?"pi":"controller";
    if(authority.value("schema",Json(nullptr))=="application-generation/1"){
        require(parse_generation_selection(authority.dump(),self,role)["initialization"]==initial);return;
    }
    const auto selected=parse_initial_application_selection(authority.dump(),self,role);
    require(selected["state"]=="ready"&&initial_application_selection_provenance(selected)==initial);
    (void)parse_generation_selection(Json{{"schema","application-generation/1"},{"identity",self},{"role",name},{"generation","1"},{"initialization",initial},{"restore_job",std::string(32,'0')}}.dump(),self,role);
}
}
RecoverySource validate_staged_recovery_source(const std::string &job_directory){
    const auto job=disk_path(job_directory);directory(job);directory(job/"decoded");
    const auto manifest=parse_recovery_json(read(job/"archive-manifest.json"),1048576);fields(manifest,{"schema","source","created_utc","files"});
    require(manifest["schema"]=="wsprrypi-recovery/1"&&manifest["created_utc"].is_number_unsigned()&&manifest["created_utc"]>0);
    const auto &metadata=manifest["source"];fields(metadata,{"identity","role","application_version","storage","platform"});
    require(hex(metadata["identity"],32)&&(metadata["role"]=="pi"||metadata["role"]=="controller")&&metadata["storage"]=="application-state/1"&&
        (metadata["platform"]=="linux"||metadata["platform"]=="darwin"||(metadata["platform"]=="windows"&&metadata["role"]=="controller"))&&metadata["application_version"].is_string()&&metadata["application_version"].get_ref<const std::string &>().size()<=128);
    require(manifest["files"].is_array()&&manifest["files"].size()>0&&manifest["files"].size()<=1024);
    const auto root=job/"validated";require(!std::filesystem::exists(root));prepare_companion_directory(text(root));
#ifdef _WIN32
    bool snapshot_owner_initialized=false;
#endif
    std::set<std::string> names;std::string previous;std::uint64_t total{};
    for(const auto &file:manifest["files"]){fields(file,{"name","size","sha256"});require(file["name"].is_string()&&file["size"].is_number_unsigned()&&hex(file["sha256"],64));
        const auto name=file["name"].get<std::string>();const auto size=file["size"].get<std::uint64_t>();require(name>previous&&names.insert(name).second&&size<=maximum(name));previous=name;total+=size;require(total<=2ULL*1024*1024*1024-2*1024*1024);
        const auto relative=std::filesystem::path(name);auto current=root;
        for(const auto &part:relative.parent_path()){current/=part;prepare_companion_directory(text(current));
#ifdef _WIN32
            if(current==root/"application"&&!snapshot_owner_initialized){
                // The archive carries logical state, not a host's lock marker.
                // Provision only this newly created, empty inactive snapshot;
                // established application state still requires its marker.
                WindowsSqliteOwnerLock::initialize_snapshot(current);snapshot_owner_initialized=true;
            }
#endif
        }
        copy(job/"decoded"/relative,root/relative,size,file["sha256"].get<std::string>());
    }
    require(names.contains("application/state.sqlite3")&&names.contains("application/authority.json"));
    const auto self=metadata["identity"].get<std::string>();const auto role=metadata["role"]=="pi"?StateRole::Pi:StateRole::Controller;
    auto database=std::make_shared<SqliteStateStore>(text(root/"application"),self,role,StateOpen::Existing);
    const auto original=database->snapshot();const auto saved=configuration_from_snapshot(original,role);
    require(original.records.contains(StateRecord::Initialization));const auto initial=strict_browser_json(original.records.at(StateRecord::Initialization));
    source_initialization(strict_browser_json(read(root/"application/authority.json")),initial,self,role);
    if(role==StateRole::Pi&&!original.records.contains(StateRecord::ManagedSettings)){
        require(original.records.contains(StateRecord::RecurrenceAuthority));const auto raw=strict_browser_json(original.records.at(StateRecord::RecurrenceAuthority));
        require(raw.contains("source_binding")&&hex(raw["source_binding"],64));
        const auto bootstrap=parse_recurrence_storage_record(raw.dump(),self,raw["source_binding"].get<std::string>());
        require(bootstrap["schema"]=="pi-recurrence-bootstrap/1"&&initial["origin"]=="fresh"&&bootstrap["installation_id"]==initial["source_revision"].get<std::string>().substr(0,32));
        // Only this private validation copy is rebound. No active generation or
        // initialized recurrence authority can enter this branch.
        database->commit(original.revision,{{StateRecord::RecurrenceAuthority,recurrence_bootstrap_record(self,Journal::digest(database->directory()+"/configuration"),bootstrap["installation_id"].get<std::string>()).dump()}});
    }
    (void)capture_recovery_evidence(database->directory());
    validate_application_storage(database);WtpCatalog catalog(sqlite_catalog_storage(database));const auto profiles=catalog.snapshot({}).first;
    const bool pairing=names.contains("pairing/selector.json");
    for(const auto *name:{"pairing/authority.json","pairing/operations.json","pairing/peers.json"})require(names.contains(name)==pairing);
    if(pairing){
        const auto selector=parse_pi_pair_history_record(read(root/"pairing/selector.json"),self);const auto pair=root/"pair-owner";
        const auto selected=disk_path(text(pair)+".ns-"+selector["namespace_id"].get<std::string>());
        prepare_companion_directory(text(pair));prepare_companion_directory(text(selected));duplicate(root/"pairing/selector.json",pair/"authority.json");
        for(const auto *name:{"authority.json","operations.json","peers.json"})duplicate(root/"pairing"/name,selected/name);
        PiPairHistory owner(text(pair),self);require(owner.available());(void)owner.snapshot();
    }
    const bool retirement=names.contains("retirement/authority.json");require(names.contains("retirement/established.json")==retirement);
    if(retirement){
        const auto book=root/"retirement-owner";prepare_companion_directory(text(book));duplicate(root/"retirement/authority.json",book/"authority.json");
        duplicate(root/"retirement/established.json",disk_path(text(book)+".established"));const auto authority=strict_browser_json(read(root/"retirement/authority.json"));
        require(authority.contains("source_binding")&&hex(authority["source_binding"],64));
        const auto assignments=text(root/"application/configuration.wtp-assignments.json");WtpFleetStore fleet(assignments,self);
        ControllerRetirementBook owner(text(book),assignments,self,fleet.snapshot().first.at("assignments"),{}, {},authority["source_binding"].get<std::string>());
        (void)owner.read("recovery-source-validation",fleet.snapshot().first.at("assignments"));
    }
    std::set<std::string> credential_names;
    for(const auto &row:profiles.at("profiles")){
        const auto profile=parse_wtp_settings(row.at("settings"),true);if(profile.transport!="network"||profile.tls_ca.empty())continue;
        const auto id=row.at("id").get<std::string>();require(hex(row.at("id"),32));const auto prefix="credentials/"+id+"/";
        for(const auto *name:{"ca.crt","client.crt","client.key"}){require(names.contains(prefix+name));credential_names.insert(prefix+name);}
        TlsCredentials credential(TlsSelection{profile.hostname,profile.tls_identity,text(root/(prefix+"ca.crt")),
            text(root/(prefix+"client.crt")),text(root/(prefix+"client.key")),static_cast<unsigned>(profile.tcp_port)});
    }
    for(const auto &name:names)if(name.starts_with("credentials/"))require(credential_names.contains(name));
    return {manifest,profiles,original,saved};
}
}
