// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_connect.hpp"
#include "wtp_integration/pi_pair_window.hpp"
#include "wtp_integration/pi_pair_server.hpp"
#include "wtp_integration/pi_pair_guarded_http.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/browser_http.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <thread>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
std::atomic_uint checks{};unsigned sequence{};
void check(bool value,const std::source_location at=std::source_location::current()){
    const auto count=++checks;if(!value)throw std::runtime_error("pair connect check "+std::to_string(count)+" at "+std::to_string(at.line()));}
std::string id(char c){return std::string(32,c);}
std::string op(){const auto value=std::to_string(++sequence);return std::string(32-value.size(),'0')+value;}
const SupportRequestGuardSnapshot network{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}};
struct Local {BrowserRequest request;std::optional<LocalBrowserPrincipal> principal;};
Local local(ManagementBrowserSessions &sessions,const std::string &path,const Json &body){
    httplib::Request command;command.method="POST";command.path=path;command.target=path;command.remote_addr="192.168.1.20";
    command.headers={{"Host","device.local"},{"Origin","http://device.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
    command.body=body.dump();auto bootstrap=command;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;
    httplib::Response response;check(sessions.establish(bootstrap,response));const auto cookie=response.get_header_value("Set-Cookie");
    command.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));command.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
    auto principal=sessions.authorize_local_mutation(command,network);check(principal.has_value());
    return {*wtp_browser_http_request(command),std::move(principal)};
}
Local window(ManagementBrowserSessions &sessions,const std::string &generation,const char *action){return local(sessions,
    "/api/control/v1/host/pairing/window",{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",op()},
    {"action",action},{"expected_authorization_generation",generation}});}
}
int main(int argc,char **argv){try{
    const bool crash=argc==4&&std::string(argv[1])=="--crash";
    char pattern[]="/tmp/pi-pair-connect-XXXXXX";const auto temporary=crash?argv[2]:mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    PiPairHistory target(root+"/target",id('a'));auto controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));
    ManagementBrowserSessions target_sessions,controller_sessions;
    auto establish=window(controller_sessions,"0","recover");controller->recover_access(establish.request,*establish.principal,controller_sessions);
    if(crash){const auto point=std::stoul(argv[3]);controller.reset();
        controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'),[point](auto stage){if(static_cast<unsigned>(stage)==point)std::_Exit(73);});}
    else controller->begin_service();
    PiPairWindow target_window(target,target_sessions,id('a'),id('c'),[&](const auto &proof){return target.commit_pair(proof);});
    std::atomic_bool block_readback{false},lose_commit_reply{false},drop_commit{false};std::atomic_uint commit_requests{},hello_requests{};
    std::atomic_uint operator_pause{};
    PiPairAdmission target_admission([&]{return std::chrono::steady_clock::now()+std::chrono::seconds(operator_pause.load());}),controller_admission;
    PiPairGuardedHttpServer http(target_admission);
    const auto bootstrap_handler=[&](const httplib::Request &request,httplib::Response &response){
        const auto principal=authorize_pi_pair_bootstrap(request,network);check(principal.has_value());
        if(request.path.ends_with("/hello"))++hello_requests;
        if(request.path.ends_with("/commit")){++commit_requests;if(drop_commit){response.status=503;return;}}
        auto result=target_window.bootstrap(*wtp_browser_http_request(request),*principal);
        if(request.path.ends_with("/commit")&&lose_commit_reply){response.status=503;return;}
        response.status=result.status;response.set_content(result.body,"application/json");
    };
    http.Get("/api/pair/v1/.*",bootstrap_handler);http.Post("/api/pair/v1/.*",bootstrap_handler);
    const auto web_port=http.bind_to_any_port("127.0.0.1");check(web_port>0);std::thread web([&]{http.listen_after_bind();});
    struct Join {PiPairGuardedHttpServer &server;std::thread &worker;~Join(){server.stop();if(worker.joinable())worker.join();}} join{http,web};http.wait_until_ready();
    PiPairServer tls(target_admission,[&](const auto &identity){return target.management_credential(identity);},
        [&](const auto &principal){return target.accept_finished(principal)==PiPairHistory::ManagementAccess::Current;},
        [&](const auto &principal,const httplib::Request &request,auto &,const auto &){
            if(block_readback)throw std::runtime_error("fixture withheld readback");
            if(request.path=="/api/pair/v1/rotate/prepare"){
                const auto value=target.rotate(principal,Json::parse(request.body),target_sessions);check(value.has_value());
                return PiPairHttpReply{{200,value->dump(),{}},{},{}};
            }
            const auto value=target.pair_operation(principal,request.path.substr(request.path.size()-32));check(value.has_value());
            return PiPairHttpReply{{200,value->dump(),{}},{},{}};
        },PiPairServer::Access::LoopbackTest);check(tls.start());
    PiPairDiscovery discovery(id('b'));discovery.available(true);
    const auto host="wsprry-management-"+id('a')+".local.";
    check(discovery.resolved({1,"Target Pi"},"local",host,31418,pi_pair_discovery_txt(id('a'),id('c'),web_port)));
    const auto locator=discovery.snapshot()["discoveries"][0]["discovery_id"];
    const PiPairConnect::LoopbackTest endpoints{{"127.0.0.1",static_cast<unsigned>(web_port)},{"127.0.0.1",tls.port()}};
    auto actor=std::make_unique<PiPairConnect>(*controller,controller_sessions,discovery,controller_admission,id('b'),id('d'),endpoints);
    bool target_service_started=false;
    const auto open=[&](const std::string &generation,const char *action){operator_pause+=3;auto request=window(target_sessions,generation,action);
        const auto result=target_window.open(request.request,*request.principal);check(result.status==200);
        if(!target_service_started){target.begin_service();target_service_started=true;}
        return Json::parse(result.body)["code"].get<std::string>();};
    const auto connect=[&](const std::string &code,const std::string &operation){auto request=local(controller_sessions,"/api/control/v1/host/pairing/connect",
        {{"contract","Wsprry-Control/1"},{"schema","pair-connect"},{"operation_id",operation},{"discovery_id",locator},{"expected_device_id",id('a')},{"code",code}});
        return actor->connect(request.request,*request.principal);};
    const auto first_code=open("0","recover"),first_operation=op();lose_commit_reply=true;
    const auto first=connect(first_code,first_operation);check(first.status==200&&Json::parse(first.body)["state"]=="committed"&&commit_requests==1);
    const auto local_receipt=[&](const std::string &operation){
        httplib::Request request;request.method="GET";request.path="/api/control/v1/host/pairing/operations/"+operation;request.target=request.path;
        request.remote_addr="192.168.1.20";request.headers={{"Host","device.local"}};
        const auto principal=authorize_local_pairing_read(request,network);check(principal.has_value());
        return controller->local_pairing_receipt(*wtp_browser_http_request(request),*principal);
    };
    check(Json::parse(local_receipt(first_operation).body)==Json::parse(first.body));

    check(!crash); // Every requested crash must occur before target commit.
    lose_commit_reply=false;auto saved=controller->snapshot();auto peers=parse_pi_pair_peers(saved.peers->bytes,id('b'));
    check(peers["profiles"].size()==1&&peers["profiles"][0]["stage"]=="active"&&peers["uncertain"].empty());
    const auto profile=peers["profiles"][0]["profile_id"].get<std::string>();
    check(peers["profiles"][0]["pair"]["current"]["tls_psk"]==parse_pi_pair_catalog(target.snapshot().catalog.bytes,id('a'))["active"]["current"]["tls_psk"]);
    check(saved.peers->bytes.find(first_code)==std::string::npos&&saved.operations.bytes.find("\"code\"")==std::string::npos);
    check(!controller->outgoing_renewal_due(profile));
    actor.reset();controller.reset();controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));
    check(controller->unclean_start());controller->begin_service();check(controller->outgoing_renewal_due(profile));
    controller->finish_service();check(Json::parse(controller->snapshot().authority->bytes)["clean_shutdown"]==false);
    controller->begin_service();check(controller->outgoing_renewal_due(profile));
    actor=std::make_unique<PiPairConnect>(*controller,controller_sessions,discovery,controller_admission,id('b'),id('d'),endpoints);
    if(!crash){
        const auto rotation=op();const auto prepare=controller->prepare_outgoing_rotation(profile,rotation);check(prepare.has_value());
        const auto &pair=peers["profiles"][0]["pair"];const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,pair["pair_id"].get<std::string>(),"1","1"};
        PiPairClient client(controller_admission,PiPairClient::Access::LoopbackTest);
        const auto controller_before=controller->snapshot().authority->signature,target_before=target.snapshot().authority->signature;bool recorded=false;
        const auto read=client.exchange(endpoints.management,id('a'),[&](const auto &requested){return controller->outgoing_credential(profile,requested);},identity,
            [&](const auto &principal){if(!recorded){recorded=controller->record_outgoing_finished(profile,principal);}return recorded;},"GET","/api/pair/v1/operations/"+identity.pair_id);
        check(read.state==PiPairClientResult::State::Replied&&recorded&&controller->snapshot().authority->signature==controller_before&&target.snapshot().authority->signature==target_before);
        const auto result=client.exchange(endpoints.management,id('a'),[&](const auto &requested){return controller->outgoing_credential(profile,requested);},identity,
            [](const auto &){return true;},"POST","/api/pair/v1/rotate/prepare",prepare->dump());
        check(result.state==PiPairClientResult::State::Replied&&result.principal&&controller->accept_outgoing_rotation(profile,*result.principal,Json::parse(result.reply->payload.body)));
    }
    block_readback=true;const auto second_code=open("1","replace"),second_operation=op();
    const auto before_replay=hello_requests.load();
    check(connect(second_code,first_operation).status==409&&hello_requests==before_replay&&commit_requests==1);
    const auto second=connect(second_code,second_operation);
    check(second.status==202&&Json::parse(second.body)["state"]=="pending"&&commit_requests==2);
    peers=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'));const auto second_pair=peers["profiles"][0]["pair"];
    const PiPairTlsIdentity second_identity{PiPairTlsPurpose::Management,second_pair["pair_id"].get<std::string>(),"2","1"};
    check(!controller->outgoing_credential(profile,second_identity)&&peers["profiles"][0]["profile_generation"]=="2");
    const auto second_staged=controller->snapshot().authority->signature;
    check(connect(second_code,second_operation).status==409&&commit_requests==2&&controller->snapshot().authority->signature==second_staged);
    actor.reset();controller.reset();controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));
    check(controller->unclean_start());controller->begin_service();
    actor=std::make_unique<PiPairConnect>(*controller,controller_sessions,discovery,controller_admission,id('b'),id('d'),endpoints);
    block_readback=false;check(actor->recover(second_operation).has_value()&&commit_requests==2&&controller->outgoing_credential(profile,second_identity));
    check(Json::parse(local_receipt(second_operation).body)["state"]=="committed");

    check(!controller->outgoing_renewal_due(profile));
    block_readback=true;const auto third_code=open("2","replace"),third_operation=op();check(connect(third_code,third_operation).status==202);
    const auto third_saved=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'))["profiles"][0];
    const auto fourth_code=open("3","replace"),fourth_operation=op();check(connect(fourth_code,fourth_operation).status==202);
    peers=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'));
    check(peers["uncertain"].size()==1&&peers["uncertain"][0]==third_saved&&peers["profiles"][0]["profile_generation"]=="4");
    const PiPairTlsIdentity third_identity{PiPairTlsPurpose::Management,third_saved["pair"]["pair_id"].get<std::string>(),"3","1"};
    check(!controller->outgoing_credential(profile,third_identity));block_readback=false;
    check(!actor->recover(third_operation));check(actor->recover(fourth_operation).has_value()&&commit_requests==4);
    peers=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'));check(peers["uncertain"].empty()&&peers["profiles"][0]["stage"]=="active");
    auto book=parse_pi_pair_operations(controller->snapshot().operations.bytes,id('b'));bool unknown=false;
    for(const auto &entry:book["entries"])if(entry["operation_id"]==third_operation)unknown=entry["state"]=="unknown"&&entry["unresolved"]==true;
    check(unknown);for(const auto &retired:peers["retired"])check(retired["pair"]["current"].is_null()&&retired["pair"]["pending"].is_null());
    // Supersede another unknown pair without sending the new public commit.
    block_readback=true;const auto fifth_code=open("4","replace"),fifth_operation=op();check(connect(fifth_code,fifth_operation).status==202);
    drop_commit=true;const auto sixth_code=open("5","replace"),sixth_operation=op();check(connect(sixth_code,sixth_operation).status==202);
    block_readback=false;const auto historical=actor->recover(fifth_operation);check(historical&&(*historical)["state"]=="committed");
    peers=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'));
    check(peers["profiles"][0]["stage"]=="awaiting_commit"&&peers["profiles"][0]["initial_operation_id"]==sixth_operation&&peers["uncertain"].empty());
    check(!actor->recover(sixth_operation));
    const auto capacity_before=controller->snapshot();auto full=parse_pi_pair_peers(capacity_before.peers->bytes,id('b'));
    while(full["retired"].size()<32){auto retained=full["retired"][0];
        retained["pair"]["pair_id"]=std::string(28,'0')+std::to_string(1000+full["retired"].size());full["retired"].push_back(retained);}
    controller->publish(capacity_before,parse_pi_pair_catalog(capacity_before.catalog.bytes,id('b')),
        parse_pi_pair_operations(capacity_before.operations.bytes,id('b')),full);
    const auto full_signature=controller->snapshot().authority->signature;const auto writes=commit_requests.load();drop_commit=false;
    const auto capacity_code=open("5","replace");const auto capacity=connect(capacity_code,op());
    check(capacity.status==429&&Json::parse(capacity.body)["error"]["code"]=="capacity"&&commit_requests==writes&&controller->snapshot().authority->signature==full_signature);
    tls.stop();http.stop();if(web.joinable())web.join();controller->finish_service();target.finish_service();
    check(Json::parse(controller->snapshot().authority->bytes)["clean_shutdown"]==true&&Json::parse(target.snapshot().authority->bytes)["clean_shutdown"]==true);
    for(unsigned point=0;point<3;++point){
        const auto path=root+"/crash-"+std::to_string(point);check(std::filesystem::create_directory(path));
        auto executable=std::string(argv[0]),mode=std::string("--crash"),directory=path,boundary=std::to_string(point);
        std::array<char *,5> arguments{executable.data(),mode.data(),directory.data(),boundary.data(),nullptr};pid_t child{};
        check(posix_spawn(&child,executable.c_str(),nullptr,nullptr,arguments.data(),environ)==0);int status{};
        check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
        PiPairHistory cold(path+"/controller",id('b')),cold_target(path+"/target",id('a'));check(cold.available()&&cold_target.available());
        const auto state=cold.snapshot();const auto retained=parse_pi_pair_peers(state.peers->bytes,id('b'));
        const auto operations=parse_pi_pair_operations(state.operations.bytes,id('b'));
        if(point==0)check(retained["profiles"].empty()&&operations["entries"].size()==1&&operations["entries"][0]["scope"]=="local");
        else{
            check(retained["profiles"].size()==1&&retained["profiles"][0]["stage"]=="awaiting_commit"&&operations["entries"].size()==2&&operations["entries"].back()["unresolved"]==true);
            const auto &pending=retained["profiles"][0];const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,
                pending["pair"]["pair_id"].get<std::string>(),pending["pair"]["authorization_generation"].get<std::string>(),"1"};
            check(!cold.outgoing_credential(pending["profile_id"].get<std::string>(),identity));
        }
        check(parse_pi_pair_catalog(cold_target.snapshot().catalog.bytes,id('a'))["active"].is_null());
    }
    std::cout<<"PASS real controller PAKE, durable staging, lost replies, cold readback and uncertain-pair isolation: "<<checks.load()<<" checks\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
