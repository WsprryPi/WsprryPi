// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_window.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/browser_http.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "privileged_network_runtime.hpp"
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <thread>
#include <future>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
std::atomic_uint checks{0};unsigned operation{};
void check(bool value,const std::source_location location=std::source_location::current()){
    const auto number=++checks;
    if(!value)throw std::runtime_error("pair window check "+std::to_string(number)+" at line "+std::to_string(location.line()));
}
std::string id(char c){return std::string(32,c);}
std::string operation_id(){const auto text=std::to_string(++operation);return std::string(32-text.size(),'0')+text;}
const SupportRequestGuardSnapshot network{true,"target.local",{},{{"192.168.1.10","255.255.255.0"}}};
httplib::Request request(std::string method,std::string path,Json body=nullptr){
    httplib::Request result;result.method=std::move(method);result.path=std::move(path);result.target=result.path;result.remote_addr="192.168.1.20";
    result.headers={{"Host","target.local"}};
    if(!body.is_null()){result.body=body.dump();result.headers.emplace("Content-Type","application/json");result.headers.emplace("Content-Length",std::to_string(result.body.size()));}
    return result;
}
struct Local {
    BrowserRequest request;
    std::optional<LocalBrowserPrincipal> principal;
};
Local local(ManagementBrowserSessions &sessions,const std::string &generation,const char *action,std::string replay={}){
    auto command=request("POST","/api/control/v1/host/pairing/window",Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},
        {"operation_id",replay.empty()?operation_id():replay},{"action",action},{"expected_authorization_generation",generation}});
    command.headers.emplace("Origin","http://target.local");command.headers.emplace("X-WsprryPico-Request","1");
    auto bootstrap=command;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;
    httplib::Response reply;check(sessions.establish(bootstrap,reply));const auto cookie=reply.get_header_value("Set-Cookie");
    command.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));command.headers.emplace("X-WsprryPi-CSRF",reply.get_header_value("X-WsprryPi-CSRF"));
    auto principal=sessions.authorize_local_mutation(command,network);check(principal.has_value());
    return {*wtp_browser_http_request(command),std::move(principal)};
}
PicoHttpResponse call(PiPairWindow &actor,const std::string &resource,Json body=nullptr){
    const auto method=body.is_null()?"GET":"POST";
    auto wire=request(method,"/api/pair/v1/"+resource,std::move(body));
    const auto principal=authorize_pi_pair_bootstrap(wire,network);check(principal.has_value());
    return actor.bootstrap(*wtp_browser_http_request(wire),*principal);
}
Json state(PiPairWindow &actor){
    auto wire=request("GET","/api/control/v1/host/pairing");const auto principal=authorize_local_pairing_read(wire,network);check(principal.has_value());
    const auto response=actor.state(*wtp_browser_http_request(wire),*principal);check(response.status==200&&response.body.find("\"code\"")==std::string::npos);
    return Json::parse(response.body);
}
Json hello(const Json &offer,const char *action){check(offer["action"]==action);return {{"contract","Wsprry-Pair/1"},{"schema","hello"},{"suite","SPAKE2+-P256-SHA256-HKDF-HMAC"},
    {"window_id",offer["window_id"]},{"pair_id",offer["pair_id"]},{"action",offer["action"]},{"controller_id",id('b')},{"controller_boot_id",id('d')},
    {"target_id",id('a')},{"target_boot_id",id('c')},{"authorization_generation",offer["authorization_generation"]},{"controller_nonce",std::string(43,'A')}};}
Json confirmation(PiPairWindow &actor,PiPairExchange &controller){
    const auto exchange=call(actor,"exchange",controller.start_controller());check(exchange.status==200);
    const auto confirm=controller.controller_confirmation(Json::parse(exchange.body));const auto confirmed=call(actor,"confirm",confirm);check(confirmed.status==200);
    const auto duplicate=call(actor,"confirm",confirm);check(duplicate.status==200&&duplicate.body==confirmed.body);
    controller.controller_confirmed(Json::parse(confirmed.body));return confirm;
}
Json commit_request(PiPairExchange &controller,const Json &greeting){
    auto keys=controller.keys();return {{"contract","Wsprry-Pair/1"},{"schema","commit"},{"pair_id",greeting["pair_id"]},
        {"request_digest",controller.request_digest()},{"proof",pi_pair_mac(keys.commit,"request/"+controller.request_digest())}};
}
Json authenticated_readback(PiPairHistory &history,const PiPairKeys &keys,const std::string &pair,const std::string &generation){
    const auto listener=socket(AF_INET,SOCK_STREAM,0);check(listener>=0);
    struct Close{int fd;~Close(){if(fd>=0)::close(fd);}} listening{listener};
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(!bind(listener,reinterpret_cast<const sockaddr *>(&address),sizeof(address))&&!listen(listener,1));
    socklen_t size=sizeof(address);check(!getsockname(listener,reinterpret_cast<sockaddr *>(&address),&size));
    const auto client_fd=socket(AF_INET,SOCK_STREAM,0);check(client_fd>=0);Close client_socket{client_fd};
    check(!connect(client_fd,reinterpret_cast<const sockaddr *>(&address),sizeof(address)));const auto server_fd=accept(listener,nullptr,nullptr);check(server_fd>=0);Close server_socket{server_fd};
    const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,pair,generation,"1"};
    auto credential=std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{identity,id('b'),id('a'),PiPairSecret(keys.tls.bytes())});
    client_socket.fd=-1;PiPairTlsChannel client(client_fd,[&](const auto &){return credential;},identity);
    server_socket.fd=-1;PiPairTlsChannel server(server_fd,[&](const auto &identity){return history.management_credential(identity);});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);bool ready=false;
    while(std::chrono::steady_clock::now()<deadline){
        const auto a=client.handshake(),b=server.handshake();
        if(a==PiPairTlsChannel::Handshake::Failed||b==PiPairTlsChannel::Handshake::Failed)break;
        if(a==PiPairTlsChannel::Handshake::Ready&&b==PiPairTlsChannel::Handshake::Ready){ready=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(ready&&history.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::Current);
    const auto result=history.pair_operation(*server.principal(),pair);check(result.has_value());return *result;
}
}
int main(int argc,char **argv){try{
    if(argc==4&&std::string(argv[1])=="--commit-crash"){
        const int point=std::stoi(argv[3]);auto current=std::chrono::steady_clock::now();
        PiPairHistory history(argv[2],id('a'),[&](auto stage){
            if(point<3&&static_cast<int>(stage)==point)_exit(73);
            if(point==3&&stage==PiPairCatalogWritePoint::Prepared)current+=std::chrono::seconds(31);
        });
        ManagementBrowserSessions sessions;PiPairWindow actor(history,sessions,id('a'),id('c'),[&](const auto &approval){return history.commit_pair(approval);},[&]{return current;});
        auto command=local(sessions,"0","pair");const auto window=Json::parse(actor.open(command.request,*command.principal).body);
        const auto offer=Json::parse(call(actor,"window").body),greeting=hello(offer,"pair"),challenge=Json::parse(call(actor,"hello",greeting).body);
        auto code=PiPairCode::normalize(window["code"].get<std::string>());PiPairExchange controller(PiPairExchange::Role::Controller,code,greeting,challenge);
        confirmation(actor,controller);const auto reply=call(actor,"commit",commit_request(controller,greeting));
        check(point==3&&reply.status==409&&Json::parse(reply.body)["code"]=="expired"&&history.available());
        const auto saved=history.snapshot();check(Json::parse(saved.catalog.bytes)["active"].is_null()&&Json::parse(saved.catalog.bytes)["authorization_generation"]=="0");
        auto again=local(sessions,"0","pair");check(actor.open(again.request,*again.principal).status==200);return 0;
    }
    char pattern[]="/tmp/pi-pair-window-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    PiPairHistory history(root+"/history",id('a'));ManagementBrowserSessions sessions;
    auto time=std::chrono::steady_clock::now();unsigned commits=0;bool lose_reply=false;
    // Actual target commit owner, with a fixture-only lost-reply boundary.
    const auto writer=[&](const PiPairCommitApproval &approval){
        check(approval.valid()&&approval.hello()["target_id"]==id('a'));++commits;
        const auto committed=history.commit_pair(approval);
        if(lose_reply)throw std::runtime_error("fixture lost committed reply");
        return committed;
    };
    PiPairWindow actor(history,sessions,id('a'),id('c'),writer,[&]{return time;});
    check(state(actor)["state"]=="access_recovery"&&!history.available());
    auto recover=local(sessions,"0","recover");const auto opened=actor.open(recover.request,*recover.principal);check(opened.status==200);
    const auto window=Json::parse(opened.body);check(window["code"].is_string()&&state(actor)["window"]["expires_in_ms"]==120000);
    const auto baseline=history.snapshot().authority->signature;const auto offer=Json::parse(call(actor,"window").body);
    check(offer["open"]==true&&!offer.contains("code")&&history.snapshot().authority->signature==baseline);
    const auto greeting=hello(offer,"recover"),challenge=Json::parse(call(actor,"hello",greeting).body);
    check(Json::parse(call(actor,"hello",greeting).body)==challenge);
    auto code=PiPairCode::normalize(window["code"].get<std::string>());PiPairExchange controller(PiPairExchange::Role::Controller,code,greeting,challenge);
    confirmation(actor,controller);const auto commit=commit_request(controller,greeting);const auto committed=call(actor,"commit",commit);check(committed.status==200&&commits==1);
    const auto committed_body=Json::parse(committed.body);auto unsigned_reply=committed_body;unsigned_reply.erase("proof");auto keys=controller.keys();
    check(pi_pair_verify_mac(keys.commit,"response/"+canonical_pi_management_json(unsigned_reply),committed_body["proof"].get<std::string>()));
    const auto exact=history.snapshot().authority->signature;check(state(actor)["state"]=="paired"&&state(actor)["window"].is_null());
    check(Json::parse(call(actor,"window").body)["open"]==false);
    check(Json::parse(call(actor,"window").body)["action"].is_null());
    const auto replay=call(actor,"commit",commit);check(replay.status==200&&replay.body==committed.body&&commits==1&&history.snapshot().authority->signature==exact);
    const auto initial_readback=authenticated_readback(history,keys,greeting["pair_id"].get<std::string>(),"1");
    check(initial_readback["state"]=="committed"&&initial_readback["request_digest"]==controller.request_digest()&&
        initial_readback["result_digest"]==committed_body["receipt_digest"]&&history.snapshot().authority->signature==exact);
    auto replace=local(sessions,"1","replace");const auto replacement=actor.open(replace.request,*replace.principal);check(replacement.status==200);
    check(actor.open(replace.request,*replace.principal).body==replacement.body&&history.snapshot().authority->signature==exact);
    auto other_browser=local(sessions,"1","replace",Json::parse(replace.request.body)["operation_id"].get<std::string>());
    const auto hidden=actor.open(other_browser.request,*other_browser.principal);
    check(hidden.status==409&&!Json::parse(hidden.body).contains("code")&&hidden.body.find(Json::parse(replacement.body)["code"].get<std::string>())==std::string::npos&&
        state(actor)["window"]["window_id"]==Json::parse(replacement.body)["window_id"]);
    for(unsigned i=0;i<5;++i){
        const auto offer=Json::parse(call(actor,"window").body);check(offer["open"]==true);
        const auto h=hello(offer,"replace");check(call(actor,"hello",h).status==200);
        const Json bad{{"contract","Wsprry-Pair/1"},{"schema","confirm"},{"pair_id",offer["pair_id"]},{"cA",std::string(43,'A')}};
        check(call(actor,"confirm",bad).status==403&&history.snapshot().authority->signature==exact);
    }
    check(Json::parse(call(actor,"window").body)["open"]==false&&actor.open(replace.request,*replace.principal).status==409);
    auto timed=local(sessions,"1","replace");check(actor.open(timed.request,*timed.principal).status==200);
    const auto old_offer=Json::parse(call(actor,"window").body);check(call(actor,"hello",hello(old_offer,"replace")).status==200);
    time+=std::chrono::seconds(30);check(Json::parse(call(actor,"window").body)["open"]==false);
    actor.advance();const auto renewed=Json::parse(call(actor,"window").body);check(renewed["open"]==true&&renewed["pair_id"]!=old_offer["pair_id"]);
    check(state(actor)["window"]["expires_in_ms"]==90000&&history.snapshot().authority->signature==exact);
    time+=std::chrono::seconds(90);actor.advance();check(Json::parse(call(actor,"window").body)["open"]==false&&actor.open(timed.request,*timed.principal).status==409);
    auto cancelling=local(sessions,"1","replace");const auto cancellation_window=Json::parse(actor.open(cancelling.request,*cancelling.principal).body);
    const auto cancellation_offer=Json::parse(call(actor,"window").body),cancel_hello=hello(cancellation_offer,"replace");
    const auto cancel_challenge=Json::parse(call(actor,"hello",cancel_hello).body);auto cancel_code=PiPairCode::normalize(cancellation_window["code"].get<std::string>());
    PiPairExchange cancel_controller(PiPairExchange::Role::Controller,cancel_code,cancel_hello,cancel_challenge);const auto share=cancel_controller.start_controller();
    std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
    testing::set_pi_pair_derivation_checkpoint([&]{std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});});
    PicoHttpResponse cancelled;std::exception_ptr worker_error;
    std::thread worker([&]{try{cancelled=call(actor,"exchange",share);}catch(...){worker_error=std::current_exception();std::lock_guard lock(mutex);entered=true;changed.notify_all();}});
    bool checkpoint_seen=false;
    {std::unique_lock lock(mutex);checkpoint_seen=changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;});}
    auto cancellation=std::async(std::launch::async,[&]{
        const auto busy=call(actor,"exchange",share);const auto offer=Json::parse(call(actor,"window").body);
        actor.cancel();return busy.status==429&&Json::parse(busy.body)["code"]=="busy"&&offer["pair_id"]==cancellation_offer["pair_id"]&&
            history.snapshot().authority->signature==exact;
    });
    const bool prompt=cancellation.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    {std::lock_guard lock(mutex);released=true;}changed.notify_all();worker.join();testing::set_pi_pair_derivation_checkpoint({});
    if(worker_error)std::rethrow_exception(worker_error);
    check(checkpoint_seen&&prompt&&cancellation.get());
    check(cancelled.status==409&&commits==1&&Json::parse(call(actor,"window").body)["open"]==false);
    {
        const auto snapshot=history.snapshot();auto entries=parse_pi_pair_operations(snapshot.operations.bytes,id('a'))["entries"];
        while(entries.size()<256)entries.push_back({{"scope","local"},{"principal_id",id('a')},{"target_id",id('a')},{"pair_id",nullptr},
            {"authorization_generation","1"},{"operation_id",operation_id()},{"request_digest",std::string(64,'e')},
            {"state","committed"},{"result_digest",std::string(64,'f')},{"unresolved",false},{"receipt",nullptr},{"continuation",nullptr}});
        history.publish(snapshot,parse_pi_pair_catalog(snapshot.catalog.bytes,id('a')),pi_pair_operations(id('a'),entries).document);
    }
    const auto before_unknown=history.snapshot().authority->signature;
    auto unknown=local(sessions,"1","replace");const auto unknown_window=Json::parse(actor.open(unknown.request,*unknown.principal).body);
    const auto unknown_offer=Json::parse(call(actor,"window").body),unknown_hello=hello(unknown_offer,"replace");const auto unknown_challenge=Json::parse(call(actor,"hello",unknown_hello).body);
    auto unknown_code=PiPairCode::normalize(unknown_window["code"].get<std::string>());PiPairExchange unknown_controller(PiPairExchange::Role::Controller,unknown_code,unknown_hello,unknown_challenge);
    confirmation(actor,unknown_controller);lose_reply=true;const auto unknown_commit=commit_request(unknown_controller,unknown_hello);const auto uncertain=call(actor,"commit",unknown_commit);
    check(uncertain.status==503&&Json::parse(uncertain.body)["state"]=="unknown"&&commits==2&&history.snapshot().authority->signature!=before_unknown);
    check(call(actor,"commit",unknown_commit).body==uncertain.body&&commits==2);
    check(!unknown.principal->matches(unknown.request)&&parse_pi_pair_operations(history.snapshot().operations.bytes,id('a'))["entries"].size()==256);
    const auto recovered=authenticated_readback(history,unknown_controller.keys(),unknown_hello["pair_id"].get<std::string>(),"2");
    check(recovered["state"]=="committed"&&recovered["request_digest"]==unknown_controller.request_digest());
    const auto after=parse_pi_pair_catalog(history.snapshot().catalog.bytes,id('a'));
    check(after["authorization_generation"]=="2"&&after["retired"].size()==1&&after["retired"][0]["current"].is_null()&&
        after["retired"][0]["pending"].is_null()&&after["retired"][0]["previous"].is_null());
    check(!history.management_credential({PiPairTlsPurpose::Management,greeting["pair_id"].get<std::string>(),"1","1"}));
    for(int point=0;point<4;++point){
        const auto path=root+"/commit-"+std::to_string(point);
        {PiPairHistory first(path,id('a'));ManagementBrowserSessions first_sessions;auto intent=local(first_sessions,"0","recover");first.recover_access(intent.request,*intent.principal,first_sessions);}
        const auto child=fork();check(child>=0);
        if(!child){const auto stage=std::to_string(point);execl(argv[0],argv[0],"--commit-crash",path.c_str(),stage.c_str(),nullptr);_exit(127);}
        int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==(point==3?0:73));
        PiPairHistory cold(path,id('a'));check(cold.available());const auto saved=cold.snapshot();const auto catalog=parse_pi_pair_catalog(saved.catalog.bytes,id('a'));
        const auto book=parse_pi_pair_operations(saved.operations.bytes,id('a'));const bool committed=point==1||point==2;
        check(catalog["active"].is_null()!=committed&&catalog["authorization_generation"]==(committed?"1":"0")&&book["entries"].size()==(committed?2U:1U));
        if(committed)check(catalog["active"]["receipt"]["result_digest"]==book["entries"].back()["result_digest"]&&book["entries"].back()["state"]=="committed");
    }
    auto policy_intent=local(sessions,"2","replace");const auto before_off=history.snapshot().authority->signature;
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);
    check(actor.open(policy_intent.request,*policy_intent.principal).status==403&&call(actor,"window").status==403&&history.snapshot().authority->signature==before_off);
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);
    check(actor.open(policy_intent.request,*policy_intent.principal).status==200);
    std::cout<<"PASS protected ephemeral pairing windows, real PAKE, deadlines, failure budgets and exact public replay: "<<checks.load()<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
