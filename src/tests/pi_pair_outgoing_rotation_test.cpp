// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_history.hpp"
#include "privileged_network_runtime.hpp"
#include "wtp_integration/pi_pair_client.hpp"
#include "wtp_integration/pi_pair_server.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_contract.hpp"
#include "wtp_integration/local_browser_principal.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "wtp_integration/browser_http.hpp"
#include <atomic>
#include <csignal>
#include <spawn.h>
extern char **environ;
#include <sys/wait.h>
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("outgoing rotation check "+std::to_string(checks));}
std::string id(char c){return std::string(32,c);}
PiPairSecret secret(unsigned char byte){std::array<unsigned char,32> bytes{};bytes.fill(byte);return PiPairSecret(bytes);}
void recover(PiPairHistory &owner){
    ManagementBrowserSessions sessions;httplib::Request request;request.method="POST";request.path="/api/control/v1/host/pairing/window";request.target=request.path;request.remote_addr="192.168.1.20";
    request.headers={{"Host","controller.local"},{"Origin","http://controller.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
    request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('e')},{"action","recover"},{"expected_authorization_generation","0"}}.dump();
    auto bootstrap=request;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;httplib::Response response;check(sessions.establish(bootstrap,response));
    const auto cookie=response.get_header_value("Set-Cookie");request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
    const auto principal=sessions.authorize_local_mutation(request,{true,"controller.local",{},{{"192.168.1.10","255.255.255.0"}}});check(principal.has_value());
    owner.recover_access(*wtp_browser_http_request(request),*principal,sessions);
}
Json pair(const std::string &self,const std::string &peer){return {{"schema","pi-management-pair/1"},{"self_id",self},{"peer_id",peer},{"pair_id",id('c')},
    {"authorization_generation","7"},{"role","manager"},{"state","active"},{"current",{{"epoch","1"},{"tls_psk",secret(1).storage_encoding()},{"service_ms","0"},{"connections","0"}}},
    {"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},{"retirement_verifier",secret(3).storage_encoding()}};}
void seed(PiPairHistory &owner,bool controller){
    const auto before=owner.snapshot();const auto self=id(controller?'b':'a');auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self),peers=parse_pi_pair_peers(before.peers->bytes,self);
    if(controller)peers["profiles"].push_back({{"profile_id",id('f')},{"profile_generation","1"},{"target_id",id('a')},{"endpoint",{{"host","target.local"},{"port",31418}}},
        {"pair",pair(id('b'),id('a'))},{"stage","active"},{"initial_operation_id",id('4')},{"initial_request_digest",std::string(64,'d')}});
    else {catalog["authorization_generation"]="7";catalog["active"]=pair(id('a'),id('b'));}
    owner.publish(before,catalog,parse_pi_pair_operations(before.operations.bytes,self),peers);
}
}
int main(int argc,char **argv){try{
    if(argc==4&&std::string_view(argv[1])=="--crash-prepare"){
        initialize_privileged_network_runtime("enforced");
        const auto point=std::stoi(argv[3]);if(point<0||point>2)return 2;
        bool armed=false;
        PiPairHistory owner(argv[2],id('b'),[&](PiPairCatalogWritePoint observed){if(armed&&static_cast<int>(observed)==point)_exit(80+point);});
        if(!owner.begin_service())return 3;
        armed=true;(void)owner.prepare_outgoing_rotation(id('f'),id('9'));return 4;
    }
    if(argc==4&&(std::string_view(argv[1])=="--crash-derived"||std::string_view(argv[1])=="--recover-derived")){
        initialize_privileged_network_runtime("enforced");const std::string directory=argv[2];const auto point=std::stoi(argv[3]);
        if(point<0||point>2)return 2;
        const bool crash=std::string_view(argv[1])=="--crash-derived";bool armed=false;
        PiPairHistory target(directory+"-target",id('a'));
        PiPairHistory controller(directory+"-controller",id('b'),[&](PiPairCatalogWritePoint observed){if(crash&&armed&&static_cast<int>(observed)==point)_exit(90+point);});
        check(target.begin_service()&&controller.begin_service());ManagementBrowserSessions sessions;PiPairAdmission incoming,outgoing;
        PiPairServer server(incoming,[&](const auto &identity){return target.management_credential(identity);},
            [&](const auto &principal){return target.accept_finished(principal,&sessions)!=PiPairHistory::ManagementAccess::Denied;},
            [&](const auto &principal,const httplib::Request &request,auto &connection,const auto &){
                auto mutation=connection.try_mutation(principal.self_id());Json value;
                if(mutation)value=target.rotate(principal,parse_pi_pair_document(request.body,"rotate-prepare"),sessions).value_or(Json());
                return PiPairHttpReply{PicoHttpResponse{value.is_null()?409U:200U,value.dump(),{}},{},{}};
            },PiPairServer::Access::LoopbackTest);
        check(server.start());PiPairClient client(outgoing,PiPairClient::Access::LoopbackTest);
        const auto request=controller.outgoing_rotation_request(id('f'));check(request.has_value());
        // The target's retained prepared reply is replayed over real Finished;
        // no serialized or constructed machine principal is admitted.
        const auto saved=parse_pi_pair_operations(controller.snapshot().operations.bytes,id('b'));
        Json prepare;for(const auto &row:saved["entries"])if(!row["continuation"].is_null()&&row["continuation"].value("kind",Json(nullptr))=="rotation-intent")prepare=row["continuation"]["prepare"];
        const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,id('c'),"7","1"};
        const auto lookup=[&](const auto &value){return controller.outgoing_credential(id('f'),value);};
        const auto response=client.exchange({"127.0.0.1",server.port()},id('a'),lookup,identity,[&](const auto &principal){return bool(lookup(principal.identity()));},
            "POST","/api/pair/v1/rotate/prepare",prepare.dump());
        check(response.reply&&response.principal&&response.reply->payload.status==200);armed=true;
        check(controller.accept_outgoing_rotation(id('f'),*response.principal,parse_pi_pair_document(response.reply->payload.body,"rotate-prepared")));
        if(crash)return 4;
        const auto peers=parse_pi_pair_peers(controller.snapshot().peers->bytes,id('b'));
        check(peers["profiles"][0]["pair"]["pending"]["tls_psk"]==parse_pi_pair_catalog(target.snapshot().catalog.bytes,id('a'))["active"]["pending"]["tls_psk"]);
        const auto book=parse_pi_pair_operations(controller.snapshot().operations.bytes,id('b'));bool erased=false;
        for(const auto &row:book["entries"])if(!row["continuation"].is_null()&&row["continuation"].value("kind",Json(nullptr))=="rotation-intent")erased=row["continuation"]["private_key"].is_null();
        check(erased);server.stop();controller.finish_service();target.finish_service();return 0;
    }
    if(argc!=1)return 2;
    char pattern[]="/tmp/pi-pair-outgoing-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    // A new executable process owns each crash, not an in-process exception or
    // fork-inherited crypto/storage owner. No socket worker exists at fork.
    for(int point=0;point<3;++point){
        const auto directory=root+"/prepare-crash-"+std::to_string(point),argument=std::to_string(point);
        {PiPairHistory seeded(directory,id('b'));recover(seeded);seed(seeded,true);}
        const auto child=fork();check(child>=0);
        if(!child){execl(argv[0],argv[0],"--crash-prepare",directory.c_str(),argument.c_str(),nullptr);_exit(127);}
        int status{};bool exited=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<deadline){
            const auto result=waitpid(child,&status,WNOHANG);
            if(result==child){exited=true;break;}
            if(result<0)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(!exited){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
        check(exited&&WIFEXITED(status)&&WEXITSTATUS(status)==80+point);
        PiPairHistory recovered(directory,id('b'));check(recovered.available()&&recovered.unclean_start());
        const auto pending=recovered.outgoing_rotation_request(id('f'));check(bool(pending)==(point!=0));
        check(recovered.begin_service());
        const auto request=recovered.prepare_outgoing_rotation(id('f'),id('9'));check(request&&valid_pi_pair_document(*request,"rotate-prepare"));
        if(pending)check(*request==*pending);
        const auto stable=recovered.snapshot().authority->signature;
        check(recovered.outgoing_rotation_request(id('f'))==request&&recovered.prepare_outgoing_rotation(id('f'),id('9'))==request&&recovered.snapshot().authority->signature==stable);
        const auto operations=parse_pi_pair_operations(recovered.snapshot().operations.bytes,id('b'));bool retained=false;
        for(const auto &operation:operations["entries"])if(operation["operation_id"]==id('9'))retained=!operation["continuation"]["private_key"].is_null();
        check(retained);recovered.finish_service();
    }
    PiPairHistory target(root+"/target",id('a'));recover(target);seed(target,false);
    auto controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));recover(*controller);seed(*controller,true);
    ManagementBrowserSessions target_sessions,controller_sessions;PiPairAdmission target_budget,controller_budget;std::atomic_bool lose_commit{false};
    PiPairServer server(target_budget,[&](const auto &identity){return target.management_credential(identity);},
        [&](const auto &principal){return target.accept_finished(principal,&target_sessions)!=PiPairHistory::ManagementAccess::Denied;},
        [&](const auto &principal,const httplib::Request &request,auto &connection,const auto &){
            Json value;
            if(request.method=="GET")value=target.pair_operation(principal,request.path.substr(request.path.rfind('/')+1)).value_or(Json());
            else {auto mutation=connection.try_mutation(principal.self_id());if(mutation)value=target.rotate(principal,parse_pi_pair_document(request.body,request.path.ends_with("prepare")?"rotate-prepare":"rotate-commit"),target_sessions).value_or(Json());}
            if(value.is_null())throw std::runtime_error("fixture dispatch refused");
            return PiPairHttpReply{{lose_commit&&request.path.ends_with("commit")?302U:200U,value.dump(),{}},{},{}};
        },PiPairServer::Access::LoopbackTest);
    check(server.start());PiPairClient client(controller_budget,PiPairClient::Access::LoopbackTest);PiPairEndpoint endpoint{"127.0.0.1",server.port()};
    const auto lookup=[&](const auto &identity){return controller->outgoing_credential(id('f'),identity);};
    const auto admit=[&](const auto &principal){const auto credential=lookup(principal.identity());return credential&&principal.self_id()==id('b')&&principal.peer_id()==id('a');};
    const PiPairTlsIdentity first{PiPairTlsPurpose::Management,id('c'),"7","1"},second{PiPairTlsPurpose::Management,id('c'),"7","2"};
    const auto prepare=controller->prepare_outgoing_rotation(id('f'),id('6'));check(prepare.has_value());const auto intent_snapshot=controller->snapshot();
    check(controller->prepare_outgoing_rotation(id('f'),id('6'))==prepare&&controller->snapshot().authority->signature==intent_snapshot.authority->signature);
    check(!controller->prepare_outgoing_rotation(id('f'),id('7')));
    controller.reset();controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));check(controller->available()&&controller->outgoing_rotation_request(id('f'))==prepare);
    const auto reply=client.exchange(endpoint,id('a'),lookup,first,admit,"POST","/api/pair/v1/rotate/prepare",prepare->dump());
    check(reply.state==PiPairClientResult::State::Replied&&reply.principal.has_value());const auto prepared=parse_pi_pair_document(reply.reply->payload.body,"rotate-prepared");
    server.stop();
    for(int point=0;point<3;++point){
        const auto directory=root+"/derived-crash-"+std::to_string(point),argument=std::to_string(point);
        const auto copy=[&](const std::string &from,const std::string &to,const PiPairCatalogStore::Snapshot &snapshot){
            const auto namespace_id=Json::parse(snapshot.authority->bytes)["namespace_id"].get<std::string>();
            std::filesystem::copy(from,to,std::filesystem::copy_options::recursive);
            std::filesystem::copy(from+".ns-"+namespace_id,to+".ns-"+namespace_id,std::filesystem::copy_options::recursive);
        };
        copy(root+"/target",directory+"-target",target.snapshot());copy(root+"/controller",directory+"-controller",controller->snapshot());
        const auto spawn=[&](const std::string &mode,int expected){
            std::array<char *,5> args{argv[0],const_cast<char *>(mode.c_str()),const_cast<char *>(directory.c_str()),const_cast<char *>(argument.c_str()),nullptr};
            pid_t child{};check(posix_spawn(&child,argv[0],nullptr,nullptr,args.data(),environ)==0);int status{};
            check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==expected);
        };
        spawn("--crash-derived",90+point);
        {
            PiPairHistory cold(directory+"-controller",id('b'));check(cold.available()&&cold.unclean_start());
            const auto request=cold.outgoing_rotation_request(id('f'));check(request&&(*request)["schema"]==(point==0?"rotate-prepare":"rotate-commit"));
            const auto book=parse_pi_pair_operations(cold.snapshot().operations.bytes,id('b'));bool private_key=false;
            for(const auto &row:book["entries"])if(!row["continuation"].is_null()&&row["continuation"].value("kind",Json(nullptr))=="rotation-intent")private_key=!row["continuation"]["private_key"].is_null();
            check(private_key==(point==0));
        }
        spawn("--recover-derived",0);
    }
    check(server.start());endpoint.port=server.port();
    check(controller->accept_outgoing_rotation(id('f'),*reply.principal,prepared));
    const auto staged=controller->snapshot();const auto peers=parse_pi_pair_peers(staged.peers->bytes,id('b'));const auto entries=parse_pi_pair_operations(staged.operations.bytes,id('b'))["entries"];
    check(entries.back()["continuation"]["private_key"].is_null()&&entries.back()["continuation"]["stage"]=="awaiting_commit");
    check(peers["profiles"][0]["pair"]["pending"]["tls_psk"]==parse_pi_pair_catalog(target.snapshot().catalog.bytes,id('a'))["active"]["pending"]["tls_psk"]);
    check(controller->accept_outgoing_rotation(id('f'),*reply.principal,prepared)&&controller->snapshot().authority->signature==staged.authority->signature);
    auto invalid=prepared;invalid["nonce"]=secret(9).storage_encoding();check(!controller->accept_outgoing_rotation(id('f'),*reply.principal,invalid));
    controller.reset();controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));const auto commit=controller->outgoing_rotation_request(id('f'));check(commit&&(*commit)["schema"]=="rotate-commit");
    lose_commit=true;const auto uncertain=client.exchange(endpoint,id('a'),lookup,first,admit,"POST","/api/pair/v1/rotate/commit",commit->dump());
    check(uncertain.state==PiPairClientResult::State::Uncertain&&!uncertain.principal&&controller->outgoing_rotation_request(id('f'))==commit);lose_commit=false;
    controller.reset();controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));
    const auto recovered=client.exchange(endpoint,id('a'),lookup,second,[&](const auto &principal){return controller->acknowledge_outgoing_rotation(id('f'),principal,controller_sessions);},"GET","/api/pair/v1/operations/"+id('6'));
    check(recovered.state==PiPairClientResult::State::Replied&&recovered.principal);
    const auto operation=parse_pi_pair_document(recovered.reply->payload.body,"operation");check(operation["state"]=="committed");
    check(!lookup(first)&&lookup(second)&&!target.management_credential(first)&&!controller->outgoing_rotation_request(id('f')));
    check(!controller->accept_outgoing_rotation(id('f'),*reply.principal,prepared));
    const auto completed=controller->snapshot();const auto book=parse_pi_pair_operations(completed.operations.bytes,id('b'));
    check(book["entries"].back()["unresolved"]==false&&book["entries"].back()["continuation"].is_null()&&book["entries"].back()["result_digest"]==operation["result_digest"]);
    check(!controller->prepare_outgoing_rotation(id('f'),id('6'))&&!controller->prepare_outgoing_rotation(id('0'),id('7')));
    // Also exercise the ordinary committed-response path, retaining at most
    // two epochs until an actual new-key Finished acknowledges it.
    const auto prepare_again=controller->prepare_outgoing_rotation(id('f'),id('7'));check(prepare_again.has_value());
    check(prepare_again->dump().find("private_key")==std::string::npos);
    const auto response_again=client.exchange(endpoint,id('a'),lookup,second,admit,"POST","/api/pair/v1/rotate/prepare",prepare_again->dump());
    check(response_again.state==PiPairClientResult::State::Replied&&response_again.principal.has_value());
    const auto prepared_again=parse_pi_pair_document(response_again.reply->payload.body,"rotate-prepared");
    auto wrong=prepared_again;wrong["operation_id"]=id('8');const auto unchanged=controller->snapshot().authority->signature;
    check(!controller->accept_outgoing_rotation(id('f'),*response_again.principal,wrong)&&controller->snapshot().authority->signature==unchanged);
    check(controller->accept_outgoing_rotation(id('f'),*response_again.principal,prepared_again));
    const auto commit_again=controller->outgoing_rotation_request(id('f'));check(commit_again.has_value());
    const auto committed_again=client.exchange(endpoint,id('a'),lookup,second,admit,"POST","/api/pair/v1/rotate/commit",commit_again->dump());
    check(committed_again.state==PiPairClientResult::State::Replied&&committed_again.principal.has_value());
    const auto committed_document=parse_pi_pair_document(committed_again.reply->payload.body,"rotate-committed");
    check(controller->accept_outgoing_rotation(id('f'),*committed_again.principal,committed_document));
    const auto pending_ack=controller->snapshot();
    check(controller->accept_outgoing_rotation(id('f'),*committed_again.principal,committed_document)&&controller->snapshot().authority->signature==pending_ack.authority->signature);
    check(!controller->acknowledge_outgoing_rotation(id('f'),*committed_again.principal,controller_sessions));
    check(lookup(second)&&controller->outgoing_rotation_request(id('f'))==commit_again);
    const PiPairTlsIdentity third{PiPairTlsPurpose::Management,id('c'),"7","3"};
    const auto confirmed=client.exchange(endpoint,id('a'),lookup,third,[&](const auto &principal){return controller->acknowledge_outgoing_rotation(id('f'),principal,controller_sessions);},"GET","/api/pair/v1/operations/"+id('7'));
    check(confirmed.state==PiPairClientResult::State::Replied&&confirmed.principal.has_value());
    check(!lookup(first)&&!lookup(second)&&lookup(third)&&!target.management_credential(second)&&!controller->outgoing_rotation_request(id('f')));
    const auto final_snapshot=controller->snapshot();
    check(controller->acknowledge_outgoing_rotation(id('f'),*confirmed.principal,controller_sessions)&&controller->snapshot().authority->signature==final_snapshot.authority->signature);
    check(!controller->outgoing_credential(id('f'),{PiPairTlsPurpose::Retirement,id('c'),"7",""}));
    server.stop();std::cout<<"PASS durable outgoing rotation, real paired TLS, cold intent and lost-commit recovery: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
