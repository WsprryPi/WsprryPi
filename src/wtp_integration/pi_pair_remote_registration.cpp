// SPDX-License-Identifier: MIT
#include "pi_pair_remote_controller.hpp"
#include "application_recovery/restore_hold.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_policy_admission.hpp"
#include "controller_contract.hpp"
#include "browser_api.hpp"
#include "fleet_runtime.hpp"
#include <algorithm>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("authority_registration_unavailable");}
}
std::size_t PiPairHistory::reclaim_outgoing_retired(const std::string &selector,const PiPairPrincipal &principal,const Json &migration){
    PiPairPolicyAdmission policy;if(!policy.allowed())return 0;
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return 0;
    try{
        require(running_marked_&&service_permits()&&valid_controller_document(migration,"target-migration-read"));verify_selection();const auto before=store_->snapshot();
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_);const auto operations=parse_pi_pair_operations(before.operations.bytes,self_);
        const Json *selected=nullptr;for(const auto &row:peers["profiles"])if(row["profile_id"]==selector)selected=&row;
        require(selected&&(*selected)["stage"]=="active"&&principal.identity().purpose==PiPairTlsPurpose::Management&&principal.self_id()==self_&&
            (*selected)["target_id"]==principal.peer_id()&&migration["binding"]["device_id"]==principal.peer_id()&&
            (*selected)["pair"]["pair_id"]==principal.identity().pair_id&&(*selected)["pair"]["authorization_generation"]==principal.identity().authorization_generation&&
            (*selected)["pair"]["current"]["epoch"]==principal.identity().epoch);
        const auto count=peers["retired"].size();Json retained=Json::array();
        for(const auto &row:peers["retired"]){const auto &pair=row["pair"];bool unknown=false;
            for(const auto &operation:operations["entries"])if(operation["pair_id"]==pair["pair_id"]&&operation["authorization_generation"]==pair["authorization_generation"]&&
                (operation["unresolved"]==true||!operation["continuation"].is_null()))unknown=true;
            if(row["target_id"]!=principal.peer_id()||unknown||!wtp_fleet_retirement_scope_closed(pair,migration))retained.push_back(row);
        }
        if(retained.size()==count)return 0;
        peers["retired"]=std::move(retained);require(publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),operations,peers));
        return count-peers["retired"].size();
    }catch(...){return 0;}
}
void PiPairRemoteController::authority_source(AuthoritySource source){std::lock_guard lock(observer_mutex_);authority_source_=std::move(source);}
void PiPairRemoteController::receipt_eviction_observer(std::function<void()> retire){std::lock_guard lock(observer_mutex_);retire_contexts_=std::move(retire);}
bool PiPairRemoteController::maintain(const std::string &selector,const std::function<bool()> &cancelled){
    if(restored_remote_authority_held())return false;
    AuthoritySource source;{std::lock_guard lock(observer_mutex_);source=authority_source_;}
    if(!source)return observe(selector,cancelled);
    try{
        const auto state=entry(selector);bool registration=false;
        {
            std::unique_lock lock(state->publication,std::try_to_lock);if(!lock.owns_lock())return false;
            registration=state->authority_turn&&std::chrono::steady_clock::now()>=state->registration_next;
            state->authority_turn=!state->authority_turn;
        }
        if(!registration)return observe(selector,cancelled);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(24);
        const auto bounded=[&]{return (cancelled&&cancelled())||std::chrono::steady_clock::now()>=deadline;};
        const auto ready=register_authority(selector,std::move(source),bounded);
        if(ready&&!bounded()){
            const auto history=history_();if(history){
                const auto peers=parse_pi_pair_peers(history->snapshot().peers->bytes,self_);
                bool retained=false;for(const auto &row:peers["retired"])if(row["profile_id"]==selector)retained=true;
                if(retained){const auto selection=select(*history,selector);
                    const auto reply=exchange(*history,selection,"/api/control/v1/targets/"+selection.target+"/migration",bounded);
                    if(reply.principal&&reply.reply&&reply.reply->payload.status==200&&!bounded()&&select(*history,selector)==selection)
                        (void)history->reclaim_outgoing_retired(selector,*reply.principal,strict_browser_json(reply.reply->payload.body,16));
                }
            }
        }
        {
            std::lock_guard lock(state->publication);
            if(ready){state->registration_failures=0;state->registration_next=std::chrono::steady_clock::now()+std::chrono::seconds(1);}
            else {state->registration_failures=std::min(state->registration_failures+1,6U);state->registration_next=std::chrono::steady_clock::now()+std::chrono::seconds(1U<<(state->registration_failures-1));}
        }
        // Registration readiness never claims migration completeness or hides
        // otherwise verified management inventory. Retry has its own backoff.
        return true;
    }catch(...){return false;}
}
bool PiPairRemoteController::register_authority(const std::string &selector,AuthoritySource source,const std::function<bool()> &cancelled){
    if(restored_remote_authority_held())return false;
    try{
        require(bool(cancelled)&&!cancelled());const auto history=history_();require(bool(history));const auto selection=select(*history,selector);const auto state=entry(selector);
        std::unique_lock registration(state->registration,std::try_to_lock);if(!registration.owns_lock())return false;
        auto admission=PicoManagementJournal::target_admission(directory_,selection.target);if(!admission)return false;
        {
            std::unique_lock lock(state->publication,std::try_to_lock);require(lock.owns_lock()&&state->selection==selection&&
                std::chrono::steady_clock::now()-state->observed<=std::chrono::seconds(30));
            bool native=false;for(const auto &target:state->targets)if(target["device_id"]==selection.target&&target["product"]=="WsprryPi")native=true;
            if(!native)return true;
        }
        auto intent=history->outgoing_authority_registration(selector);
        if(!intent){
            const auto result=source();require(result.status==200);const auto book=strict_browser_json(result.body);
            require(valid_controller_document(book,"controller-retirements")&&book["controller_id"]==self_);
            Json assignments=Json::array();
            for(const auto &row:book["assignments"])if(row["target_id"]==selection.target){
                require(row["assignment_revision"].is_string());
                assignments.push_back({{"assignment_id",row["assignment_id"]},{"assignment_revision",row["assignment_revision"]},{"state",row["state"]}});
            }
            Json request{{"contract","Wsprry-Pair/1"},{"schema","authority-register"},{"pair_id",selection.identity.pair_id},
                {"authorization_generation",selection.identity.authorization_generation},{"target_id",selection.target},{"controller_id",self_},
                {"controller_incarnation",book["controller_incarnation"]},{"controller_generation",book["generation"]},{"assignments",assignments},
                {"endpoint",{{"host","wsprry-management-"+self_+".local."},{"port",31418}}}};
            request["operation_id"]=PiManagementJournal::digest(canonical_pi_management_json(Json{{"purpose","authority-registration/1"},{"request",request}})).substr(0,32);
            require(valid_pi_pair_document(request,"authority-register")&&select(*history,selector)==selection&&!cancelled());
            intent=history->stage_authority_registration(selector,request);require(intent.has_value());
        }
        if((*intent)["stage"]=="complete"){state->registration_absence=false;return true;}
        const auto request=(*intent)["request"];const auto operation=request["operation_id"].get<std::string>();
        require(request["target_id"]==selection.target&&request["pair_id"]==selection.identity.pair_id&&request["authorization_generation"]==selection.identity.authorization_generation);
        if(!state->registration_selection||!(*state->registration_selection==selection)||state->registration_operation!=operation){
            state->registration_selection=selection;state->registration_operation=operation;state->registration_absence=false;state->registration_context_ready=false;
        }
        const bool send=(*intent)["stage"]=="prepared"||state->registration_absence;
        if(send&&!state->registration_context_ready){
            const BrowserRequest request{"GET","/api/control/v1/targets/"+selection.target+"/context",{},{},false};
            const auto result=read(request,selector,cancelled);
            require(result.payload.status==200&&!cancelled()&&select(*history,selector)==selection);
            const auto context=strict_browser_json(result.payload.body);
            require(valid_controller_document(context,"target-context")&&context["target"]["product"]=="WsprryPi"&&context["binding"]["device_id"]==selection.target&&
                !context["authorization"].is_null()&&context["authorization"]["pair_id"]==selection.identity.pair_id&&
                context["authorization"]["authorization_generation"]==selection.identity.authorization_generation&&context["authorization"]["renewal_required"]==false);
            state->registration_context_ready=true;return true;
        }
        state->registration_absence=false;state->registration_context_ready=false;
        if(!send){
            const auto result=exchange(*history,selection,"/api/pair/v1/operations/"+operation,cancelled);
            require(result.reply&&result.principal&&!cancelled()&&select(*history,selector)==selection);
            if(result.reply->payload.status==200){
                const auto reply=parse_pi_pair_document(result.reply->payload.body,"operation");std::function<void()> retire;
                {std::lock_guard lock(observer_mutex_);retire=retire_contexts_;}
                return history->accept_authority_registration(selector,selection.generation,*result.principal,reply,retire);
            }
            if(result.reply->payload.status==404){
                const auto absent=parse_pi_pair_document(result.reply->payload.body,"result");
                require(absent["pair_id"]==selection.identity.pair_id&&absent["state"]=="rejected"&&absent["code"]=="operation_unknown");
                // A later pass may replay only this exact durable registration,
                // after current authenticated absence. Native saves/Stop are
                // never replayed by this worker. The target's monotonic source
                // generation, retained assignments and operation digest fence it.
                state->registration_absence=true;
            }
            return false;
        }
        bool recorded=false,checkpoint=false;
        const auto result=client_.exchange(test_?test_->endpoint:selection.endpoint,selection.target,
            [&](const auto &identity){return history->outgoing_credential(selector,identity);},selection.identity,
            [&](const auto &principal){try{if(cancelled()||!(select(*history,selector)==selection))return false;
                if(!recorded){recorded=history->record_outgoing_finished(selector,principal);}return recorded;}catch(...){return false;}},
            "POST","/api/pair/v1/authorities/register",request.dump(),{},cancelled,[&](const auto &principal){
                auto guard=history->admit_authority_registration(selector,selection.generation,principal,operation,true,!checkpoint);if(guard)checkpoint=true;return guard;
            });
        if(!result.reply||!result.principal||cancelled()||!(select(*history,selector)==selection))return false;
        if(result.reply->payload.status!=200)return false;
        const auto reply=parse_pi_pair_document(result.reply->payload.body,"operation");std::function<void()> retire;
        {std::lock_guard lock(observer_mutex_);retire=retire_contexts_;}
        return history->accept_authority_registration(selector,selection.generation,*result.principal,reply,retire);
    }catch(...){return false;}
}
} // namespace wsprrypi
