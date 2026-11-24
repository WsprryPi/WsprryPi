// SPDX-License-Identifier: MIT
#include "companion_files.hpp"
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include "native_private_acl.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "sqlite_state_store.hpp"
#include <array>
#include <cerrno>
#include <filesystem>
#include <memory>
#include <openssl/evp.h>
#include <set>
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw StateStorageError(StateStorageError::Code::Unavailable);}
bool hex(const std::string &value,std::size_t n){return value.size()==n&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
const char *label(CompanionKind kind){
    switch(kind){
        case CompanionKind::FleetAssignments:return "fleet-assignments";
        case CompanionKind::FleetContexts:return "fleet-contexts";
        case CompanionKind::UdpLogging:return "udp-logging";
        case CompanionKind::UsbLogging:return "usb-logging";
        case CompanionKind::TargetManagement:return "target-management";
    }throw StateStorageError(StateStorageError::Code::InvalidInput);
}
void validate(const CompanionFile &file){
    (void)label(file.kind);
    require(file.kind==CompanionKind::TargetManagement?hex(file.target,32):file.target.empty());
    require(file.size<=companion_maximum_bytes(file.kind)&&hex(file.digest,64)&&!file.signature.empty()&&file.signature.size()<=256);
}
struct ReadResult{std::uint64_t size;std::string digest,signature;};
#ifdef _WIN32
#include "companion_files_windows.hpp"
#else
struct Fd{int value{-1};~Fd(){if(value>=0)::close(value);}};
std::string signature(const struct stat &info){
#if defined(__APPLE__)
    const auto modified=info.st_mtimespec,changed=info.st_ctimespec;
#else
    const auto modified=info.st_mtim,changed=info.st_ctim;
#endif
    return std::to_string(info.st_dev)+":"+std::to_string(info.st_ino)+":"+std::to_string(info.st_size)+":"+
        std::to_string(info.st_mode)+":"+std::to_string(info.st_uid)+":"+std::to_string(info.st_nlink)+":"+
        std::to_string(modified.tv_sec)+":"+std::to_string(modified.tv_nsec)+":"+
        std::to_string(changed.tv_sec)+":"+std::to_string(changed.tv_nsec);
}
std::optional<ReadResult> stream(const std::string &path,CompanionKind kind,int output=-1,const std::string &expected={}){
    struct stat parent{};
    require(!::lstat(std::filesystem::path(path).parent_path().c_str(),&parent)&&S_ISDIR(parent.st_mode)&&
        (parent.st_uid==geteuid()||parent.st_uid==0)&&!(parent.st_mode&0022));
    struct stat first{};
    if(::lstat(path.c_str(),&first)){require(errno==ENOENT);return {};}
    const auto unsafe=kind==CompanionKind::UdpLogging?0022:0077;
    if(unsafe==0077)require(native_private_acl_path(std::filesystem::path(path).parent_path().c_str(),parent.st_uid));
    require(S_ISREG(first.st_mode)&&first.st_uid==geteuid()&&!(first.st_mode&unsafe)&&first.st_size>=0&&
        static_cast<std::uint64_t>(first.st_size)<=companion_maximum_bytes(kind));
    Fd input{::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};require(input.value>=0);
    struct stat opened{};require(!::fstat(input.value,&opened)&&signature(first)==signature(opened));
    if(unsafe==0077)require(native_private_acl(input.value,opened.st_uid));
    const auto stamp=signature(opened);require(expected.empty()||expected==stamp);
    auto digest=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    require(digest&&EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)==1);
    std::array<unsigned char,65536> buffer{};std::uint64_t total=0;
    for(;;){
        const auto count=::read(input.value,buffer.data(),buffer.size());
        if(count<0&&errno==EINTR)continue;
        require(count>=0);if(!count)break;
        total+=static_cast<std::uint64_t>(count);require(total<=companion_maximum_bytes(kind));
        require(EVP_DigestUpdate(digest.get(),buffer.data(),static_cast<std::size_t>(count))==1);
        std::size_t offset=0;
        while(output>=0&&offset<static_cast<std::size_t>(count)){
            const auto written=::write(output,buffer.data()+offset,static_cast<std::size_t>(count)-offset);
            if(written<0&&errno==EINTR)continue;
            require(written>0);offset+=static_cast<std::size_t>(written);
        }
    }
    struct stat after{},named{};
    require(!::fstat(input.value,&after)&&!::lstat(path.c_str(),&named)&&signature(after)==stamp&&signature(named)==stamp&&total==static_cast<std::uint64_t>(opened.st_size));
    std::array<unsigned char,EVP_MAX_MD_SIZE> bytes{};unsigned length{};
    require(EVP_DigestFinal_ex(digest.get(),bytes.data(),&length)==1&&length==32);
    std::string result;for(unsigned i=0;i<length;++i){result+="0123456789abcdef"[bytes[i]>>4];result+="0123456789abcdef"[bytes[i]&15];}
    return ReadResult{total,result,stamp};
}
void sync_parent(const std::string &path){
    Fd parent{::open(std::filesystem::path(path).parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    require(parent.value>=0&&!::fsync(parent.value));
}
#endif
}
std::uint64_t companion_maximum_bytes(CompanionKind kind){
    switch(kind){
        case CompanionKind::FleetAssignments:return 32*16384+16384;
        case CompanionKind::FleetContexts:return 1048576;
        case CompanionKind::UdpLogging:return 16384;
        case CompanionKind::UsbLogging:return 4096;
        case CompanionKind::TargetManagement:return 16*1048576;
    }throw StateStorageError(StateStorageError::Code::InvalidInput);
}
std::string companion_relative_path(const CompanionFile &file){
    (void)label(file.kind);
    require(file.kind==CompanionKind::TargetManagement?hex(file.target,32):file.target.empty());
    switch(file.kind){
        case CompanionKind::FleetAssignments:return "configuration.wtp-assignments.json";
        case CompanionKind::FleetContexts:return "configuration.wtp-fleet-contexts.json";
        case CompanionKind::UdpLogging:return "configuration.udp-logs.json";
        case CompanionKind::UsbLogging:return "configuration.usb-logs.json";
        case CompanionKind::TargetManagement:return "configuration.management/"+file.target+".json";
    }throw StateStorageError(StateStorageError::Code::InvalidInput);
}
std::string companion_source_path(const std::string &prefix,const CompanionFile &file){
#ifdef _WIN32
    (void)disk_path(prefix);
#else
    require(std::filesystem::path(prefix).is_absolute()&&prefix.find('\0')==std::string::npos);
#endif
    return prefix+companion_relative_path(file).substr(std::string("configuration").size());
}
std::string companion_backup_name(const CompanionFile &file){
    (void)companion_relative_path(file);return std::string(label(file.kind))+(file.target.empty()?"":"-"+file.target);
}
std::optional<CompanionFile> inspect_companion_file(const std::string &prefix,CompanionKind kind,const std::string &target){
    CompanionFile result{kind,target,0,{}, {}};
    const auto read=stream(companion_source_path(prefix,result),kind);
    if(!read)return {};
    result.size=read->size;result.digest=read->digest;result.signature=read->signature;validate(result);return result;
}
nlohmann::json companion_manifest(const std::vector<CompanionFile> &files){
    require(files.size()<=100);std::set<std::string> unique;unsigned targets=0;nlohmann::json value=nlohmann::json::array();
    for(const auto &file:files){
        validate(file);require(unique.insert(companion_relative_path(file)).second);
        if(file.kind==CompanionKind::TargetManagement)require(++targets<=96);
        value.push_back({{"kind",label(file.kind)},{"target",file.target},{"size",file.size},{"digest",file.digest},{"signature",file.signature}});
    }return value;
}
std::vector<CompanionFile> parse_companion_manifest(const nlohmann::json &value){
    require(value.is_array()&&value.size()<=100);std::vector<CompanionFile> result;
    for(const auto &row:value){
        require(row.is_object()&&row.size()==5&&row.contains("kind")&&row["kind"].is_string()&&row.contains("target")&&row["target"].is_string()&&
            row.contains("size")&&row["size"].is_number_unsigned()&&row.contains("digest")&&row["digest"].is_string()&&row.contains("signature")&&row["signature"].is_string());
        std::optional<CompanionKind> kind;
        for(auto candidate:{CompanionKind::FleetAssignments,CompanionKind::FleetContexts,CompanionKind::UdpLogging,CompanionKind::UsbLogging,CompanionKind::TargetManagement})
            if(row["kind"]==label(candidate))kind=candidate;
        require(kind.has_value());result.push_back({*kind,row["target"].get<std::string>(),row["size"].get<std::uint64_t>(),row["digest"].get<std::string>(),row["signature"].get<std::string>()});
    }(void)companion_manifest(result);return result;
}
void verify_companion_file(const std::string &path,const CompanionFile &file){
    validate(file);const auto current=stream(path,file.kind);require(current&&current->size==file.size&&current->digest==file.digest);
#ifdef _WIN32
    (void)open_private(disk_path(path),0);
#else
    struct stat info{};require(!::lstat(path.c_str(),&info)&&(info.st_mode&0777)==0600&&info.st_nlink==1);
#endif
}
std::string read_logging_companion(const std::string &prefix,const CompanionFile &file){
    validate(file);require(file.kind==CompanionKind::UdpLogging||file.kind==CompanionKind::UsbLogging);
    const auto path=companion_source_path(prefix,file);
#ifdef _WIN32
    std::string bytes;const auto current=stream(path,file.kind,INVALID_HANDLE_VALUE,file.signature,&bytes);
    require(current&&current->size==file.size&&current->digest==file.digest);return bytes;
#else
    Fd input{::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};require(input.value>=0);
    struct stat info{};require(!::fstat(input.value,&info)&&signature(info)==file.signature);
    std::string bytes;std::array<char,4096> buffer{};
    for(;;){const auto count=::read(input.value,buffer.data(),buffer.size());if(count<0&&errno==EINTR)continue;
        require(count>=0);if(!count)break;bytes.append(buffer.data(),static_cast<std::size_t>(count));require(bytes.size()<=companion_maximum_bytes(file.kind));}
    require(!::fstat(input.value,&info)&&signature(info)==file.signature&&bytes.size()==file.size);
    unsigned char digest[EVP_MAX_MD_SIZE];unsigned size{};
    require(EVP_Digest(bytes.data(),bytes.size(),digest,&size,EVP_sha256(),nullptr)==1&&size==32);
    std::string hashed;for(unsigned i=0;i<size;++i){hashed+="0123456789abcdef"[digest[i]>>4];hashed+="0123456789abcdef"[digest[i]&15];}
    require(hashed==file.digest);return bytes;
#endif
}
void prepare_companion_directory(const std::string &path){
#ifdef _WIN32
    const auto native=disk_path(path);const auto owner=wf::current_sid();wf::ParentChain parents(native,owner);
    if(!native_exists(native)){
        sync_parent(path);wf::PrivateSecurity security(owner);
        require(::CreateDirectoryW(wf::api_path(native).c_str(),security.attributes()));sync_parent(path);
    }
    (void)open_private(native,0,true);
#else
    if(!::mkdir(path.c_str(),0700))sync_parent(path);else require(errno==EEXIST);
    struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==geteuid()&&(info.st_mode&0777)==0700);
    require(native_private_acl_path(path.c_str(),info.st_uid));
#endif
}
void copy_companion_file(const std::string &source,const std::string &destination,const CompanionFile &file,bool original_source){
#ifdef _WIN32
    validate(file);const auto native=disk_path(destination),writing=disk_path(destination+".writing");
    const auto owner=wf::current_sid();wf::ParentChain parents(native,owner);
    (void)open_private(native.parent_path(),0,true);sync_parent(destination);
    if(native_exists(native)){
        // Native no-replace rename has one name after success; an additional
        // staging name is never interpreted as the POSIX hard-link window.
        require(!native_exists(writing));verify_companion_file(destination,file);return;
    }
    if(native_exists(writing)){remove_writing(writing,file.kind);sync_parent(destination);}
    wf::PrivateSecurity security(owner);bool created=false;
    try{
        {
            wf::Handle output(::CreateFileW(wf::api_path(writing).c_str(),GENERIC_WRITE|READ_CONTROL,0,
                security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
            require(output.get()!=INVALID_HANDLE_VALUE);created=true;(void)wf::inspect(output.get(),false);
            wf::verify_exact_owner(output.get(),owner);wf::verify_security(output.get(),owner,true,false);
            const auto copied=stream(source,file.kind,output.get(),original_source?file.signature:std::string{});
            require(copied&&copied->size==file.size&&copied->digest==file.digest&&::FlushFileBuffers(output.get()));
        }
        require(::MoveFileExW(wf::api_path(writing).c_str(),wf::api_path(native).c_str(),MOVEFILE_WRITE_THROUGH));
        created=false;sync_parent(destination);verify_companion_file(destination,file);
    }catch(...){if(created)try{remove_writing(writing,file.kind);sync_parent(destination);}catch(...){}throw;}
#else
    validate(file);struct stat existing{};
    if(!::lstat(destination.c_str(),&existing)){
        const auto writing=destination+".writing";struct stat staged{};
        if(existing.st_nlink==2&&!::lstat(writing.c_str(),&staged)&&S_ISREG(staged.st_mode)&&
            staged.st_dev==existing.st_dev&&staged.st_ino==existing.st_ino&&staged.st_uid==geteuid()&&
            (staged.st_mode&0777)==0600&&staged.st_nlink==2){
            const auto current=stream(destination,file.kind);require(current&&current->size==file.size&&current->digest==file.digest);
            require(!::unlink(writing.c_str()));sync_parent(destination);
        }
        verify_companion_file(destination,file);return;
    }
    require(errno==ENOENT);
    const auto writing=destination+".writing";
    if(!::lstat(writing.c_str(),&existing)){
        require(S_ISREG(existing.st_mode)&&existing.st_uid==geteuid()&&(existing.st_mode&0777)==0600&&existing.st_nlink==1&&
            existing.st_size>=0&&static_cast<std::uint64_t>(existing.st_size)<=companion_maximum_bytes(file.kind));
        require(!::unlink(writing.c_str()));
    }else require(errno==ENOENT);
    Fd output{::open(writing.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};require(output.value>=0);
    require(native_private_acl(output.value,::geteuid()));
    try{
        const auto copied=stream(source,file.kind,output.value,original_source?file.signature:std::string{});
        require(copied&&copied->size==file.size&&copied->digest==file.digest&&!::fsync(output.value));
        const auto descriptor=output.value;output.value=-1;require(!::close(descriptor));
        // Admission excludes other writers; link publishes without overwriting
        // an unexpectedly appeared destination, then removes the staging name.
        require(!::link(writing.c_str(),destination.c_str())&&!::unlink(writing.c_str()));sync_parent(destination);
        verify_companion_file(destination,file);
    }catch(...){(void)::unlink(writing.c_str());throw;}
#endif
}
} // namespace wsprrypi
