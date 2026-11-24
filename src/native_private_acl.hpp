// SPDX-License-Identifier: MIT
#pragma once
#include <sys/types.h>
#ifdef __APPLE__
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <membership.h>
#include <sys/acl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace wsprrypi {
// Mode bits alone do not describe Darwin extended grants. Reject any allow
// entry for another principal; deny entries cannot widen private access.
inline bool native_private_acl(int descriptor,uid_t owner){
#ifdef __APPLE__
    const auto acl=::acl_get_fd_np(descriptor,ACL_TYPE_EXTENDED);
    if(!acl)return errno==ENOENT||errno==ENOATTR;
    struct Cleanup {acl_t value;~Cleanup(){::acl_free(value);}} cleanup{acl};
    if(::acl_valid(acl))return false;
    uuid_t own{};if(::mbr_uid_to_uuid(owner,own))return false;
    acl_entry_t entry{};int next=::acl_get_entry(acl,ACL_FIRST_ENTRY,&entry);
    while(next==0){
        acl_tag_t tag{};if(::acl_get_tag_type(entry,&tag))return false;
        if(tag==ACL_EXTENDED_ALLOW){
            const auto principal=::acl_get_qualifier(entry);if(!principal)return false;
            const bool same=std::memcmp(principal,own,sizeof own)==0;::acl_free(principal);
            if(!same)return false;
        }
        next=::acl_get_entry(acl,ACL_NEXT_ENTRY,&entry);
    }
    return next==-1&&errno==EINVAL;
#else
    (void)descriptor;(void)owner;return true;
#endif
}
inline bool native_private_acl_path(const char *path,uid_t owner){
#ifdef __APPLE__
    const auto descriptor=::open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(descriptor<0)return false;
    struct stat info{};
    const bool valid=!::fstat(descriptor,&info)&&info.st_uid==owner&&native_private_acl(descriptor,owner);
    ::close(descriptor);return valid;
#else
    (void)path;(void)owner;return true;
#endif
}
inline bool native_private_acl_ancestors(const char *path){
#ifdef __APPLE__
    auto parent=std::filesystem::absolute(path).parent_path();
    while(!parent.empty()){
        struct stat info{};
        if(::lstat(parent.c_str(),&info))return errno==ENOENT;
        if(!S_ISDIR(info.st_mode)||(info.st_uid!=0&&info.st_uid!=::geteuid())||
            ((info.st_mode&0022)&&!(info.st_uid==0&&(info.st_mode&S_ISVTX)))||
            !native_private_acl_path(parent.c_str(),info.st_uid))return false;
        const auto next=parent.parent_path();if(next==parent)break;parent=next;
    }
#else
    (void)path;
#endif
    return true;
}
}
