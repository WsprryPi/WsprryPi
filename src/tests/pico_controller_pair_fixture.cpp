// SPDX-License-Identifier: MIT
// Actual controller HTTP/catalog/context/journal stack, no local output.
// Only the OS identity and loopback transport admission are typed fixture seams.
#include "controller_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "scheduling.hpp"
#include "version.hpp"
#include "web_server.hpp"
#include "wtp_integration/pico_target_controller.hpp"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
volatile std::sig_atomic_t stopping{};
void stop(int){stopping=1;}

}
int main(int argc,char **argv) {
    if(argc<3)return 2;
    set_hardware_platform_detection_enabled(false);webServer.bind_loopback_for_test();
    const std::string credentials=argv[1];
    wsprrypi::testing::set_wtp_pi_private_state_path(credentials+"/fixture-ledger");
    try {configure_controller_only(argc-1,argv+1);}catch(...){return 2;}
    wsprrypi::WtpCatalog catalog(config.ini_filename+".wtp-devices.json");
    auto controller=std::make_shared<wsprrypi::PicoTargetController>(catalog,[]{return std::optional<WtpSettings>{};},config.ini_filename+".management",std::string(32,'c'),
        [&](const wsprrypi::WtpCatalogProfile &profile) {
            const auto &s=profile.settings;
            const auto credential=[&](const std::string &value,const char *name){
                return std::filesystem::path(value).make_preferred()==
                    (std::filesystem::path(credentials)/s.device_id.substr(0,1)/name).make_preferred();
            };
            if(s.transport!="network"||(s.device_id!=std::string(32,'a')&&s.device_id!=std::string(32,'b'))||
                (s.tcp_port!=18443&&s.tcp_port!=18444)||s.hostname!="wsprrypico-"+s.device_id+".local"||
                !credential(s.tls_ca,"client-ca.crt")||!credential(s.tls_certificate,"client.crt")||!credential(s.tls_key,"client.key"))
                throw std::runtime_error("Fixture permits only the two exact software peers");
            return wsprrypi::PicoManagementTarget::Fixtures{{},{},{},true};
        });
    wsprrypi::testing::set_pico_target_controller(controller);
    std::signal(SIGTERM,stop);std::signal(SIGINT,stop);
    std::atomic_bool done{};bool invalid_stop{};
    std::thread monitor([&]{
#ifdef _WIN32
        // Node cannot deliver a graceful POSIX signal on Windows. This fixed
        // fixture-only input invokes the same application shutdown path.
        std::string command;
#endif
        while(!done.load()&&!stopping){
#ifdef _WIN32
            DWORD available{};const auto input=::GetStdHandle(STD_INPUT_HANDLE);
            if(!::PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)){
                invalid_stop=::GetLastError()!=ERROR_BROKEN_PIPE;stopping=1;
            }else if(available){
                char value{};DWORD count{};
                if(!::ReadFile(input,&value,1,&count,nullptr)||count!=1){invalid_stop=true;stopping=1;}
                else{command+=value;if(command=="STOP\n")stopping=1;
                    else if(command.size()>=5||std::string_view("STOP\n").substr(0,command.size())!=command){invalid_stop=true;stopping=1;}}
            }
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(stopping)request_wspr_shutdown("pair fixture shutdown");
    });
    const bool result=controller_only_loop(std::string(32,'c'));done=true;monitor.join();
    wsprrypi::testing::set_pico_target_controller({});return result&&!invalid_stop?0:1;
}
