// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_client.hpp"
#include "wtp_integration/pi_pair_server.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <iostream>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair client check "+std::to_string(checks));}
std::string id(char c){return std::string(32,c);}
const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,id('c'),"1","1"};
PiPairSecret secret(unsigned char value){std::array<unsigned char,32> bytes{};bytes.fill(value);return PiPairSecret(bytes);}
std::shared_ptr<const PiPairTlsCredential> credential(const PiPairTlsIdentity &requested,bool client,unsigned key=1){
    if(requested!=identity)return {};
    return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{identity,id(client?'b':'a'),id(client?'a':'b'),secret(key)});
}
}
int main(){try{
    using State=PiPairClientResult::State;
    PiPairAdmission budget;std::atomic_uint dispatches{0};std::atomic_bool lose_reply{false};
    PiPairServer server(budget,[](const auto &requested){return credential(requested,false);},[](const auto &){return true;},
        [&](const auto &,const httplib::Request &request,auto &,const auto &){
            ++dispatches;
            return PiPairHttpReply{{lose_reply?302U:200U,"{}","\"revision\""},"/api/control/v1/targets/"+id('a')+"/operations/"+id('d'),request.method=="PUT"?"settings":"status"};
        },PiPairServer::Access::LoopbackTest);
    check(server.start());PiPairClient client(budget,PiPairClient::Access::LoopbackTest);
    PiPairEndpoint endpoint{"127.0.0.1",server.port()};
    const auto lookup=[](const auto &requested){return credential(requested,true);};
    const auto admit=[](const auto &principal){return principal.self_id()==id('b')&&principal.peer_id()==id('a')&&principal.identity()==identity;};
    const auto path="/api/control/v1/targets/"+id('a')+"/settings";
    auto result=client.exchange(endpoint,id('a'),lookup,identity,admit,"GET",path);
    check(result.state==State::Replied&&result.reply&&result.reply->payload.status==200&&result.reply->payload.etag=="\"revision\""&&result.reply->location.ends_with(id('d'))&&result.reply->recovery_resource=="status"&&dispatches==1);
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}",{{"If-Match","\"revision\""}});
    check(result.state==State::Replied&&result.reply->recovery_resource=="settings"&&dispatches==2);
    auto count=dispatches.load();std::atomic_uint admissions{0};
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}",{},[]{return false;},
        [&](const auto &principal)->std::optional<PiPairMutationGuard>{check(admit(principal));return {};});
    check(result.state==State::Unavailable&&!result.reply&&dispatches==count);
    result=client.exchange(endpoint,id('a'),lookup,identity,[&](const auto &){return ++admissions==1;},"PUT",path,"{}");
    check(result.state==State::Unavailable&&!result.reply&&admissions==2&&dispatches==count);
    std::atomic_bool final_cancel{false};admissions=0;
    result=client.exchange(endpoint,id('a'),lookup,identity,[&](const auto &){if(++admissions==2)final_cancel=true;return true;},"PUT",path,"{}",{},[&]{return final_cancel.load();});
    check(result.state==State::Unavailable&&!result.reply&&dispatches==count);
    admissions=0;
    result=client.exchange(endpoint,id('a'),lookup,identity,[&](const auto &){if(++admissions==2)std::this_thread::sleep_for(std::chrono::seconds(9));return true;},"PUT",path,"{}");
    check(result.state==State::Unavailable&&!result.reply&&dispatches==count);
    result=client.exchange(endpoint,id('d'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Unavailable&&dispatches==count);
    result=client.exchange(endpoint,id('a'),[](const auto &requested){return credential(requested,true,2);},identity,admit,"PUT",path,"{}");check(result.state==State::Unavailable&&dispatches==count);
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}",{},[]{return true;});check(result.state==State::Refused&&dispatches==count);
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}",{{"Cookie","forbidden"}});check(result.state==State::Refused&&dispatches==count);
    result=client.exchange({"198.51.100.1",endpoint.port},id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Unavailable&&dispatches==count);
    PiPairClient production(budget);
    result=production.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Refused&&dispatches==count);
    result=production.exchange({"127.0.0.1",31418},id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Unavailable&&dispatches==count);
    lose_reply=true;result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Uncertain&&!result.reply&&dispatches==count+1);lose_reply=false;
    // The target mutation slot remains held until its actual worker exits.
    std::promise<void> entered,release;auto released=release.get_future().share();std::atomic_bool cancelled{false};
    auto pending=std::async(std::launch::async,[&]{return client.exchange(endpoint,id('a'),lookup,identity,[&](const auto &){entered.set_value();released.wait();return true;},"PUT",path,"{}",{},[&]{return cancelled.load();});});
    check(entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready);
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Refused);
    const auto child_path="/api/control/v1/targets/"+id('d')+"/pico/hostname";
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",child_path,"{}");
    check(result.state==State::Replied&&result.reply&&result.reply->payload.status==200);
    cancelled=true;result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Refused);
    release.set_value();check(pending.wait_for(std::chrono::seconds(2))==std::future_status::ready&&pending.get().state==State::Unavailable);
    result=client.exchange(endpoint,id('a'),lookup,identity,admit,"PUT",path,"{}");check(result.state==State::Replied);
    server.stop();budget.stop();result=client.exchange(endpoint,id('a'),lookup,identity,admit,"GET",path);check(result.state==State::Refused);
    std::cout<<"PASS actual bounded paired client, address/port refusal, recovery metadata and mutation uncertainty: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
