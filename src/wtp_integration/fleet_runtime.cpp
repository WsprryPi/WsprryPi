// SPDX-License-Identifier: MIT
#include <system_error>
#include "fleet_runtime.hpp"
#include "application_storage/application_state.hpp"
#include "fleet_test.hpp"
#include "fleet_store.hpp"
#include "controller_retirement.hpp"
#include "pi_pair_owner.hpp"
#include "controller_contract.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_endpoint/identity.hpp"
#include "browser_api.hpp"
#include "management_identity.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
// Retained records are projections only. No carrier, dispatch permit, worker,
// mailbox, cleanup executor, or recurrence scheduler can be constructed here.
struct FleetContext {
    std::string id,generation{wtp_random_identity()},message;
    std::uint64_t control{},observation{};
    Json row,retirement,published;
    bool recovery{},removed{},binding_conflict{},recurrence_retired{};
};
bool fleet_id(const Json& value) {
    if(!value.is_string())return false;
    const auto s=value.get<std::string>();
    return s.size()==32 && s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
std::string bounded(std::string value,std::size_t limit) {
    if(value.size()<=limit)return value;
    auto end=limit-3;
    while(end && (static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
    return value.substr(0,end)+"...";
}
class Fleet {
public:
    Fleet(const std::string& ini,const std::string& self,WtpFleetTestHooks hooks={},std::function<void(PiPairInitializationProof)> pairing={})
        :store_(ini+".wtp-assignments.json",self),journal_(ini+".wtp-fleet-contexts.json",self),
         pairing_(std::move(pairing)),self_id_(self),hooks_(std::move(hooks)),incarnation_(wtp_random_identity()) {
        const auto saved=store_.snapshot();config_=saved.first;etag_=saved.second;
        const auto retirement_directory=hooks_.retirement_directory.empty()?private_device_state_directory(PrivateDeviceDomain::ControllerRetirements,self):hooks_.retirement_directory;
        retirement_book_=std::make_unique<ControllerRetirementBook>(retirement_directory,ini+".wtp-assignments.json",self,config_.at("assignments"),
            std::function<void(RetirementWritePoint)>{},pairing_,application_database_selected()?
                PiManagementJournal::digest(application_origin_namespace()+".wtp-assignments.json"):std::string{});
        // Book recovery may atomically publish a committed resume assignment.
        // The pre-recovery cache must never seed stale connection contexts.
        const auto recovered=store_.reload();config_=recovered.first;etag_=recovered.second;
        Json entries=Json::array();
        try {entries=journal_.snapshot().at("contexts");}
        catch(const std::exception& e){journal_error_=bounded(e.what(),512);}
        unsigned index=0;
        for(const auto& row:config_.at("assignments")) {
            auto c=create_context(row,index++);
            contexts_.emplace(c->id,c);
        }
        for(const auto& entry:entries) {
            const auto id=entry.at("assignment").at("device_id").get<std::string>();
            auto found=contexts_.find(id);
            if(found==contexts_.end()) {
                if(contexts_.size()>=32)throw std::runtime_error("Combined retained-context capacity exceeded");
                auto c=create_context(entry.at("assignment"),index++);c->removed=true;
                found=contexts_.emplace(id,c).first;
            }
            auto& c=*found->second;c.retirement=entry;c.recovery=true;
            // A conflicting restored row is never overwritten with frozen data.
            auto expected=entry.at("assignment"),actual=c.row;
            expected["enabled"]=false;actual["enabled"]=false;
            expected["in_flight"]=false;actual["in_flight"]=false;
            if(expected!=actual){c.binding_conflict=true;c.message="Retained assignment binding conflicts; explicit maintenance review required";}
        }
        // Legacy rows and crash records are retained for operator recovery.
        // No carrier owner, inspection, reconnect, cleanup, or dispatch thread
        // is created by retired software, including for disabled assignments.
        const auto retirement_view=retirement_book_->read(incarnation_,config_.at("assignments"));
        for (auto& [id,c] : contexts_) {
            for(const auto &row:retirement_view.at("assignments"))
                if(row.at("target_id")==id && row.at("state")=="retired")c->recurrence_retired=true;
            c->published={{"state","migration_required"},
                {"message","Migration required: use WTP-capable software to reconcile legacy ownership and retirement before replacement."},
                {"can_resume",false},{"boot_id",nullptr}};
            if(c->recurrence_retired){
                c->published["state"]="retired";
                c->published["message"]="Controller retirement confirmed. Manage the device's native schedules separately.";
            }
        }
    }
    ~Fleet(){shutdown();}
    void shutdown() noexcept {
        {std::lock_guard lock(mutex_);accepting_=false;}
        if(hooks_.before_shutdown_join)hooks_.before_shutdown_join();
    }
    bool retirement_scope_closed(const Json &pair,const Json &migration){return retirement_book_->retirement_scope_closed(pair,migration);}
    PicoHttpResponse machine_retirement(const PiPairRetirementPrincipal &principal,PiPairHistory &history,const std::function<bool()> &cancelled){
        const auto &wire=principal.request();
        const auto refusal=[&](unsigned status,const char *code){return PicoHttpResponse{status,Json{
            {"contract","Wsprry-Pair/1"},{"schema","result"},{"pair_id",wire.at("pair_id")},
            {"state","rejected"},{"code",code},{"retryable",false}}.dump(),{}};};
        if((cancelled&&cancelled())||principal.transport().self_id()!=self_id_||wire.at("controller_id")!=self_id_||!history.retirement_finished(principal.transport(),true))
            return refusal(403,"invalid_proof");
        if(const auto retained=retirement_book_->machine_receipt(wire))return {retained->at("schema")=="operation"?202U:200U,retained->dump(),{}};
        if(wire.at("schema")=="retirement-status")return {404,Json{{"contract","Wsprry-Pair/1"},{"schema","result"},
            {"pair_id",wire.at("pair_id")},{"state","rejected"},{"code","operation_unknown"},{"retryable",false}}.dump(),{}};
        if(const auto fence=retirement_book_->retired_fence(wire)){
            const auto response=history.retirement_response(wire,(*fence)["receipt"]);
            if(!response)return refusal(503,"storage_unavailable");
            const auto guard=history.admit_retirement(principal);if(!guard||(cancelled&&cancelled()))return refusal(403,"invalid_proof");
            retirement_book_->bind_retired(wire,(*fence)["revision"],*response);
            return {200,response->dump(),{}};
        }
        return refusal(409,"busy");
    }
    PicoHttpResponse request(const std::string& method,const std::string& body,const std::string& revision,
                             const std::string& resource,const std::string& ids) {
        if(resource=="retirements")return retirement_request(method,body,revision);
        if(method=="GET") {
            std::lock_guard lock(mutex_);
            const auto now=now_checked();
            if(resource=="actions")return action_query(ids,now);
            if(resource!="operations" && resource!="fleet")throw std::runtime_error("Unknown Fleet resource");
            auto data=snapshot_locked(now,resource=="operations");
            const auto bytes=data.dump();
            if(bytes.size()>(resource=="operations"?512U:1024U)*1024)throw std::runtime_error("Fleet projection exceeds serialized budget");
            return {200,bytes,etag_};
        }
        if(method!="POST" || resource!="fleet")return {405,R"({"error":{"code":"method_not_allowed"}})",{}};
        (void)body; (void)revision;
        return controller_target_error(409,"migration_required");
    }
    void with_backup_files(const std::function<void(const std::map<std::string,std::string> &)> &capture){
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())throw std::system_error(std::make_error_code(std::errc::device_or_resource_busy));
        if(!accepting_||!capture)throw std::runtime_error("fleet_backup_unavailable");
        const auto assignments=store_.snapshot();const auto contexts=journal_.snapshot();
        retirement_book_->with_backup_files(capture);
        if(store_.snapshot()!=assignments||journal_.snapshot()!=contexts)throw std::runtime_error("fleet_backup_changed");
    }
private:
    std::uint64_t now_ms() const {
        if(hooks_.monotonic_now)return hooks_.monotonic_now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::shared_ptr<FleetContext> create_context(Json row,unsigned) {
        auto c=std::make_shared<FleetContext>();c->id=row.at("device_id").get<std::string>();c->row=std::move(row);
        if(c->row.contains("in_flight"))c->recovery=c->row.at("in_flight").get<bool>();
        return c;
    }
    std::uint64_t now_checked() {
        const auto now=now_ms();
        if(now<last_clock_) {
            accepting_=false;clock_fault_=true;
            for(auto& [id,c]:contexts_){(void)id;c->recovery=true;if(c->control<UINT64_MAX)++c->control;}
            ++publication_;
            throw std::runtime_error("Controller clock discontinuity; restart/reconcile required");
        }
        last_clock_=now;return now;
    }
    PicoHttpResponse action_query(const std::string& ids,std::uint64_t) {
        if(ids.empty() || ids.size()>2048)throw std::runtime_error("Invalid action query capacity");
        std::set<std::string> unique;Json result=Json::object();std::size_t begin=0;
        do {
            const auto end=ids.find(',',begin);const auto id=ids.substr(begin,end==std::string::npos?end:end-begin);
            if(!fleet_id(id) || !unique.insert(id).second || unique.size()>32)throw std::runtime_error("Invalid action query IDs");
            result[id]=Json{{"state","unknown_or_expired"},{"action_id",id}};
            if(end==std::string::npos)break;
            begin=end+1;
        }while(true);
        auto bytes=Json{{"controller_incarnation",incarnation_},{"publication_revision",std::to_string(publication_)},{"actions",result}}.dump();
        if(bytes.size()>128*1024)throw std::runtime_error("Action projection exceeds serialized budget");
        return {200,bytes,etag_};
    }
    Json snapshot_locked(std::uint64_t now,bool operations) {
        auto data=config_;data["scope"]="wsprrypi-wtp-fleet/1";data["maximum_outputs"]=32;
        data["controller_incarnation"]=incarnation_;data["publication_revision"]=std::to_string(publication_);
        data["now_ms"]=std::to_string(now);data["stale_after_ms"]=15000;data["outputs"]=Json::object();
        data["storage_error"]=journal_error_;data["replacement_pending"]=!accepting_;data["clock_fault"]=clock_fault_;
        for(const auto& [id,c]:contexts_) {
            auto p=c->published;p["assignment_generation"]=c->generation;p["control_revision"]=std::to_string(c->control);
            p["observation_revision"]=std::to_string(c->observation);
            p["can_pause"]=false;
            p["can_reconcile"]=false;
            if(p.contains("age_origin_ms") && p["age_origin_ms"].is_string()) {
                const auto origin=wtp_fleet_decimal(p["age_origin_ms"]);
                p["observation_age_ms"]=now>=origin?Json(std::to_string(now-origin)):Json(nullptr);
                if(now<origin || now-origin>=15000)p["can_resume"]=false;
            }else p["observation_age_ms"]=nullptr;
            if(p.contains("host_age_origin_ms") && p["host_age_origin_ms"].is_string()) {
                const auto origin=wtp_fleet_decimal(p["host_age_origin_ms"]);
                p["host_observation_age_ms"]=now>=origin?Json(std::to_string(now-origin)):Json(nullptr);
                if(now<origin || now-origin>=15000)p["can_resume"]=false;
            }
            if(p.contains("utc_anchor_ms"))p["utc_anchor_age_ms"]=std::to_string(now-wtp_fleet_decimal(p["utc_anchor_ms"]));
            if(!accepting_ || c->recovery)p["can_resume"]=false;
            if(clock_fault_){p["state"]="recovery_required";p["message"]="Controller clock fault; a fresh controller incarnation is required";}
            p["actions"]=Json::array();
            data["outputs"][id]=std::move(p);
        }
        if(operations) {
            // Operations exposes no endpoints, credential paths or full CAPS.
            for(auto& row:data["assignments"])row.erase("settings");
        }
        return data;
    }
    PicoHttpResponse retirement_request(const std::string &method,const std::string &body,const std::string &revision,const Json &machine_request=nullptr) {
        Json assignments;
        {std::lock_guard lock(mutex_);assignments=config_.at("assignments");}
        if(method=="GET") {
            const auto document=retirement_book_->read(incarnation_,assignments);
            if(!valid_controller_document(document,"controller-retirements"))return controller_target_error(503,"storage_unavailable");
            return {200,document.dump(),"\""+document.at("revision").get<std::string>()+"\""};
        }
        if(method!="POST")return controller_target_error(405,"unsupported_resource");
        Json intent;try{intent=strict_browser_json(body,16);}catch(...){return controller_target_error(400,"invalid_json");}
        if(!valid_controller_document(intent,"controller-retirement"))return controller_target_error(400,"invalid_shape");
        if(revision.empty())return controller_target_error(428,"revision_required");
        if(revision.size()!=66||revision.front()!='"'||revision.back()!='"'||revision.substr(1,64).find_first_not_of("0123456789abcdef")!=std::string::npos)
            return controller_target_error(400,"invalid_shape");
        // Read-only replay remains available even if an operator has removed
        // the old v1 row. It neither needs a live carrier nor grants new work.
        try {
            if(machine_request.is_null()){
                if(const auto receipt=retirement_book_->replay(intent,revision.substr(1,64)))
                    return {receipt->at("state")=="retired"?200U:202U,receipt->dump(),{}};
            }
        }catch(const ControllerRetirementConflict&){return controller_target_error(409,"revision_conflict");}
        catch(...){return controller_target_error(503,"storage_unavailable");}
        return controller_target_error(409,"migration_required");
    }
    WtpFleetStore store_;WtpFleetJournal journal_;std::unique_ptr<ControllerRetirementBook> retirement_book_;
    std::function<void(PiPairInitializationProof)> pairing_;
    std::string self_id_;WtpFleetTestHooks hooks_;std::string incarnation_;
    std::uint64_t last_clock_{},publication_{1};bool accepting_{true},clock_fault_{};
    std::mutex mutex_;Json config_;std::string etag_,journal_error_;
    std::map<std::string,std::shared_ptr<FleetContext>> contexts_;
};
std::mutex manager_mutex,lifecycle_mutex;
std::shared_ptr<Fleet> manager;
std::string startup_error;
void replace_manager(const std::string& ini,const std::string& self,WtpFleetTestHooks hooks={}) {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Fleet> old;{std::lock_guard lock(manager_mutex);old=manager;}
    if(old)old->shutdown(); // No candidate sessions/writers before old quiescence.
    try {
        const auto identity=self.empty()?wtp_pi_device_id():self;std::function<void(PiPairInitializationProof)> pairing;
        if(self.empty())try{const auto owner=prepare_pi_pair_owner(identity);pairing=[owner](PiPairInitializationProof proof){owner->provision(std::move(proof));};}
            catch(...){/* Independent pairing storage cannot reopen or stop Fleet authority. */}
        if(!pairing)if(const auto owner=pi_pair_owner();owner&&owner->self()==identity)
            pairing=[owner](PiPairInitializationProof proof){owner->provision(std::move(proof));};
        auto candidate=std::make_shared<Fleet>(ini,identity,std::move(hooks),std::move(pairing));
        std::lock_guard lock(manager_mutex);manager=std::move(candidate);startup_error.clear();
    }catch(const std::exception& e) {
        // The old owner is already quiescent. Do not publish its stopped
        // projection as a working manager or overwrite a later startup result.
        std::lock_guard lock(manager_mutex);manager.reset();startup_error=bounded(e.what(),512);throw;
    }
}
} // namespace
void start_wtp_fleet(const std::string& ini,const std::string& self) {
    try{replace_manager(ini,self);}
    catch(const std::exception&){ /* The lifecycle owner published the failure. */ }
}
void start_wtp_fleet_private_storage_for_test(const std::string &ini,const std::string &self,const std::string &directory) {
    if(directory.empty())throw std::runtime_error("Explicit private fixture namespace required");
    WtpFleetTestHooks hooks;hooks.retirement_directory=directory;
    try{replace_manager(ini,self,std::move(hooks));}catch(const std::exception&){}
}
void start_wtp_fleet_for_test(const std::string& ini,const std::string& self,WtpFleetTestHooks hooks) {
    if(hooks.retirement_directory.empty())hooks.retirement_directory=ini+".controller-retirements";
    replace_manager(ini,self,std::move(hooks));
}
void stop_wtp_fleet() noexcept {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Fleet> old;{std::lock_guard lock(manager_mutex);old=manager;}
    if(old)old->shutdown();
    {std::lock_guard lock(manager_mutex);manager.reset();}
}
void wtp_fleet_with_backup_files(const std::function<void(const std::map<std::string,std::string> &)> &capture){
    std::unique_lock lifecycle(lifecycle_mutex,std::try_to_lock);
    if(!lifecycle.owns_lock())throw std::system_error(std::make_error_code(std::errc::device_or_resource_busy));
    std::shared_ptr<Fleet> current;{std::lock_guard lock(manager_mutex);current=manager;}
    if(!current)throw std::runtime_error("fleet_backup_unavailable");
    current->with_backup_files(capture);
}
bool wtp_fleet_retirement_scope_closed(const Json &pair,const Json &migration){
    std::unique_lock lifecycle(lifecycle_mutex,std::try_to_lock);if(!lifecycle.owns_lock())return false;
    std::shared_ptr<Fleet> current;{std::lock_guard lock(manager_mutex);current=manager;}
    if(!current)return false;
    try{return current->retirement_scope_closed(pair,migration);}catch(...){return false;}
}
PicoHttpResponse wtp_fleet_retirement(const PiPairRetirementPrincipal &principal,PiPairHistory &history,const std::function<bool()> &cancelled){
    std::shared_ptr<Fleet> current;{std::lock_guard lock(manager_mutex);current=manager;}
    if(!current)return controller_target_error(503,"storage_unavailable");
    try{return current->machine_retirement(principal,history,cancelled);}
    catch(const ControllerRetirementConflict &){return controller_target_error(409,"revision_conflict");}
    catch(...){return controller_target_error(503,"storage_unavailable");}
}
PicoHttpResponse wtp_fleet_api(const std::string& method,const std::string& body,const std::string& revision,
                              const std::string& resource,const std::string& ids) {
    std::shared_ptr<Fleet> current;std::string error;
    {std::lock_guard lock(manager_mutex);current=manager;error=startup_error;}
    if(!current&&resource=="retirements")return controller_target_error(503,"storage_unavailable");
    if(!current)return {503,Json{{"error",{{"code","fleet_unavailable"},{"message",error}}}}.dump(),{}};
    try{return current->request(method,body,revision,resource,ids);}
    catch(const std::exception& e){if(resource=="retirements")return controller_target_error(503,"storage_unavailable");const std::string reason=e.what();return {reason=="revision_conflict"?412U:409U,
        Json{{"error",{{"code",reason=="revision_conflict"?"revision_conflict":"fleet_request_rejected"},{"message",bounded(reason,512)}}}}.dump(),{}};}
}
} // namespace wsprrypi
