// SPDX-License-Identifier: MIT
#include "pi_pair_history.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_rotation.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
#include "management_browser_session.hpp"
#include "pi_pair_policy_admission.hpp"
#include <openssl/rand.h>
#include <array>
#include <charconv>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("outgoing_pair_unavailable");}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::uint64_t epoch(const Json &value){const auto &text=value.get_ref<const std::string &>();std::uint64_t number{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&number);return number;}
Json &profile(Json &peers,const std::string &id){for(auto &row:peers["profiles"])if(row["profile_id"]==id)return row;throw std::runtime_error("outgoing_pair_unavailable");}
Json *rotation(Json &book,const Json &row){
    Json *found=nullptr;
    for(auto &entry:book["entries"])if(entry["scope"]=="device"&&entry["pair_id"]==row["pair"]["pair_id"]&&!entry["continuation"].is_null()&&entry["continuation"]["kind"]=="rotation-intent"){
        require(!found&&entry["continuation"]["profile_id"]==row["profile_id"]);found=&entry;
    }
    return found;
}
bool bound(const Json &row,const PiPairPrincipal &principal,const std::string &self){const auto &pair=row["pair"];
    return row["stage"]=="active"&&pair["state"]=="active"&&principal.self_id()==self&&row["target_id"]==principal.peer_id()&&
        principal.identity().purpose==PiPairTlsPurpose::Management&&pair["pair_id"]==principal.identity().pair_id&&
        pair["authorization_generation"]==principal.identity().authorization_generation;
}
Json commit(const Json &intent){return {{"contract","Wsprry-Pair/1"},{"schema","rotate-commit"},{"operation_id",intent["operation_id"]},
    {"pair_id",intent["pair_id"]},{"expected_epoch",intent["expected_epoch"]},{"next_epoch",intent["prepared"]["next_epoch"]}};}
Json committed(const Json &intent){return {{"contract","Wsprry-Pair/1"},{"schema","rotate-committed"},{"operation_id",intent["operation_id"]},
    {"pair_id",intent["pair_id"]},{"epoch",intent["prepared"]["next_epoch"]}};}
}
bool PiPairHistory::publish_outgoing(const PiPairCatalogStore::Snapshot &before,Json catalog,const Json &operations,Json peers){
    prepare_usage(before,catalog,peers);
    try{store_->publish(before,catalog,operations,peers);}
    catch(...){require(store_->recover_own_failure());}
    const auto after=store_->snapshot();selected_=after.authority;verify_selection();reconcile_usage(after);
    return parse_pi_pair_catalog(after.catalog.bytes,self_)==catalog&&parse_pi_pair_operations(after.operations.bytes,self_)==operations&&
        after.peers&&parse_pi_pair_peers(after.peers->bytes,self_)==peers;
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_rotation_handoff(const std::string &selector,const std::string &generation,
    const std::string &operation,const PiPairPrincipal &principal){
    try{
        PiPairPolicyAdmission policy;require(policy.allowed());std::unique_lock guard(mutex_,std::try_to_lock);require(guard.owns_lock()&&running_marked_&&service_permits());
        verify_selection();const auto state=store_->snapshot();auto peers=parse_pi_pair_peers(state.peers->bytes,self_),book=parse_pi_pair_operations(state.operations.bytes,self_);
        const auto &row=profile(peers,selector);const auto *intent=rotation(book,row);
        require(bound(row,principal,self_)&&row["profile_generation"]==generation&&intent&&(*intent)["operation_id"]==operation&&
            (*intent)["unresolved"]==true&&(*intent)["continuation"]["expected_epoch"]==principal.identity().epoch);
        return PiPairMutationGuard(std::move(policy.configuration),std::move(policy.admission),std::move(guard));
    }catch(...){return {};}
}
std::shared_ptr<const PiPairTlsCredential> PiPairHistory::outgoing_credential(const std::string &selector,const PiPairTlsIdentity &identity){
    std::lock_guard guard(mutex_);
    try{
        require(service_permits()&&id(selector)&&identity.purpose==PiPairTlsPurpose::Management);verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_);const auto &row=profile(peers,selector),&pair=row["pair"];
        require(row["stage"]=="active"&&pair["pair_id"]==identity.pair_id&&pair["authorization_generation"]==identity.authorization_generation);
        for(const auto *name:{"current","pending","previous"})if(!pair[name].is_null()&&pair[name]["epoch"]==identity.epoch)
            return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{identity,self_,row["target_id"].get<std::string>(),
                PiPairSecret::from_storage(pair[name]["tls_psk"].get_ref<const std::string &>())});
    }catch(...){}
    return {};
}
std::optional<Json> PiPairHistory::prepare_outgoing_rotation(const std::string &selector,const std::string &operation){
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock guard(mutex_,std::try_to_lock);if(!guard.owns_lock()||!service_permits())return {};
    try{
        require(id(selector)&&id(operation));verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
        auto &row=profile(peers,selector);auto &pair=row["pair"];require(row["stage"]=="active"&&pair["state"]=="active");
        if(const auto retained=rotation(book,row)){
            require((*retained)["operation_id"]==operation&&(*retained)["continuation"]["stage"]=="awaiting_prepare");
            return (*retained)["continuation"]["prepare"];
        }
        for(const auto &entry:book["entries"])require(entry["operation_id"]!=operation);
        require(pair["pending"].is_null()&&pair["previous"].is_null());const auto number=epoch(pair["current"]["epoch"]);require(number<UINT64_MAX);
        auto key=PiPairRotationKey::generate();std::array<unsigned char,32> nonce{};require(RAND_bytes(nonce.data(),static_cast<int>(nonce.size()))==1);
        Json request{{"contract","Wsprry-Pair/1"},{"schema","rotate-prepare"},{"operation_id",operation},{"pair_id",pair["pair_id"]},
            {"expected_epoch",pair["current"]["epoch"]},{"pA",key.public_point()},{"nonce",PiPairSecret(nonce).storage_encoding()}};
        Json intent{{"kind","rotation-intent"},{"self_id",self_},{"peer_id",row["target_id"]},{"profile_id",selector},{"pair_id",pair["pair_id"]},
            {"authorization_generation",pair["authorization_generation"]},{"operation_id",operation},{"expected_epoch",pair["current"]["epoch"]},
            {"stage","awaiting_prepare"},{"prepare",request},{"prepared",nullptr},{"private_key",key.export_private().storage_encoding()}};
        book["entries"].push_back({{"scope","device"},{"principal_id",self_},{"target_id",row["target_id"]},{"pair_id",pair["pair_id"]},
            {"authorization_generation",pair["authorization_generation"]},{"operation_id",operation},{"request_digest",PiManagementJournal::digest(canonical_pi_management_json(request))},
            {"state","pending"},{"result_digest",nullptr},{"unresolved",true},{"receipt",nullptr},{"continuation",std::move(intent)}});
        const auto bounded=pi_pair_operations(self_,book["entries"]);require(!bounded.evicted);
        require(publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),bounded.document,peers));return request;
    }catch(...){return {};}
}
std::optional<Json> PiPairHistory::outgoing_rotation_request(const std::string &selector){
    std::lock_guard guard(mutex_);
    try{
        require(id(selector));verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);const auto &row=profile(peers,selector);
        require(row["stage"]=="active");const auto entry=rotation(book,row);require(entry);
        const auto &intent=(*entry)["continuation"];
        return intent["stage"]=="awaiting_prepare"?intent["prepare"]:commit(intent);
    }catch(...){return {};}
}
bool PiPairHistory::accept_outgoing_rotation(const std::string &selector,const PiPairPrincipal &principal,const Json &reply){
    const bool prepared=valid_pi_pair_document(reply,"rotate-prepared");
    if(!prepared&&!valid_pi_pair_document(reply,"rotate-committed"))return false;
    std::lock_guard guard(mutex_);
    try{
        require(id(selector));verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);auto &row=profile(peers,selector);
        require(bound(row,principal,self_));auto &pair=row["pair"];auto entry=rotation(book,row);require(entry);
        auto &intent=(*entry)["continuation"];require(reply["operation_id"]==intent["operation_id"]&&intent["expected_epoch"]==principal.identity().epoch);
        if(prepared){
            if(intent["stage"]=="awaiting_commit")return intent["prepared"]==reply;
            require(intent["stage"]=="awaiting_prepare"&&pair["current"]["epoch"]==intent["expected_epoch"]&&pair["pending"].is_null()&&pair["previous"].is_null());
            const auto number=epoch(intent["expected_epoch"]);require(number<UINT64_MAX&&epoch(reply["next_epoch"])==number+1);
            auto key=PiPairRotationKey::restore(PiPairSecret::from_storage(intent["private_key"].get_ref<const std::string &>()),intent["prepare"]["pA"].get_ref<const std::string &>());
            auto next=key.derive_next(PiPairSecret::from_storage(pair["current"]["tls_psk"].get_ref<const std::string &>()),number,intent["prepare"],reply,PiPairRotationKey::Role::Controller);
            pair["pending"]={{"epoch",reply["next_epoch"]},{"tls_psk",next.storage_encoding()},{"service_ms","0"},{"connections","0"}};
            intent["stage"]="awaiting_commit";intent["prepared"]=reply;intent["private_key"]=nullptr;
        }else {
            require(intent["stage"]=="awaiting_commit"&&committed(intent)==reply);
            if((*entry)["state"]=="committed")return pair["current"]["epoch"]==reply["epoch"];
            require(pair["current"]["epoch"]==intent["expected_epoch"]&&!pair["pending"].is_null()&&pair["pending"]["epoch"]==reply["epoch"]&&pair["previous"].is_null());
            pair["previous"]=pair["current"];pair["current"]=pair["pending"];pair["pending"]=nullptr;
            (*entry)["state"]="committed";(*entry)["result_digest"]=PiManagementJournal::digest(canonical_pi_management_json(reply));
            pair["receipt"]={{"operation_id",intent["operation_id"]},{"request_digest",(*entry)["request_digest"]},{"result_digest",(*entry)["result_digest"]}};
        }
        const auto bounded=pi_pair_operations(self_,book["entries"]);require(!bounded.evicted);
        return publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),bounded.document,peers);
    }catch(...){return false;}
}
bool PiPairHistory::acknowledge_outgoing_rotation(const std::string &selector,const PiPairPrincipal &principal,ManagementBrowserSessions &sessions){
    std::lock_guard guard(mutex_);
    try{
        require(id(selector));verify_selection();const auto before=store_->snapshot();require(before.peers.has_value());
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);auto &row=profile(peers,selector);
        require(bound(row,principal,self_));auto &pair=row["pair"];const auto entry=rotation(book,row);
        if(!entry)return pair["current"]["epoch"]==principal.identity().epoch&&pair["pending"].is_null()&&pair["previous"].is_null();
        auto &intent=(*entry)["continuation"];require(intent["stage"]=="awaiting_commit"&&intent["prepared"]["next_epoch"]==principal.identity().epoch);
        if(!pair["pending"].is_null()){
            require(pair["pending"]["epoch"]==principal.identity().epoch&&pair["current"]["epoch"]==intent["expected_epoch"]&&pair["previous"].is_null());
            pair["current"]=pair["pending"];pair["pending"]=nullptr;
        }
        require(pair["current"]["epoch"]==principal.identity().epoch);pair["previous"]=nullptr;
        const auto reply=committed(intent);(*entry)["state"]="committed";(*entry)["result_digest"]=PiManagementJournal::digest(canonical_pi_management_json(reply));
        pair["receipt"]={{"operation_id",intent["operation_id"]},{"request_digest",(*entry)["request_digest"]},{"result_digest",(*entry)["result_digest"]}};
        (*entry)["continuation"]=nullptr;(*entry)["unresolved"]=false;
        Json ordered=Json::array();for(const auto &row:book["entries"])if(row["operation_id"]!=(*entry)["operation_id"])ordered.push_back(row);ordered.push_back(*entry);
        const auto bounded=pi_pair_operations(self_,ordered);if(bounded.evicted)sessions.retire();
        return publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),bounded.document,peers);
    }catch(...){return false;}
}
} // namespace wsprrypi

namespace wsprrypi {
std::shared_ptr<const PiPairTlsCredential> PiPairHistory::retirement_server_credential(const PiPairTlsIdentity &identity){
    std::lock_guard guard(mutex_);
    try{
        if(!service_permits()||identity.purpose!=PiPairTlsPurpose::Retirement)return {};
        (void)pi_pair_tls_identity(identity);verify_selection();const auto state=store_->snapshot();
        const auto peers=parse_pi_pair_peers(state.peers->bytes,self_);
        for(const auto *group:{"profiles","retired"})for(const auto &row:peers[group]){
            const auto &pair=row["pair"];
            if(pair["pair_id"]!=identity.pair_id||pair["authorization_generation"]!=identity.authorization_generation)continue;
            if(std::string_view(group)=="profiles"&&row["stage"]!="active"&&row["stage"]!="access_removed")return {};
            if(pair["retirement_verifier"].is_null())return {};
            const auto target=row["target_id"].get<std::string>();
            const auto verifier=PiPairSecret::from_storage(pair["retirement_verifier"].get_ref<const std::string &>());
            return std::make_shared<PiPairTlsCredential>(PiPairTlsCredential{identity,self_,target,
                pi_pair_retirement_tls_key(verifier,target,self_,identity.pair_id,identity.authorization_generation)});
        }
    }catch(...){return {};}
    return {};
}
std::shared_ptr<const PiPairTlsCredential> PiPairHistory::retirement_client_credential(const PiPairTlsIdentity &identity){
    std::lock_guard guard(mutex_);
    try{
        if(!service_permits()||identity.purpose!=PiPairTlsPurpose::Retirement)return {};
        (void)pi_pair_tls_identity(identity);verify_selection();const auto state=store_->snapshot();
        const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);
        const auto credential=[&](const nlohmann::json &pair)->std::shared_ptr<const PiPairTlsCredential>{
            if(pair.is_null()||(pair["state"]!="active"&&pair["state"]!="revoked")||pair["pair_id"]!=identity.pair_id||pair["authorization_generation"]!=identity.authorization_generation||pair["retirement_verifier"].is_null())return {};
            const auto controller=pair["peer_id"].get<std::string>();
            const auto verifier=PiPairSecret::from_storage(pair["retirement_verifier"].get_ref<const std::string &>());
            return std::make_shared<PiPairTlsCredential>(PiPairTlsCredential{identity,self_,controller,
                pi_pair_retirement_tls_key(verifier,self_,controller,identity.pair_id,identity.authorization_generation)});
        };
        if(const auto active=credential(catalog["active"]))return active;
        for(const auto &pair:catalog["retired"])if(const auto retained=credential(pair))return retained;
    }catch(...){return {};}
    return {};
}
}
