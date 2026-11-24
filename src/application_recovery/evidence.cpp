// SPDX-License-Identifier: MIT
#include "evidence.hpp"
#include "private_json.hpp"
#include "wtp_integration/pi_management_journal.hpp"
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
#include <openssl/evp.h>
#include <set>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::StorageUnavailable);}
bool hex(std::string_view value,std::size_t length){return value.size()==length&&value.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
void fields(const Json &value,std::initializer_list<const char *> names){require(value.is_object()&&value.size()==names.size());for(const auto *name:names)require(value.contains(name));}
#ifdef _WIN32
std::filesystem::path disk_path(const std::string &path){return windows_file::path_from_utf8(path);}
std::string text(const std::filesystem::path &path){return recovery_windows::text(path);}
void directory(const std::filesystem::path &path){(void)recovery_windows::private_directory(path);}
std::string digest_file(const std::filesystem::path &path,std::uint64_t expected){recovery_windows::Input input(path,expected);return recovery_windows::digest(input,expected);}
#else
std::filesystem::path disk_path(const std::string &path){return path;}
std::string text(const std::filesystem::path &path){return path.string();}
void directory(const std::filesystem::path &path){struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0700&&std::filesystem::canonical(path)==path);}
std::string digest_file(const std::filesystem::path &path,std::uint64_t expected){
    const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);require(fd>=0);struct Close{int fd;~Close(){::close(fd);}}close{fd};
    struct stat before{},after{};require(!::fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_uid==::geteuid()&&(before.st_mode&0777)==0600&&before.st_nlink==1&&
        before.st_size>=0&&static_cast<std::uint64_t>(before.st_size)==expected);
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(hash&&EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)==1);
    std::array<char,65536> buffer{};std::uint64_t total{};
    while(true){const auto count=::read(fd,buffer.data(),buffer.size());if(count<0&&errno==EINTR)continue;require(count>=0);if(!count)break;total+=count;require(total<=expected&&EVP_DigestUpdate(hash.get(),buffer.data(),count)==1);}
    require(total==expected&&!::fstat(fd,&after)&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_size==after.st_size);
#ifdef __APPLE__
    require(before.st_mtimespec.tv_sec==after.st_mtimespec.tv_sec&&before.st_mtimespec.tv_nsec==after.st_mtimespec.tv_nsec&&before.st_ctimespec.tv_sec==after.st_ctimespec.tv_sec&&before.st_ctimespec.tv_nsec==after.st_ctimespec.tv_nsec);
#else
    require(before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec&&before.st_ctim.tv_sec==after.st_ctim.tv_sec&&before.st_ctim.tv_nsec==after.st_ctim.tv_nsec);
#endif
    std::array<unsigned char,32> digest{};unsigned size{};require(EVP_DigestFinal_ex(hash.get(),digest.data(),&size)==1&&size==32);std::string result;
    for(const auto byte:digest){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
#endif
}
std::optional<std::uint64_t> recovery_evidence_member_maximum(std::string_view name){
    constexpr std::string_view prefix="application/evidence/";if(!name.starts_with(prefix))return {};name.remove_prefix(prefix.size());
    if(name.size()<34||!hex(name.substr(0,32),32)||name[32]!='/')return {};
    name.remove_prefix(33);
    if(name=="index.json")return 1048576;
    for(const auto candidate:{"state-configuration.json","state-managed-settings.json","state-operations.json","state-catalog.json","state-recurrence-authority.json",
        "state-recurrence-initialization.json","state-initialization.json","state-recurrence-recovery.json","state-selector.json","pair-selector.json","pair-authority.json",
        "pair-operations.json","pair-peers.json","retirement-authority.json","retirement-established.json","fleet-assignments.json","fleet-contexts.json"})if(name==candidate)return 1048576;
    if(name=="progress.json")return 16384;
    if(name.size()==44&&name.starts_with("target-")&&hex(name.substr(7,32),32)&&name.substr(39)==".json")return 16ULL*1024*1024;
    if(name.starts_with("c-")&&name.size()>=37&&hex(name.substr(2,32),32)&&(name.substr(34)=="-ca"||name.substr(34)=="-crt"||name.substr(34)=="-key"))return 1048576;
    return {};
}
std::map<std::string,std::string> capture_recovery_evidence(const std::string &generation){
    const auto root=disk_path(generation)/"evidence";
#ifdef _WIN32
    windows_file::ParentChain parents(root,windows_file::current_sid());
    if(::GetFileAttributesW(windows_file::api_path(root).c_str())==INVALID_FILE_ATTRIBUTES){const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return {};}
#else
    struct stat info{};
    if(::lstat(root.c_str(),&info)){require(errno==ENOENT);return {};}
#endif
    directory(root);std::map<std::string,std::string> files;std::uint64_t total{};
    for(const auto &entry:std::filesystem::directory_iterator(root)){
        const auto id=entry.path().filename().string();require(hex(id,32));directory(entry.path());
        const auto record=PiManagementJournal::read_private_record(text(entry.path()/"index.json"));require(bool(record));const auto index=parse_recovery_json(record->bytes,1048576);
        fields(index,{"schema","source","created_utc","archive_sha256","content_digest","files"});require(index["schema"]=="recovery-evidence/1"&&index["created_utc"].is_number_unsigned()&&index["created_utc"]>0&&index["created_utc"].get<std::uint64_t>()<(1ULL<<63));
        const auto &source=index["source"];fields(source,{"identity","role","application_version","storage","platform"});
        require(source["identity"].is_string()&&hex(source["identity"].get<std::string>(),32)&&(source["role"]=="pi"||source["role"]=="controller")&&
            source["storage"]=="application-state/1"&&(source["platform"]=="linux"||source["platform"]=="darwin"||(source["platform"]=="windows"&&source["role"]=="controller"))&&source["application_version"].is_string()&&
            !source["application_version"].get_ref<const std::string &>().empty()&&source["application_version"].get_ref<const std::string &>().size()<=128&&
            index["archive_sha256"].is_string()&&hex(index["archive_sha256"].get<std::string>(),64)&&index["content_digest"].is_string()&&hex(index["content_digest"].get<std::string>(),64)&&
            index["files"].is_array()&&!index["files"].empty()&&index["files"].size()<1024);
        const Json content{{"schema",index["schema"]},{"source",source},{"files",index["files"]}};
        const auto digest=PiManagementJournal::digest(content.dump(-1,' ',true));require(digest==index["content_digest"].get<std::string>()&&digest.substr(0,32)==id);
        std::set<std::string> names{"index.json"};std::string previous;
        for(const auto &file:index["files"]){fields(file,{"name","size","sha256"});require(file["name"].is_string()&&file["size"].is_number_unsigned()&&file["sha256"].is_string()&&hex(file["sha256"].get<std::string>(),64));
            const auto name=file["name"].get<std::string>(),logical="application/evidence/"+id+"/"+name;const auto maximum=recovery_evidence_member_maximum(logical);
            require(name!="index.json"&&name>previous&&maximum&&file["size"].get<std::uint64_t>()<=*maximum&&names.insert(name).second);previous=name;
            total+=file["size"].get<std::uint64_t>();require(total<=2ULL*1024*1024*1024-2*1024*1024&&files.size()<1023);
            require(digest_file(entry.path()/name,file["size"].get<std::uint64_t>())==file["sha256"].get<std::string>());files[logical]=text(entry.path()/name);
        }
        std::set<std::string> actual;for(const auto &file:std::filesystem::directory_iterator(entry.path()))actual.insert(file.path().filename().string());require(actual==names&&files.size()<1024);
        files["application/evidence/"+id+"/index.json"]=text(entry.path()/"index.json");
    }return files;
}
}
