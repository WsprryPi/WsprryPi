// SPDX-License-Identifier: MIT
#include "pi_pair_history.hpp"
#include "pi_pair_connect.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_contract.hpp"
#include "local_browser_principal.hpp"
#include "management_browser_session.hpp"
#include "controller_contract.hpp"
#include "pi_pair_usage.hpp"
#include "pi_pair_policy_admission.hpp"
#include <openssl/rand.h>
#include <array>
#include <charconv>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("outgoing_pair_unavailable");}
std::string next(const Json &value){
    const auto text=value.get<std::string>();std::uint64_t number{};
    const auto result=std::from_chars(text.data(),text.data()+text.size(),number);
    require(result.ec==std::errc{}&&result.ptr==text.data()+text.size()&&number<UINT64_MAX);return std::to_string(number+1);
}
std::string fresh(){std::array<unsigned char,16> bytes{};require(RAND_bytes(bytes.data(),bytes.size())==1);std::string value;
    for(const auto byte:bytes){value+="0123456789abcdef"[byte>>4];value+="0123456789abcdef"[byte&15];}return value;}
Json *initial(Json &peers,const std::string &operation){
    for(const auto *group:{"profiles","uncertain"})for(auto &row:peers[group])
        if(row["initial_operation_id"]==operation)return &row;
    return nullptr;
}
Json *operation(Json &book,const std::string &id){for(auto &entry:book["entries"])if(entry["operation_id"]==id)return &entry;return nullptr;}
bool bound(const Json &row,const PiPairPrincipal &principal,const std::string &self){return
    row["stage"]=="awaiting_commit"&&row["pair"]["state"]=="pending"&&principal.self_id()==self&&row["target_id"]==principal.peer_id()&&
    principal.identity().purpose==PiPairTlsPurpose::Management&&row["pair"]["pair_id"]==principal.identity().pair_id&&
    row["pair"]["authorization_generation"]==principal.identity().authorization_generation&&principal.identity().epoch=="1";}
Json revoked(const Json &row){
    auto pair=row["pair"];pair["state"]="revoked";pair["current"]=nullptr;pair["pending"]=nullptr;pair["previous"]=nullptr;
    return {{"profile_id",row["profile_id"]},{"target_id",row["target_id"]},{"endpoint",row["endpoint"]},{"pair",std::move(pair)}};
}
}
Json PiPairHistory::stage_outgoing_initial(const BrowserRequest &request,const LocalBrowserPrincipal &principal,
    ManagementBrowserSessions &sessions,const Json &discovery,PiPairExchange &exchange){
    require(request.method=="POST"&&request.path=="/api/control/v1/host/pairing/connect"&&request.body.size()<=32768&&
        principal.matches(request)&&sessions.owns(principal));
    const auto intent=strict_browser_json(request.body,8);require(valid_controller_document(intent,"pair-connect"));
    require(valid_controller_document({{"contract","Wsprry-Control/1"},{"schema","pair-discovery"},{"available",true},
        {"discoveries",Json::array({discovery})}},"pair-discovery"));
    const auto &hello=exchange.confirmed_controller_hello();auto keys=exchange.keys();
    require(hello["controller_id"]==self_&&hello["target_id"]==intent["expected_device_id"]&&
        discovery["discovery_id"]==intent["discovery_id"]&&discovery["device_id"]==hello["target_id"]&&discovery["boot_id"]==hello["target_boot_id"]);
    PiPairPolicyAdmission policy;require(policy.allowed());
    std::unique_lock guard(mutex_,std::try_to_lock);require(guard.owns_lock()&&service_permits()&&principal.matches(request)&&sessions.owns(principal));verify_selection();const auto before=store_->snapshot();
    auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
    const auto op=intent["operation_id"].get<std::string>();require(!operation(book,op));
    Json *old=nullptr;for(auto &row:peers["profiles"])if(row["target_id"]==hello["target_id"])old=&row;
    unsigned unresolved=0;for(const auto &entry:book["entries"])if(entry["unresolved"]==true){
        const bool resolved=old&&(*old)["stage"]=="active"&&entry["pair_id"]==(*old)["pair"]["pair_id"]&&
            !entry["continuation"].is_null()&&entry["state"]=="committed";
        if(!resolved)++unresolved;
    }
    if(unresolved>=32||(!old&&peers["profiles"].size()>=64)||
        (old&&(*old)["stage"]!="access_removed"&&peers["retired"].size()+peers["uncertain"].size()>=32))
        throw PiPairConnectRefusal(PiPairConnectRefusal::Reason::Capacity);
    const auto profile_id=old?(*old)["profile_id"].get<std::string>():fresh(),generation=old?next((*old)["profile_generation"]):"1";
    auto redacted=intent;redacted.erase("code");
    const auto digest=PiManagementJournal::digest(canonical_pi_management_json({{"intent",redacted},{"pair_context",exchange.request_digest()}}));
    Json row{{"profile_id",profile_id},{"profile_generation",generation},{"target_id",hello["target_id"]},
        {"endpoint",{{"host",discovery["host"]},{"port",31418}}},{"stage","awaiting_commit"},{"initial_operation_id",op},
        {"initial_request_digest",exchange.request_digest()},
        {"pair",{{"schema","pi-management-pair/1"},{"self_id",self_},{"peer_id",hello["target_id"]},{"pair_id",hello["pair_id"]},
            {"authorization_generation",next(hello["authorization_generation"])},{"role","manager"},{"state","pending"},
            {"current",nullptr},{"previous",nullptr},{"pending",{{"epoch","1"},{"tls_psk",keys.tls.storage_encoding()},{"service_ms","0"},{"connections","0"}}},
            {"receipt",nullptr},{"retirement_verifier",keys.retirement.storage_encoding()}}}};
    if(old&&(*old)["stage"]=="awaiting_commit"){
        auto *prior=operation(book,(*old)["initial_operation_id"].get<std::string>());require(prior&&(*prior)["unresolved"]==true);
        (*prior)["state"]="unknown";if(!(*prior)["receipt"].is_null())(*prior)["receipt"]["state"]="unknown";
        peers=supersede_pi_pair_pending(peers,self_,row);
    }else if(old){
        // This explicit, mutually confirmed replacement retires the old
        // local manager. Preserve unknown operation evidence without keeping
        // a private continuation that could restore its retired authority.
        Json retained=Json::array(),completed=Json::array();
        for(auto entry:book["entries"]){
            if(entry["pair_id"]==(*old)["pair"]["pair_id"]&&!entry["continuation"].is_null()){
                entry["continuation"]=nullptr;
                if(entry["state"]=="committed"){entry["unresolved"]=false;completed.push_back(std::move(entry));continue;}
                entry["state"]="unknown";entry["result_digest"]=nullptr;entry["unresolved"]=true;
                if(!entry["receipt"].is_null())entry["receipt"]["state"]="unknown";
            }
            retained.push_back(std::move(entry));
        }
        for(auto &entry:completed)retained.push_back(std::move(entry));
        book["entries"]=std::move(retained);
        if((*old)["stage"]!="access_removed")peers["retired"].push_back(revoked(*old));
        *old=row;
    }else peers["profiles"].push_back(row);
    Json receipt{{"contract","Wsprry-Control/1"},{"schema","target-operation"},
        {"binding",{{"device_id",hello["target_id"]},{"boot_id",hello["target_boot_id"]},{"profile_generation",generation},{"connection_generation","1"}}},
        {"operation_id",op},{"request_digest",digest},{"state","pending"},{"revision",nullptr},{"error",nullptr}};
    book["entries"].push_back({{"scope","device"},{"principal_id",self_},{"target_id",hello["target_id"]},{"pair_id",hello["pair_id"]},
        {"authorization_generation",row["pair"]["authorization_generation"]},{"operation_id",op},{"request_digest",digest},{"state","pending"},
        {"result_digest",nullptr},{"unresolved",true},{"receipt",receipt},{"continuation",nullptr}});
    const auto bounded=pi_pair_operations(self_,book["entries"]);require(valid_pi_pair_peers(peers,self_));
    auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_);prepare_usage(before,catalog,peers);
    try{store_->publish(before,catalog,bounded.document,peers,PiPairCatalogStore::Checkpoint::Preserve,[&]{
        require(principal.matches(request)&&sessions.owns(principal));if(bounded.evicted)sessions.retire();});}
    catch(...){require(store_->recover_own_failure());}
    const auto after=store_->snapshot();selected_=after.authority;verify_selection();reconcile_usage(after);
    require(after.peers&&parse_pi_pair_peers(after.peers->bytes,self_)==peers&&parse_pi_pair_operations(after.operations.bytes,self_)==bounded.document);
    return receipt;
}
bool PiPairHistory::outgoing_operation_known(const std::string &id){
    std::lock_guard guard(mutex_);require(service_permits());verify_selection();
    const auto saved=store_->snapshot();const auto book=parse_pi_pair_operations(saved.operations.bytes,self_);
    for(const auto &entry:book["entries"])if(entry["operation_id"]==id)return true;
    return false;
}
std::optional<Json> PiPairHistory::outgoing_initial(const std::string &id){
    std::lock_guard guard(mutex_);
    try{require(service_permits());verify_selection();const auto saved=store_->snapshot();auto peers=parse_pi_pair_peers(saved.peers->bytes,self_),book=parse_pi_pair_operations(saved.operations.bytes,self_);
        const auto *row=initial(peers,id),*entry=operation(book,id);require(row&&entry&&(*row)["stage"]=="awaiting_commit");
        return Json{{"profile_id",(*row)["profile_id"]},{"target_id",(*row)["target_id"]},{"endpoint",(*row)["endpoint"]},
            {"pair_id",(*row)["pair"]["pair_id"]},{"authorization_generation",(*row)["pair"]["authorization_generation"]},
            {"request_digest",(*row)["initial_request_digest"]},{"receipt",(*entry)["receipt"]}};
    }catch(...){return {};}
}
std::shared_ptr<const PiPairTlsCredential> PiPairHistory::initial_credential(const std::string &op,const PiPairTlsIdentity &identity){
    std::lock_guard guard(mutex_);
    try{require(service_permits());verify_selection();const auto saved=store_->snapshot();auto peers=parse_pi_pair_peers(saved.peers->bytes,self_);const auto *row=initial(peers,op);
        require(row&&(*row)["stage"]=="awaiting_commit"&&identity.purpose==PiPairTlsPurpose::Management&&identity.epoch=="1"&&
            (*row)["pair"]["pair_id"]==identity.pair_id&&(*row)["pair"]["authorization_generation"]==identity.authorization_generation);
        return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{identity,self_,(*row)["target_id"].get<std::string>(),
            PiPairSecret::from_storage((*row)["pair"]["pending"]["tls_psk"].get<std::string>())});
    }catch(...){return {};}
}
std::optional<PiPairMutationGuard> PiPairHistory::admit_initial_commit(const std::string &op){
    try{
        PiPairPolicyAdmission policy;require(policy.allowed());std::unique_lock history(mutex_,std::try_to_lock);require(history.owns_lock()&&service_permits());
        verify_selection();const auto saved=store_->snapshot();auto peers=parse_pi_pair_peers(saved.peers->bytes,self_),book=parse_pi_pair_operations(saved.operations.bytes,self_);
        const Json *row=nullptr;for(const auto &candidate:peers["profiles"])if(candidate["initial_operation_id"]==op)row=&candidate;
        const auto *entry=operation(book,op);require(row&&entry&&(*row)["stage"]=="awaiting_commit"&&(*entry)["state"]=="pending"&&(*entry)["unresolved"]==true);
        return PiPairMutationGuard(std::move(policy.configuration),std::move(policy.admission),std::move(history));
    }catch(...){return {};}
}
bool PiPairHistory::accept_outgoing_initial(const std::string &op,const PiPairPrincipal &principal,const Json &reply,ManagementBrowserSessions &sessions){
    std::lock_guard guard(mutex_);
    try{
        require(valid_pi_pair_document(reply,"operation")&&reply["state"]=="committed");verify_selection();const auto before=store_->snapshot();
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_),book=parse_pi_pair_operations(before.operations.bytes,self_);
        const auto *found=initial(peers,op);require(found&&bound(*found,principal,self_));const auto row=*found;
        require(reply["operation_id"]==row["pair"]["pair_id"]&&reply["pair_id"]==row["pair"]["pair_id"]&&
            reply["authorization_generation"]==row["pair"]["authorization_generation"]&&reply["request_digest"]==row["initial_request_digest"]);
        auto outcome=reply;outcome.erase("result_digest");require(reply["result_digest"]==PiManagementJournal::digest(canonical_pi_management_json(outcome)));
        auto *entry=operation(book,op);require(entry&&!(*entry)["receipt"].is_null());auto completed=*entry;
        completed["state"]="committed";completed["unresolved"]=false;
        completed["receipt"]["state"]="committed";completed["receipt"]["revision"]=row["initial_request_digest"];
        completed["result_digest"]=PiManagementJournal::digest(canonical_pi_management_json(completed["receipt"]));
        Json entries=Json::array();for(const auto &retained:book["entries"])if(retained["operation_id"]!=op)entries.push_back(retained);
        entries.push_back(std::move(completed));
        bool current=false;
        for(auto &profile:peers["profiles"])if(profile["initial_operation_id"]==op){
            current=true;profile["stage"]="active";auto &pair=profile["pair"];pair["state"]="active";pair["current"]=pair["pending"];pair["pending"]=nullptr;
            pair["receipt"]={{"operation_id",reply["operation_id"]},{"request_digest",reply["request_digest"]},{"result_digest",reply["result_digest"]}};
        }
        Json uncertain=Json::array();
        for(const auto &old:peers["uncertain"]){
            if(old["initial_operation_id"]==op||(current&&old["target_id"]==row["target_id"])){
                peers["retired"].push_back(revoked(old));
                // New authority proves removal, never whether the old commit
                // happened. Its unresolved operation remains unknown evidence.
                if(old["initial_operation_id"]!=op)for(auto &old_entry:entries)if(old_entry["operation_id"]==old["initial_operation_id"]){
                    old_entry["state"]="unknown";if(!old_entry["receipt"].is_null())old_entry["receipt"]["state"]="unknown";
                }
            }else uncertain.push_back(old);
        }
        peers["uncertain"]=std::move(uncertain);const auto bounded=pi_pair_operations(self_,entries);
        if(bounded.evicted)sessions.retire();
        const auto published=publish_outgoing(before,parse_pi_pair_catalog(before.catalog.bytes,self_),bounded.document,peers);
        if(published&&current&&running_marked_)usage_->authenticated(principal.identity());
        return published;
    }catch(...){return false;}
}
} // namespace wsprrypi
