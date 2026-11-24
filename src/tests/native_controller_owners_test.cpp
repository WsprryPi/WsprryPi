// SPDX-License-Identifier: MIT
#include "native_controller_owners.hpp"
#include "native_controller_paths.hpp"
#include <filesystem>
#include <iostream>
using namespace wsprrypi;
void check(bool value){if(!value)throw std::runtime_error("Native owner contract failed");}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception&){refused=true;}check(refused);}
int main(){try{
    const auto paths=posix_controller_paths();
    for(const auto path:{paths.runtime,paths.maintenance_runtime,paths.maintenance_root,paths.application_root,
        paths.web_cache,paths.python,paths.worker,paths.age,paths.backend})
        check(std::filesystem::path(path).is_absolute());
#ifdef __APPLE__
    const std::string home="/Library/Application Support/WsprryPi/Controller",web_home="/Library/Application Support/WsprryPi/Web";
    check(std::string(paths.runtime)=="/Library/Application Support/WsprryPi/Run/Backend"&&
        std::string(paths.maintenance_runtime)=="/Library/Application Support/WsprryPi/Run/Maintenance");
    check(std::string(paths.application_root)==home+"/application-state"&&std::string(paths.web_cache)==web_home+"/listeners.json");
#else
    const std::string home="/var/lib/wsprrypi/controller",web_home="/var/lib/wsprrypi-web";
    check(std::string(paths.runtime)=="/run/wsprrypi"&&std::string(paths.application_root)=="/var/lib/wsprrypi/application-state");
#endif
    NativeControllerAccount backend{home,1001,1001},web{web_home,1002,1002},http{"/var/empty",1003,1003};
    check(!select_native_controller_owners({},{},http));
    const auto owners=select_native_controller_owners(backend,web,http);check(owners&&owners->backend.uid==1001&&owners->web.gid==1002);
    refuses([&]{select_native_controller_owners(backend,{},http);});
    refuses([&]{select_native_controller_owners({},web,http);});
    refuses([&]{select_native_controller_owners(backend,web,{});});
    for(unsigned kind=0;kind<3;++kind){
        auto b=backend,w=web,h=http;auto &owner=kind==0?b:kind==1?w:h;owner.uid=0;
        refuses([&]{select_native_controller_owners(b,w,h);});
        owner.uid=1000;owner.gid=0;refuses([&]{select_native_controller_owners(b,w,h);});
    }
    auto copied=web;copied.uid=backend.uid;refuses([&]{select_native_controller_owners(backend,copied,http);});
    copied=web;copied.gid=backend.gid;refuses([&]{select_native_controller_owners(backend,copied,http);});
    copied=http;copied.gid=backend.gid;refuses([&]{select_native_controller_owners(backend,web,copied);});
    copied=http;copied.gid=web.gid;refuses([&]{select_native_controller_owners(backend,web,copied);});
    copied=backend;copied.home+="/other";refuses([&]{select_native_controller_owners(copied,web,http);});
    copied=web;copied.home="/tmp";refuses([&]{select_native_controller_owners(backend,copied,http);});
    const auto self=native_controller_account(nullptr);check(self&&self->uid==::geteuid());
    std::cout<<"PASS distinct native backend/web/HTTP owners, fixed homes, no root groups, and partial-installation refusal\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
