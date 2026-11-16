// SPDX-License-Identifier: MIT
// Actual production main with private ledger and loopback-only HTTP seams.
// Simulation must still be explicitly requested through the production CLI.
#include "wtp_endpoint/runtime.hpp"
#include "web_server.hpp"
#include "application_simulation_fixture.hpp"
#include <string_view>
int pi_managed_production_main(int,char **);
int main(int argc,char **argv){
    if(argc<4)return 2;
    bool simulated=false;
    for(int i=2;i<argc;++i){
        const std::string_view option(argv[i]);
        if(option=="--backend"){
            if(simulated||i+1>=argc||std::string_view(argv[++i])!="simulated")return 2;
            simulated=true;
        }else if(option.starts_with("--backend=")){
            if(simulated||option!="--backend=simulated")return 2;
            simulated=true;
        }
    }
    if(!simulated)return 2;
    try{
        wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
        WsprTransmitter::SimulatedRuntimeConfig simulation;
        simulation.trace_path=std::string(argv[1])+".simulated-trace.json";
        wsprrypi::testing::set_application_simulation_fixture(simulation);
        webServer.bind_loopback_for_test();
    }catch(...){return 2;}
    return pi_managed_production_main(argc-1,argv+1);
}
