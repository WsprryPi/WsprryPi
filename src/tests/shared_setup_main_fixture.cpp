// SPDX-License-Identifier: MIT
// Actual native Pi main with explicit simulated output and two exact Pico
// software management peers. No physical device or RF path can be selected.
#include "software_control_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_runtime_bridge.hpp"
#include "web_server.hpp"
#include "version.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/pico_target_controller.hpp"
#include <atomic>
#include <string_view>
int pi_managed_production_main(int,char **);

int main(int argc,char **argv){
    if(argc<6)return 2;
    for(int index=3;index<argc;++index)if(std::string_view(argv[index])=="--web-service"){
        wsprrypi::testing::SoftwareControlRuntime runtime(argv[1],argc,argv);
        argv[2]=argv[0];return pi_managed_production_main(argc-2,argv+2);
    }
    const std::string credentials=argv[2];std::string state;
    bool selected=false,initialization=false;
    for(int i=3;i<argc;++i){
        const std::string_view option(argv[i]);
        if(option=="--backend"){
            if(selected||i+1>=argc||std::string_view(argv[++i])!="simulated")return 2;
            selected=true;
        }else if(option.starts_with("--backend=")){
            if(selected||option!="--backend=simulated")return 2;
            selected=true;
        }else if(option=="--state-directory"&&state.empty()&&i+1<argc)state=argv[++i];
        else if((option=="--initialize-state"||option=="--migrate-state")&&!initialization)initialization=true;
        else if(option=="--ini-source"&&i+1<argc)++i;
        else if((option=="--web-port"||option=="--socket-port")&&i+1<argc)++i;
        else if(option=="--socket-loopback-only"){}
        else if(option=="--socket-loopback-family"&&i+1<argc&&std::string_view(argv[++i])=="ipv4"){}
        else return 2;
    }
    if((!selected&&!initialization)||state.empty())return 2;
    wsprrypi::testing::SoftwareControlRuntime control_runtime(argv[1],argc,argv);
    try{
        wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
        // Model the supported operator platform while every execution remains
        // explicitly simulated; the platform hook cannot select hardware.
        set_raspberry_pi_generation_override_for_test(4);
        webServer.bind_loopback_for_test();
    }catch(...){return 2;}
    wsprrypi::testing::set_pico_management_target_fixtures(
        [credentials](const wsprrypi::WtpCatalogProfile &profile){
            const auto &p=profile.settings;
            if(p.transport!="network"||(p.device_id!=std::string(32,'a')&&p.device_id!=std::string(32,'b'))||
                p.tcp_port!=(p.device_id.front()=='a'?18443:18444)||p.hostname!="wsprrypico-"+p.device_id+".local"||
                p.tls_ca!=credentials+"/"+p.device_id.substr(0,1)+"/client-ca.crt"||
                p.tls_certificate!=credentials+"/"+p.device_id.substr(0,1)+"/client.crt"||
                p.tls_key!=credentials+"/"+p.device_id.substr(0,1)+"/client.key")throw std::runtime_error("Only exact software management peers are admitted");
            return wsprrypi::PicoManagementTarget::Fixtures{{},{},{},true};
        });
    argv[2]=argv[0];
    const auto result=pi_managed_production_main(argc-2,argv+2);
    wsprrypi::testing::set_pico_management_target_fixtures({});
    return result;
}
