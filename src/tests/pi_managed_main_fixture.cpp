// SPDX-License-Identifier: MIT
// Actual production main with private ledger and loopback-only HTTP seams.
// Simulation must still be explicitly requested through the production CLI.
#include "software_control_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "web_server.hpp"
#include "application_simulation_fixture.hpp"
#include <string_view>
#ifdef __APPLE__
#include <pthread/qos.h>
#include <cstdio>
#endif
int pi_managed_production_main(int,char **);
int main(int argc,char **argv){
    if(argc<4)return 2;
#ifdef __APPLE__
    // This fixture verifies a strict 50 ms realtime launch boundary. Request
    // latency-sensitive scheduling before creating the simulated workers so
    // hosted macOS timer coalescing does not turn every slot into a late refusal.
    // No production thread policy or admission tolerance is changed.
    const int qos_error=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
    if(qos_error){std::fprintf(stderr,"Fixture QoS request failed: %d\n",qos_error);return 2;}
#endif
    bool simulated=false,state_command=false,web=false;
    for(int i=2;i<argc;++i){
        const std::string_view option(argv[i]);
        if(option=="--web-service"){state_command=true;web=true;}
        if(option=="--initialize-state"||option=="--migrate-state"||option=="--check-state"||option=="--route-maintenance")state_command=true;
        if(option=="--backend"){
            if(simulated||i+1>=argc||std::string_view(argv[++i])!="simulated")return 2;
            simulated=true;
        }else if(option.starts_with("--backend=")){
            if(simulated||option!="--backend=simulated")return 2;
            simulated=true;
        }
    }
    if(!simulated&&!state_command)return 2;
    wsprrypi::testing::SoftwareControlRuntime control_runtime(argv[1],argc,argv);
    if(web)return pi_managed_production_main(argc-1,argv+1);
    try{
        wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
        WsprTransmitter::SimulatedRuntimeConfig simulation;
        simulation.trace_path=std::string(argv[1])+".simulated-trace.json";
        if(simulated)wsprrypi::testing::set_application_simulation_fixture(simulation);
        webServer.bind_loopback_for_test();
    }catch(...){return 2;}
    return pi_managed_production_main(argc-1,argv+1);
}
