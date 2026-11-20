// SPDX-License-Identifier: MIT
#include "pi_recovery_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "application_storage/application_state.hpp"
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "privileged_network_runtime.hpp"
#include "privileged_network_admin.hpp"
#include "scheduling.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"
#include "logging.hpp"
#include <chrono>
#include <thread>
namespace wsprrypi {
PiRecoveryRuntime::~PiRecoveryRuntime(){
    if(monitor_)iniMonitor.stop();
    if(websocket_)socketServer.stop();
    if(http_)webServer.stop();
}
bool PiRecoveryRuntime::await_configuration(){
    if(entered_)throw std::logic_error("recovery_runtime_already_entered");
    entered_=true;
    if(!pi_managed_host_selected())return true;
    const auto host=pi_managed_host();
    if(host&&host->ready_for_native_startup())return true;
    // A saved preference to disable ordinary network safety cannot relax this
    // exceptional, protected recovery workflow. Do not persist that override.
    set_active_privileged_network_mode(PrivilegedNetworkMode::enforced);
    policy_override_=true;
    try{
        if(web_server_start_enabled(config)&&!application_database_selected()){
            webServer.start(config.web_port);http_=true;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!webServer.isListening()&&!exiting_wspr.load()&&std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if(!webServer.isListening())throw std::runtime_error("recovery_http_unavailable");
        }
        if(websocket_server_start_enabled(config)&&!application_database_selected()){
            if(!socketServer.start(config.socket_port,SOCKET_KEEPALIVE,config.socket_loopback_only,config.socket_loopback_family))
                throw std::runtime_error("recovery_websocket_unavailable");
            websocket_=true;
        }
        if(host&&!application_database_selected()){
            // MonitorFile inspects filesystem metadata only. Its callback never
            // opens/parses input, publishes defaults, or reaches runtime reload.
            const std::weak_ptr<PiManagedHost> weak=host;
            iniMonitor.filemon(config.ini_filename,[weak]{if(const auto owner=weak.lock())owner->signal_saved_configuration();});
            monitor_=true;
        }
        llog.logS(WARN,"Transmitter startup is held. Review the protected configuration recovery page.");
        std::unique_lock lock(exitwspr_mtx);
        while(!exitwspr_ready){
            lock.unlock();
            const bool ready=host&&host->consume_configuration_signal()&&host->retry_saved_configuration();
            lock.lock();
            if(ready){
                lock.unlock();
                if(monitor_){iniMonitor.stop();monitor_=false;}
                return true;
            }
            exitwspr_cv.wait_for(lock,std::chrono::milliseconds(100));
        }
    }catch(const std::exception &){
        llog.logS(ERROR,"Protected configuration recovery could not start or continue. Output remains closed.");
        return false;
    }
    return false;
}
void PiRecoveryRuntime::finish_native_startup(){
    if(!policy_override_)return;
    std::lock_guard lock(config_update_mutex());
    if(pi_managed_host_state()!=PiHostReadiness::Ready)return;
    if(application_database_selected()){
        const auto saved=database_configuration_input().settings;
        initialize_privileged_network_runtime(saved.private_settings.network_safety==SavedNetworkSafety::Enforced?
            "enforced":"insecure-disabled");
        policy_override_=false;return;
    }
    // Ordinary JSON persistence preserves private raw lines without retaining
    // them in the component's data map. Read the verified saved bytes instead
    // of accidentally replacing a deliberate Off preference with a default.
    std::optional<std::string> saved;
    const bool owned=read_owned_ini_snapshot(config.ini_filename,saved);
    const auto state=pi_managed_host_state();
    if(!owned||!saved||state!=PiHostReadiness::Ready)return;
    initialize_privileged_network_runtime(read_privileged_network_ini_value(*saved));policy_override_=false;
}
} // namespace wsprrypi
