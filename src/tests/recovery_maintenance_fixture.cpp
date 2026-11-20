// SPDX-License-Identifier: MIT
// Actual application/backend/web/maintenance entry points with private paths.
#include "software_control_runtime.hpp"
#include "application_recovery/paths.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "web_server.hpp"
#include "scheduling.hpp"
#include <filesystem>
#include <string_view>
int pi_managed_production_main(int,char **);
int main(int argc,char *argv[]){
    if(argc<7)return 2;
    const auto executable=std::filesystem::canonical(argv[0]);
    wsprrypi::testing::select_wtp_pi_device_identity(wsprrypi::PiManagementJournal::digest(argv[1]).substr(0,32));
    wsprrypi::RecoveryPaths paths;paths.root=std::string(argv[1])+".maintenance";paths.age=std::filesystem::canonical(argv[2]).string();
    paths.application_root=argv[3];paths.backend_runtime=argv[4];paths.runtime=argv[5];
    paths.worker=(executable.parent_path().parent_path().parent_path()/"application_recovery/worker.py").string();
    paths.backend_command={executable.string(),argv[1],argv[2],argv[3],argv[4],argv[5]};
    paths.software_backend_command=paths.backend_command;
    for(const auto &argument:std::vector<std::string>{"--state-directory",argv[3],"--runtime-directory",argv[4],"--backend","simulated","--socket-loopback-only","--socket-loopback-family","ipv4"})paths.software_backend_command.push_back(argument);
    for(int index=6;index<argc;++index)if(std::string_view(argv[index])=="--controller-only"||
        (std::string_view(argv[index])=="--role"&&index+1<argc&&std::string_view(argv[index+1])=="controller")){
        paths.software_backend_command.push_back("--controller-only");break;
    }
    bool controller=false;
    for(int index=6;index<argc;++index)if(std::string_view(argv[index])=="--controller-only")controller=true;
    std::vector<char *> forwarded{argv[0]};
    for(int index=6;index<argc;++index){
        if(std::string_view(argv[index])=="--fixture-listener-override"){
            if(index+2>=argc)return 2;
            for(const auto *option:{"--web-port","--socket-port"}){
                const std::string port=argv[++index];
                if(port.empty()||port.size()>5||port.find_first_not_of("0123456789")!=std::string::npos||std::stoi(port)<1024||std::stoi(port)>49151)return 2;
                paths.software_backend_command.push_back(option);paths.software_backend_command.push_back(port);
            }
            continue;
        }
        // The fixture retains explicit simulated-only admission below. The
        // real controller profile has no transmitter/backend/family options.
        if(controller&&(std::string_view(argv[index])=="--backend"||std::string_view(argv[index])=="--socket-loopback-family")){
            if(index+1>=argc)return 2;
            ++index;continue;
        }
        if(std::string_view(argv[index])=="--fixture-fault-phase"){
            if(!paths.software_worker_fault_phase.empty()||index+1>=argc)return 2;
            paths.software_worker_fault_phase=argv[++index];
        }else forwarded.push_back(argv[index]);
    }
    forwarded.push_back(nullptr);
    wsprrypi::testing::set_recovery_paths(std::move(paths));
    wsprrypi::testing::SoftwareControlRuntime runtime(argv[1],argc,argv);
    wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
    bool early=false,simulated=false;
    for(int index=6;index<argc;++index){
        const std::string_view option=argv[index];
        if(option=="--web-service"||option=="--maintenance-worker"||option=="--maintenance"||option=="--diagnostic-export"||option=="--recovery-backend"||option=="--migrate-state"||option=="--check-state"||option=="--initialize-state")early=true;
        if(option=="--backend"){
            if(simulated||index+1>=argc||std::string_view(argv[++index])!="simulated")return 2;
            simulated=true;
        }
    }
    if(!early&&!simulated)return 2;
    webServer.bind_loopback_for_test();set_scheduler_execution_suppressed_for_test(true);
    return pi_managed_production_main(static_cast<int>(forwarded.size())-1,forwarded.data());
}
