// SPDX-License-Identifier: MIT
#include "wtp_endpoint/runtime.hpp"
#include "wtp_pi_control.hpp"
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "httplib.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){if(!value)throw std::runtime_error("Pi WTP listener binding check "+std::to_string(checks+1));++checks;}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused);}
bool open(unsigned port){
    const int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return false;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    const bool connected=connect(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address))==0;close(fd);return connected;
}
}
int main(){try{
    char pattern[]="/tmp/pi-wtp-binding-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{temporary};
    testing::set_wtp_pi_private_state_path(std::string(temporary)+"/revocation-v1");testing::set_wtp_pi_loopback_fixture(true);
    init_default_config();config.use_ini=true;config.ini_filename=std::string(temporary)+"/settings.ini";config.transmit=false;
    config.transmit_backend=TransmitBackendKind::SIMULATED;config.simulated_backend_override=true;config.wtp_server.enabled=true;config.wtp_server_port_override=0;
    std::string error;auto unsupported=config;unsupported.transmit_backend=TransmitBackendKind::WTP;
    check(!start_wtp_pi_endpoint(unsupported,&error)&&error.find("explicit simulated backend")!=std::string::npos);
    check(start_wtp_pi_endpoint(config,&error));struct Stop {~Stop(){stop_wtp_pi_endpoint();}} stop;
    auto before=wtp_pi_management_observation();check(before&&!before->wtp_binding);
    activate_wtp_pi_endpoint();const auto status=nlohmann::json::parse(wtp_pi_endpoint_status_json());
    check(status["listener_running"]==true&&status["dns_sd_published"]==false&&status["address"]=="127.0.0.1");
    const auto wtp_port=status["port"].get<unsigned>();check(wtp_port>0&&wtp_port<=65535&&open(wtp_port));
    check(!wtp_pi_management_observation()->wtp_binding);
    httplib::Server http;http.Get("/",[](const auto &,auto &reply){reply.set_content("ready","text/plain");});
    const auto http_port=http.bind_to_any_port("127.0.0.1");check(http_port>0);
    std::thread listener([&]{http.listen_after_bind();});
    struct StopHttp {httplib::Server &server;std::thread &thread;~StopHttp(){server.stop();if(thread.joinable())thread.join();}} stop_http{http,listener};
    for(unsigned attempt=0;attempt<1000&&!http.is_running();++attempt)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(http.is_running());
    httplib::Client client("127.0.0.1",http_port);const auto reply=client.Get("/");check(reply&&reply->status==200);
    const auto self=before->device_id;int owner{},other{};
    struct Withdraw {const void *owner;~Withdraw(){wtp_pi_withdraw_management_http(owner);}} withdraw{&owner};
    wtp_pi_publish_management_http(&owner,self,static_cast<unsigned>(http_port),[&]{return http.is_running();});
    auto current=wtp_pi_management_observation();check(current&&current->wtp_binding&&current->wtp_binding->wtp_port==wtp_port&&
        current->wtp_binding->management_port==static_cast<unsigned>(http_port)&&current->wtp_binding->revocation_generation==0);
    wtp_pi_withdraw_management_http(&other);check(wtp_pi_management_observation()->wtp_binding.has_value());
    refuses([&]{wtp_pi_publish_management_http(&owner,self,0,[]{return true;});});check(wtp_pi_management_observation()->wtp_binding.has_value());
    auto foreign=self;foreign[0]=foreign[0]=='0'?'1':'0';
    wtp_pi_publish_management_http(&owner,foreign,http_port,[&]{return http.is_running();});check(!wtp_pi_management_observation()->wtp_binding);
    wtp_pi_publish_management_http(&owner,self,http_port,[]{return false;});check(!wtp_pi_management_observation()->wtp_binding);
    wtp_pi_publish_management_http(&owner,self,http_port,[]()->bool{throw std::runtime_error("Artificial unavailable HTTP readiness");});check(!wtp_pi_management_observation()->wtp_binding);
    wtp_pi_publish_management_http(&owner,self,http_port,[&]{return http.is_running();});check(wtp_pi_management_observation()->wtp_binding.has_value());
    // A retained read reservation must not keep a stopped listener alive.
    auto retained=wtp_pi_management_observation();check(retained&&retained->wtp_binding);stop_wtp_pi_endpoint();
    check(!wtp_pi_management_observation()&&!open(wtp_port)&&!retained->reserve_configuration());
    activate_wtp_pi_endpoint();check(!open(wtp_port));retained.reset();current.reset();before.reset();
    config.wtp_server.enabled=false;check(start_wtp_pi_endpoint(config,&error));activate_wtp_pi_endpoint();
    check(!wtp_pi_management_observation()->wtp_binding);stop_wtp_pi_endpoint();testing::set_wtp_pi_loopback_fixture(false);
    std::cout<<"PASS actual simulated loopback WTP + HTTP listener qualification, port/counter/owner binding, readiness refusal and stopped-owner lifetime: "<<checks<<" checks; no DNS-SD, hardware or RF\n";
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
