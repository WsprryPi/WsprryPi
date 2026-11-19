// SPDX-License-Identifier: MIT
#include "pi_managed_host_runtime.hpp"
#include "arg_parser.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "config_handler.hpp"
#include "controller_runtime.hpp"
#include "scheduling.hpp"
#include "wtp_pi_control.hpp"
#include "wtp_endpoint/identity.hpp"
#include "config_handler_serialization.hpp"
#include "web_server.hpp"
#include "web_server_routes.hpp"
#include "httplib.hpp"
#include "privileged_network_runtime.hpp"
#include "backend_http_guard.hpp"
#include "pi_recovery_runtime.hpp"
#include <chrono>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused,"Expected protected runtime refusal");}
void fixture(const std::string &ini){
    testing::set_wtp_pi_private_state_path(ini+".ledger");
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
    config.wspr.callsign=config.callsign;config.wspr.grid_square=config.grid_square;config.wspr.power_dbm=config.power_dbm;config.wspr.frequencies=config.frequencies;
    config.wtp.transport="network_plain";config.wtp.hostname="127.0.0.1";config.wtp.device_id=std::string(32,'b');config.wtp.tcp_port=31417;
    resolve_backend_specific_config(config);config_to_json();jConfig["Operation"]["Transmit Backend"]="gpio";
    {std::ofstream output(ini);output<<"[Security]\nPrivileged Network Safety = enforced\n";}
    check(!chmod(ini.c_str(),0600),"Private runtime fixture");iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
}
int free_port(int excluded=0){
    for(int port=38000;port<39000;++port){
        if(port==excluded)continue;
        const auto fd=socket(AF_INET,SOCK_STREAM,0);check(fd>=0,"Fixture port socket");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
        const bool available=bind(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address))==0;close(fd);
        if(available)return port;
    }
    throw std::runtime_error("No available fixture port");
}
void gateway(const nlohmann::json &valid,int port){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!webServer.isListening()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(webServer.isListening(),"Production recovery HTTP listener");
    httplib::Client client("127.0.0.1",port);client.set_read_timeout(2,0);client.set_write_timeout(2,0);
    const auto readiness=client.Get("/api/control/v1/host/readiness");
    check(readiness&&readiness->status==200&&nlohmann::json::parse(readiness->body)["state"]=="configuration_recovery","Protected production readiness route");
    const auto legacy=client.Get("/config");
    check(legacy&&legacy->status==409&&nlohmann::json::parse(legacy->body)["schema"]=="configuration-recovery","Legacy configuration returns an unconfirmed draft rather than defaults");
    const auto cache_buster=client.Get("/config?_=1234567890123");
    check(cache_buster&&cache_buster->status==409,"Existing jQuery cache-busting preserves the same protected recovery view");
    const auto unknown_query=client.Get("/config?other=1");
    check(unknown_query&&unknown_query->status==400,"No arbitrary legacy query is translated into a recovery request");
    const auto outside=client.Get("/config",{{std::string(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER),"203.0.113.42"}});
    check(outside&&outside->status==403,"Legacy recovery document enforces protected proxy-client admission");
    const auto cross_origin=client.Get("/api/control/v1/host/configuration-recovery",{{"Origin","http://outside.invalid"}});
    check(cross_origin&&cross_origin->status==403,"Recovery reads reject a foreign Origin");
    for(const auto *path:{"/status","/config/si5351-addresses","/api/wtp","/api/network-safety"}){
        const auto denied=client.Get(path);check(denied&&denied->status==503,"Legacy reads cannot fabricate output status or probe hardware during recovery");
    }
    const auto current=client.Get("/api/control/v1/host/configuration-recovery");check(current&&current->status==200,"Production recovery document");
    const auto oversized=client.Put("/api/control/v1/host/configuration-recovery",std::string(32769,'x'),"application/json");
    check(oversized&&oversized->status==413,"Oversized declared recovery input is rejected before buffering or mutation");
    const auto oversized_header=client.Get("/api/control/v1/host/readiness",{{"X-Fixture-Padding-A",std::string(4200,'x')},{"X-Fixture-Padding-B",std::string(4200,'x')}});
    check(oversized_header&&oversized_header->status==431,"Aggregate recovery headers are bounded before document access");
    const std::string chunked_data(32769,'x');
    const auto chunked=client.Put("/api/control/v1/host/configuration-recovery",[&](std::size_t offset,httplib::DataSink &sink){
        const auto size=std::min<std::size_t>(4096,chunked_data.size()-offset);
        if(size&&!sink.write(chunked_data.data()+offset,size))return false;
        if(offset+size==chunked_data.size())sink.done();
        return true;
    },"application/json");
    check(chunked&&chunked->status==413,"Unknown-length recovery input stops at the same body bound");
    auto doc=nlohmann::json::parse(current->body);auto configuration=config_handler_serialization::saved_configuration_from_internal_json(valid);
    for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})configuration["WTP"][key]="";
    const nlohmann::json command{{"contract","Wsprry-Control/1"},{"schema","configuration-recovery-save"},{"binding",doc["binding"]},
        {"capability_revision",doc["capability_revision"]},{"expected_revision",doc["revision"]},{"operation_id",std::string(32,'e')},{"configuration",configuration}};
    httplib::Headers headers{{"Origin","http://127.0.0.1:"+std::to_string(port)},{"X-WsprryPico-Request","1"},
        {"If-Match",current->get_header_value("ETag")}};
    auto response=client.Put("/api/control/v1/host/configuration-recovery",headers,command.dump(),"application/json");
    check(response&&response->status==403,"HTTP recovery requires a server-issued CSRF session");
    const auto session=client.Post("/api/control/v1/host/management-session",headers,R"({"contract":"Wsprry-Control/1","schema":"controller-browser-session"})","application/json");
    check(session&&session->status==200,"Production management session for recovery");
    headers.erase("Cookie");headers.emplace("Cookie",session->get_header_value("Set-Cookie").substr(0,session->get_header_value("Set-Cookie").find(';')));
    headers.emplace("X-WsprryPi-CSRF",session->get_header_value("X-WsprryPi-CSRF"));
    response=client.Put("/api/control/v1/host/configuration-recovery",headers,command.dump(),"application/json");
    check(response&&response->status==200&&!wtp_pi_management_ready(),"Protected production save persists without enabling output");
    auto replay=client.Put("/api/control/v1/host/configuration-recovery",headers,command.dump(),"application/json");
    const auto replay_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(replay&&replay->status==409&&nlohmann::json::parse(replay->body)["error"]["code"]=="capacity"&&
        std::chrono::steady_clock::now()<replay_deadline){
        // Automatic owning-parser readback may temporarily hold the shared
        // configuration gate. Retry only this identical accepted operation.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        replay=client.Put("/api/control/v1/host/configuration-recovery",headers,command.dump(),"application/json");
    }
    check(replay&&replay->status==200&&replay->body==response->body,"Actual HTTP lost-reply retry returns the exact durable receipt");
}
}
int main(int argc,char **argv){
 try{
    if(argc==1){
        for(const auto *mode:{"--valid","--unavailable-path","--invalid-http"}){
            const auto child=fork();check(child>=0,"Fork isolated process-owned fixture");
            if(!child){execl(argv[0],argv[0],mode,nullptr);_exit(127);}int status{};
            check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Production OS identity and runtime guard child");
        }
        std::cout<<"PASS: production OS identity, process-owned registry, constructor-failure read-only recovery, owner teardown hold, and no backend or role fallback\n";return 0;
    }
    check(argc==2,"Fixture mode");
    char pattern[]="/tmp/pi-managed-host-runtime-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private runtime directory");const std::string root=temporary;
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    const auto valid=root+"/native.ini";fixture(valid);
    const auto valid_configuration=jConfig;const bool invalid_http=std::string(argv[1])=="--invalid-http";
    if(invalid_http){auto invalid=jConfig;invalid["CW"]["Fade Shape"]="invalid";std::ofstream output(valid);output<<serialize_config_ini_projection_for_management(invalid);}
    const bool missing=std::string(argv[1])=="--unavailable-path";
    auto host=prepare_pi_managed_host(missing?root+"/missing/native.ini":valid);
    check(pi_managed_host_selected()&&!wtp_pi_management_ready()&&!controller_only_mode(),"Selection closes admission independently of role and backend");
    check(pi_managed_host_held(),"All selected unready owners enforce the route hold");
    for(const auto *path:{"/api/control/v1/host/readiness","/api/control/v1/host/configuration-recovery","/config","/version"})
        check(pi_managed_host_http_allowed("GET",path),"Recovery reads remain available");
    for(const auto *path:{"/config","/status","/control/stop","/api/control/v1/host/config","/api/control/v1/host/wtp-endpoint/recover","/api/control/v1/targets"})
        for(const auto *method:{"POST","PUT","DELETE"})
            check(!pi_managed_host_http_allowed(method,path),"Legacy actions and unrelated writes cannot bypass startup");
    check(!pi_managed_host_http_allowed("GET","/status")&&!pi_managed_host_http_allowed("GET","/api/control/v1/host/config"),
        "No fabricated idle state or defaults-as-saved configuration");
    check(pi_managed_host_http_allowed("POST","/api/control/v1/host/management-session")&&
        pi_managed_host_http_allowed("PUT","/api/control/v1/host/configuration-recovery")&&
        pi_managed_host_http_allowed("POST","/api/control/v1/host/configuration-recovery"),"Only the approved protected recovery writer remains routable");
    const auto readiness=[](){const auto result=pi_managed_host_recovery({"GET","/api/control/v1/host/readiness"},true);check(result.status==200,"Closed protected readiness document");return nlohmann::json::parse(result.body);};
    check(readiness()["device_id"]==wtp_pi_device_id()&&readiness()["role"]=="transmitter", "Automatic production identity is used without a fixture identity or role fallback");
    refuses([&]{prepare_pi_managed_host(valid);});
    if(missing){
        check(!host&&pi_managed_host_state()==PiHostReadiness::StorageRecovery,"Unavailable owning directory remains read-only");
        refuses([&]{iniFile.set_filename(valid);});refuses([&]{validate_config_ini_input(valid);});
        iniFile.set_string_value("CW","Message","UNCOORDINATED");
        refuses([&]{iniFile.save();});
        check(iniFile.hasPendingChanges(),"Refusing constructor-failure writer preserves the pending draft");
        const auto result=pi_managed_host_recovery({"GET","/api/control/v1/host/configuration-recovery"},true);
        const auto doc=nlohmann::json::parse(result.body);
        check(result.status==200&&doc["binding"].is_null()&&doc["writable"]==false&&doc["validation"]=="storage_unavailable"&&
            result.body.find(root)==std::string::npos,"Constructor failure has a redacted read-only view, never defaults asserted as saved");
    }else{
        check(bool(host),"Owning directory and OS identity are available");std::string error;check(load_json(valid,&error,nullptr)!=invalid_http,"Production prepared parser boundary");host->parsed();
        if(invalid_http){
            initialize_privileged_network_runtime("insecure-disabled");webServer.bind_loopback_for_test();
            config.web_port=free_port();config.socket_port=static_cast<uint16_t>(free_port(config.web_port));config.socket_loopback_only=true;
            const auto listening_port=config.web_port;
            PiRecoveryRuntime recovery;bool resumed=false;
            std::thread waiter([&]{resumed=recovery.await_configuration();});
            try{gateway(valid_configuration,listening_port);}
            catch(...){request_wspr_shutdown("Recovery fixture failed");waiter.join();throw;}
            waiter.join();
            check(resumed&&recovery.owns_http()&&recovery.owns_websocket()&&webServer.isListening()&&
                config.web_port!=listening_port&&host->ready_for_native_startup(),
                "Accepted HTTP save automatically resumes validated startup while retaining the original browser listeners");
            check(active_privileged_network_mode()==PrivilegedNetworkMode::enforced&&!wtp_pi_management_ready(),
                "Recovery enforces protection and restores the actual saved private policy before native readiness");
        }
        check(pi_managed_host()==host&&pi_managed_host_state()==PiHostReadiness::Starting&&!wtp_pi_management_ready(),"Saved validation alone does not qualify native startup");
        check(pi_managed_host_target_path("/api/control/v1/targets/"+host->device_id()+"/context"),"Local target routing uses server identity");
        if(!invalid_http){
            // Exercise the final handler without its earlier admission guard:
            // an owner can become held between those two boundaries.
            httplib::Server direct;
            web_server_routes::register_config(direct,[](httplib::Response &){});
            const auto port=direct.bind_to_any_port("127.0.0.1");check(port>0,"Direct legacy read fixture port");
            std::thread worker([&]{direct.listen_after_bind();});
            try{
                httplib::Client client("127.0.0.1",port);
                const auto reply=client.Get("/config");
                check(reply&&reply->status==503&&reply->get_header_value("Cache-Control")=="no-store"&&
                    nlohmann::json::parse(reply->body)["error"]["code"]=="authority_pending",
                    "Final ordinary handler refuses a held owner without disclosing a draft or stale settings");
            }catch(...){direct.stop();worker.join();throw;}
            direct.stop();worker.join();
        }
        host.reset();check(!pi_managed_host()&&readiness()["state"]=="storage_recovery"&&!wtp_pi_management_ready(),"An expired owner cannot reopen ordinary admission");
    }
    check(pi_managed_host_recovery({"GET","/api/control/v1/host/readiness"},false).status==403&&!config.transmit,"Unauthorized reads do not expose state or enable output");
    return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
