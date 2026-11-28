// SPDX-License-Identifier: MIT
#include "maintenance_service.hpp"
#include "backend_capabilities.hpp"
#ifdef _WIN32
#include "native_windows_owners.hpp"
#include "windows_controller_service.hpp"
#include "job_lock.hpp"
#include "signal_handler.hpp"
#else
#include "native_controller_owners.hpp"
#endif
#include "application_storage/controller_identity.hpp"
#include "maintenance_domain.hpp"
#include "paths.hpp"
#include "application_control/http_sessions.hpp"
#include "application_control/runtime_options.hpp"
#include "application_storage/companion_files.hpp"
#include "wtp_endpoint/identity.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <pwd.h>
#include <unistd.h>
#endif
#include <string_view>
#include <thread>
#include <atomic>
namespace wsprrypi {
namespace {
#ifdef _WIN32
std::atomic_bool stopping{false};
#else
volatile std::sig_atomic_t stopping{};
void stop(int){stopping=1;}
#endif
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
}
LocalTransportOptions maintenance_control_options(MaintenancePeer peer){
    const auto paths=recovery_paths();
#ifdef _WIN32
    (void)peer;
    if(testing::application_control_runtime_fixture_selected())return {paths.runtime,windows_file::current_sid(),L"",LocalChannel::Control,{},false};
    const auto owners=native_windows_controller_owners();
    return {paths.runtime,owners.maintenance.sid,owners.web.sid,LocalChannel::Control,owners.backend.sid,true};
#else
    const bool browser=peer==MaintenancePeer::Browser;

#if WSPRRYPI_CONTROLLER_ONLY
    if(!testing::application_control_runtime_fixture_selected())if(const auto owners=native_controller_owners()){
        const bool issuer=peer==MaintenancePeer::BackendSessionIssuer;
        const auto self=::geteuid();
        if(self!=0&&!(browser&&self==owners->web.uid)&&!(issuer&&self==owners->backend.uid))
            throw LocalControlError(LocalControlError::Code::Unauthorized);
        return {paths.runtime,uid_t{0},owners->web.uid,owners->web.gid,LocalChannel::Control,
                issuer?std::optional<uid_t>{owners->backend.uid}:std::nullopt};
    }
#endif
    const auto user=::getpwnam("www-data");const auto self=::geteuid();
    const auto web=user?user->pw_uid:static_cast<uid_t>(-1);
    return {paths.runtime,browser&&self==web?uid_t{0}:self,web,self==0&&user?user->pw_gid: ::getegid()};
#endif
}
std::optional<int> maintenance_service_main(int argc,char *argv[]){
    return maintenance_service_main(argc,argv,{},{});
}

std::optional<int> maintenance_service_main(int argc,char *argv[],const std::function<bool()> &stop_requested,
                                           const std::function<void()> &ready){
    bool selected=false;for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--maintenance-worker")selected=true;
    if(!selected)return {};
    try{
        // Role is an installation decision. It never follows GPIO availability,
        // uploaded state, a missing DB, or hardware initialization failure.
        require(argc==4&&std::string_view(argv[1])=="--maintenance-worker"&&std::string_view(argv[2])=="--role");
        const std::string role=argv[3];require(role=="pi"||role=="controller");
        const auto paths=recovery_paths();
#ifdef __linux__
        require(::geteuid()==0||!paths.software_backend_command.empty());
#elif defined(_WIN32)
        if(paths.software_backend_command.empty()){
#if WSPRRYPI_CONTROLLER_ONLY
            require(role=="controller"&&windows_file::current_sid()==native_windows_controller_owners().maintenance.sid);
            recovery_windows::service_detail::unprivileged();
#else
            require(false);
#endif
        }
#elif defined(__APPLE__)
        if(paths.software_backend_command.empty()){
#if WSPRRYPI_CONTROLLER_ONLY
            require(role=="controller"&&::geteuid()==0&&native_controller_owners().has_value());
#else
            require(false);
#endif
        }
#else
        require(!paths.software_backend_command.empty());
#endif
        if(role=="controller")configure_native_controller_identity();
        prepare_companion_directory(paths.root);
#ifdef _WIN32
        RecoveryJobLock root_lock(windows_file::path_from_utf8(paths.root),true,RecoveryJobLock::NativeLibrary{paths.native_library});
#else
        const int lock=::open(paths.root.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct Lock {int descriptor;~Lock(){if(descriptor>=0)::close(descriptor);}} root_lock{lock};
        require(lock>=0&&!::flock(lock,LOCK_EX|LOCK_NB));
#endif
        if(stop_requested&&stop_requested())return 0;
        RecoveryWorkerProcess worker(paths,wtp_pi_device_id(),role);
        RecoveryBrowserAdmission admission(paths.root,wtp_pi_device_id(),SupportRequestGuard::discover_local_networks);
        RecoveryMaintenanceDomain router(paths,worker,admission);
        const auto stop_now=[&]{return stopping||(stop_requested&&stop_requested());};
        LocalHttpSessions sessions([&](auto input){return router.dispatch(std::move(input));},[&]{return !stop_now();});
        const auto boot=WtpPiBootIdentity{}.new_boot_id();
        auto options=maintenance_control_options(MaintenancePeer::BackendSessionIssuer);
        LocalControlServer control(options,boot,[&](LocalPeer peer,const LocalRequest &request){
            require(peer==LocalPeer::Administrator&&request.operation==LocalOperation::RecoverySessionIssue&&request.arguments.is_object()&&request.arguments.size()==2&&
                request.arguments.contains("host")&&request.arguments["host"].is_string()&&request.arguments.contains("secure")&&request.arguments["secure"].is_boolean());
            return admission.issue(request.arguments["host"].get<std::string>(),request.arguments["secure"].get<bool>());
        });
        options=maintenance_control_options();
        options.channel=LocalChannel::Recovery;
        LocalControlServer administrator(options,boot,[&](LocalPeer peer,const LocalRequest &request){
            require(peer==LocalPeer::Administrator&&request.operation==LocalOperation::RecoveryCommand);
            const auto result=worker.request(request.arguments);
            if(result["ok"]==true)return result["value"];
            const auto code=result["error"].get<std::string>();
            throw LocalCommandRefused(code=="busy"?LocalRefusal::Busy:code=="invalid_request"?LocalRefusal::InvalidRequest:LocalRefusal::StorageUnavailable);
        });
        options.channel=LocalChannel::Http;
        LocalControlServer http(options,boot,[&](LocalPeer,const LocalRequest &request){return sessions.request(request);},
            [&](LocalPeer,const std::string &connection){sessions.disconnected(connection);});
#ifdef _WIN32
        stopping=false;SignalHandler signals;signals.setCallback([](int,bool){stopping=true;});signals.start();require(signals.running());
        struct StopSignals {SignalHandler &value;~StopSignals(){if(!value.stop())std::terminate();}} stop_signals{signals};
#else
        stopping=0;const auto previous_term=std::signal(SIGTERM,stop),previous_int=std::signal(SIGINT,stop);
        struct RestoreSignals {decltype(previous_term) term,interrupt;~RestoreSignals(){std::signal(SIGTERM,term);std::signal(SIGINT,interrupt);}}restore{previous_term,previous_int};
#endif
        if(stop_now())return 0;
        control.start();administrator.start();http.start();
        if(ready)ready();
        while(!stop_now()){
            if(!worker.alive())throw LocalControlError(LocalControlError::Code::UnknownOutcome);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        http.stop();administrator.stop();control.stop();return 0;
    }catch(...){std::cerr<<"WsprryPi maintenance worker is unavailable; its private recovery state has been retained.\n";return 2;}
}
}
