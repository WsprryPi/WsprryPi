// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "windows_service_host.hpp"
#include "windows_owner_relay.hpp"
#include <optional>
namespace wsprrypi {
inline std::optional<int> windows_controller_service_entry(int argc,char **argv,
    const std::function<int(const std::vector<std::string> &)> &application,const std::function<void()> &stop){
    bool selected=false;for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--native-controller-service")selected=true;
    if(!selected)return {};
    try{
        windows_file::require(argc==3&&std::string_view(argv[1])=="--native-controller-service"&&std::string_view(argv[2])=="--controller-only");
        recovery_windows::ControllerServiceHost host([&](const auto &invocation,HANDLE stop_event,const std::function<void()> &ready){
            if(::WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0)return 0;
            recovery_windows::OwnerRelay relay(invocation);
            if(::WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0)return 0;
            ready();
            if(invocation.mode==recovery_windows::service_detail::Mode::RecoveryOwner){
                for(;;){const auto state=::WaitForSingleObject(stop_event,50);if(state==WAIT_OBJECT_0)return 0;
                    windows_file::require(state==WAIT_TIMEOUT&&relay.alive());}
            }
            const auto owners=native_windows_controller_owners();const auto home=windows_file::path_from_utf8(owners.backend.home);
            const auto text=[](const std::filesystem::path &p){const auto value=p.u8string();return std::string(value.begin(),value.end());};
            std::atomic_bool relay_failed{false};std::jthread monitor([&](std::stop_token token){
                while(!token.stop_requested()){
                    try{if(!relay.alive()){relay_failed=true;stop();return;}}
                    catch(...){relay_failed=true;try{stop();}catch(...){std::terminate();}return;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
            const auto result=application({text(windows_program_root()/L"bin"/L"wsprrypi.exe"),"--controller-only","--state-directory",text(home/L"application-state"),"--runtime-directory",text(home/L"Run")});
            return relay_failed.load()?1:result;
        },stop);return host.dispatch();
    }catch(...){return 2;}
}
}
#endif
