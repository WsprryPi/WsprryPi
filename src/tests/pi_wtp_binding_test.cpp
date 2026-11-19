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
void check(bool value){if(!value)throw std::runtime_error("Native/retired binding check "+std::to_string(checks+1));++checks;}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused);}

}
int main(){try{
    char pattern[]="/tmp/pi-wtp-binding-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{temporary};
    testing::set_wtp_pi_private_state_path(std::string(temporary)+"/revocation-v1");
    init_default_config();config.use_ini=true;config.ini_filename=std::string(temporary)+"/settings.ini";config.transmit=false;
    config.transmit_backend=TransmitBackendKind::SIMULATED;config.simulated_backend_override=true;
    config.wtp_server.enabled=true;
    std::string error;
    check(!start_wtp_pi_endpoint(config,&error)&&error.find("Migration")!=std::string::npos);
    check(!wtp_pi_management_observation());
    config.wtp_server.enabled=false;
    check(start_wtp_pi_endpoint(config,&error));struct Stop {~Stop(){stop_wtp_pi_endpoint();}} stop;
    auto before=wtp_pi_management_observation();check(before&&!before->wtp_binding);
    activate_wtp_pi_endpoint();const auto status=nlohmann::json::parse(wtp_pi_endpoint_status_json());
    check(status["enabled"]==false&&status["listener_running"]==false&&status["dns_sd_published"]==false);
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
    auto current=wtp_pi_management_observation();check(current&&!current->wtp_binding&&current->device_id==before->device_id);
    wtp_pi_withdraw_management_http(&other);check(!wtp_pi_management_observation()->wtp_binding);
    refuses([&]{wtp_pi_publish_management_http(&owner,self,0,[]{return true;});});check(!wtp_pi_management_observation()->wtp_binding);
    auto foreign=self;foreign[0]=foreign[0]=='0'?'1':'0';
    wtp_pi_publish_management_http(&owner,foreign,http_port,[&]{return http.is_running();});check(!wtp_pi_management_observation()->wtp_binding);
    wtp_pi_publish_management_http(&owner,self,http_port,[]{return false;});check(!wtp_pi_management_observation()->wtp_binding);
    wtp_pi_publish_management_http(&owner,self,http_port,[]()->bool{throw std::runtime_error("Artificial unavailable HTTP readiness");});check(!wtp_pi_management_observation()->wtp_binding);
    wtp_pi_publish_management_http(&owner,self,http_port,[&]{return http.is_running();});check(!wtp_pi_management_observation()->wtp_binding);
    // A retained read reservation cannot act on a stopped native owner.
    auto retained=wtp_pi_management_observation();check(retained&&!retained->wtp_binding);stop_wtp_pi_endpoint();
    check(!wtp_pi_management_observation()&&!retained->reserve_configuration());
    activate_wtp_pi_endpoint();check(!wtp_pi_management_observation());retained.reset();current.reset();before.reset();
    config.wtp_server.enabled=false;check(start_wtp_pi_endpoint(config,&error));activate_wtp_pi_endpoint();
    check(!wtp_pi_management_observation()->wtp_binding);stop_wtp_pi_endpoint();
    std::cout<<"PASS native authority plus actual management HTTP, retired WTP binding refusal, owner/readiness guards and stopped-owner lifetime: "<<checks<<" checks; no DNS-SD, hardware or RF\n";
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
