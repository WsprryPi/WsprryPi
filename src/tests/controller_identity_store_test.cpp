// SPDX-License-Identifier: MIT
#include "application_storage/controller_identity.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "software_private_acl.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <source_location>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <grp.h>
#include <membership.h>
#include <sys/acl.h>
#endif

namespace fs=std::filesystem;
template<class F> void refused(F f, std::source_location at=std::source_location::current()) {
    bool failed=false;try{f();}catch(const std::exception &){failed=true;}
    if(!failed)std::cerr<<"Expected refusal at line "<<at.line()<<'\n';
    assert(failed);
}
int main() {
    char pattern[]="/tmp/controller-identity-XXXXXX";
    const auto created=::mkdtemp(pattern);assert(created);
    const auto root=fs::canonical(created);
    struct Cleanup {fs::path root;~Cleanup(){std::error_code error;fs::remove_all(root,error);}} cleanup{root};
    auto home=[&](const char *name){const auto path=root/name;fs::create_directory(path);::chmod(path.c_str(),0700);return path;};
    const auto first_home=home("first");
    wsprrypi::ControllerIdentityStore first(first_home.string());
    assert(!first.read()&&!fs::exists(first.directory()));
    unsigned generated=0;
    const auto random=[&]{++generated;return wsprrypi::WtpPiBootIdentity{}.new_boot_id();};
    const auto id=first.initialize(random);
    assert(first.read()==id&&first.initialize(random)==id&&generated==1);
    const auto second_home=home("second");
    wsprrypi::ControllerIdentityStore second(second_home.string());
    assert(second.initialize(random)!=id);
    const auto file=fs::path(first.directory())/"authority.json";
    const auto bytes=[&]{std::ifstream in(file);return std::string(std::istreambuf_iterator<char>(in),{});}();
    // A deleted committed image is a hold, never permission to generate again.
    fs::remove(file);const auto count=generated;
    refused([&]{first.initialize(random);});assert(generated==count&&!fs::exists(file));
    {std::ofstream out(file);out<<bytes;}::chmod(file.c_str(),0600);
    for(const auto &invalid:{std::string("{}"),std::string(513,'x'),
        std::string("{\"schema\":\"controller-identity/1\",\"schema\":\"controller-identity/1\",\"device_id\":\"")+id+"\"}",
        std::string("{\"schema\":\"controller-identity/2\",\"device_id\":\"")+id+"\"}",
        std::string("{\"schema\":\"controller-identity/1\",\"device_id\":\"")+std::string(32,'0')+"\"}"}) {
        {std::ofstream out(file);out<<invalid;}
        refused([&]{first.read();});refused([&]{first.initialize(random);});assert(generated==count);
    }
    {std::ofstream out(file);out<<bytes;}
    ::chmod(file.c_str(),0644);refused([&]{first.read();});::chmod(file.c_str(),0600);
    fs::create_hard_link(file,first_home/"linked");refused([&]{first.read();});fs::remove(first_home/"linked");
    fs::rename(file,first_home/"retained");fs::create_symlink(first_home/"retained",file);
    refused([&]{first.read();});fs::remove(file);fs::rename(first_home/"retained",file);
    assert(first.read()==id);
    ::chmod(first_home.c_str(),0777);refused([&]{first.read();});::chmod(first_home.c_str(),0700);
#ifdef __APPLE__
    const auto private_record=(first_home/"private-record.json").string();
    const auto record=wsprrypi::PiManagementJournal::publish_private_record(private_record,{},"{}");
    set_software_other_read_acl(private_record.c_str(),true);
    refused([&]{wsprrypi::PiManagementJournal::read_private_record(private_record);});
    refused([&]{wsprrypi::PiManagementJournal::publish_private_record(private_record,record,"changed");});
    set_software_other_read_acl(private_record.c_str(),false);
    assert(wsprrypi::PiManagementJournal::read_private_record(private_record)->bytes=="{}");
    set_software_other_read_acl(first_home.c_str(),true);
    refused([&]{wsprrypi::PiManagementJournal::publish_private_record(private_record,record,"changed");});
    set_software_other_read_acl(first_home.c_str(),false);
    // Extended ACL grants must not bypass otherwise-private POSIX mode bits.
    const auto everyone=::getgrnam("everyone");assert(everyone);
    uuid_t group_uuid{};assert(!::mbr_gid_to_uuid(everyone->gr_gid,group_uuid));
    auto acl=::acl_init(1);assert(acl);acl_entry_t entry{};
    assert(!::acl_create_entry(&acl,&entry)&&!::acl_set_tag_type(entry,ACL_EXTENDED_ALLOW)&&
        !::acl_set_qualifier(entry,group_uuid));
    acl_permset_t permissions{};assert(!::acl_get_permset(entry,&permissions)&&
        !::acl_add_perm(permissions,ACL_READ_DATA)&&!::acl_set_permset(entry,permissions));
    assert(!::acl_set_file(file.c_str(),ACL_TYPE_EXTENDED,acl));::acl_free(acl);
    refused([&]{first.read();});
    acl=::acl_init(0);assert(acl&&!::acl_set_file(file.c_str(),ACL_TYPE_EXTENDED,acl));::acl_free(acl);
    assert(first.read()==id);
#endif
    const auto lost=home("lost");fs::create_directory(lost/(".wsprrypi-pairing-"+id));
    wsprrypi::ControllerIdentityStore missing(lost.string());
    refused([&]{missing.initialize(random);});assert(!fs::exists(missing.directory()));
    const auto recovery_home=home("lost-with-recovery");fs::create_directory(recovery_home/".wsprrypi-recovery");
    wsprrypi::ControllerIdentityStore recovery_missing(recovery_home.string());
    refused([&]{recovery_missing.initialize(random);});assert(!fs::exists(recovery_missing.directory()));
    // The accepted publication primitive resumes the same staged identity.
    for(const auto point:{wsprrypi::OwnedStatePublicationPoint::PreparedDirectory,
                         wsprrypi::OwnedStatePublicationPoint::WrittenImage,
                         wsprrypi::OwnedStatePublicationPoint::StagedImage,
                         wsprrypi::OwnedStatePublicationPoint::PublishedDirectory,
                         wsprrypi::OwnedStatePublicationPoint::SyncedDirectory}) {
        const auto path=home(("crash-"+std::to_string(static_cast<int>(point))).c_str());
        wsprrypi::ControllerIdentityStore store(path.string());
        unsigned calls=0;
        const auto source=[&]{return std::string(32,++calls==1?'a':'b');};
        refused([&]{store.initialize(source,[&](auto at){if(at==point)throw std::runtime_error("interruption");});});
        const auto resumed=store.initialize(source);
        const bool abandoned_writing=point==wsprrypi::OwnedStatePublicationPoint::WrittenImage;
        assert(calls==(abandoned_writing?2U:1U));
        assert(resumed==std::string(32,abandoned_writing?'b':'a')&&store.read()==resumed);
    }
    fs::create_symlink(first_home,root/"alias");
    refused([&]{wsprrypi::ControllerIdentityStore bad((root/"alias").string());});
    std::cout<<"PASS persistent CSPRNG identity, distinct installs, immutable missing/corrupt hold, strict private records, lost-authority refusal, and interrupted publication\n";
}
