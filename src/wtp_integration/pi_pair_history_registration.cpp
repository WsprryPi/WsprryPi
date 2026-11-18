// SPDX-License-Identifier: MIT
#include "pi_pair_history.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_policy_admission.hpp"
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("authority_registration_unavailable");}
const Json &profile(const Json &peers,const std::string &selector){for(const auto &row:peers["profiles"])if(row["profile_id"]==selector)return row;throw std::runtime_error("authority_profile_unavailable");}
Json *pending(Json &book,const Json &profile){
    Json *found=nullptr;
    for(auto &row:book["entries"]){const auto &continuation=row["continuation"];
        if(row["scope"]=="device"&&row["pair_id"]==profile["pair"]["pair_id"]&&!continuation.is_null()&&continuation["kind"]=="authority-registration-intent"){
            require(!found&&continuation["profile_id"]==profile["profile_id"]&&continuation["profile_generation"]==profile["profile_generation"]);found=&row;
        }
    }return found;
}
bool selected(const Json &row,const PiPairPrincipal &principal,const std::string &self){return row["stage"]=="active"&&
    principal.self_id()==self&&row["target_id"]==principal.peer_id()&&principal.identity().purpose==PiPairTlsPurpose::Management&&
    row["pair"]["pair_id"]==principal.identity().pair_id&&row["pair"]["authorization_generation"]==principal.identity().authorization_generation&&
    row["pair"]["current"]["epoch"]==principal.identity().epoch;}
}
std::optional<Json> PiPairHistory::outgoing_authority_registration(const std::string &selector){
    std::lock_guard lock(mutex_);
    try{
        require(service_permits());verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());
        const auto peers=parse_pi_pair_peers(before.peers->bytes,self_);auto book=parse_pi_pair_operations(before.operations.bytes,self_);
        const auto &row=profile(peers,selector);require(row["stage"]=="active");
        const auto intent=pending(book,row);return intent?std::optional<Json>((*intent)["continuation"]):std::nullopt;
    }catch(...){return {};}
}
std::optional<Json> PiPairHistory::stage_authority_registration(const std::string &selector,const Json &request){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||!service_permits())return {};
    try{
        require(valid_pi_pair_document(request,"authority-register")&&request["controller_id"]==self_);verify_selection();
        const auto before=store_->snapshot();require(before.peers.has_value());auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
        const auto &row=profile(peers,selector);require(row["stage"]=="active"&&request["target_id"]==row["target_id"]&&
            request["pair_id"]==row["pair"]["pair_id"]&&request["authorization_generation"]==row["pair"]["authorization_generation"]);
        if(const auto intent=pending(book,row))return (*intent)["continuation"];
        const auto digest=PiManagementJournal::digest(canonical_pi_management_json(request));
        for(const auto &entry:book["entries"])if(entry["operation_id"]==request["operation_id"]){
            require(entry["scope"]=="device"&&entry["principal_id"]==self_&&entry["target_id"]==row["target_id"]&&entry["pair_id"]==request["pair_id"]&&
                entry["authorization_generation"]==request["authorization_generation"]&&entry["request_digest"]==digest&&entry["state"]=="committed"&&entry["result_digest"]==digest);
            return Json{{"stage","complete"}};
        }
        const Json continuation{{"kind","authority-registration-intent"},{"self_id",self_},{"peer_id",row["target_id"]},
            {"profile_id",selector},{"profile_generation",row["profile_generation"]},{"stage","prepared"},{"request",request}};
        book["entries"].push_back({{"scope","device"},{"principal_id",self_},{"target_id",row["target_id"]},{"pair_id",request["pair_id"]},
            {"authorization_generation",request["authorization_generation"]},{"operation_id",request["operation_id"]},{"request_digest",digest},
            {"state","pending"},{"result_digest",nullptr},{"unresolved",true},{"receipt",nullptr},{"continuation",continuation}});
        const auto bounded=pi_pair_operations(self_,book["entries"]);require(!bounded.evicted);
        require(publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),bounded.document,peers));return continuation;
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_authority_registration(const std::string &selector,const std::string &generation,
    const PiPairPrincipal &principal,const std::string &operation,bool absence,bool counters){
    auto guard=admit_outgoing_mutation(selector,generation,principal,counters);if(!guard)return {};
    try{
        verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
        const auto &row=profile(peers,selector);const auto intent=pending(book,row);
        require(selected(row,principal,self_)&&intent&&(*intent)["operation_id"]==operation&&
            ((*intent)["continuation"]["stage"]=="prepared"||absence));
        if((*intent)["continuation"]["stage"]=="prepared"){
            (*intent)["continuation"]["stage"]="awaiting_receipt";
            require(publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),book,peers));
        }
        return guard;
    }catch(...){return {};}
}
bool PiPairHistory::accept_authority_registration(const std::string &selector,const std::string &generation,const PiPairPrincipal &principal,const Json &reply,const std::function<void()> &retire){
    auto guard=admit_outgoing_mutation(selector,generation,principal,false);if(!guard)return false;
    try{
        require(valid_pi_pair_document(reply,"operation")&&reply["state"]=="committed");verify_selection();
        const auto before=store_->snapshot();require(before.peers.has_value());auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
        const auto &row=profile(peers,selector);auto intent=pending(book,row);require(selected(row,principal,self_)&&intent&&
            reply["operation_id"]==(*intent)["operation_id"]&&reply["pair_id"]==(*intent)["pair_id"]&&reply["authorization_generation"]==(*intent)["authorization_generation"]&&
            reply["request_digest"]==(*intent)["request_digest"]&&reply["result_digest"]==(*intent)["request_digest"]);
        (*intent)["state"]="committed";(*intent)["result_digest"]=reply["result_digest"];(*intent)["unresolved"]=false;(*intent)["continuation"]=nullptr;
        Json ordered=Json::array();for(const auto &entry:book["entries"])if(entry["operation_id"]!=reply["operation_id"])ordered.push_back(entry);ordered.push_back(*intent);
        const auto bounded=pi_pair_operations(self_,ordered);
        if(bounded.evicted){require(bool(retire));retire();}
        return publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),bounded.document,peers);
    }catch(...){return false;}
}
} // namespace wsprrypi
