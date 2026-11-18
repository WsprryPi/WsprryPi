// SPDX-License-Identifier: MIT
#include "pi_pair_history.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_policy_admission.hpp"
#include "controller_contract.hpp"
#include "local_browser_principal.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <array>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("retirement_authorization_unavailable");}
std::string mac(const PiPairSecret &key,const Json &body,std::string_view prefix={}){
    const auto bytes=std::string(prefix)+canonical_pi_management_json(body);std::array<unsigned char,32> result{};std::size_t size{};
    require(EVP_Q_mac(nullptr,"HMAC",nullptr,"SHA256",nullptr,key.bytes().data(),key.bytes().size(),
        reinterpret_cast<const unsigned char *>(bytes.data()),bytes.size(),result.data(),result.size(),&size)&&size==result.size());
    // Canonical unpadded base64url, using the existing fixed 32-byte encoding.
    return PiPairSecret(result).storage_encoding();
}
bool proof(const PiPairSecret &key,Json body,std::string_view prefix={}){
    if(!body.contains("proof")||!body["proof"].is_string())return false;
    const auto provided=body["proof"].get<std::string>();body.erase("proof");const auto expected=mac(key,body,prefix);
    return provided.size()==expected.size()&&CRYPTO_memcmp(provided.data(),expected.data(),expected.size())==0;
}
Json unsigned_body(Json value){value.erase("proof");return value;}
}
std::optional<PiPairResumeTargetProof> PiPairHistory::incoming_resume_target(const BrowserRequest &request,const LocalBrowserPrincipal &principal){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        require(running_marked_&&service_permits()&&principal.matches(request)&&request.method=="POST"&&
            request.path=="/api/control/v1/targets/"+self_+"/migration/rollback");
        const auto body=strict_browser_json(request.body,16);require(valid_controller_document(body,"target-migration-rollback")&&body["binding"]["device_id"]==self_);
        verify_selection();const auto state=store_->snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);const auto &pair=catalog["active"];
        require(!pair.is_null()&&pair["state"]=="active"&&body["resume_controller_id"]==pair["peer_id"]);
        const Json scope{{"pair_id",pair["pair_id"]},{"authorization_generation",pair["authorization_generation"]},{"controller_id",pair["peer_id"]}};
        return PiPairResumeTargetProof(this,scope,PiManagementJournal::digest(canonical_pi_management_json(body)),principal.lease_,
            std::min(principal.deadline_,std::chrono::steady_clock::now()+std::chrono::seconds(30)));
    }catch(...){return {};}
}
std::optional<PiPairResumeTargetProof> PiPairHistory::incoming_resume_target(const BrowserRequest &request,const PiPairPrincipal &principal){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        require(running_marked_&&service_permits()&&request.method=="POST"&&request.path=="/api/control/v1/targets/"+self_+"/migration/rollback");
        const auto body=strict_browser_json(request.body,16);require(valid_controller_document(body,"target-migration-rollback")&&body["binding"]["device_id"]==self_);
        verify_selection();const auto state=store_->snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);const auto &pair=catalog["active"];
        require(!pair.is_null()&&pair["state"]=="active"&&principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management&&
            pair["peer_id"]==principal.peer_id()&&pair["pair_id"]==principal.identity().pair_id&&pair["authorization_generation"]==principal.identity().authorization_generation&&
            pair["current"]["epoch"]==principal.identity().epoch&&body["resume_controller_id"]==principal.peer_id());
        return PiPairResumeTargetProof(this,{{"pair_id",pair["pair_id"]},{"authorization_generation",pair["authorization_generation"]},{"controller_id",pair["peer_id"]}},
            PiManagementJournal::digest(canonical_pi_management_json(body)),principal);
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_resume_target(const PiPairResumeTargetProof &proof){
    if(proof.owner_!=this||std::chrono::steady_clock::now()>=proof.until_)return {};
    if(proof.principal_)return admit_mutation(*proof.principal_);
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        const auto lease=proof.lease_.lock();require(lease&&lease->load()&&running_marked_&&service_permits());
        verify_selection();const auto state=store_->snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);const auto &pair=catalog["active"];
        require(!pair.is_null()&&pair["state"]=="active"&&pair["peer_id"]==proof.scope_["controller_id"]&&pair["pair_id"]==proof.scope_["pair_id"]&&
            pair["authorization_generation"]==proof.scope_["authorization_generation"]);
        return PiPairMutationGuard(std::move(policy.configuration),std::move(policy.admission),std::move(lock));
    }catch(...){return {};}
}
std::optional<PiPairSecret> PiPairHistory::retirement_verifier(const PiPairTlsIdentity &identity,bool server,const std::string &peer){
    require(service_permits()&&identity.purpose==PiPairTlsPurpose::Retirement);(void)pi_pair_tls_identity(identity);verify_selection();const auto state=store_->snapshot();
    const auto selected=[&](const Json &pair)->std::optional<PiPairSecret>{
        if(pair.is_null()||(pair["state"]!="active"&&pair["state"]!="revoked")||pair["pair_id"]!=identity.pair_id||
            pair["authorization_generation"]!=identity.authorization_generation||pair["peer_id"]!=peer||pair["retirement_verifier"].is_null())return {};
        return PiPairSecret::from_storage(pair["retirement_verifier"].get_ref<const std::string &>());
    };
    if(server){
        require(state.peers.has_value());const auto peers=parse_pi_pair_peers(state.peers->bytes,self_);
        for(const auto *group:{"profiles","retired"})for(const auto &row:peers[group]){
            if(row["pair"]["pair_id"]!=identity.pair_id||row["pair"]["authorization_generation"]!=identity.authorization_generation)continue;
            if(std::string_view(group)=="profiles"&&row["stage"]!="active"&&row["stage"]!="access_removed")return {};
            require(row["target_id"]==peer);return selected(row["pair"]);
        }
    }else{
        const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);
        if(!catalog["active"].is_null()&&catalog["active"]["pair_id"]==identity.pair_id)return selected(catalog["active"]);
        for(const auto &pair:catalog["retired"])if(pair["pair_id"]==identity.pair_id)return selected(pair);
    }
    return {};
}
bool PiPairHistory::retirement_finished(const PiPairPrincipal &principal,bool server){
    std::lock_guard lock(mutex_);
    try{return principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Retirement&&
        bool(retirement_verifier(principal.identity(),server,principal.peer_id()));}catch(...){return false;}
}
std::optional<PiPairRetirementPrincipal> PiPairHistory::authorize_retirement(const PiPairPrincipal &principal,const Json &request){
    std::lock_guard lock(mutex_);
    try{
        const bool status=request.is_object()&&request.value("schema",Json(nullptr))=="retirement-status";
        require(valid_pi_pair_document(request,status?"retirement-status":"retirement")&&principal.self_id()==self_&&
            request["controller_id"]==self_&&request["target_id"]==principal.peer_id()&&request["pair_id"]==principal.identity().pair_id&&
            request["authorization_generation"]==principal.identity().authorization_generation&&principal.identity().purpose==PiPairTlsPurpose::Retirement);
        const auto key=retirement_verifier(principal.identity(),true,principal.peer_id());require(key&&proof(*key,request));
        return PiPairRetirementPrincipal(this,principal,request);
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_retirement(const PiPairRetirementPrincipal &principal){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        require(principal.owner_==this&&std::chrono::steady_clock::now()<principal.until_&&principal.request_["schema"]=="retirement"&&
            principal.principal_.self_id()==self_&&principal.request_["controller_id"]==self_&&principal.request_["target_id"]==principal.principal_.peer_id());
        const auto key=retirement_verifier(principal.principal_.identity(),true,principal.principal_.peer_id());require(key&&proof(*key,principal.request_));
        return PiPairMutationGuard(std::move(policy.configuration),std::move(policy.admission),std::move(lock));
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_retirement_request(const PiPairPrincipal &principal,const Json &request){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        const bool status=request.is_object()&&request.value("schema",Json(nullptr))=="retirement-status";
        require(running_marked_&&valid_pi_pair_document(request,status?"retirement-status":"retirement")&&
            principal.identity().purpose==PiPairTlsPurpose::Retirement&&principal.self_id()==self_&&request["target_id"]==self_&&
            request["controller_id"]==principal.peer_id()&&request["pair_id"]==principal.identity().pair_id&&
            request["authorization_generation"]==principal.identity().authorization_generation);
        const auto key=retirement_verifier(principal.identity(),false,principal.peer_id());require(key&&proof(*key,request));
        return PiPairMutationGuard(std::move(policy.configuration),std::move(policy.admission),std::move(lock));
    }catch(...){return {};}
}
std::optional<Json> PiPairHistory::retirement_request(const Json &unsigned_request){
    std::lock_guard lock(mutex_);
    try{
        require(unsigned_request.is_object()&&!unsigned_request.contains("proof"));auto result=unsigned_request;result["proof"]=std::string(43,'A');
        const bool status=result.value("schema",Json(nullptr))=="retirement-status";
        require(valid_pi_pair_document(result,status?"retirement-status":"retirement")&&result["target_id"]==self_);
        const PiPairTlsIdentity identity{PiPairTlsPurpose::Retirement,result["pair_id"].get<std::string>(),result["authorization_generation"].get<std::string>(),{}};
        const auto key=retirement_verifier(identity,false,result["controller_id"].get<std::string>());require(key.has_value());
        result["proof"]=mac(*key,unsigned_request);require(valid_pi_pair_document(result,status?"retirement-status":"retirement"));return result;
    }catch(...){return {};}
}
std::optional<Json> PiPairHistory::retirement_response(const Json &request,const Json &receipt){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        require(valid_pi_pair_document(request,"retirement")&&valid_controller_document(receipt,"controller-retirement-operation")&&
            request["controller_id"]==self_&&receipt["controller_id"]==self_&&receipt["target_id"]==request["target_id"]&&
            receipt["assignment_id"]==request["assignment_id"]&&receipt["state"]=="retired");
        // The source book can bind a fresh machine request to a permanent
        // earlier local/browser fence for this exact frozen assignment. Its
        // historical Control receipt keeps its own original transition; the
        // fresh signed Pair response echoes the target's current transition.
        const PiPairTlsIdentity identity{PiPairTlsPurpose::Retirement,request["pair_id"].get<std::string>(),request["authorization_generation"].get<std::string>(),{}};
        const auto key=retirement_verifier(identity,true,request["target_id"].get<std::string>());require(key&&proof(*key,request));
        auto response=unsigned_body(request);response["schema"]="retired";response["receipt_digest"]=PiManagementJournal::digest(canonical_pi_management_json(receipt));
        response["proof"]=mac(*key,response);require(valid_pi_pair_document(response,"retired"));return response;
    }catch(...){return {};}
}
std::optional<PiPairRetiredProof> PiPairHistory::retired_proof(const PiPairPrincipal &principal,const Json &request,const Json &response){
    std::lock_guard lock(mutex_);
    try{
        require(valid_pi_pair_document(request,"retirement")&&valid_pi_pair_document(response,"retired")&&principal.self_id()==self_&&
            principal.identity().purpose==PiPairTlsPurpose::Retirement&&request["target_id"]==self_&&request["controller_id"]==principal.peer_id()&&
            request["pair_id"]==principal.identity().pair_id&&request["authorization_generation"]==principal.identity().authorization_generation);
        auto expected=unsigned_body(request);expected["schema"]="retired";auto received=unsigned_body(response);received.erase("receipt_digest");
        require(received==expected);const auto key=retirement_verifier(principal.identity(),false,principal.peer_id());
        require(key&&proof(*key,request)&&proof(*key,response));return PiPairRetiredProof(response);
    }catch(...){return {};}
}
std::optional<Json> PiPairHistory::resume_acknowledgement(const Json &unsigned_ack){
    std::lock_guard lock(mutex_);
    try{
        require(unsigned_ack.is_object()&&!unsigned_ack.contains("proof"));auto response=unsigned_ack;response["proof"]=std::string(43,'A');
        require(valid_pi_pair_document(response,"resume-ack")&&response["controller_id"]==self_);
        const PiPairTlsIdentity identity{PiPairTlsPurpose::Retirement,response["pair_id"].get<std::string>(),response["authorization_generation"].get<std::string>(),{}};
        const auto key=retirement_verifier(identity,true,response["target_id"].get<std::string>());require(key.has_value());
        response["proof"]=mac(*key,unsigned_ack,"Wsprry-Pair/1/resume-ack/");require(valid_pi_pair_document(response,"resume-ack"));return response;
    }catch(...){return {};}
}
std::optional<PiPairResumeOutcomeProof> PiPairHistory::resume_outcome_proof(const PiPairPrincipal &principal,const Json &response){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
    try{
        require(valid_pi_pair_document(response,"resume-ack")&&principal.identity().purpose==PiPairTlsPurpose::Management&&
            principal.self_id()==self_&&response["target_id"]==self_&&response["controller_id"]==principal.peer_id());
        verify_selection();const auto catalog=parse_pi_pair_catalog(store_->snapshot().catalog.bytes,self_);const auto &active=catalog["active"];
        require(!active.is_null()&&active["state"]=="active"&&active["peer_id"]==principal.peer_id()&&
            active["pair_id"]==principal.identity().pair_id&&active["authorization_generation"]==principal.identity().authorization_generation&&
            active["current"]["epoch"]==principal.identity().epoch);
        const PiPairTlsIdentity original{PiPairTlsPurpose::Retirement,response["pair_id"].get<std::string>(),response["authorization_generation"].get<std::string>(),{}};
        const auto key=retirement_verifier(original,false,principal.peer_id());
        require(key&&proof(*key,response,"Wsprry-Pair/1/resume-ack/"));return PiPairResumeOutcomeProof(this,principal,response);
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_resume_outcome(const PiPairResumeOutcomeProof &outcome){
    try{
        require(outcome.owner_==this&&std::chrono::steady_clock::now()<outcome.until_);
        auto guard=admit_mutation(outcome.principal_);if(!guard)return {};
        // The actual current pair is still checked by normal mutation
        // admission. Recheck the old continuity key while that owner is locked.
        const auto &response=outcome.document_;
        require(valid_pi_pair_document(response,"resume-ack")&&response["target_id"]==self_&&response["controller_id"]==outcome.principal_.peer_id());
        const PiPairTlsIdentity original{PiPairTlsPurpose::Retirement,response["pair_id"].get<std::string>(),response["authorization_generation"].get<std::string>(),{}};
        const auto key=retirement_verifier(original,false,outcome.principal_.peer_id());
        require(key&&proof(*key,response,"Wsprry-Pair/1/resume-ack/"));return guard;
    }catch(...){return {};}
}
} // namespace wsprrypi
