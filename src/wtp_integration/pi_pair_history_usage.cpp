// SPDX-License-Identifier: MIT
#include "pi_pair_history.hpp"
#include "pi_pair_usage.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_peers.hpp"
#include "config_handler.hpp"
#include "privileged_network_runtime.hpp"
#include "pi_pair_policy_admission.hpp"
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("pairing_usage_unavailable");}
using Key=std::tuple<std::string,std::string,std::string>;
Key key(const Json &pair,const Json &slot){return {pair["pair_id"].get<std::string>(),pair["authorization_generation"].get<std::string>(),slot["epoch"].get<std::string>()};}
bool current(const Json &active,const PiPairPrincipal &principal,const std::string &self){return
    principal.self_id()==self&&principal.identity().purpose==PiPairTlsPurpose::Management&&!active.is_null()&&active["state"]=="active"&&
    active["peer_id"]==principal.peer_id()&&active["pair_id"]==principal.identity().pair_id&&
    active["authorization_generation"]==principal.identity().authorization_generation&&active["current"]["epoch"]==principal.identity().epoch;}
template<class F>void current(Json &catalog,Json &peers,F visit){
    if(!catalog["active"].is_null()&&catalog["active"]["state"]=="active")visit(catalog["active"]);
    for(auto &row:peers["profiles"])if(row["stage"]=="active")visit(row["pair"]);
}
}
void PiPairHistory::prepare_usage(const PiPairCatalogStore::Snapshot &before,Json &catalog,Json &peers){
    require(valid_pi_pair_catalog(catalog,self_)&&valid_pi_pair_peers(peers,self_));if(!running_marked_)return;
    require(before.peers.has_value());
    // Project the still-authoritative before-image. Registering a proposed new
    // epoch here would erase an old unclean-renewal hold on prepared rollback.
    auto measured=usage_->project(parse_pi_pair_catalog(before.catalog.bytes,self_),parse_pi_pair_peers(before.peers->bytes,self_));
    std::map<Key,Json> slots;current(measured.catalog,measured.peers,[&](const auto &pair){slots.emplace(key(pair,pair["current"]),pair["current"]);});
    const auto apply=[&](auto &pair){for(const auto *name:{"current","pending","previous"})if(!pair[name].is_null()){
        auto &slot=pair[name];const auto found=slots.find(key(pair,slot));if(found!=slots.end()){
            require(slot["tls_psk"]==found->second["tls_psk"]);slot["service_ms"]=found->second["service_ms"];slot["connections"]=found->second["connections"];
        }
    }};
    if(!catalog["active"].is_null())apply(catalog["active"]);
    for(auto &row:peers["profiles"])apply(row["pair"]);
}
void PiPairHistory::reconcile_usage(const PiPairCatalogStore::Snapshot &state){
    if(running_marked_)usage_->reconcile(parse_pi_pair_catalog(state.catalog.bytes,self_),parse_pi_pair_peers(state.peers->bytes,self_));
}
bool PiPairHistory::record_outgoing_finished(const std::string &profile,const PiPairPrincipal &principal){
    std::lock_guard guard(mutex_);
    try{
        require(running_marked_&&principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management);
        verify_selection();const auto state=store_->snapshot();auto peers=parse_pi_pair_peers(state.peers->bytes,self_);bool matched=false;
        for(const auto &row:peers["profiles"])if(row["profile_id"]==profile&&row["stage"]=="active"&&row["target_id"]==principal.peer_id()&&
            row["pair"]["pair_id"]==principal.identity().pair_id&&row["pair"]["authorization_generation"]==principal.identity().authorization_generation&&
            row["pair"]["current"]["epoch"]==principal.identity().epoch)matched=true;
        require(matched);reconcile_usage(state);usage_->authenticated(principal.identity());return true;
    }catch(...){return false;}
}
bool PiPairHistory::renewal_due(const PiPairPrincipal &principal){
    std::lock_guard guard(mutex_);
    try{
        require(running_marked_&&principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management);verify_selection();const auto state=store_->snapshot();
        const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);const auto &active=catalog["active"];
        require(!active.is_null()&&active["peer_id"]==principal.peer_id()&&active["pair_id"]==principal.identity().pair_id&&
            active["authorization_generation"]==principal.identity().authorization_generation&&active["current"]["epoch"]==principal.identity().epoch);
        reconcile_usage(state);return usage_->due(principal.identity());
    }catch(...){return true;}
}
bool PiPairHistory::outgoing_renewal_due(const std::string &profile){
    std::lock_guard guard(mutex_);
    try{
        require(running_marked_);verify_selection();const auto state=store_->snapshot();auto peers=parse_pi_pair_peers(state.peers->bytes,self_);reconcile_usage(state);
        for(const auto &row:peers["profiles"])if(row["profile_id"]==profile&&row["stage"]=="active"){
            const auto &pair=row["pair"];return usage_->due({PiPairTlsPurpose::Management,pair["pair_id"].get<std::string>(),pair["authorization_generation"].get<std::string>(),pair["current"]["epoch"].get<std::string>()});
        }
    }catch(...){}
    return true;
}
std::optional<Json> PiPairHistory::management_authorization(const PiPairPrincipal &principal){
    std::lock_guard guard(mutex_);
    try{
        require(service_permits());
        const auto policy=privileged_network_runtime_state();require(policy.active_known&&policy.active==PrivilegedNetworkMode::enforced&&policy.configured==PrivilegedNetworkMode::enforced);
        verify_selection();const auto state=store_->snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);
        require(current(catalog["active"],principal,self_));reconcile_usage(state);
        return Json{{"pair_id",principal.identity().pair_id},{"authorization_generation",principal.identity().authorization_generation},
            {"renewal_required",!running_marked_||usage_->due(principal.identity())}};
    }catch(...){return {};}
}
bool PiPairHistory::observe_management(const PiPairPrincipal &principal,const std::function<void()> &observe){
    try{
        PiPairPolicyAdmission policy;require(policy.allowed()&&bool(observe));
        std::unique_lock guard(mutex_,std::try_to_lock);require(guard.owns_lock()&&running_marked_&&service_permits());
        verify_selection();const auto state=store_->snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);
        require(current(catalog["active"],principal,self_));
        // No transport, settings action, counter checkpoint or ordinary write
        // guard escapes this callback. Only this backend can call the method.
        observe();return true;
    }catch(...){return false;}
}
bool PiPairHistory::observe_outgoing_management(const std::string &profile,const std::string &generation,
    const PiPairPrincipal &principal,const std::function<void()> &observe){
    try{
        PiPairPolicyAdmission policy;require(policy.allowed()&&bool(observe));
        std::unique_lock guard(mutex_,std::try_to_lock);require(guard.owns_lock()&&running_marked_&&service_permits());
        verify_selection();const auto state=store_->snapshot();require(state.peers.has_value());const auto peers=parse_pi_pair_peers(state.peers->bytes,self_);
        for(const auto &row:peers["profiles"])if(row["profile_id"]==profile&&row["profile_generation"]==generation&&row["stage"]=="active"){
            const auto &pair=row["pair"];
            require(principal.self_id()==self_&&principal.peer_id()==row["target_id"].get_ref<const std::string &>()&&principal.identity()==PiPairTlsIdentity{
                PiPairTlsPurpose::Management,pair["pair_id"].get<std::string>(),pair["authorization_generation"].get<std::string>(),pair["current"]["epoch"].get<std::string>()});
            // Enrollment evidence only. Due keys may still observe; no network,
            // settings mutation or ordinary-write admission escapes this scope.
            observe();return true;
        }
    }catch(...){return false;}
    return false;
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_mutation(const PiPairPrincipal &principal,bool checkpoint_counters){
    try{
        std::unique_lock configuration(config_update_mutex(),std::try_to_lock);require(configuration.owns_lock());
        std::unique_lock admission(privileged_network_admission_mutex(),std::try_to_lock);require(admission.owns_lock());
        std::unique_lock history(mutex_,std::try_to_lock);require(history.owns_lock()&&running_marked_&&service_permits());
        const auto policy=privileged_network_runtime_state();require(policy.active_known&&policy.active==PrivilegedNetworkMode::enforced&&policy.configured==PrivilegedNetworkMode::enforced);
        verify_selection();const auto state=store_->snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self_);
        require(current(catalog["active"],principal,self_));reconcile_usage(state);require(!usage_->due(principal.identity()));
        // Counter checkpoint is part of this local mutation admission, never
        // a connection/read. It grants no settings/output authority itself.
        if(checkpoint_counters)checkpoint(PiPairCatalogStore::Checkpoint::Preserve);
        return PiPairMutationGuard(std::move(configuration),std::move(admission),std::move(history));
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_outgoing_mutation(const std::string &profile,const std::string &generation,
    const PiPairPrincipal &principal,bool checkpoint_counters){
    try{
        PiPairPolicyAdmission policy;require(policy.allowed()&&!restored_remote_authority_held());std::unique_lock history(mutex_,std::try_to_lock);require(history.owns_lock()&&running_marked_&&service_permits());
        require(principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management);verify_selection();const auto state=store_->snapshot();
        const auto peers=parse_pi_pair_peers(state.peers->bytes,self_);bool matched=false;
        for(const auto &row:peers["profiles"])if(row["profile_id"]==profile&&row["profile_generation"]==generation&&row["stage"]=="active"&&
            row["target_id"]==principal.peer_id()&&row["pair"]["pair_id"]==principal.identity().pair_id&&row["pair"]["authorization_generation"]==principal.identity().authorization_generation&&
            row["pair"]["current"]["epoch"]==principal.identity().epoch&&row["pair"]["pending"].is_null()&&row["pair"]["previous"].is_null())matched=true;
        require(matched);reconcile_usage(state);require(!usage_->due(principal.identity()));
        if(checkpoint_counters)checkpoint(PiPairCatalogStore::Checkpoint::Preserve);
        return PiPairMutationGuard(std::move(policy.configuration),std::move(policy.admission),std::move(history));
    }catch(...){return {};}
}
}
