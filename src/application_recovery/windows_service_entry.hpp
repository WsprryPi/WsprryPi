// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "application_control/web_service.hpp"
#include "maintenance_service.hpp"
#include "windows_service_host.hpp"
#include "windows_owner_relay.hpp"
#include <iostream>
#include <optional>
#include <sstream>
namespace wsprrypi {
inline std::optional<int> windows_frontend_service_entry(int argc,char **argv){
    using Host=recovery_windows::ControllerServiceHost;
    const auto option=argc>1?std::string_view(argv[1]):std::string_view{};
    if(option!="--native-web-service"&&option!="--native-maintenance-service")return {};
    if(argc!=2)return 2;
    try{
        const bool web=option=="--native-web-service";
        Host host([&](const auto &,HANDLE stop_event,const std::function<void()> &ready){
            const auto stop_requested=[stop_event]{return ::WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT;};
            if(stop_requested())return 0;
            std::string program(argv[0]),web_option("--web-service"),maintenance_option("--maintenance-worker");
            std::string role_option("--role"),role("controller");
            if(web){
                char *arguments[]{program.data(),web_option.data(),nullptr};
                return web_service_main(2,arguments,stop_requested,ready).value_or(2);
            }
            char *arguments[]{program.data(),maintenance_option.data(),role_option.data(),role.data(),nullptr};
            return maintenance_service_main(4,arguments,stop_requested,ready).value_or(2);
        },[]{},web?Host::Role::Web:Host::Role::Maintenance);
        return host.dispatch();
    }catch(...){return 2;}
}

inline std::optional<int> windows_controller_service_entry(int argc,char **argv,
    const std::function<int(const std::vector<std::string> &)> &application,const std::function<void()> &stop){
    bool selected=false;for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--native-controller-service")selected=true;
    if(!selected)return {};
    try{
        windows_file::require(argc==3&&std::string_view(argv[1])=="--native-controller-service"&&std::string_view(argv[2])=="--controller-only");
        recovery_windows::ControllerServiceHost host([&](const auto &invocation,HANDLE stop_event,const std::function<void()> &ready){
            if(::WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0)return 0;
            const auto owners=native_windows_controller_owners();
            const auto home=windows_file::path_from_utf8(owners.backend.home);
            const auto text=[](const std::filesystem::path &path){const auto value=path.u8string();return std::string(value.begin(),value.end());};
            const auto program=text(windows_program_root()/L"bin"/L"wsprrypi.exe");
            const auto state=text(home/L"application-state");
            using Mode=recovery_windows::service_detail::Mode;
            if(invocation.mode==Mode::Initialize||invocation.mode==Mode::Inspect){
                const bool initialize=invocation.mode==Mode::Initialize;
                const auto gate=windows_controller_root()/L"Configuration"/L"admit-controller";
                const auto bytes=windows_file::read_protected(text(gate),false,128,L"S-1-5-18");
                windows_file::require(std::string_view(reinterpret_cast<const char *>(bytes.data()),bytes.size())==
                    (initialize?"wsprrypi-controller-bootstrap/1 initialize\n":"wsprrypi-controller-bootstrap/1 inspect\n"));
                ready();
                if(initialize)return application({program,"--controller-only","--initialize-state","--state-directory",state});
                std::ostringstream output;
                struct RestoreOutput {std::streambuf *previous;~RestoreOutput(){std::cout.rdbuf(previous);}} restore{std::cout.rdbuf(output.rdbuf())};
                const int result=application({program,"--controller-only","--check-state","--state-directory",state,"--listener-bootstrap"});
                if(result)return result;
                const auto data=output.str();
                windows_file::require(!data.empty()&&data.size()<=4096);
                const auto destination=home/L"Run"/L"installer-listeners.json";
                windows_file::ParentChain parents(destination,owners.backend.sid);
                windows_file::PrivateSecurity security(owners.backend.sid);
                windows_file::Handle file(::CreateFileW(windows_file::api_path(destination).c_str(),GENERIC_WRITE,0,
                    security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
                DWORD count{};
                windows_file::require(file.get()!=INVALID_HANDLE_VALUE&&::WriteFile(file.get(),data.data(),static_cast<DWORD>(data.size()),&count,nullptr)
                    &&count==data.size()&&::FlushFileBuffers(file.get()));
                return 0;
            }
            recovery_windows::OwnerRelay relay(invocation);
            if(::WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0)return 0;
            ready();
            if(invocation.mode==Mode::RecoveryOwner){
                for(;;){const auto state=::WaitForSingleObject(stop_event,50);if(state==WAIT_OBJECT_0)return 0;
                    windows_file::require(state==WAIT_TIMEOUT&&relay.alive());}
            }
            std::atomic_bool relay_failed{false};std::jthread monitor([&](std::stop_token token){
                while(!token.stop_requested()){
                    try{if(!relay.alive()){relay_failed=true;stop();return;}}
                    catch(...){relay_failed=true;try{stop();}catch(...){std::terminate();}return;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
            const auto result=application({program,"--controller-only","--state-directory",state,"--runtime-directory",text(home/L"Run"),"--socket-loopback-only"});
            return relay_failed.load()?1:result;
        },stop);return host.dispatch();
    }catch(...){return 2;}
}
}
#endif
