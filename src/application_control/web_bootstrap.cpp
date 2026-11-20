// SPDX-License-Identifier: MIT
#include "web_bootstrap.hpp"
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("Listener bootstrap cache is unavailable or unsafe");}
bool identity(const Json &value){return value.is_string()&&value.get_ref<const std::string &>().size()==32&&value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
struct Descriptor {int fd;~Descriptor(){if(fd>=0)::close(fd);}};
int parent(const std::string &path){
    const std::filesystem::path file(path);require(file.is_absolute()&&file.filename()!="."&&file.filename()!=".."&&path.find('\0')==std::string::npos);
    const auto name=file.filename().string();require(name=="listeners.json"||name.ends_with(".web-listeners.json"));
    auto directory=file.parent_path();
    while(!directory.empty()){
        struct stat info{};require(!::lstat(directory.c_str(),&info)&&S_ISDIR(info.st_mode)&&(info.st_uid==0||info.st_uid==::geteuid())&&
            (!(info.st_mode&0022)||(info.st_uid==0&&(info.st_mode&S_ISVTX))));
        const auto next=directory.parent_path();if(next==directory)break;directory=next;
    }
    const auto fd=::open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0);return fd;
}
void safe_file(int directory,const std::string &name,bool missing){
    struct stat info{};
    if(::fstatat(directory,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)){require(missing&&errno==ENOENT);return;}
    require(S_ISREG(info.st_mode)&&info.st_uid==::geteuid()&&info.st_nlink==1&&!(info.st_mode&0077));
}
}
bool valid_web_bootstrap(const Json &value){
    if(!value.is_object()||value.size()!=11)return false;
    for(const auto *name:{"schema","device_id","role","boot","web_port","socket_port","http_enabled","websocket_enabled","http_loopback_only","socket_loopback_only","socket_loopback_family"})if(!value.contains(name))return false;
    if(value["schema"]!="web-service-bootstrap/1"||!identity(value["device_id"])||!identity(value["boot"])||(value["role"]!="pi"&&value["role"]!="controller"))return false;
    for(const auto *name:{"web_port","socket_port"})if(!value[name].is_number_integer()||value[name]<1024||value[name]>49151)return false;
    if(value["web_port"]==value["socket_port"])return false;
    for(const auto *name:{"http_enabled","websocket_enabled","http_loopback_only","socket_loopback_only"})if(!value[name].is_boolean())return false;
    return value["socket_loopback_family"]=="auto"||value["socket_loopback_family"]=="ipv4"||value["socket_loopback_family"]=="ipv6";
}
Json read_web_bootstrap_cache(const std::string &path){
    Descriptor directory{parent(path)};const auto name=std::filesystem::path(path).filename().string();safe_file(directory.fd,name,false);
    Descriptor input{::openat(directory.fd,name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK)};require(input.fd>=0);
    struct stat info{};require(!::fstat(input.fd,&info)&&S_ISREG(info.st_mode)&&info.st_uid==::geteuid()&&info.st_nlink==1&&!(info.st_mode&0077)&&info.st_size>0&&info.st_size<=8192);
    std::string bytes(info.st_size,'\0');std::size_t offset=0;
    while(offset<bytes.size()){const auto count=::read(input.fd,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR)continue;require(count>0);offset+=count;}
    std::vector<std::set<std::string>> keys;
    auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        require(depth<=8);if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(item.get<std::string>()).second);
        else if(event==Json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
        return true;
    });require(valid_web_bootstrap(value));return value;
}
void write_web_bootstrap_cache(const std::string &path,const Json &value){
    require(valid_web_bootstrap(value));Descriptor directory{parent(path)};
    const auto name=std::filesystem::path(path).filename().string();safe_file(directory.fd,name,true);
    // One admitted frontend writes this cache. A stale, bounded, private
    // temporary file is never authority and may be removed after a crash.
    const auto temporary=name+".writing";
    struct stat stale{};
    if(!::fstatat(directory.fd,temporary.c_str(),&stale,AT_SYMLINK_NOFOLLOW)){
        safe_file(directory.fd,temporary,false);require(stale.st_size>=0&&stale.st_size<=8192);
        require(!::unlinkat(directory.fd,temporary.c_str(),0)&&!::fsync(directory.fd));
    }else require(errno==ENOENT);
    // Reconnection to the same backend boot does not rewrite the SD card.
    if(!::fstatat(directory.fd,name.c_str(),&stale,AT_SYMLINK_NOFOLLOW)){
        try{if(read_web_bootstrap_cache(path)==value)return;}catch(const std::exception &){
            // A live verified backend can replace malformed cache contents;
            // the ownership/type checks above still prohibit unsafe paths.
        }
    }else require(errno==ENOENT);
    Descriptor output{::openat(directory.fd,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};require(output.fd>=0);
    try{
        const auto bytes=value.dump();std::size_t offset=0;
        while(offset<bytes.size()){const auto count=::write(output.fd,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR)continue;require(count>0);offset+=count;}
        require(!::fsync(output.fd));safe_file(directory.fd,name,true);
        require(!::renameat(directory.fd,temporary.c_str(),directory.fd,name.c_str())&&!::fsync(directory.fd));
    }catch(...){::unlinkat(directory.fd,temporary.c_str(),0);throw;}
}
}
