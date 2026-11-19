// SPDX-License-Identifier: MIT
#include "pi_pair_retirement_client.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
namespace wsprrypi {
PiPairRetirementClient::PiPairRetirementClient(PiPairHistory &history,PiPairAdmission &admission)
    :history_(history),client_(admission){}
PiPairRetirementClient::PiPairRetirementClient(PiPairHistory &history,PiPairAdmission &admission,LoopbackTest test)
    :history_(history),client_(admission,PiPairClient::Access::LoopbackTest),test_(std::move(test)){}
PiPairRetirementClient::Result PiPairRetirementClient::exchange(const PiPairEndpoint &endpoint,const nlohmann::json &original,Mode mode,
    const Handoff &handoff,const std::function<bool()> &cancelled){
    bool attempted=false;
    try{
        if(!cancelled||cancelled()||!valid_pi_pair_document(original,"retirement")||
            (mode!=Mode::Request&&mode!=Mode::Status)||endpoint.port!=31418||
            endpoint.host!="wsprry-management-"+original["controller_id"].get<std::string>()+".local."||
            (mode==Mode::Request&&!handoff))return {};
        auto body=original;
        if(mode==Mode::Status){
            body.erase("proof");body.erase("assignment_revision");body.erase("retirement_generation");body["schema"]="retirement-status";
            const auto signed_status=history_.retirement_request(body);if(!signed_status)return {};body=*signed_status;
        }
        const PiPairTlsIdentity identity{PiPairTlsPurpose::Retirement,original["pair_id"].get<std::string>(),original["authorization_generation"].get<std::string>(),{}};
        const auto result=client_.exchange(test_?test_->endpoint:endpoint,original["controller_id"],
            [&](const auto &requested){return history_.retirement_client_credential(requested);},identity,
            [&](const auto &principal){return history_.retirement_finished(principal,false);},"POST",
            mode==Mode::Request?"/api/pair/v1/retirement":"/api/pair/v1/retirement/status",body.dump(),{},cancelled,
            [&](const auto &principal)->std::optional<PiPairMutationGuard>{
                auto guard=history_.admit_retirement_request(principal,body);
                if(!guard||cancelled())return {};
                // The owner records its exact frozen intent as attempted in
                // its local WAL before the first application write. No owner
                // lock survives this returned single-write admission scope.
                // A throwing handoff may already have committed its local
                // decision. Preserve uncertainty even without an HTTP write.
                attempted=true;
                if(mode==Mode::Request&&!handoff(*guard))return {};
                return guard;
            });
        if(!result.principal||!result.reply)return {attempted||mode==Mode::Status?State::Unknown:State::Unavailable,{}};
        if(cancelled())return {State::Unknown,{}};
        const auto &reply=result.reply->payload;
        if(reply.status==200){
            const auto response=parse_pi_pair_document(reply.body,"retired");
            auto proof=history_.retired_proof(*result.principal,original,response);
            if(proof)return {State::Retired,std::move(proof)};
        }else if(reply.status==202){
            const auto response=parse_pi_pair_document(reply.body,"operation");
            if(response["operation_id"]==original["transition_id"]&&response["pair_id"]==original["pair_id"]&&
                response["authorization_generation"]==original["authorization_generation"]&&response["state"]=="pending"&&response["result_digest"].is_null()&&
                response["request_digest"]==PiManagementJournal::digest(canonical_pi_management_json(original)))return {State::Pending,{}};
        }
        // An authenticated absence/refusal does not prove an earlier unknown
        // command was never accepted. Recovery never repeats retirement here.
        return {State::Unknown,{}};
    }catch(...){return {attempted||mode==Mode::Status?State::Unknown:State::Refused,{}};}
}
}
