// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_history.hpp"
#include "wtp_integration/pi_pair_service_lifetime.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_rotation.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "wtp_integration/browser_http.hpp"
#include "pairing_provider/rotation-vector.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair history TLS check "+std::to_string(checks));}
std::string id(char value){return std::string(32,value);}
PiPairSecret secret(unsigned char byte){std::array<unsigned char,32> value{};value.fill(byte);return PiPairSecret(value);}
void recover(PiPairHistory &owner){
    ManagementBrowserSessions sessions;httplib::Request request;
    request.method="POST";request.path="/api/control/v1/host/pairing/window";request.target=request.path;request.remote_addr="192.168.1.20";
    request.headers={{"Host","controller.local"},{"Origin","http://controller.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
    request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('e')},{"action","recover"},{"expected_authorization_generation","0"}}.dump();
    auto bootstrap=request;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;
    httplib::Response response;check(sessions.establish(bootstrap,response));const auto cookie=response.get_header_value("Set-Cookie");
    request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
    const auto principal=sessions.authorize_local_mutation(request,{true,"controller.local",{},{{"192.168.1.10","255.255.255.0"}}});check(principal.has_value());
    owner.recover_access(*wtp_browser_http_request(request),*principal,sessions);
}
std::pair<int,int> sockets(){
    const auto listener=socket(AF_INET,SOCK_STREAM,0);check(listener>=0);
    struct Close{int fd;~Close(){if(fd>=0)::close(fd);}} cleanup{listener};
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(!bind(listener,reinterpret_cast<const sockaddr *>(&address),sizeof(address))&&!listen(listener,1));
    socklen_t size=sizeof(address);check(!getsockname(listener,reinterpret_cast<sockaddr *>(&address),&size));
    const auto client=socket(AF_INET,SOCK_STREAM,0);check(client>=0);Close client_cleanup{client};
    check(!connect(client,reinterpret_cast<const sockaddr *>(&address),sizeof(address)));
    const auto server=accept(listener,nullptr,nullptr);check(server>=0);client_cleanup.fd=-1;return {client,server};
}
void establish(PiPairTlsChannel &client,PiPairTlsChannel &server){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<end){
        const auto a=client.handshake(),b=server.handshake();check(a!=PiPairTlsChannel::Handshake::Failed&&b!=PiPairTlsChannel::Handshake::Failed);
        if(a==PiPairTlsChannel::Handshake::Ready&&b==PiPairTlsChannel::Handshake::Ready)return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("pair history TLS fixture timeout");
}
void rotating(PiPairHistory &owner){
    const auto before=owner.snapshot();
    auto catalog=parse_pi_pair_catalog(before.catalog.bytes,id('a'));catalog["authorization_generation"]="1";
    const auto epoch=[](const char *number,unsigned char byte){return Json{{"epoch",number},{"tls_psk",secret(byte).storage_encoding()},{"service_ms","0"},{"connections","0"}};};
    auto prepare=Json::parse(rotation_fixture::prepare_json),prepared=Json::parse(rotation_fixture::prepared_json);
    prepare["expected_epoch"]="1";prepared["next_epoch"]="2";
    Json continuation{{"kind","rotation-intent"},{"self_id",id('a')},{"peer_id",id('b')},{"profile_id",id('f')},{"pair_id",id('c')},
        {"authorization_generation","1"},{"operation_id",id('d')},{"expected_epoch","1"},{"stage","awaiting_commit"},
        {"prepare",prepare},{"prepared",prepared},{"private_key",nullptr}};
    Json entry{{"scope","paired"},{"principal_id",id('b')},{"target_id",id('a')},{"pair_id",id('c')},{"authorization_generation","1"},
        {"operation_id",id('d')},{"request_digest",std::string(64,'e')},{"state","committed"},{"result_digest",std::string(64,'f')},
        {"unresolved",true},{"receipt",nullptr},{"continuation",continuation}};
    catalog["active"]={{"schema","pi-management-pair/1"},{"self_id",id('a')},{"peer_id",id('b')},{"pair_id",id('c')},
        {"authorization_generation","1"},{"role","manager"},{"state","active"},{"current",epoch("2",2)},{"pending",nullptr},{"previous",epoch("1",1)},
        {"receipt",{{"operation_id",id('d')},{"request_digest",entry["request_digest"]},{"result_digest",entry["result_digest"]}}},
        {"retirement_verifier",secret(3).storage_encoding()}};
    owner.publish(before,catalog,pi_pair_operations(id('a'),Json::array({entry})).document);
}
std::shared_ptr<const PiPairTlsCredential> client_credential(const PiPairTlsIdentity &identity){
    return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{identity,id('b'),id('a'),secret(identity.epoch=="1"?1:2)});
}
Json rotation_prepare(){auto value=Json::parse(rotation_fixture::prepare_json);value["expected_epoch"]="2";value["operation_id"]=id('6');return value;}
Json rotation_commit(){const auto prepare=rotation_prepare();return {{"contract","Wsprry-Pair/1"},{"schema","rotate-commit"},
    {"operation_id",prepare["operation_id"]},{"pair_id",id('c')},{"expected_epoch","2"},{"next_epoch","3"}};}
void ready_rotation(PiPairHistory &owner,bool prepare){
    rotating(owner);const PiPairTlsIdentity next{PiPairTlsPurpose::Management,id('c'),"1","2"};const auto [a,b]=sockets();
    PiPairTlsChannel client(a,client_credential,next),server(b,[&](const auto &identity){return owner.management_credential(identity);});
    establish(client,server);check(owner.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::Current);
    if(prepare){ManagementBrowserSessions sessions;check(owner.rotate(*server.principal(),rotation_prepare(),sessions).has_value());}
}
}
int main(int argc,char **argv){try{
    if(argc==5&&std::string(argv[1])=="--rotation-crash"){
        const auto point=std::stoi(argv[3]);PiPairHistory owner(argv[2],id('a'),[&](auto stage){if(static_cast<int>(stage)==point)_exit(73);});
        const PiPairTlsIdentity next{PiPairTlsPurpose::Management,id('c'),"1","2"};const auto [a,b]=sockets();
        PiPairTlsChannel client(a,client_credential,next),server(b,[&](const auto &identity){return owner.management_credential(identity);});
        establish(client,server);ManagementBrowserSessions sessions;
        (void)owner.rotate(*server.principal(),std::string(argv[4])=="prepare"?rotation_prepare():rotation_commit(),sessions);return 3;
    }
    if(argc==4&&std::string(argv[1])=="--ack-crash"){
        const auto point=std::stoi(argv[3]);PiPairHistory owner(argv[2],id('a'),[&](auto stage){if(static_cast<int>(stage)==point)_exit(73);});
        const PiPairTlsIdentity next{PiPairTlsPurpose::Management,id('c'),"1","2"};const auto [a,b]=sockets();
        PiPairTlsChannel client(a,client_credential,next),server(b,[&](const auto &identity){return owner.management_credential(identity);});
        establish(client,server);check(owner.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::Current);return 3;
    }
    char pattern[]="/tmp/pi-pair-history-tls-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    PiPairHistory owner(root+"/pairing",id('a'));recover(owner);rotating(owner);
    {
        const auto before=owner.snapshot();auto entries=parse_pi_pair_operations(before.operations.bytes,id('a'))["entries"];
        for(unsigned i=0;i<256;++i){const auto text=std::to_string(i);
            entries.push_back({{"scope","local"},{"principal_id",id('a')},{"target_id",id('a')},{"pair_id",nullptr},{"authorization_generation","1"},
                {"operation_id",std::string(32-text.size(),'0')+text},{"request_digest",std::string(64,'a')},{"state","committed"},
                {"result_digest",std::string(64,'b')},{"unresolved",false},{"receipt",nullptr},{"continuation",nullptr}});
        }
        owner.publish(before,parse_pi_pair_catalog(before.catalog.bytes,id('a')),pi_pair_operations(id('a'),entries).document);
    }
    const PiPairTlsIdentity old{PiPairTlsPurpose::Management,id('c'),"1","1"},next{PiPairTlsPurpose::Management,id('c'),"1","2"};
    const auto untouched=owner.snapshot().authority->signature;
    for(const auto &identity:{old,next}){const auto loaded=owner.management_credential(identity);check(loaded&&loaded->self_id==id('a')&&loaded->peer_id==id('b'));}
    check(!owner.management_credential({PiPairTlsPurpose::Retirement,id('c'),"1",""}));
    check(!owner.management_credential({PiPairTlsPurpose::Management,id('c'),"2","2"}));
    check(!owner.management_credential({PiPairTlsPurpose::Management,id('e'),"1","2"}));
    check(!owner.management_credential({PiPairTlsPurpose::Management,id('c'),"1","3"}));
    check(owner.snapshot().authority->signature==untouched);
    const auto [old_client_fd,old_server_fd]=sockets();PiPairTlsChannel old_client(old_client_fd,client_credential,old);
    PiPairTlsChannel old_server(old_server_fd,[&](const auto &identity){return owner.management_credential(identity);});establish(old_client,old_server);
    check(owner.accept_finished(*old_server.principal())==PiPairHistory::ManagementAccess::RotationRecovery);
    check(owner.pair_operation(*old_server.principal(),id('d')).has_value()&&!owner.pair_operation(*old_server.principal(),id('0')));
    check(owner.snapshot().authority->signature==untouched);
    const auto [client_fd,server_fd]=sockets();PiPairTlsChannel client(client_fd,client_credential,next);
    PiPairTlsChannel server(server_fd,[&](const auto &identity){return owner.management_credential(identity);});establish(client,server);
    // Even a successful handshake's lookup is read-only. Only the separate
    // post-Finished admission may acknowledge the new epoch and erase the old.
    check(owner.snapshot().authority->signature==untouched);
    check(owner.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::Denied&&owner.snapshot().authority->signature==untouched);
    ManagementBrowserSessions sessions;auto command=httplib::Request{};
    command.method="POST";command.path="/api/control/v1/host/pairing/window";command.target=command.path;command.remote_addr="192.168.1.20";
    command.headers={{"Host","controller.local"},{"Origin","http://controller.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
    command.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('e')},{"action","replace"},{"expected_authorization_generation","1"}}.dump();
    auto bootstrap=command;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;
    httplib::Response reply;check(sessions.establish(bootstrap,reply));const auto cookie=reply.get_header_value("Set-Cookie");
    command.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));command.headers.emplace("X-WsprryPi-CSRF",reply.get_header_value("X-WsprryPi-CSRF"));
    const auto principal=sessions.authorize_local_mutation(command,{true,"controller.local",{},{{"192.168.1.10","255.255.255.0"}}});
    check(principal&&principal->matches(*wtp_browser_http_request(command)));
    check(owner.accept_finished(*server.principal(),&sessions)==PiPairHistory::ManagementAccess::Current);
    check(!principal->matches(*wtp_browser_http_request(command)));
    const auto acknowledged=owner.snapshot();const auto active=parse_pi_pair_catalog(acknowledged.catalog.bytes,id('a'))["active"];
    check(active["previous"].is_null()&&active["current"]["epoch"]=="2"&&!owner.management_credential(old));
    const auto operations=parse_pi_pair_operations(acknowledged.operations.bytes,id('a'));
    check(operations["entries"].size()==256&&operations["entries"].back()["operation_id"]==id('d')&&
        operations["entries"].back()["continuation"].is_null()&&operations["entries"].back()["unresolved"]==false);
    check(owner.pair_operation(*server.principal(),id('d')).has_value()&&!owner.pair_operation(*old_server.principal(),id('d')));
    check(owner.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::Current&&owner.snapshot().authority->signature==acknowledged.authority->signature);
    check(owner.accept_finished(*old_server.principal())==PiPairHistory::ManagementAccess::Denied);
    auto rotation_key=PiPairRotationKey::generate();
    Json prepare{{"contract","Wsprry-Pair/1"},{"schema","rotate-prepare"},{"operation_id",id('6')},{"pair_id",id('c')},
        {"expected_epoch","2"},{"pA",rotation_key.public_point()},{"nonce",secret(9).storage_encoding()}};
    const auto prepared=owner.rotate(*server.principal(),prepare,sessions);check(prepared.has_value());
    const auto prepared_snapshot=owner.snapshot();
    check(owner.rotate(*server.principal(),prepare,sessions)==prepared&&owner.snapshot().authority->signature==prepared_snapshot.authority->signature);
    auto conflicting=prepare;conflicting["nonce"]=secret(8).storage_encoding();check(!owner.rotate(*server.principal(),conflicting,sessions));
    conflicting=prepare;conflicting["operation_id"]=id('7');check(!owner.rotate(*server.principal(),conflicting,sessions));
    check(!owner.rotate(*old_server.principal(),prepare,sessions));
    auto derived=rotation_key.derive_next(secret(2),2,prepare,*prepared,PiPairRotationKey::Role::Controller);
    const auto prepared_catalog=parse_pi_pair_catalog(prepared_snapshot.catalog.bytes,id('a'));
    check(prepared_catalog["active"]["pending"]["tls_psk"]==derived.storage_encoding()&&prepared_catalog["active"]["previous"].is_null());
    Json commit{{"contract","Wsprry-Pair/1"},{"schema","rotate-commit"},{"operation_id",id('6')},{"pair_id",id('c')},{"expected_epoch","2"},{"next_epoch","3"}};
    conflicting=commit;conflicting["operation_id"]=id('7');check(!owner.rotate(*server.principal(),conflicting,sessions));
    const auto committed=owner.rotate(*server.principal(),commit,sessions);check(committed&&(*committed)["epoch"]=="3");
    const auto committed_snapshot=owner.snapshot();
    check(owner.rotate(*server.principal(),commit,sessions)==committed&&owner.snapshot().authority->signature==committed_snapshot.authority->signature);
    check(owner.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::RotationRecovery&&owner.pair_operation(*server.principal(),id('6')));
    const PiPairTlsIdentity third{PiPairTlsPurpose::Management,id('c'),"1","3"};
    const auto [third_client_fd,third_server_fd]=sockets();
    PiPairTlsChannel third_client(third_client_fd,[&](const auto &requested){return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{requested,id('b'),id('a'),PiPairSecret::from_storage(derived.storage_encoding())});},third);
    PiPairTlsChannel third_server(third_server_fd,[&](const auto &requested){return owner.management_credential(requested);});establish(third_client,third_server);
    check(owner.accept_finished(*third_server.principal(),&sessions)==PiPairHistory::ManagementAccess::Current&&!owner.management_credential(next));
    check(!owner.rotate(*server.principal(),commit,sessions)&&owner.rotate(*third_server.principal(),commit,sessions)==committed);
    conflicting=commit;conflicting["next_epoch"]="4";check(!owner.rotate(*third_server.principal(),conflicting,sessions));
    // Already authenticated channels cannot restore authority after a durable
    // remove/replacement. Their copied TLS keys are not commit permission.
    const auto final_snapshot=owner.snapshot();auto revoked=parse_pi_pair_catalog(final_snapshot.catalog.bytes,id('a'));revoked["authorization_generation"]="2";
    auto record=revoked["active"];record["state"]="revoked";record["current"]=nullptr;record["pending"]=nullptr;record["previous"]=nullptr;
    revoked["active"]=nullptr;revoked["retired"].push_back(record);owner.publish(final_snapshot,revoked);
    check(!owner.management_credential(old)&&!owner.management_credential(next));
    check(owner.accept_finished(*server.principal())==PiPairHistory::ManagementAccess::Denied&&owner.available());
    check(!owner.rotate(*third_server.principal(),commit,sessions)&&!owner.management_credential(third));
    for(auto stage:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const auto path=root+"/ack-"+std::to_string(static_cast<int>(stage));
        {PiPairHistory first(path,id('a'));recover(first);rotating(first);}
        const auto child=fork();check(child>=0);
        if(!child){const auto point=std::to_string(static_cast<int>(stage));execl(argv[0],argv[0],"--ack-crash",path.c_str(),point.c_str(),nullptr);_exit(127);}
        int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
        PiPairHistory cold(path,id('a'));check(cold.available());const auto snapshot=cold.snapshot();
        const auto catalog=parse_pi_pair_catalog(snapshot.catalog.bytes,id('a')),book=parse_pi_pair_operations(snapshot.operations.bytes,id('a'));
        const bool committed=stage!=PiPairCatalogWritePoint::Prepared;
        check(catalog["active"]["previous"].is_null()==committed);
        check(book["entries"][0]["continuation"].is_null()==committed&&book["entries"][0]["unresolved"]==!committed);
        check(bool(cold.management_credential(old))==!committed&&bool(cold.management_credential(next)));
    }
    for(const auto *action:{"prepare","commit"})for(auto stage:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const bool committing=std::string(action)=="commit";const auto path=root+"/rotate-"+action+"-"+std::to_string(static_cast<int>(stage));
        {PiPairHistory first(path,id('a'));recover(first);ready_rotation(first,committing);}
        const auto child=fork();check(child>=0);
        if(!child){const auto point=std::to_string(static_cast<int>(stage));execl(argv[0],argv[0],"--rotation-crash",path.c_str(),point.c_str(),action,nullptr);_exit(127);}
        int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
        PiPairHistory cold(path,id('a'));check(cold.available());const auto snapshot=cold.snapshot();
        const auto catalog=parse_pi_pair_catalog(snapshot.catalog.bytes,id('a')),book=parse_pi_pair_operations(snapshot.operations.bytes,id('a'));
        const auto &active=catalog["active"];const bool published=stage!=PiPairCatalogWritePoint::Prepared;
        check(active["current"]["epoch"]==(committing&&published?"3":"2"));
        check((!active["pending"].is_null())==(committing?!published:published)&&(!active["previous"].is_null())==(committing&&published));
        const PiPairTlsIdentity epoch{PiPairTlsPurpose::Management,id('c'),"1","2"};const auto [a,b]=sockets();
        PiPairTlsChannel client(a,client_credential,epoch),server(b,[&](const auto &identity){return cold.management_credential(identity);});establish(client,server);
        ManagementBrowserSessions sessions;const auto replay=cold.rotate(*server.principal(),committing?rotation_commit():rotation_prepare(),sessions);check(replay.has_value());
        const auto receipt=cold.pair_operation(*server.principal(),rotation_prepare()["operation_id"]);check(receipt&&(*receipt)["state"]==(committing?"committed":"pending"));
        if(!committing&&published){
            const auto &retained=book["entries"].back()["continuation"]["prepared"];check(*replay==retained);
        }
    }
    {
        auto closing=std::make_shared<PiPairHistory>(root+"/closing",id('a'));recover(*closing);rotating(*closing);
        PiPairAdmission admission;ManagementBrowserSessions local_sessions;
        PiPairServiceLifetime lifetime(closing,admission,local_sessions);check(lifetime.open());
        const auto [a,b]=sockets();
        PiPairTlsChannel client(a,client_credential,next),server(b,[&](const auto &identity){return closing->management_credential(identity);});
        establish(client,server);const auto &principal=*server.principal();
        check(closing->accept_finished(principal)==PiPairHistory::ManagementAccess::Current);
        check(closing->management_authorization(principal).has_value());
        {auto guard=closing->admit_mutation(principal,false);check(guard.has_value());}
        const auto before_close=closing->snapshot().authority->signature;
        lifetime.close();
        check(!closing->management_credential(next)&&!closing->management_authorization(principal));
        check(!closing->admit_mutation(principal,false)&&closing->accept_finished(principal)==PiPairHistory::ManagementAccess::Denied);
        check(!closing->rotate(principal,rotation_prepare(),local_sessions));
        check(closing->snapshot().authority->signature==before_close);
        lifetime.drain();check(lifetime.open()&&closing->management_credential(next));
        lifetime.close();lifetime.drain();
    }
    std::cout<<"PASS actual TLS Finished and authoritative pair/epoch acknowledgement isolation: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
