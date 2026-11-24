// SPDX-License-Identifier: MIT
#include "controller_identity.hpp"
#ifndef _WIN32
#include "controller_identity_record.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "json.hpp"
#include <filesystem>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <pwd.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <membership.h>
#include <sys/acl.h>
#include <cstring>
#endif

namespace wsprrypi {
namespace {
void require(bool value) { if(!value)throw std::runtime_error("controller_identity_storage_hold"); }
struct Descriptor {int value;~Descriptor(){if(value>=0)::close(value);}};
void private_acl(int descriptor,std::uint64_t owner) {
#ifdef __APPLE__
    const auto acl=::acl_get_fd_np(descriptor,ACL_TYPE_EXTENDED);
    // Darwin reports an absent extended ACL as ENOENT on a valid descriptor.
    if(!acl){require(errno==ENOENT||errno==ENOATTR);return;}
    struct Cleanup {acl_t value;~Cleanup(){::acl_free(value);}} cleanup{acl};
    require(!::acl_valid(acl));
    uuid_t owner_uuid{};require(!::mbr_uid_to_uuid(static_cast<uid_t>(owner),owner_uuid));
    acl_entry_t entry{};int next=::acl_get_entry(acl,ACL_FIRST_ENTRY,&entry);
    while(next==0) {
        acl_tag_t tag{};require(!::acl_get_tag_type(entry,&tag));
        if(tag==ACL_EXTENDED_ALLOW) {
            const auto qualifier=::acl_get_qualifier(entry);require(qualifier);
            const bool own=std::memcmp(qualifier,owner_uuid,sizeof owner_uuid)==0;::acl_free(qualifier);
            require(own);
        }
        next=::acl_get_entry(acl,ACL_NEXT_ENTRY,&entry);
    }
    require(next==-1&&errno==EINVAL);
#else
    (void)descriptor;(void)owner;
#endif
}
bool stable_metadata(const struct stat &before,const struct stat &after) {
#ifdef __APPLE__
    const auto a=before.st_mtimespec,b=after.st_mtimespec,c=before.st_ctimespec,d=after.st_ctimespec;
#else
    const auto a=before.st_mtim,b=after.st_mtim,c=before.st_ctim,d=after.st_ctim;
#endif
    return before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_mode==after.st_mode&&
        before.st_uid==after.st_uid&&before.st_nlink==after.st_nlink&&before.st_size==after.st_size&&
        a.tv_sec==b.tv_sec&&a.tv_nsec==b.tv_nsec&&c.tv_sec==d.tv_sec&&c.tv_nsec==d.tv_nsec;
}
bool present(const std::string &path) {
    struct stat info{};
    if(!::lstat(path.c_str(),&info))return true;
    require(errno==ENOENT);return false;
}
std::string parse(const std::string &bytes) {
    return parse_controller_identity_record(bytes);
}

}

ControllerIdentityStore::ControllerIdentityStore(std::string home,std::optional<std::uint64_t> owner_uid)
    :home_(std::move(home)),owner_uid_(owner_uid.value_or(::geteuid())) {
    validate_home();
}
void ControllerIdentityStore::validate_home() const {
    require(::geteuid()==0||::geteuid()==owner_uid_);
    require(std::filesystem::path(home_).is_absolute()&&home_.find('\0')==std::string::npos);
    // Reject symbolic owner paths as well as replaceable/untrusted ancestors.
    for(auto path=std::filesystem::path(home_);;path=path.parent_path()) {
        struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&
            (info.st_uid==owner_uid_||info.st_uid==0)&&
            (!(info.st_mode&0022)||(info.st_uid==0&&(info.st_mode&S_ISVTX))));
        if(path==path.root_path())break;
    }
    struct stat owner{};require(!::lstat(home_.c_str(),&owner)&&owner.st_uid==owner_uid_);
    Descriptor descriptor{::open(home_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    require(descriptor.value>=0);private_acl(descriptor.value,owner_uid_);
}
std::string ControllerIdentityStore::directory() const {
    return (std::filesystem::path(home_)/".wsprrypi-controller-identity").string();
}
std::optional<std::string> ControllerIdentityStore::read() const {
    validate_home();
    if(!present(directory())) {
        // Only the explicit initializer may resume a prepared publication.
        require(!present(directory()+".prepare"));
        return {};
    }
    Descriptor parent{::open(directory().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat info{};require(parent.value>=0&&!::fstat(parent.value,&info)&&S_ISDIR(info.st_mode)&&
        info.st_uid==owner_uid_&&!(info.st_mode&0077));
    const auto parent_info=info;
    private_acl(parent.value,owner_uid_);
    Descriptor input{::openat(parent.value,"authority.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
    require(input.value>=0&&!::fstat(input.value,&info)&&S_ISREG(info.st_mode)&&
        info.st_uid==owner_uid_&&info.st_nlink==1&&!(info.st_mode&0077)&&info.st_size>0&&info.st_size<=512);
    const auto before=info;
    private_acl(input.value,owner_uid_);
    std::string bytes;
    char buffer[513];
    while(true){
        const auto count=::read(input.value,buffer,sizeof buffer);
        if(count<0&&errno==EINTR)continue;
        require(count>=0);if(!count)break;
        bytes.append(buffer,static_cast<std::size_t>(count));require(bytes.size()<=512);
    }
    require(!::fstat(input.value,&info)&&stable_metadata(before,info)&&
        bytes.size()==static_cast<std::size_t>(before.st_size));
    require(!::fstatat(parent.value,"authority.json",&info,AT_SYMLINK_NOFOLLOW)&&
        info.st_dev==before.st_dev&&info.st_ino==before.st_ino);
    require(!::lstat(directory().c_str(),&info)&&info.st_dev==parent_info.st_dev&&info.st_ino==parent_info.st_ino);
    return parse(bytes);
}
std::string ControllerIdentityStore::initialize(const std::function<std::string()> &random,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint) const {
    require(bool(random));
    validate_home();
    // Maintenance may inspect the backend identity but never provision it.
    require(::geteuid()==owner_uid_);
    if(!present(directory())) {
        unsigned count=0;
        for(const auto &entry:std::filesystem::directory_iterator(home_)) {
            require(++count<=4096);
            const auto name=entry.path().filename().string();
            // Lost identity/state must not appear to be an empty enrollment.
            require(name!=".wsprrypi-wtp"&&name!=".wsprrypi-recovery"&&!name.starts_with(".wsprrypi-pairing-")&&
                !name.starts_with(".wsprrypi-controller-retirements-")&&!name.starts_with(".wsprrypi-recurrence-"));
        }
    }
    const auto prepared=prepare_owned_state_directory(directory(),[&]{
        const auto bytes=nlohmann::json{{"schema","controller-identity/1"},{"device_id",random()}}.dump();
        (void)parse(bytes);return bytes;
    },[](const auto &bytes){(void)parse(bytes);},checkpoint);
    (void)prepared;
    return *read();
}
}
#endif
