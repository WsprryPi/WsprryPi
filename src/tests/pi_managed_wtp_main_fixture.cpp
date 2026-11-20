// SPDX-License-Identifier: MIT
// Actual main with an explicitly selected simulated backend and private ledger.
// Only loopback management is available; no external execution transport exists.
#include "software_control_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_runtime_bridge.hpp"
#include "web_server.hpp"
#include <atomic>
#include <string_view>
int pi_managed_production_main(int,char **);

int main(int argc,char **argv){
    if(argc<4)return 2;
    bool selected=false,state_command=false,web=false;
    for(int i=2;i<argc;++i){
        const std::string_view option(argv[i]);
        if(option=="--web-service"){state_command=true;web=true;}
        if(option=="--initialize-state"||option=="--migrate-state"||option=="--check-state")state_command=true;
        if(option=="--backend"){
            if(selected||i+1>=argc||std::string_view(argv[++i])!="simulated")return 2;
            selected=true;
        }else if(option.starts_with("--backend=")){
            if(selected||option!="--backend=simulated")return 2;
            selected=true;
        }
    }
    if(!selected&&!state_command)return 2;
    wsprrypi::testing::SoftwareControlRuntime control_runtime(argv[1],argc,argv);
    if(web)return pi_managed_production_main(argc-1,argv+1);
    try{
        wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
        webServer.bind_loopback_for_test();
    }catch(...){return 2;}
    const auto result=pi_managed_production_main(argc-1,argv+1);
    return result;
}
