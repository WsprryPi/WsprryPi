// SPDX-License-Identifier: MIT
#include "pi_pair_connect.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
#include "privileged_network_runtime.hpp"
#include <openssl/rand.h>
#include <array>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("pairing_connect_unavailable");}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string nonce(){std::array<unsigned char,32> value{};require(RAND_bytes(value.data(),static_cast<int>(value.size()))==1);return PiPairSecret(value).storage_encoding();}
PicoHttpResponse receipt(const Json &value){require(valid_controller_document(value,"target-operation"));return {value["state"]=="committed"?200U:202U,value.dump(),{}};}
}
PiPairConnect::PiPairConnect(PiPairHistory &history,ManagementBrowserSessions &sessions,PiPairDiscovery &discovery,PiPairAdmission &admission,
    std::string self,std::string boot):history_(history),sessions_(sessions),discovery_(discovery),admission_(admission),self_(std::move(self)),boot_(std::move(boot)),client_(admission){require(id(self_)&&id(boot_));}
PiPairConnect::PiPairConnect(PiPairHistory &history,ManagementBrowserSessions &sessions,PiPairDiscovery &discovery,PiPairAdmission &admission,
    std::string self,std::string boot,LoopbackTest test):history_(history),sessions_(sessions),discovery_(discovery),admission_(admission),
    self_(std::move(self)),boot_(std::move(boot)),test_(std::move(test)),bootstrap_(PiPairSocket::Access::LoopbackTest),client_(admission,PiPairClient::Access::LoopbackTest){require(id(self_)&&id(boot_));}
PicoHttpResponse PiPairConnect::connect(const BrowserRequest &request,const LocalBrowserPrincipal &principal,const std::function<bool()> &external_cancel){
    std::optional<Json> staged;
    try{
        require(bool(external_cancel)&&request.method=="POST"&&request.path=="/api/control/v1/host/pairing/connect"&&request.body.size()<=32768&&
            principal.matches(request)&&sessions_.owns(principal));
        const auto intent=strict_browser_json(request.body,8);require(valid_controller_document(intent,"pair-connect"));
        const auto operation=intent["operation_id"].get<std::string>();
        if(history_.outgoing_operation_known(operation))return controller_target_error(409,"request_id_reuse");
        auto code=PiPairCode::normalize(intent["code"].get_ref<const std::string &>());
        const auto observation=discovery_.candidate(intent["discovery_id"].get<std::string>());
        require(observation&&(*observation)["device_id"]==intent["expected_device_id"]&&(*observation)["device_id"]!=self_);
        const auto target=(*observation)["device_id"].get<std::string>();
        const PiPairEndpoint locator{(*observation)["host"].get<std::string>(),(*observation)["bootstrap_port"].get<unsigned>()};
        auto connection=admission_.try_outgoing();if(!connection)return controller_target_error(429,"capacity");
        auto mutation=connection->try_mutation(target);if(!mutation)return controller_target_error(409,"authority_pending");
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        const auto cancelled=[&]{const auto policy=privileged_network_runtime_state();return external_cancel()||std::chrono::steady_clock::now()>=end||
            !policy.active_known||policy.active!=PrivilegedNetworkMode::enforced||policy.configured!=PrivilegedNetworkMode::enforced;};
        const auto invoke=[&](const std::string &resource,const Json &body=nullptr){
            const auto result=bootstrap_.request(test_?test_->bootstrap:locator,resource,body,*connection,end,cancelled);
            require(result.state==PiPairBootstrapResult::State::Replied&&result.reply&&result.reply->payload.status==200);
            return Json::parse(result.reply->payload.body);
        };
        const auto offer=invoke("window");require(offer["open"]==true&&offer["device_id"]==target&&offer["boot_id"]==(*observation)["boot_id"]);
        Json hello{{"contract","Wsprry-Pair/1"},{"schema","hello"},{"suite",offer["suite"]},{"window_id",offer["window_id"]},{"pair_id",offer["pair_id"]},
            {"action",offer["action"]},{"controller_id",self_},{"controller_boot_id",boot_},{"target_id",target},{"target_boot_id",offer["boot_id"]},
            {"authorization_generation",offer["authorization_generation"]},{"controller_nonce",nonce()}};
        const auto challenge=invoke("hello",hello);require(!cancelled());PiPairExchange exchange(PiPairExchange::Role::Controller,code,hello,challenge);
        require(!cancelled());const auto confirm=exchange.controller_confirmation(invoke("exchange",exchange.start_controller()));
        exchange.controller_confirmed(invoke("confirm",confirm));require(!cancelled());
        const auto current=discovery_.candidate(intent["discovery_id"].get<std::string>());require(current&&*current==*observation);
        staged=history_.stage_outgoing_initial(request,principal,sessions_,*observation,exchange);
        auto keys=exchange.keys();
        const Json commit{{"contract","Wsprry-Pair/1"},{"schema","commit"},{"pair_id",hello["pair_id"]},{"request_digest",exchange.request_digest()},
            {"proof",pi_pair_mac(keys.commit,"request/"+exchange.request_digest())}};
        // Never repeat this write. Durable pending-key readback handles even
        // an absent, malformed or unauthenticated public commit response.
        if(!cancelled())(void)bootstrap_.request(test_?test_->bootstrap:locator,"commit",commit,*connection,end,cancelled,[&]{return history_.admit_initial_commit(operation);});
        mutation.reset();connection->cancel();connection.reset();
        if(!cancelled())if(const auto confirmed=recover(operation,cancelled))return receipt(*confirmed);
        return receipt(*staged);
    }catch(const PiPairConnectRefusal &){
        return controller_target_error(429,"capacity");
    }catch(...){if(staged)return receipt(*staged);return controller_target_error(503,"transport_unavailable");}
}
std::optional<Json> PiPairConnect::recover(const std::string &operation,const std::function<bool()> &cancelled){
    try{
        require(id(operation)&&bool(cancelled)&&!cancelled());const auto pending=history_.outgoing_initial(operation);if(!pending)return {};
        const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,(*pending)["pair_id"].get<std::string>(),(*pending)["authorization_generation"].get<std::string>(),"1"};
        const PiPairEndpoint endpoint{(*pending)["endpoint"]["host"].get<std::string>(),31418};
        const auto result=client_.exchange(test_?test_->management:endpoint,(*pending)["target_id"].get<std::string>(),
            [&](const auto &requested){return history_.initial_credential(operation,requested);},identity,
            [&](const auto &principal){return principal.self_id()==self_&&(*pending)["target_id"]==principal.peer_id()&&principal.identity()==identity&&bool(history_.initial_credential(operation,identity));},
            "GET","/api/pair/v1/operations/"+identity.pair_id,"",{},cancelled);
        require(result.state==PiPairClientResult::State::Replied&&result.reply&&result.principal&&result.reply->payload.status==200);
        const auto reply=parse_pi_pair_document(result.reply->payload.body,"operation");
        require(history_.accept_outgoing_initial(operation,*result.principal,reply,sessions_));
        auto outcome=(*pending)["receipt"];outcome["state"]="committed";outcome["revision"]=(*pending)["request_digest"];return outcome;
    }catch(...){return {};}
}
}
