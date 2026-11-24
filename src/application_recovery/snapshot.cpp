// SPDX-License-Identifier: MIT
#include "snapshot.hpp"
#include "evidence.hpp"
#include "restore_hold.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/companion_capture.hpp"
#include "application_storage/companion_files.hpp"
#include "application_control/local_transport.hpp"
#include "config_handler.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "native_recurrence.hpp"
#include "scheduling.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "version.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
#include "wtp_integration/tls.hpp"
#include "wtp_settings_json.hpp"
#include <array>
#include <cerrno>
#include <filesystem>
#ifdef _WIN32
#include "windows_file_io.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <map>
#include <mutex>
#include <system_error>
#include <openssl/evp.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::StorageUnavailable);}
#ifdef _WIN32
std::filesystem::path disk_path(const std::string &path){return windows_file::path_from_utf8(path);}
std::string text(const std::filesystem::path &path){return recovery_windows::text(path);}
struct File {
    std::string source,resolved,name;
    std::uint64_t maximum{};
    bool credential{};
    windows_file::Snapshot observed{};
};
auto policy(bool credential){return credential?recovery_windows::Input::Policy::Credential:recovery_windows::Input::Policy::PrivateState;}
File inspect(const std::string &source,const std::string &name,std::uint64_t maximum,bool credential=false){
    recovery_windows::Input input(disk_path(source),maximum,recovery_windows::Input::Access::Read,policy(credential));
    input.unchanged();return {source,source,name,maximum,credential,input.observed()};
}
void recheck(const File &file){
    recovery_windows::Input input(disk_path(file.resolved),file.maximum,recovery_windows::Input::Access::Read,policy(file.credential));
    require(windows_file::same_snapshot(file.observed,input.observed()));input.unchanged();
}
std::uint64_t file_size(const File &file){return static_cast<std::uint64_t>(file.observed.standard.EndOfFile.QuadPart);}
void sync_directory(const std::string &path){windows_file::flush_directory(disk_path(path),windows_file::current_sid());}
void copy(const File &file,const std::string &root){
    const auto destination=disk_path(root)/file.name;
    std::vector<std::filesystem::path> parents;
    for(auto path=destination.parent_path();path!=disk_path(root);path=path.parent_path()){
        require(!path.empty()&&path!=path.parent_path());parents.push_back(path);
    }
    for(auto iterator=parents.rbegin();iterator!=parents.rend();++iterator)prepare_companion_directory(text(*iterator));
    recheck(file);
    recovery_windows::copy(disk_path(file.resolved),destination,file_size(file),{},policy(file.credential),[]{
        if(exiting_wspr.load())throw LocalCommandRefused(LocalRefusal::Busy);
    });
    recheck(file);
}
#else
std::filesystem::path disk_path(const std::string &path){return path;}
std::string text(const std::filesystem::path &path){return path.string();}
struct Fd {int value{-1};~Fd(){if(value>=0)::close(value);}};
bool same(const struct stat &a,const struct stat &b){
#ifdef __APPLE__
    const bool times=a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&
        a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
#else
    const bool times=a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
#endif
    return times&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink;
}
struct File {
    std::string source,resolved,name;
    std::uint64_t maximum{};
    bool credential{};
    struct stat observed{};
};
File inspect(const std::string &source,const std::string &name,std::uint64_t maximum,bool credential=false){
    File result{source,credential?std::filesystem::canonical(source).string():source,name,maximum,credential,{}};
    require(!::lstat(result.resolved.c_str(),&result.observed)&&S_ISREG(result.observed.st_mode)&&result.observed.st_nlink>=1&&
        result.observed.st_size>=0&&static_cast<std::uint64_t>(result.observed.st_size)<=maximum);
    if(!credential)require(result.observed.st_nlink==1&&result.observed.st_uid==::geteuid()&&(result.observed.st_mode&0777)==0600);
    return result;
}
void recheck(const File &file){
    if(file.credential)require(std::filesystem::canonical(file.source).string()==file.resolved);
    struct stat info{};require(!::lstat(file.resolved.c_str(),&info)&&same(info,file.observed));
}
void sync_directory(const std::string &path){
    Fd owner{::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};require(owner.value>=0&&!::fsync(owner.value));
}
void copy(const File &file,const std::string &root){
    const auto destination=std::filesystem::path(root)/file.name;
    std::vector<std::filesystem::path> parents;
    for(auto path=destination.parent_path();path!=std::filesystem::path(root);path=path.parent_path()){
        require(!path.empty()&&path!=path.parent_path());parents.push_back(path);
    }
    for(auto iterator=parents.rbegin();iterator!=parents.rend();++iterator)prepare_companion_directory(iterator->string());
    Fd input{::open(file.resolved.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};require(input.value>=0);
    struct stat before{};require(!::fstat(input.value,&before)&&same(before,file.observed));
    Fd output{::open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};require(output.value>=0);
    std::array<char,65536> buffer{};std::uint64_t total=0;
    for(;;){
        if(exiting_wspr.load())throw LocalCommandRefused(LocalRefusal::Busy);
        const auto count=::read(input.value,buffer.data(),buffer.size());if(count<0&&errno==EINTR)continue;
        require(count>=0);if(!count)break;total+=static_cast<std::uint64_t>(count);require(total<=file.maximum);
        std::size_t written=0;
        while(written<static_cast<std::size_t>(count)){
            const auto amount=::write(output.value,buffer.data()+written,static_cast<std::size_t>(count)-written);
            if(amount<0&&errno==EINTR)continue;
            require(amount>0);written+=static_cast<std::size_t>(amount);
        }
    }
    struct stat after{};require(total==static_cast<std::uint64_t>(before.st_size)&&!::fstat(input.value,&after)&&same(before,after)&&!::fsync(output.value));
    recheck(file);sync_directory(destination.parent_path().string());
}
std::uint64_t file_size(const File &file){return static_cast<std::uint64_t>(file.observed.st_size);}
#endif
bool present(const std::string &path){
#ifdef _WIN32
    windows_file::ParentChain parents(disk_path(path),windows_file::current_sid());
    if(::GetFileAttributesW(windows_file::api_path(disk_path(path)).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
#else
    struct stat info{};if(!::lstat(path.c_str(),&info))return true;require(errno==ENOENT);return false;
#endif
}
void append(std::vector<File> &files,const std::map<std::string,std::string> &names){
    for(const auto &[name,path]:names)files.push_back(inspect(path,name,1048576));
}
}
Json capture_application_recovery_snapshot(const std::string &requested){
    std::unique_lock configuration(config_update_mutex(),std::try_to_lock);
    if(!configuration.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
    const auto database=application_database();require(bool(database)&&!database->held());
    if(controller_only_mode())require(controller_only_status().value("controller_ready",false));
    else{
        require(!pi_managed_host_held());
        // No long snapshot barrier may delay an armed local occurrence or an
        // execution. Capture is observational and never stops output itself.
        if((config.transmit&&!local_restore_output_held())||native_occurrence_active()||current_test_tone_status().owned||!transmitter_output_inactive_confirmed())
            throw LocalCommandRefused(LocalRefusal::Busy);
    }
    const auto root=disk_path(requested);
    require(root.is_absolute()&&requested.find('\0')==std::string::npos);
#ifdef _WIN32
    auto owner=recovery_windows::private_directory(root);
    require(windows_file::normalized_path(owner.get())==root&&std::filesystem::is_empty(windows_file::api_path(root)));
#else
    struct stat owner{};require(!::lstat(root.c_str(),&owner)&&S_ISDIR(owner.st_mode)&&owner.st_uid==::geteuid()&&(owner.st_mode&0777)==0700&&
        std::filesystem::canonical(root)==root&&std::filesystem::is_empty(root));
#endif
    const auto application=text(root/"application");prepare_companion_directory(application);
    const auto pair=pi_pair_owner();require(bool(pair));
    Json result;
    try{pair->with_backup_files([&](const auto &pair_files){
        wtp_fleet_with_backup_files([&](const auto &retirement_files){
            const auto before=database->snapshot();
            auto &catalog=application_catalog();const auto profiles=catalog.snapshot({});
            const auto prefix=database->directory()+"/configuration";
            const auto companions=capture_application_companions(prefix,database->identity(),catalog);
            std::vector<File> files;
            const auto authority=text(disk_path(database->directory()).parent_path()/"authority.json");
            files.push_back(inspect(authority,"application/authority.json",1048576));
            const auto progress=prefix+".recurrence-progress.json";
            const bool progress_present=present(progress);
            if(progress_present)files.push_back(inspect(progress,"application/configuration.recurrence-progress.json",16384));
            append(files,pair_files);append(files,retirement_files);
            const auto evidence=capture_recovery_evidence(database->directory());
            for(const auto &[logical,path]:evidence)
                files.push_back(inspect(path,logical,*recovery_evidence_member_maximum(logical)));
            std::vector<std::pair<std::string,TlsSelection>> credentials;
            for(const auto &row:profiles.first.at("profiles")){
                const auto profile=parse_wtp_settings(row.at("settings"),true);
                if(profile.transport!="network"||profile.tls_ca.empty())continue;
                // Parse the actual configured material before admitting it to a
                // recovery archive; a profile path is not a generic file reader.
                TlsSelection selection{profile.hostname,profile.tls_identity,profile.tls_ca,profile.tls_certificate,profile.tls_key,static_cast<unsigned>(profile.tcp_port)};
                TlsCredentials validated(selection);
                const auto id=row.at("id").template get<std::string>();
                require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);
                for(const auto &[name,path]:std::map<std::string,std::string>{{"ca.crt",profile.tls_ca},{"client.crt",profile.tls_certificate},{"client.key",profile.tls_key}})
                    files.push_back(inspect(path,"credentials/"+id+"/"+name,1048576,true));
                credentials.emplace_back(id,selection);
            }
            std::uint64_t staging_bytes=database->snapshot_size_bytes();
            for(const auto &file:files)staging_bytes+=file_size(file)+1024;
            for(const auto &file:companions)staging_bytes+=file.size+1024;
            require(std::filesystem::space(root).available>=2*staging_bytes+1048576);
            // The SQL read cut lies inside every included companion's stable
            // observation interval. Actual owner locks exclude coordinated
            // pairing/retirement decisions; remaining file mutations invalidate
            // capture rather than blocking execution or admitting a mixed copy.
            database->snapshot_to(application);
            for(const auto &file:files)copy(file,text(root));
            for(auto &[id,selection]:credentials){
                const auto directory=text(root/"credentials"/id);
                selection.ca_file=directory+"/ca.crt";selection.certificate_file=directory+"/client.crt";selection.key_file=directory+"/client.key";
                TlsCredentials validated(selection);
            }
            for(const auto &file:companions){
                if(exiting_wspr.load())throw LocalCommandRefused(LocalRefusal::Busy);
                const auto name=companion_relative_path(file);const auto path=text(root/"application"/name);
                if(file.kind==CompanionKind::TargetManagement)prepare_companion_directory(text(root/"application/configuration.management"));
                copy_companion_file(companion_source_path(prefix,file),path,file,true);
            }
            require(database->snapshot()==before&&catalog.snapshot({})==profiles&&capture_application_companions(prefix,database->identity(),catalog)==companions);
            require(capture_recovery_evidence(database->directory())==evidence);
            for(const auto &file:files)recheck(file);
            if(!progress_present)require(!present(progress));
            auto names=Json::array({"application/state.sqlite3"});
            for(const auto &file:files)names.push_back(file.name);
            for(const auto &file:companions)names.push_back("application/"+companion_relative_path(file));
#ifdef _WIN32
            const auto platform="windows";
#elif defined(__APPLE__)
            const auto platform="darwin";
#else
            const auto platform="linux";
#endif
            result={{"schema","recovery-snapshot/1"},{"source",{{"identity",database->identity()},{"role",database->role()==StateRole::Pi?"pi":"controller"},
                {"application_version",get_exe_version()},{"storage","application-state/1"},{"platform",platform}}},{"files",names},{"storage_revision",before.revision}};
            sync_directory(application);sync_directory(text(root));
        });
    });}catch(const std::system_error &error){
        if(error.code()==std::make_error_code(std::errc::device_or_resource_busy))throw LocalCommandRefused(LocalRefusal::Busy);
        throw;
    }
    return result;
}
}
