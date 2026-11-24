// SPDX-License-Identifier: MIT
#include "pi_pair_history.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_http_principals.hpp"
#include "pi_pair_local_routes.hpp"
#include "pi_pair_policy_admission.hpp"
#include "management_browser_session.hpp"
#include "controller_contract.hpp"
#include <charconv>
#include <set>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
bool identity(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool enforced(){const auto state=privileged_network_runtime_state();return state.active_known&&
    state.active==PrivilegedNetworkMode::enforced&&state.configured==PrivilegedNetworkMode::enforced;}
}
PicoHttpResponse PiPairHistory::local_pairing_receipt(const BrowserRequest &request,const LocalPairingReadPrincipal &principal){
    const auto operation=local_pairing_operation_id(request.path);
    if(!operation||!principal.matches(request)||!enforced())return controller_target_error(403,"unauthorized");
    std::lock_guard guard(mutex_);
    try{
        verify_selection();const auto state=store_->snapshot();const auto book=parse_pi_pair_operations(state.operations.bytes,self_);
        for(const auto &entry:book["entries"])if(entry["operation_id"]==std::string(*operation)&&!entry["receipt"].is_null()&&
            ((entry["scope"]=="local"&&entry["target_id"]==self_)||
             (entry["scope"]=="device"&&entry["principal_id"]==self_))){
            if(!valid_controller_document(entry["receipt"],"target-operation"))return controller_target_error(503,"storage_unavailable");
            return {200,entry["receipt"].dump(),{}};
        }
        return controller_target_error(404,"operation_unknown");
    }catch(...){return controller_target_error(503,"storage_unavailable");}
}
PicoHttpResponse PiPairHistory::remove_access(const BrowserRequest &request,const LocalBrowserPrincipal &principal,
    ManagementBrowserSessions &sessions,const std::string &boot){
    if(request.method!="POST"||request.path!="/api/control/v1/host/pairing/remove"||
        !principal.matches(request)||!sessions.owns(principal))return controller_target_error(403,"csrf_failed");
    if(request.body.size()>4096)return controller_target_error(413,"payload_too_large");
    Json intent;
    try{intent=strict_browser_json(request.body,8);}catch(...){return controller_target_error(400,"invalid_json");}
    if(!valid_controller_document(intent,"pair-remove")||!identity(boot))return controller_target_error(400,"invalid_shape");
    PiPairPolicyAdmission policy;if(!policy.allowed())return controller_target_error(403,"unauthorized");
    std::unique_lock guard(mutex_,std::try_to_lock);if(!guard.owns_lock()||!service_permits())return controller_target_error(409,"authority_pending");
    try{
        verify_selection();const auto before=store_->snapshot();
        auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
        const auto digest=PiManagementJournal::digest(canonical_pi_management_json(Json{{"method",request.method},{"path",request.path},{"intent",intent}}));
        const auto operation=intent["operation_id"].get<std::string>(),scope=principal.session_scope();
        const auto retained=[&](const Json &record)->std::optional<Json>{
            for(const auto &entry:record["entries"])if(entry["operation_id"]==operation){
                if(entry["scope"]=="local"&&entry["principal_id"]==scope&&entry["target_id"]==self_&&entry["request_digest"]==digest&&!entry["receipt"].is_null())return entry["receipt"];
                throw std::invalid_argument("pairing_request_id_reuse");
            }
            return {};
        };
        if(const auto replay=retained(book))return {200,replay->dump(),{}};
        if(catalog["authorization_generation"]!=intent["expected_authorization_generation"])return controller_target_error(409,"revision_conflict");
        if(catalog["active"].is_null())return controller_target_error(409,"confirmation_required");
        if(catalog["active"]["state"]!="active")return controller_target_error(503,"authority_pending");
        const auto generation=catalog["authorization_generation"].get<std::string>();std::uint64_t count{};
        const auto parsed=std::from_chars(generation.data(),generation.data()+generation.size(),count);
        if(parsed.ec!=std::errc{}||parsed.ptr!=generation.data()+generation.size()||!count||count==UINT64_MAX||catalog["retired"].size()>=32)
            return controller_target_error(429,"capacity");
        const auto next=std::to_string(count+1);auto revoked=catalog["active"];const auto old_pair=revoked["pair_id"];
        revoked["state"]="revoked";revoked["current"]=nullptr;revoked["pending"]=nullptr;revoked["previous"]=nullptr;
        catalog["retired"].push_back(std::move(revoked));catalog["active"]=nullptr;catalog["authorization_generation"]=next;
        // Resolve only this target's own exact rotation. Other unknown work,
        // outgoing pairs and all native scheduling/settings receipts survive.
        std::set<std::string> completed;
        for(auto &entry:book["entries"])if(entry["scope"]=="paired"&&entry["pair_id"]==old_pair&&!entry["continuation"].is_null()){
            completed.insert(entry["operation_id"].get<std::string>());entry["continuation"]=nullptr;entry["unresolved"]=false;
            if(entry["state"]!="committed"){
                entry["state"]="rejected";entry["result_digest"]=nullptr;
                if(!entry["receipt"].is_null()){
                    entry["receipt"]["state"]="rejected";entry["receipt"]["revision"]=nullptr;
                    entry["receipt"]["error"]={{"code","unauthorized"},{"retryable",false}};
                }
            }
        }
        Json ordered=Json::array();for(const auto &entry:book["entries"])if(!completed.contains(entry["operation_id"].get<std::string>()))ordered.push_back(entry);
        for(const auto &entry:book["entries"])if(completed.contains(entry["operation_id"].get<std::string>()))ordered.push_back(entry);
        const auto resolved=pi_pair_operations(self_,ordered);
        const Json outcome{{"contract","Wsprry-Control/1"},{"schema","pairing-state"},{"device_id",self_},{"boot_id",boot},
            {"authorization_generation",next},{"state","unpaired"},{"manager",nullptr},{"window",nullptr}};
        const Json receipt{{"contract","Wsprry-Control/1"},{"schema","target-operation"},
            {"binding",{{"device_id",self_},{"boot_id",boot},{"profile_generation",generation},{"connection_generation","1"}}},
            {"operation_id",operation},{"request_digest",digest},{"state","committed"},
            {"revision",PiManagementJournal::digest(canonical_pi_management_json(outcome))},{"error",nullptr}};
        if(!valid_controller_document(receipt,"target-operation"))throw std::runtime_error("pairing_receipt_invalid");
        auto entries=resolved.document["entries"];
        entries.push_back({{"scope","local"},{"principal_id",scope},{"target_id",self_},{"pair_id",nullptr},{"authorization_generation",generation},
            {"operation_id",operation},{"request_digest",digest},{"state","committed"},
            {"result_digest",PiManagementJournal::digest(canonical_pi_management_json(receipt))},{"unresolved",false},{"receipt",receipt},{"continuation",nullptr}});
        const auto bounded=pi_pair_operations(self_,entries);auto peers=parse_pi_pair_peers(before.peers->bytes,self_);prepare_usage(before,catalog,peers);
        try{
            store_->publish(before,catalog,bounded.document,peers,PiPairCatalogStore::Checkpoint::Preserve,[&]{
                if(!principal.matches(request)||!sessions.owns(principal))throw std::runtime_error("pairing_local_intent_expired");
            });
            selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());
        }catch(...){
            sessions.retire();
            if(!store_->recover_own_failure()){store_.reset();throw;}
            selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());
            const auto recovered=parse_pi_pair_operations(store_->snapshot().operations.bytes,self_);
            if(const auto committed=retained(recovered))return {200,committed->dump(),{}};
            return controller_target_error(503,"storage_unavailable");
        }
        sessions.retire();return {200,receipt.dump(),{}};
    }catch(const std::invalid_argument &error){
        if(std::string_view(error.what())=="pairing_request_id_reuse")return controller_target_error(409,"request_id_reuse");
        return controller_target_error(503,"storage_unavailable");
    }catch(...){return controller_target_error(503,"storage_unavailable");}
}
}
