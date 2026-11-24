// SPDX-License-Identifier: MIT
#include "application_storage/companion_files.hpp"
#include "software_private_acl.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected companion refusal");}
void put(const std::string &path,const std::string &bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();check(bool(out)&&!chmod(path.c_str(),0600),"Private source fixture");}
}
int main(){
    try{
        char pattern[]="/tmp/companion-files-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const auto prefix=root+"/old.ini",target=std::string(32,'a');
        prepare_companion_directory(prefix+".management");
        const auto path=prefix+".management/"+target+".json";
        const auto payload=std::string(2*1048576+37,'x');put(path,payload);
        auto captured=inspect_companion_file(prefix,CompanionKind::TargetManagement,target);check(captured.has_value(),"Capture existing journal");
#ifdef __APPLE__
        set_software_other_read_acl(path.c_str(),true);
        refuses([&]{inspect_companion_file(prefix,CompanionKind::TargetManagement,target);});
        set_software_other_read_acl(path.c_str(),false);
        set_software_other_read_acl((prefix+".management").c_str(),true);
        refuses([&]{inspect_companion_file(prefix,CompanionKind::TargetManagement,target);});
        set_software_other_read_acl((prefix+".management").c_str(),false);
        captured=inspect_companion_file(prefix,CompanionKind::TargetManagement,target);check(captured.has_value(),"Private journal restored after ACL repair");
#endif
        check(captured->size==payload.size(),"Journal larger than a settings record is supported");
        const auto manifest=companion_manifest({*captured});check(parse_companion_manifest(manifest)==std::vector{*captured},"Closed bounded manifest round trip");
        const auto destination=root+"/copied";
        copy_companion_file(path,destination,*captured,true);verify_companion_file(destination,*captured);
        copy_companion_file(path,destination,*captured,true);
        auto invalid=manifest;invalid[0]["target"]="../other";refuses([&]{parse_companion_manifest(invalid);});
        invalid=manifest;invalid[0]["kind"]="arbitrary-file";refuses([&]{parse_companion_manifest(invalid);});
        invalid=manifest;invalid[0]["extra"]=true;refuses([&]{parse_companion_manifest(invalid);});
        refuses([&]{companion_manifest({*captured,*captured});});
        put(path,std::string(payload.size(),'z'));
        refuses([&]{copy_companion_file(path,root+"/changed",*captured,true);});
        check(!std::filesystem::exists(root+"/changed")&&!std::filesystem::exists(root+"/changed.writing"),"Changed input publishes no destination");
        const auto interrupted=root+"/interrupted";
        put(interrupted+".writing",payload);check(!::link((interrupted+".writing").c_str(),interrupted.c_str()),"Crash between publication and staging unlink");
        copy_companion_file(path,interrupted,*captured,true);verify_companion_file(interrupted,*captured);
        check(!std::filesystem::exists(interrupted+".writing"),"Exact interrupted publication finishes idempotently without rereading changed source");
        const auto unsafe=root+"/unsafe";check(!::symlink(destination.c_str(),unsafe.c_str()),"Symlink fixture");
        refuses([&]{copy_companion_file(path,unsafe,*captured,true);});
        check(!::mkfifo((prefix+".usb-logs.json").c_str(),0600),"FIFO source fixture");
        refuses([&]{inspect_companion_file(prefix,CompanionKind::UsbLogging);});
        check(!::unlink((prefix+".usb-logs.json").c_str()),"Remove FIFO");
        check(!inspect_companion_file(prefix,CompanionKind::UsbLogging),"Absence stays absence");
        std::cout<<"PASS: bounded streaming companion copies, source fencing, manifest closure, interrupted publication and unsafe paths\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
