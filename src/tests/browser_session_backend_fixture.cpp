// SPDX-License-Identifier: MIT
// Actual application main and IPC owners. This fixture exercises the existing
// Test Tone lifecycle reservation/cleanup seam, not transmitter execution.
#include "software_control_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "web_server.hpp"
#include "scheduling.hpp"
#include "application_recovery/paths.hpp"
#include <string_view>
int pi_managed_production_main(int,char **);
int main(int argc,char *argv[]){
    if(argc<3)return 2;
    bool early=false,simulated=false,web=false;
    for(int index=2;index<argc;++index){
        const std::string_view option=argv[index];
        if(option=="--web-service"){early=true;web=true;}
        if(option=="--settings"||option=="--recovery-backend"||option=="--migrate-state"||option=="--check-state"||option=="--initialize-state")early=true;
        if(option=="--backend"){
            if(simulated||index+1>=argc||std::string_view(argv[++index])!="simulated")return 2;
            simulated=true;
        }else if(option.starts_with("--backend="))return 2;
    }
    if(!early&&!simulated)return 2;
    wsprrypi::testing::SoftwareControlRuntime runtime(argv[1],argc,argv);
    if(web)return pi_managed_production_main(argc-1,argv+1);
    auto recovery=wsprrypi::RecoveryPaths{};recovery.root=std::string(argv[1])+".maintenance";
    wsprrypi::testing::set_recovery_paths(std::move(recovery));
    wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
    webServer.bind_loopback_for_test();
    set_scheduler_execution_suppressed_for_test(true);
    return pi_managed_production_main(argc-1,argv+1);
}
