// SPDX-License-Identifier: MIT
#include "wtp_integration/controller_retirement.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Expected storage-binding refusal");}
void put(const std::string &path,const std::string &value){std::ofstream out(path);out<<value;out.close();check(bool(out)&&!chmod(path.c_str(),0600),"Private fixture assignment file");}
}
int main(){
    try {
        char pattern[]="/tmp/retirement-storage-binding-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture root");
        const auto root=std::filesystem::canonical(temporary).string();
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const auto old_source=root+"/old.ini.wtp-assignments.json",new_source=root+"/configuration.wtp-assignments.json",directory=root+"/authority";
        const std::string self(32,'a');const auto rows=nlohmann::json::array();
        const auto body=nlohmann::json{{"version",1},{"assignments",rows},{"removals",rows}}.dump();
        put(old_source,body);
        {
            ControllerRetirementBook original(directory,old_source,self,rows);
            (void)original.read("first-process",rows);
        }
        const auto before=PiManagementJournal::read_private_record(directory+"/authority.json");
        const auto anchor=PiManagementJournal::read_private_record(directory+".established");
        check(before&&anchor,"Existing established authority is present");
        put(new_source,body);check(!::unlink(old_source.c_str()),"Original assignment pathname retired");
        refuses([&]{ControllerRetirementBook wrong(directory,new_source,self,rows);});
        {
            ControllerRetirementBook relocated(directory,new_source,self,rows,{}, {},PiManagementJournal::digest(old_source));
            (void)relocated.read("new-process",rows);
        }
        check(PiManagementJournal::read_private_record(directory+"/authority.json")->bytes==before->bytes&&
            PiManagementJournal::read_private_record(directory+".established")->bytes==anchor->bytes,
            "Relocation preserves exact established fences and immutable source identity without reopening the old path");
        refuses([&]{ControllerRetirementBook wrong(directory,new_source,self,rows,{}, {},std::string(64,'f'));});
        refuses([&]{ControllerRetirementBook malformed(directory,new_source,self,rows,{}, {},"not-a-binding");});
        std::cout<<"PASS: assignment relocation preserves established retirement authority, rejects changed bindings and does not read retired source paths\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
