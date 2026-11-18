// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_history.hpp"
#include "wtp_integration/pi_pair_local_service.hpp"
#include "wtp_integration/pi_pair_runtime.hpp"
#include "wtp_integration/pi_pair_client.hpp"
#include "wtp_integration/pi_pair_guarded_http.hpp"
#include "wtp_integration/pi_pair_contract.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "web_server_routes.hpp"
#include <thread>
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "wtp_integration/pi_pair_http_principals.hpp"
#include "wtp_integration/pi_pair_window.hpp"
#include "wtp_integration/browser_http.hpp"
#include "privileged_network_runtime.hpp"
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("local pairing check "+std::to_string(checks));}
std::string id(char c){return std::string(32,c);}
PiPairSecret secret(unsigned char byte){std::array<unsigned char,32> value{};value.fill(byte);return PiPairSecret(value);}
const SupportRequestGuardSnapshot network{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}};
auto command(ManagementBrowserSessions &sessions,const std::string &operation=id('e'),const std::string &generation="1"){
    httplib::Request request;request.method="POST";request.path="/api/control/v1/host/pairing/remove";request.target=request.path;request.remote_addr="192.168.1.20";
    request.headers={{"Host","device.local"},{"Origin","http://device.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
    request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-remove"},{"operation_id",operation},{"expected_authorization_generation",generation}}.dump();
    httplib::Response response;check(sessions.establish(request,response));const auto cookie=response.get_header_value("Set-Cookie");
    request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
    auto principal=sessions.authorize_local_mutation(request,network);check(principal.has_value());
    return std::pair{std::move(request),std::move(principal)};
}
PicoHttpResponse read(PiPairHistory &history,const std::string &operation=id('e')){
    httplib::Request request;request.method="GET";request.path="/api/control/v1/host/pairing/operations/"+operation;request.target=request.path;request.remote_addr="192.168.1.20";request.headers={{"Host","device.local"}};
    const auto principal=authorize_local_pairing_read(request,network);check(principal.has_value());
    return history.local_pairing_receipt(*wtp_browser_http_request(request),*principal);
}
std::unique_ptr<PiPairHistory> seed(const std::string &root){
    std::unique_ptr<PiPairHistory> history;
    {ControllerRetirementBook book(root+"/provision",root+"/assignments.json",id('a'),Json::array(),{},[&](PiPairInitializationProof proof){history=PiPairHistory::initialize(root+"/pairing",std::move(proof));});}
    if(!history)throw std::runtime_error("local_pairing_fixture_provision");
    const auto before=history->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,id('a'));catalog["authorization_generation"]="1";
    catalog["active"]={{"schema","pi-management-pair/1"},{"self_id",id('a')},{"peer_id",id('b')},{"pair_id",id('c')},{"authorization_generation","1"},
        {"role","manager"},{"state","active"},{"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},{"retirement_verifier",secret(2).storage_encoding()},
        {"current",{{"epoch","1"},{"tls_psk",secret(1).storage_encoding()},{"service_ms","0"},{"connections","0"}}}};
    history->publish(before,catalog);return history;
}
void crash_child(const std::string &root,unsigned point){
    auto initial=seed(root);initial.reset();bool armed=false;
    PiPairHistory history(root+"/pairing",id('a'),[&](PiPairCatalogWritePoint at){if(armed&&static_cast<unsigned>(at)==point)_Exit(80+point);});
    history.begin_service();ManagementBrowserSessions sessions;auto [request,principal]=command(sessions);armed=true;
    (void)history.remove_access(*wtp_browser_http_request(request),*principal,sessions,id('d'));
    throw std::runtime_error("local_pairing_crash_not_reached");
}
void runtime_owner(const std::string &root){
    std::filesystem::create_directory(root+"/runtime");
    {auto prepared=seed(root+"/runtime");const auto before=prepared->snapshot();auto peers=parse_pi_pair_peers(before.peers->bytes,id('a'));
     auto retired=parse_pi_pair_catalog(before.catalog.bytes,id('a'))["active"];retired["peer_id"]=id('e');retired["pair_id"]=id('f');retired["authorization_generation"]="2";
     retired["state"]="revoked";retired["current"]=nullptr;retired["retirement_verifier"]=secret(3).storage_encoding();
     peers["retired"].push_back({{"profile_id",id('7')},{"target_id",id('e')},{"endpoint",{{"host","old-target.local"},{"port",31418}}},{"pair",retired}});
     prepared->publish(before,parse_pi_pair_catalog(before.catalog.bytes,id('a')),parse_pi_pair_operations(before.operations.bytes,id('a')),peers);}
    std::unique_ptr<PiPairHistory> callback_target;
    {ControllerRetirementBook proof(root+"/callback-provision",root+"/callback-assignments",id('e'),Json::array(),{},
        [&](PiPairInitializationProof proof){callback_target=PiPairHistory::initialize(root+"/callback-pairing",std::move(proof));});}
    check(bool(callback_target));
    {const auto before=callback_target->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,id('e'));
     auto pair=Json{{"schema","pi-management-pair/1"},{"self_id",id('e')},{"peer_id",id('a')},{"pair_id",id('f')},{"authorization_generation","2"},
        {"role","manager"},{"state","active"},{"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},{"retirement_verifier",secret(3).storage_encoding()},
        {"current",{{"epoch","1"},{"tls_psk",secret(4).storage_encoding()},{"service_ms","0"},{"connections","0"}}}};
     catalog["active"]=pair;catalog["authorization_generation"]="2";callback_target->publish(before,catalog);callback_target->begin_service();}

    auto owner=std::make_shared<PiPairOwner>(root+"/runtime/pairing",id('a'));std::atomic_bool web{false};
    unsigned retirement_reads=0;
    PiPairRuntime runtime(owner,id('d'),38999,[&]{return web.load();},
        [](const auto &,const auto &){return PicoHttpResponse{200,Json{{"contract","Wsprry-Control/1"},{"schema","targets"},{"targets",Json::array()}}.dump(),{}};},
        [&](const auto &principal,const auto &request,const auto &){
            check(principal.identity().purpose==PiPairTlsPurpose::Retirement&&request.path=="/api/pair/v1/retirement/status");
            check(bool(owner->history()->authorize_retirement(principal,parse_pi_pair_document(request.body,"retirement-status"))));++retirement_reads;
            return PiPairHttpReply{{404,Json{{"contract","Wsprry-Pair/1"},{"schema","result"},{"pair_id",id('f')},
                {"state","rejected"},{"code","operation_unknown"},{"retryable",false}}.dump(),{}},{},{}};
        },PiPairRuntime::LoopbackTest{});
    const auto wait=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!predicate()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));check(predicate());};
    const auto untouched=owner->history()->snapshot().authority->signature;
    check(runtime.start()&&!runtime.start());std::this_thread::sleep_for(std::chrono::milliseconds(120));
    check(!runtime.ready()&&!runtime.local_service()&&owner->history()->snapshot().authority->signature==untouched);
    web.store(true);wait([&]{return runtime.ready();});check(runtime.port()>0);
    auto local=runtime.local_service();check(local!=nullptr);
    httplib::Request read;read.method="GET";read.path="/api/control/v1/host/pairing";read.target=read.path;read.remote_addr="192.168.1.20";read.headers={{"Host","device.local"}};
    check(local->handle(read,network)->status==200&&!Json::parse(owner->history()->snapshot().authority->bytes)["clean_shutdown"].get<bool>());
    PiPairAdmission outgoing;PiPairClient client(outgoing,PiPairClient::Access::LoopbackTest);
    const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,id('c'),"1","1"};
    const auto exchange=[&]{return client.exchange({"127.0.0.1",runtime.port()},id('a'),[&](const auto &requested){
        return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{requested,id('b'),id('a'),secret(1)});},identity,
        [](const auto &){return true;},"GET","/api/control/v1/targets");};
    const auto before_read=owner->history()->snapshot().authority->signature;
    const auto targets=exchange();check(targets.reply&&targets.reply->payload.status==200&&valid_controller_document(Json::parse(targets.reply->payload.body),"targets"));
    check(owner->history()->snapshot().authority->signature==before_read);
    const PiPairTlsIdentity retirement{PiPairTlsPurpose::Retirement,id('f'),"2",{}};
    const auto retirement_status=callback_target->retirement_request(Json{{"contract","Wsprry-Pair/1"},{"schema","retirement-status"},
        {"target_id",id('e')},{"controller_id",id('a')},{"assignment_id",id('1')},{"transition_id",id('2')},{"pair_id",id('f')},{"authorization_generation","2"}});
    check(bool(retirement_status));const auto before_retirement=owner->history()->snapshot().authority->signature;
    const auto callback=[&](const std::string &method,const std::string &path,const std::string &body){return client.exchange({"127.0.0.1",runtime.port()},id('a'),
        [&](const auto &requested){return callback_target->retirement_client_credential(requested);},retirement,
        [&](const auto &principal){return callback_target->retirement_finished(principal,false);},method,path,body);};
    const auto readback=callback("POST","/api/pair/v1/retirement/status",retirement_status->dump());
    check(readback.principal&&readback.reply&&readback.reply->payload.status==404&&valid_pi_pair_document(Json::parse(readback.reply->payload.body),"result")&&retirement_reads==1);
    check(owner->history()->snapshot().authority->signature==before_retirement);
    const auto forbidden=callback("GET","/api/control/v1/targets",{});check(forbidden.reply&&forbidden.reply->payload.status==403&&retirement_reads==1);
    callback_target->finish_service();
    web.store(false);wait([&]{return !runtime.local_service();});
    check(!runtime.ready()&&local->handle(read,network)->status==503);local.reset();
    wait([&]{return Json::parse(owner->history()->snapshot().authority->bytes)["clean_shutdown"]==true;});
    web.store(true);wait([&]{return runtime.ready();});
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);wait([&]{return !runtime.local_service();});
    check(!runtime.ready());
    wait([&]{return Json::parse(owner->history()->snapshot().authority->bytes)["clean_shutdown"]==true;});
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);wait([&]{return runtime.ready();});
    runtime.request_stop();check(!runtime.ready()&&!runtime.port());runtime.stop();runtime.stop();
    check(!runtime.local_service()&&Json::parse(owner->history()->snapshot().authority->bytes)["clean_shutdown"]==true);
    check(!owner->history()->management_credential(identity));
    check(runtime.start());wait([&]{return runtime.ready();});runtime.stop();
    check(Json::parse(owner->history()->snapshot().authority->bytes)["clean_shutdown"]==true);
    auto absent=std::make_shared<PiPairOwner>(root+"/absent-runtime",id('a'));
    PiPairRuntime recovery(absent,id('d'),38999,[]{return true;},[](const auto &,const auto &){return controller_target_error(503,"authority_pending");},
        [](const auto &,const auto &,const auto &){return PiPairHttpReply{controller_target_error(503,"authority_pending"),{},{}};},PiPairRuntime::LoopbackTest{});
    check(recovery.start());wait([&]{return recovery.ready();});
    const auto status=recovery.local_service()->handle(read,network);check(status&&status->status==200&&Json::parse(status->body)["state"]=="access_recovery");
    recovery.stop();check(!std::filesystem::exists(root+"/absent-runtime"));
}
void http_routes(const std::string &root){
    std::filesystem::create_directory(root+"/http");auto history=std::shared_ptr<PiPairHistory>(seed(root+"/http"));
    ManagementBrowserSessions sessions;PiPairAdmission budget;PiPairDiscovery discovery(id('a'));
    auto service=std::make_shared<PiPairLocalService>(history,sessions,budget,discovery,id('a'),id('d'));check(service->begin());
    auto request=command(sessions).first;const auto removed=service->handle(request,network);check(removed&&removed->status==200);
    PiPairGuardedHttpServer server(budget);const auto port=server.bind_to_any_port("127.0.0.1");check(port>0);
    const auto host="127.0.0.1:"+std::to_string(port);
    const SupportRequestGuardSnapshot guard{true,host,{},{{"127.0.0.1","255.0.0.0"}}};
    std::mutex selection_mutex;auto selected=service;
    web_server_routes::register_pairing(server,[&]{std::lock_guard lock(selection_mutex);return selected;},[&]{return guard;});
    std::thread listener([&]{server.listen_after_bind();});
    struct Stop {httplib::Server &server;std::thread &listener;~Stop(){server.stop();if(listener.joinable())listener.join();}} stop{server,listener};
    httplib::Client browser("127.0.0.1",port);browser.set_read_timeout(3,0);
    const auto path="/api/control/v1/host/pairing/operations/"+id('e');
    const auto signature=history->snapshot().authority->signature;
    const auto receipt=browser.Get(path);check(receipt&&receipt->status==200&&receipt->body==removed->body);
    check(receipt->get_header_value("Cache-Control")=="no-store"&&receipt->get_header_value("Connection")=="close");
    check(history->snapshot().authority->signature==signature);
    const auto foreign=browser.Get(path,{{"Host","other.local"}});check(foreign&&foreign->status==403);
    const auto origin=browser.Get(path,{{"Origin","http://other.local"}});check(origin&&origin->status==403);
    const auto query=browser.Get(path+"?extra=1");check(query&&query->status==400);
    const auto absent=browser.Get("/api/control/v1/host/pairing/operations/"+id('f'));check(absent&&absent->status==404);
    const auto bootstrap=browser.Get("/api/pair/v1/window");check(bootstrap&&bootstrap->status==503&&valid_pi_pair_document(Json::parse(bootstrap->body),"result"));
    {std::lock_guard lock(selection_mutex);selected.reset();}
    const auto no_owner=browser.Get(path);check(no_owner&&no_owner->status==503&&valid_controller_document(Json::parse(no_owner->body),"target-error"));
    const auto hello=browser.Post("/api/pair/v1/hello","{}","application/json");check(hello&&hello->status==503&&valid_pi_pair_document(Json::parse(hello->body),"result"));
    service->request_stop();server.stop();listener.join();service->stop_and_drain();
    check(Json::parse(history->snapshot().authority->bytes)["clean_shutdown"]==true);
}
}
int main(int argc,char **argv){try{
    initialize_privileged_network_runtime("enforced");
    if(argc==4&&std::string_view(argv[1])=="--crash"){crash_child(argv[2],static_cast<unsigned>(std::stoul(argv[3])));return 2;}
    char pattern[]="/tmp/pi-pair-local-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} cleanup{root};
    if(argc==2&&std::string_view(argv[1])=="--runtime"){runtime_owner(root);std::cout<<"PASS supervisor ownership, actual TLS, policy/web shutdown and recovery without initialization: "<<checks<<" checks\n";return 0;}
    if(argc==2&&std::string_view(argv[1])=="--http"){http_routes(root);std::cout<<"PASS actual guarded local pairing HTTP receipt recovery and closed refusal families: "<<checks<<" checks\n";return 0;}
    std::filesystem::create_directory(root+"/normal");auto history=seed(root+"/normal");history->begin_service();ManagementBrowserSessions sessions;
    auto [request,principal]=command(sessions);auto browser=*wtp_browser_http_request(request);const auto before=history->snapshot().authority->signature;
    auto wrong=browser;wrong.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-remove"},{"operation_id",id('f')},{"expected_authorization_generation","1"}}.dump();
    check(history->remove_access(wrong,*principal,sessions,id('d')).status==403&&history->snapshot().authority->signature==before);
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);
    check(history->remove_access(browser,*principal,sessions,id('d')).status==403&&history->snapshot().authority->signature==before);
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);
    const auto result=history->remove_access(browser,*principal,sessions,id('d'));check(result.status==200&&!sessions.owns(*principal));
    const auto selected=history->snapshot();check(read(*history).body==result.body&&history->snapshot().authority->signature==selected.authority->signature);
    check(history->remove_access(browser,*principal,sessions,id('d')).status==403);
    check(!history->management_credential({PiPairTlsPurpose::Management,id('c'),"1","1"}));
    check(selected.catalog.bytes.find(secret(1).storage_encoding())==std::string::npos&&selected.operations.bytes.find(secret(1).storage_encoding())==std::string::npos);
    auto [stale,stale_principal]=command(sessions,id('f'),"1");
    check(history->remove_access(*wtp_browser_http_request(stale),*stale_principal,sessions,id('d')).status==409&&history->snapshot().authority->signature==selected.authority->signature);
    history->finish_service();history.reset();
    history=std::make_unique<PiPairHistory>(root+"/normal/pairing",id('a'));check(read(*history).body==result.body&&!history->unclean_start());
    check(read(*history,id('f')).status==404);
    {
        std::filesystem::create_directory(root+"/service");
        auto service_history=std::shared_ptr<PiPairHistory>(seed(root+"/service"));
        ManagementBrowserSessions local_sessions;PiPairAdmission budget;PiPairDiscovery discovery(id('a'));
        PiPairLocalService service(service_history,local_sessions,budget,discovery,id('a'),id('d'));
        httplib::Request get;get.method="GET";get.path="/api/control/v1/host/pairing";get.target=get.path;
        get.remote_addr="192.168.1.20";get.headers={{"Host","device.local"}};
        check(service.handle(get,network)->status==503&&service.begin()&&!service.begin());
        auto status=service.handle(get,network);check(status&&status->status==200&&Json::parse(status->body)["state"]=="paired");
        auto window=command(local_sessions).first;window.path="/api/control/v1/host/pairing/window";window.target=window.path;
        window.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('8')},{"action","replace"},{"expected_authorization_generation","1"}}.dump();
        check(service.handle(window,network)->status==503);
        // Removal and readback stay protected and available even without a TLS
        // listener/provider. No transmitter context or old TLS key is borrowed.
        auto removal=command(local_sessions).first;const auto removed=service.handle(removal,network);
        check(removed&&removed->status==200);
        get.path="/api/control/v1/host/pairing/operations/"+id('e');get.target=get.path;
        const auto signature=service_history->snapshot().authority->signature;
        check(service.handle(get,network)->body==removed->body&&service_history->snapshot().authority->signature==signature);
        auto foreign=get;foreign.headers={{"Host","other.local"}};check(service.handle(foreign,network)->status==403);
        set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);
        check(service.handle(get,network)->status==503);
        set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);
        service.request_stop();check(service.handle(get,network)->status==503&&!service.transport_ready());
        service.stop_and_drain();check(service.begin());service.stop_and_drain();
        auto legacy=get;legacy.path="/api/control/v1/host/devices";legacy.target=legacy.path;check(!service.handle(legacy,network));
    }
    {
        auto absent=std::make_shared<PiPairHistory>(root+"/service-recovery",id('a'));
        ManagementBrowserSessions local_sessions;PiPairAdmission budget;PiPairDiscovery discovery(id('a'));
        PiPairLocalService service(absent,local_sessions,budget,discovery,id('a'),id('d'));
        check(service.begin()&&!std::filesystem::exists(root+"/service-recovery"));
        auto request=command(local_sessions).first;request.path="/api/control/v1/host/pairing/window";request.target=request.path;
        request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('9')},{"action","recover"},{"expected_authorization_generation","0"}}.dump();
        check(service.handle(request,network)->status==503&&!std::filesystem::exists(root+"/service-recovery"));
        check(service.transport_ready());const auto opened=service.handle(request,network);
        check(opened&&opened->status==200&&Json::parse(opened->body)["code"].is_string());
        check(absent->available()&&!Json::parse(absent->snapshot().authority->bytes)["clean_shutdown"].get<bool>());
        // Recovery retires the previous lease; repeating its old request cannot
        // return a code, even though the supervisor can now start maintenance.
        check(service.handle(request,network)->status==403&&service.transport_ready());
        service.request_stop();service.stop_and_drain();check(Json::parse(absent->snapshot().authority->bytes)["clean_shutdown"].get<bool>());
    }
    for(unsigned point=0;point<3;++point){
        const auto directory=root+"/crash-"+std::to_string(point);std::filesystem::create_directory(directory);
        std::string executable=std::filesystem::absolute(argv[0]).string(),mode="--crash",stage=std::to_string(point),path=directory;
        std::array<char *,5> args{executable.data(),mode.data(),path.data(),stage.data(),nullptr};pid_t child{};
        check(!posix_spawn(&child,executable.c_str(),nullptr,nullptr,args.data(),environ));int status{};
        check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==80+static_cast<int>(point));
        PiPairHistory recovered(directory+"/pairing",id('a'));check(recovered.available()&&recovered.unclean_start());
        const auto state=recovered.snapshot();const auto signature=state.authority->signature;const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,id('a'));
        const auto receipt=read(recovered);
        check(recovered.snapshot().authority->signature==signature);
        if(point==0)check(receipt.status==404&&catalog["authorization_generation"]=="1"&&!catalog["active"].is_null()&&catalog["retired"].empty());
        else check(receipt.status==200&&catalog["authorization_generation"]=="2"&&catalog["active"].is_null()&&catalog["retired"].size()==1&&
            state.catalog.bytes.find(secret(1).storage_encoding())==std::string::npos&&!recovered.management_credential({PiPairTlsPurpose::Management,id('c'),"1","1"}));
    }
    // Expired originating authority after preparation must roll back rather
    // than publish a revocation whose local confirmation is no longer live.
    std::filesystem::create_directory(root+"/cancel");auto initial=seed(root+"/cancel");initial.reset();bool armed=false;ManagementBrowserSessions cancelled;
    PiPairHistory cancel(root+"/cancel/pairing",id('a'),[&](PiPairCatalogWritePoint point){if(armed&&point==PiPairCatalogWritePoint::Prepared)cancelled.retire();});
    cancel.begin_service();auto [cancel_request,cancel_principal]=command(cancelled);const auto original=cancel.snapshot().catalog.bytes;armed=true;
    check(cancel.remove_access(*wtp_browser_http_request(cancel_request),*cancel_principal,cancelled,id('d')).status==503);
    check(cancel.available()&&cancel.snapshot().catalog.bytes==original&&read(cancel).status==404);armed=false;cancel.finish_service();
    // The producer reserves a tombstone slot, so a full replacement history
    // still permits Remove without throwing away an old retirement verifier.
    std::filesystem::create_directory(root+"/capacity");auto capacity=seed(root+"/capacity");
    const auto base=capacity->snapshot();auto populated=parse_pi_pair_catalog(base.catalog.bytes,id('a'));
    populated["authorization_generation"]="32";populated["active"]["authorization_generation"]="32";
    for(unsigned n=0;n<31;++n){
        auto retired=populated["active"];retired["state"]="revoked";retired["current"]=nullptr;
        retired["authorization_generation"]=std::to_string(n+1);
        retired["pair_id"]=std::string(30,'0')+"0123456789abcdef"[n/16]+"0123456789abcdef"[n%16];populated["retired"].push_back(retired);
    }
    capacity->publish(base,populated);capacity->begin_service();ManagementBrowserSessions capacity_sessions;
    PiPairWindow window(*capacity,capacity_sessions,id('a'),id('d'),[&](const auto &approval){return capacity->commit_pair(approval);});
    const auto offer=[&](const std::string &action,const std::string &generation){
        auto [request,unused]=command(capacity_sessions,id('f'),generation);(void)unused;
        request.path="/api/control/v1/host/pairing/window";request.target=request.path;
        request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('f')},{"action",action},{"expected_authorization_generation",generation}}.dump();
        const auto principal=capacity_sessions.authorize_local_mutation(request,network);check(principal.has_value());
        return window.open(*wtp_browser_http_request(request),*principal);
    };
    const auto full=capacity->snapshot().authority->signature;check(offer("replace","32").status==429&&capacity->snapshot().authority->signature==full);
    auto [remove_request,remove_principal]=command(capacity_sessions,id('e'),"32");
    check(capacity->remove_access(*wtp_browser_http_request(remove_request),*remove_principal,capacity_sessions,id('d')).status==200);
    const auto removed=capacity->snapshot();const auto retained=parse_pi_pair_catalog(removed.catalog.bytes,id('a'));
    check(retained["active"].is_null()&&retained["retired"].size()==32);
    check(offer("pair","33").status==429&&capacity->snapshot().authority->signature==removed.authority->signature);
    capacity->finish_service();
    std::cout<<"PASS protected local revocation/readback, stale scopes, cold reload, three fresh-process journal crashes and pre-commit cancellation: "<<checks<<" checks\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
