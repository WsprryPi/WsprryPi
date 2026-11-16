// SPDX-License-Identifier: MIT
// Actual controller HTTP/catalog/context/journal stack, no local output.
// Only the OS identity and loopback transport admission are typed fixture seams.
#include "controller_runtime.hpp"
#include "scheduling.hpp"
#include "version.hpp"
#include "web_server.hpp"
#include "wtp_integration/pico_target_controller.hpp"
#include <chrono>
#include <csignal>
#include <thread>
namespace {
volatile std::sig_atomic_t stopping{};
void stop(int){stopping=1;}

}
int main(int argc,char **argv) {
    if(argc<3)return 2;
    set_hardware_platform_detection_enabled(false);webServer.bind_loopback_for_test();
    const std::string credentials=argv[1];
    try {configure_controller_only(argc-1,argv+1);}catch(...){return 2;}
    wsprrypi::WtpCatalog catalog(config.ini_filename+".wtp-devices.json");
    auto controller=std::make_shared<wsprrypi::PicoTargetController>(catalog,[]{return std::optional<WtpSettings>{};},config.ini_filename+".management",std::string(32,'c'),
        [&](const wsprrypi::WtpCatalogProfile &profile) {
            const auto &s=profile.settings;
            if(s.transport!="network"||(s.device_id!=std::string(32,'a')&&s.device_id!=std::string(32,'b'))||
                (s.tcp_port!=18443&&s.tcp_port!=18444)||s.hostname!="wsprrypico-"+s.device_id+".local"||
                s.tls_ca!=credentials+"/"+s.device_id.substr(0,1)+"/client-ca.crt"||s.tls_certificate!=credentials+"/"+s.device_id.substr(0,1)+"/client.crt"||s.tls_key!=credentials+"/"+s.device_id.substr(0,1)+"/client.key")
                throw std::runtime_error("Fixture permits only the two exact software peers");
            return wsprrypi::PicoManagementTarget::Fixtures{{},{},{},true};
        });
    wsprrypi::testing::set_pico_target_controller(controller);
    std::signal(SIGTERM,stop);std::signal(SIGINT,stop);
    std::atomic_bool done{};std::thread monitor([&]{while(!done.load()&&!stopping)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(stopping)request_wspr_shutdown("pair fixture shutdown");});
    const bool result=controller_only_loop(std::string(32,'c'));done=true;monitor.join();
    wsprrypi::testing::set_pico_target_controller({});return result?0:1;
}
