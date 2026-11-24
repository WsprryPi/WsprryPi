// SPDX-License-Identifier: MIT
#pragma once
#ifdef __APPLE__
#include <cassert>
#include <grp.h>
#include <membership.h>
#include <sys/acl.h>
inline void set_software_other_read_acl(const char *path,bool grant){
    auto acl=::acl_init(grant?1:0);assert(acl);
    if(grant){
        const auto group=::getgrnam("everyone");assert(group);
        uuid_t principal{};assert(!::mbr_gid_to_uuid(group->gr_gid,principal));
        acl_entry_t entry{};assert(!::acl_create_entry(&acl,&entry)&&!::acl_set_tag_type(entry,ACL_EXTENDED_ALLOW)&&!::acl_set_qualifier(entry,principal));
        acl_permset_t permissions{};assert(!::acl_get_permset(entry,&permissions)&&!::acl_add_perm(permissions,ACL_READ_DATA)&&!::acl_set_permset(entry,permissions));
    }
    assert(!::acl_set_file(path,ACL_TYPE_EXTENDED,acl));::acl_free(acl);
}
#endif
