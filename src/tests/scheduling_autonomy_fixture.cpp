// SPDX-License-Identifier: MIT
// Production main/scheduler/management/storage/simulated execution. Only OS
// identity, target-local clock health, loopback binding and private paths are
// fixture seams. No production CLI, environment or INI bypass is added.
#define wtp_pi_device_id phase7_os_device_id
#include "software_control_runtime.hpp"
#include "../wtp_endpoint/identity.cpp"
#undef wtp_pi_device_id
#include "wtp_endpoint/runtime.hpp"
#include "web_server.hpp"
#include "native_recurrence.hpp"
#include "application_simulation_fixture.hpp"
#include <chrono>
namespace {std::string fixture_self;}
namespace wsprrypi {
std::string wtp_pi_device_id(){return fixture_self;}
std::string wtp_pi_device_id(const std::string &path){return phase7_os_device_id(path);}
}
#include "Singleton/src/singleton.hpp"
// A separate real loopback UDP lease represents each isolated device. The
// ordinary application keeps its fixed host-wide lease; no production bypass.
class Phase7Singleton {
    SingletonProcess lease_{0};
public:
    explicit Phase7Singleton(int){}
    bool operator()(){return lease_();}
};
#define SingletonProcess Phase7Singleton
#define main pi_managed_production_main
#include "../main.cpp"
#undef main
#undef SingletonProcess
int main(int argc,char **argv){
    if(argc<4)return 2;
    fixture_self=argv[2];
    if(fixture_self.size()!=32||fixture_self.find_first_not_of("0123456789abcdef")!=std::string::npos)return 2;
    wsprrypi::testing::select_wtp_pi_device_identity(fixture_self);
    bool simulated=false,state_command=false,web=false;
    for(int i=3;i<argc;++i){
        const std::string_view option=argv[i];
        if(option=="--web-service"){state_command=true;web=true;}
        if(option=="--initialize-state"||option=="--migrate-state"||option=="--check-state")state_command=true;
        if(option=="--backend"){
        if(simulated||i+1>=argc||std::string_view(argv[++i])!="simulated")return 2;
        simulated=true;
        }
    }
    if(!simulated&&!state_command)return 2;
    wsprrypi::testing::SoftwareControlRuntime control_runtime(argv[1],argc,argv);
    if(web)return pi_managed_production_main(argc-2,argv+2);
    wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
    WsprTransmitter::SimulatedRuntimeConfig simulation;simulation.trace_path=std::string(argv[1])+".simulated-trace.json";
    if(simulated)wsprrypi::testing::set_application_simulation_fixture(simulation);webServer.bind_loopback_for_test();
    wsprrypi::testing::set_native_clock_reader([](bool){
        wsprrypi::SolarClockObservation result;result.state=wsprrypi::SolarClockState::Synchronized;
        result.utc_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        result.generation=1;result.maximum_error_ns=1000;result.healthy_sample_this_boot=true;return result;
    });
    return pi_managed_production_main(argc-2,argv+2);
}
