// SPDX-License-Identifier: MIT
// Actual main with an explicitly selected simulated backend and an always-offline typed
// software transport. This fixture cannot open a device or network connection.
#include "wtp_endpoint/runtime.hpp"
#include "wtp_runtime_bridge.hpp"
#include "web_server.hpp"
#include <atomic>
#include <string_view>
int pi_managed_production_main(int,char **);

int main(int argc,char **argv){
    if(argc<4)return 2;
    bool selected=false;
    for(int i=2;i<argc;++i){
        const std::string_view option(argv[i]);
        if(option=="--backend"){
            if(selected||i+1>=argc||std::string_view(argv[++i])!="simulated")return 2;
            selected=true;
        }else if(option.starts_with("--backend=")){
            if(selected||option!="--backend=simulated")return 2;
            selected=true;
        }
    }
    if(!selected)return 2;
    try{
        wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
        webServer.bind_loopback_for_test();
    }catch(...){return 2;}
    const auto result=pi_managed_production_main(argc-1,argv+1);
    return result;
}
