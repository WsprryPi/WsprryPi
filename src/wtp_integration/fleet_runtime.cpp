// SPDX-License-Identifier: MIT
#include "fleet_runtime.hpp"
#include "fleet_limits.hpp"
#include "catalog.hpp"
#include "fleet_test.hpp"
#include "runtime_config_bridge.hpp"
#include "fleet_store.hpp"
#include "fleet_schedule.hpp"
#include "fleet_recovery.hpp"
#include "target_runtime.hpp"
#include "browser_api.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_endpoint/identity.hpp"
#include "WSPR-Transmitter/src/execution_plan_compiler.hpp"
#include "WSPR-Transmitter/src/gpio_band_policy.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <thread>
#include <set>
#include <vector>
#include <algorithm>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
std::optional<Json> row_for(const Json& data,const std::string& id) {
    for(const auto& row:data.at("assignments")) if(row.at("device_id")==id) return row;
    return {};
}
void modify_row(Json& data,const std::string& id,const std::function<void(Json&)>& modify) {
    for(auto& row:data.at("assignments")) if(row.at("device_id")==id) {modify(row);return;}
    throw std::runtime_error("Output assignment no longer exists");
}
void check_caps(const Json& row,const WtpRuntimeStatus& status) {
    if(!status.identity || !status.capabilities || !status.remote ||
       status.identity->device_id!=row.at("device_id").get<std::string>() || status.identity->product!=row.at("product").get<std::string>())
        throw std::runtime_error("Output identity, product, status or capabilities unavailable");
    auto request=wtp_fleet_request(row.at("schedule"));
    request.policy.allow_quantization=parse_wtp_settings(row.at("settings"),true).allow_frequency_adjustment;
    const auto policy=current_test_tone_planning_config_snapshot();
    request.policy.allow_unqualified_frequency=policy.allow_unqualified_frequency;
    request.policy.allow_non_amateur_frequency=policy.allow_non_amateur_frequency;
    request.id.value=1;
    ExecutionPlanCompiler compiler;
    const auto plan=compiler.compile(request);
    const auto admission=evaluate_gpio_band_policy(plan);
    if(!admission.allowed)throw std::runtime_error(admission.error);
    const auto prepared=prepare_wtp_plan(plan,*status.capabilities,
        {std::string(32,'1'),121000000000ULL,status.capabilities->maximum_arm_uncertainty_ns});
    if(!prepared) throw std::runtime_error(prepared.explanation);
}
std::unique_ptr<WtpTargetRuntime> make_target(const WtpFleetTestHooks& hooks,
    WtpSettings settings, std::function<bool()> admission = {}, std::shared_ptr<void> lease = {}) {
    if(hooks.create_target)return hooks.create_target(std::move(settings),std::move(admission),std::move(lease));
    return std::make_unique<WtpTargetRuntime>(std::move(settings),std::move(admission),std::move(lease));
}
Json revocation_status(const WtpFleetTestHooks& hooks,const WtpSettings& settings,unsigned port) {
    return hooks.revocation_status?hooks.revocation_status(settings,port):wtp_pi_revocation_status(settings,port);
}
// Every context owns its transport, execution loop and bounded action mailbox.
// HTTP only admits work and reads immutable projections under mutex_.
struct FleetAction {
    Json command,record;
    std::string digest,etag,id,operation;
    std::uint64_t fence{},deadline{},completed{};
    bool pending{true},priority{};
};
struct FleetContext {
    std::string id,generation{wtp_random_identity()};
    std::uint64_t control{},observation{},last_status{},last_origin{},last_sequence{};
    Json row,retirement,published;
    std::shared_ptr<WtpDispatchPermit> permit{std::make_shared<WtpDispatchPermit>()};
    std::shared_ptr<FleetAction> ordinary,pause;
    std::shared_ptr<WtpTargetRuntime> target;
    std::mutex target_mutex;
    std::atomic_bool quit{false},recovery{false},finished{false},reclaimable{false};
    std::thread thread;
    bool initial{},dispatched{},removed{},binding_conflict{},cleanup_ok{true};
    std::mutex notice_mutex;
    std::string external_fault;
    std::uint64_t initial_due{},last_host{};
    Json host,host_result;
    std::optional<std::uint64_t> host_origin;
    std::mutex host_result_mutex,host_io_mutex;
    std::thread host_thread;
    std::atomic_bool host_busy{false};
    std::string host_error;
    std::uint64_t host_result_origin{};
    std::string message;
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
bool observed_healthy(const WtpRuntimeStatus& s,const Json& row) {
    return s.session_phase==wtp::SessionPhase::Ready && s.identity && s.capabilities && s.remote &&
        s.status_observed_ms && s.status_age_origin_ms && s.identity->device_id==row.at("device_id").get<std::string>() &&
        s.identity->product==row.at("product").get<std::string>() && s.remote->boot_id==s.identity->boot_id &&
        !s.uncertain && !s.safety_fault;
}
bool observed_idle(const WtpRuntimeStatus& s) {
    return s.remote && !s.remote->owner_id && !s.remote->output_active &&
        (s.remote->state==wtp::State::Empty || s.remote->state==wtp::State::Complete ||
         s.remote->state==wtp::State::Aborted || s.remote->state==wtp::State::Missed);
}
class Fleet {
public:
    Fleet(const std::string& ini,const std::string& self,WtpFleetTestHooks hooks={})
        :store_(ini+".wtp-assignments.json",self),journal_(ini+".wtp-fleet-contexts.json",self),
         self_id_(self),hooks_(std::move(hooks)),incarnation_(wtp_random_identity()) {
        const auto saved=store_.snapshot();config_=saved.first;etag_=saved.second;
        Json entries=Json::array();
        try {entries=journal_.snapshot().at("contexts");}
        catch(const std::exception& e){journal_error_=bounded(e.what(),512);}
        unsigned index=0;
        for(const auto& row:config_.at("assignments")) {
            auto c=create_context(row,index++);contexts_.emplace(c->id,c);
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
        try {for(auto& [id,c]:contexts_){(void)id;start(c);}}
        catch(...) {shutdown();throw;}
        try {reaper_=std::thread([this]{
            while(!exiting_) {
                std::vector<std::shared_ptr<FleetContext>> retired;
                {std::lock_guard lock(mutex_);
                 for(auto it=contexts_.begin();it!=contexts_.end();) {
                     if(it->second->finished && it->second->reclaimable) {retired.push_back(it->second);it=contexts_.erase(it);++publication_;}
                     else ++it;
                 }}
                for(auto& c:retired)if(c->thread.joinable())c->thread.join();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });}catch(...){shutdown();throw;}
    }
    ~Fleet(){shutdown();}
    void shutdown() noexcept {
        exiting_=true;if(reaper_.joinable())reaper_.join();
        std::vector<std::shared_ptr<FleetContext>> owners;
        {std::lock_guard lock(mutex_);accepting_=false;
         for(auto& [id,c]:contexts_){(void)id;c->quit=true;c->permit->revoke();owners.push_back(c);}}
        for(auto& c:owners)signal_stop(*c);
        for(auto& c:owners)if(c->thread.joinable())c->thread.join();
    }
    PicoHttpResponse request(const std::string& method,const std::string& body,const std::string& revision,
                             const std::string& resource,const std::string& ids) {
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
        const auto command=strict_browser_json(body);
        validate_command(command);
        const auto operation=command.at("operation").get<std::string>();
        const auto action_id=command.at("action_id").get<std::string>();
        const auto id=operation=="assign"?parse_wtp_settings(command.at("settings"),true).device_id:
            command.at("device_id").get<std::string>();
        std::shared_ptr<FleetContext> stop;
        PicoHttpResponse response;
        {
            std::lock_guard lock(mutex_);
            const auto now=now_checked();
            const auto digest=wtp_catalog_revision(Json{{"command",command},{"etag",revision}});
            if(auto found=actions_.find(action_id);found!=actions_.end()) {
                if(found->second->digest!=digest)throw std::runtime_error("Action ID reused with another request");
                return action_response(*found->second);
            }
            if(clock_fault_)throw std::runtime_error("Controller clock fault requires a fresh incarnation before new admission");
            const auto deadline=wtp_fleet_decimal(command.at("accept_before_ms"));
            if(now>=deadline || deadline-now>45000)throw std::runtime_error("Action admission window expired or invalid");
            if(revision.empty())return {428,R"({"error":{"code":"revision_required"}})",{}};
            if(revision!=etag_)throw std::runtime_error("revision_conflict");
            if(command.at("controller_incarnation")!=incarnation_)throw std::runtime_error("Controller changed; read and review again");
            if(!accepting_ && operation!="pause")throw std::runtime_error("Fleet replacement pending; new work inhibited");
            if(!journal_error_.empty() && operation!="pause" && operation!="recover")throw std::runtime_error(journal_error_);
            auto found=contexts_.find(id);
            if(operation=="assign") {
                if(found!=contexts_.end() || id==self_id_)throw std::runtime_error("Duplicate, self or unresolved output");
                if(contexts_.size()>=32)throw std::runtime_error("Fleet supports at most 32 remote contexts; reconcile unresolved outputs first");
                if(!command.at("assignment_generation").is_null() || !command.at("reviewed_control_revision").is_null())
                    throw std::runtime_error("New assignment cannot reuse a context review");
            } else {
                if(found==contexts_.end())throw std::runtime_error("Output context no longer exists");
                auto& c=*found->second;
                if(!journal_error_.empty() && operation=="pause" && (!c.published.contains("identity") || c.published.at("identity").is_null()))
                    throw std::runtime_error("Corrupt recovery storage: no verified live owner for Pause");
                if(c.quit)throw std::runtime_error("Owner retirement is stopping work; replacement pending");
                if(command.at("assignment_generation")!=c.generation ||
                   wtp_fleet_decimal(command.at("reviewed_control_revision"))!=c.control)
                    throw std::runtime_error("Output control changed; read and review again");
                if(c.published.value("revoked",false) && operation!="recover")throw std::runtime_error("Removed context permits reconciliation only");
                if(c.pause && (c.pause->pending || (now<c.pause->deadline && terminal_count()>=256)))
                    throw std::runtime_error("Busy: priority stop capacity occupied");
                if(c.pause && !c.pause->pending)c.pause.reset();
                if(c.ordinary && c.ordinary->pending && operation!="pause")throw std::runtime_error("Busy: output action pending");
                if(operation=="enable") {
                    if(c.ordinary && c.ordinary->pending)throw std::runtime_error("Previous work is not quiescent");
                    if(!c.published.value("can_resume",false))throw std::runtime_error("Output is not eligible to resume; read status");
                    if(command.at("reviewed_boot_id").is_null() || command.at("reviewed_boot_id")!=c.published.at("boot_id"))
                        throw std::runtime_error("Output boot changed; read and review again");
                }
            }
            expire_actions(now);
            // Completed protected records occupy their per-context slot. The
            // history pool can absorb them only after bounded reservation.
            if(actions_.size()>=320)throw std::runtime_error("Busy: action ledger full");
            if(found!=contexts_.end() && operation!="pause" && found->second->ordinary &&
               !found->second->ordinary->pending && now<found->second->ordinary->deadline && terminal_count()>=256)
                throw std::runtime_error("Busy: output result protection active");
            if(operation=="assign") {
                auto c=create_context(Json{{"device_id",id}},0);
                c->row=nullptr;found=contexts_.emplace(id,c).first;
            }
            auto& c=*found->second;
            if(c.control==UINT64_MAX)throw std::runtime_error("Control revision exhausted");
            auto action=std::make_shared<FleetAction>();
            action->command=command;action->etag=revision;action->digest=digest;action->id=action_id;
            action->operation=operation;action->deadline=deadline;action->fence=++c.control;action->priority=operation=="pause";
            action->record={{"action_id",action_id},{"operation",operation},{"state","accepted"},{"work_pending",true},
                {"device_id",id},{"controller_incarnation",incarnation_},{"assignment_generation",c.generation},
                {"control_revision",std::to_string(c.control)},{"reviewed_etag",revision},
                {"reviewed_boot_id",command.at("reviewed_boot_id")},{"observation_baseline",std::to_string(c.observation)},{"result_boot_id",nullptr},{"job_id",nullptr},
                {"accepted_ms",std::to_string(now)},{"completed_ms",nullptr},{"reason",""},{"postconditions",Json::object()}};
            if(operation=="pause" || operation=="recover" || operation=="remove") {
                const auto row=row_for(config_,id);
                if(row && row->value("in_flight",false) && c.published.contains("wtp")) {
                    const auto job=c.published.at("wtp").value("job_id",std::string{});
                    if(!job.empty())action->record["job_id"]=job;
                }
            }
            actions_.emplace(action_id,action);
            if(operation=="pause") {
                c.permit->revoke();stop=found->second;
                if(c.ordinary && c.ordinary->pending)c.ordinary->record["state"]="superseded";
                c.pause=action;
            } else {
                c.ordinary=action;
                if(operation!="enable" && operation!="assign")c.permit->revoke();
            }
            ++publication_;
            if(!c.thread.joinable()) {
                try {start(found->second);}
                catch(const std::exception& e) {
                    action->pending=false;action->completed=now;action->record["state"]="refused";
                    action->record["work_pending"]=false;action->record["completed_ms"]=std::to_string(now);
                    action->record["reason"]=bounded(e.what(),128);action->command=nullptr;
                    if(c.row.is_null())contexts_.erase(found);
                }
            }
            response=action_response(*action);
        }
        if(stop)signal_stop(*stop);
        return response;
    }
private:
    std::uint64_t now_ms() const {return hooks_.monotonic_now?hooks_.monotonic_now():clock_.now_ms();}
    std::shared_ptr<FleetContext> create_context(Json row,unsigned index) {
        auto c=std::make_shared<FleetContext>();c->id=row.at("device_id").get<std::string>();c->row=std::move(row);
        c->initial_due=now_ms()+index*30;
        c->permit->allowed=false;
        if(c->row.contains("in_flight"))c->recovery=c->row.at("in_flight").get<bool>();
        c->published={{"state","starting"},{"message",""},{"can_resume",false},{"boot_id",nullptr}};
        return c;
    }
    void start(const std::shared_ptr<FleetContext>& c){c->thread=std::thread([this,c]{run(*c);});}
    static void signal_stop(FleetContext& c) {
        std::shared_ptr<WtpTargetRuntime> target;
        {std::lock_guard lock(c.target_mutex);target=c.target;}
        if(target)target->app->request_stop();
    }
    std::shared_ptr<WtpTargetRuntime> target(FleetContext& c) {
        std::lock_guard lock(c.target_mutex);return c.target;
    }
    void set_target(FleetContext& c,std::unique_ptr<WtpTargetRuntime> fresh) {
        fresh->app->set_dispatch_permit(c.permit);
        std::lock_guard lock(c.target_mutex);c.target=std::move(fresh);
    }
    std::uint64_t now_checked() {
        const auto now=now_ms();
        if(now<last_clock_) {
            accepting_=false;clock_fault_=true;
            for(auto& [id,c]:contexts_){(void)id;c->permit->revoke();c->recovery=true;if(c->control<UINT64_MAX)++c->control;}
            ++publication_;
            throw std::runtime_error("Controller clock discontinuity; restart/reconcile required");
        }
        last_clock_=now;return now;
    }
    void validate_command(const Json& c) {
        if(!c.is_object())throw std::runtime_error("Fleet request must be an object");
        const std::set<std::string> common={"operation","action_id","controller_incarnation","assignment_generation",
            "reviewed_control_revision","reviewed_boot_id","accept_before_ms"};
        auto fields=common;
        const auto op=c.at("operation").get<std::string>();
        if(op=="assign")for(const auto* k:{"settings","name","schedule","enabled","management_port","consent_plain"})fields.insert(k);
        else if(op=="schedule"){fields.insert("device_id");fields.insert("schedule");}
        else if(op=="enable" || op=="pause" || op=="recover" || op=="remove")fields.insert("device_id");
        else throw std::runtime_error("Unknown Fleet operation");
        if(c.size()!=fields.size())throw std::runtime_error("Invalid Fleet action fields");
        for(const auto& key:fields)if(!c.contains(key))throw std::runtime_error("Missing Fleet action field");
        if(!fleet_id(c.at("action_id")) || !fleet_id(c.at("controller_incarnation")) ||
           !(c.at("assignment_generation").is_null() || fleet_id(c.at("assignment_generation"))) ||
           !(c.at("reviewed_boot_id").is_null() || fleet_id(c.at("reviewed_boot_id"))))
            throw std::runtime_error("Invalid Fleet action binding");
        (void)wtp_fleet_decimal(c.at("accept_before_ms"));
        if(!c.at("reviewed_control_revision").is_null())(void)wtp_fleet_decimal(c.at("reviewed_control_revision"));
        if(op!="assign" && !fleet_id(c.at("device_id")))throw std::runtime_error("Invalid Fleet device ID");
        if(op=="assign" || op=="schedule")validate_wtp_fleet_schedule(c.at("schedule"));
        if(op=="assign") {
            const auto settings=parse_wtp_settings(c.at("settings"),true);
            if(!fleet_id(settings.device_id) || !c.at("enabled").is_boolean() || !c.at("consent_plain").is_boolean() ||
               (settings.transport=="network_plain" && c.at("consent_plain")!=true) ||
               !c.at("management_port").is_number_integer() || c.at("management_port").get<int>()<1 || c.at("management_port").get<int>()>65535 ||
               !c.at("name").is_string() || c.at("name").get<std::string>().empty() || c.at("name").get<std::string>().size()>80)
                throw std::runtime_error("Invalid assignment fields or Plain LAN consent");
        }
    }
    PicoHttpResponse action_response(const FleetAction& a) const {
        return {a.pending?202U:(a.record.at("state")=="confirmed"?200U:409U),
            Json{{"controller_incarnation",incarnation_},{"publication_revision",std::to_string(publication_)},{"action",a.record}}.dump(),etag_};
    }
    std::size_t terminal_count() const {
        std::size_t result=0;for(const auto& [id,a]:actions_){(void)id;if(!a->pending)++result;}return result;
    }
    void expire_actions(std::uint64_t now) {
        std::vector<std::shared_ptr<FleetAction>> eligible;
        for(const auto& [id,a]:actions_){(void)id;if(!a->pending && now>=a->deadline)eligible.push_back(a);}
        std::sort(eligible.begin(),eligible.end(),[](const auto& a,const auto& b){return a->completed<b->completed;});
        auto count=terminal_count();
        for(const auto& a:eligible)if(count>256 || now-a->completed>=900000){actions_.erase(a->id);--count;}
    }
    PicoHttpResponse action_query(const std::string& ids,std::uint64_t now) {
        if(ids.empty() || ids.size()>2048)throw std::runtime_error("Invalid action query capacity");
        std::set<std::string> unique;Json result=Json::object();std::size_t begin=0;
        do {
            const auto end=ids.find(',',begin);const auto id=ids.substr(begin,end==std::string::npos?end:end-begin);
            if(!fleet_id(id) || !unique.insert(id).second || unique.size()>32)throw std::runtime_error("Invalid action query IDs");
            const auto found=actions_.find(id);
            result[id]=found==actions_.end() || (!found->second->pending && now>=found->second->deadline && now-found->second->completed>=900000)
                ?Json{{"state","unknown_or_expired"},{"action_id",id}}:found->second->record;
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
            p["can_pause"]=!clock_fault_ && !c->published.value("revoked",false) && !c->quit &&
                (journal_error_.empty() || (c->published.contains("identity") && !c->published.at("identity").is_null()));
            p["can_reconcile"]=!clock_fault_ && journal_error_.empty() && !c->quit && !c->binding_conflict;
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
            for(const auto& a:{c->ordinary,c->pause})if(a && (a->pending || now-a->completed<900000))p["actions"].push_back(a->record);
            data["outputs"][id]=std::move(p);
        }
        if(operations) {
            // Operations exposes no endpoints, credential paths or full CAPS.
            for(auto& row:data["assignments"])row.erase("settings");
        }
        return data;
    }
    bool current(FleetContext& c,const FleetAction& a) {
        std::lock_guard lock(mutex_);return !c.quit && a.fence==c.control && a.record.at("state")!="superseded";
    }
    void update_config(const std::string& revision,const std::function<void(Json&)>& mutation) {
        if(hooks_.before_persist)hooks_.before_persist();
        auto saved=store_.update(revision,mutation);
        std::lock_guard lock(mutex_);config_=std::move(saved.first);etag_=std::move(saved.second);++publication_;
    }
    void pause_saved(FleetContext& c) {
        update_config({},[&](Json& d){if(row_for(d,c.id))modify_row(d,c.id,[](Json& r){r["enabled"]=false;});});
        if(c.row.is_object())c.row["enabled"]=false;
    }
    void latch(FleetContext& c,const std::string& reason) {
        c.permit->revoke();
        {std::lock_guard lock(mutex_);if(!c.recovery.exchange(true)){if(c.control==UINT64_MAX)accepting_=false;else ++c.control;++publication_;}}
        c.message=bounded(reason,512);
        try{pause_saved(c);}catch(const std::exception& e){c.message=bounded(c.message+"; paused persistence unconfirmed: "+e.what(),512);}
    }
    void make_owner(FleetContext& c,const WtpSettings& settings) {
        set_target(c,make_target(hooks_,settings,[this,&c]{return dispatch_allowed(c);}));
    }
    bool dispatch_allowed(FleetContext& c) {
        if(c.quit || c.recovery || !storage_error().empty())return false;
        {std::lock_guard lock(c.permit->mutex);if(!c.permit->allowed)return false;}
        try {
            const auto row=row_for(store_.snapshot().first,c.id);
            if(!row || !row->at("enabled").get<bool>())return false;
            const auto status=target(c)->app->status();
            if(row->at("product")=="WsprryPi") {
                const auto state=host_read(c,*row);
                if(state.at("device_id")!=c.id || !status.identity || !state.contains("boot_id") ||
                   state.at("boot_id")!=status.identity->boot_id || state.value("revocation_unavailable",true) ||
                   state.value("local_requested",true) || state.value("output_unknown",true) ||
                   state.at("revocation_generation")!=row->at("revocation_generation"))
                    throw std::runtime_error("Pi takeover check refused dispatch");
            }
            check_caps(*row,status);return !c.quit && !c.recovery;
        }catch(const std::exception& e){
            c.permit->revoke();
            // The scheduler must not mutate its owner's stores or row. Hand
            // failure back to the owning loop, which persists the barrier.
            std::lock_guard lock(c.notice_mutex);c.external_fault=bounded(e.what(),512);return false;
        }
    }
    Json host_read(FleetContext& c,const Json& row) {
        std::lock_guard channel(c.host_io_mutex);
        auto state=revocation_status(hooks_,parse_wtp_settings(row.at("settings"),true),row.at("management_port").get<unsigned>());
        if(!state.is_object() || state.value("schema",std::string{})!="wsprrypi-wtp-endpoint/1" ||
           state.at("device_id")!=c.id || !fleet_id(state.at("boot_id")) ||
           !state.at("revocation_unavailable").is_boolean() || !state.at("local_requested").is_boolean() ||
           !state.at("output_unknown").is_boolean())throw std::runtime_error("Invalid Pi takeover observation binding");
        (void)wtp_fleet_decimal(state.at("revocation_generation"));
        return state;
    }
    bool refresh_host(FleetContext& c,const Json& row,bool force=false) {
        if(row.at("product")!="WsprryPi")return true;
        if(force) {
            if(c.host_thread.joinable())c.host_thread.join();
            c.last_host=now_ms();c.host_origin.reset();
            return apply_host(c,row,host_read(c,row),c.last_host);
        }
        if(c.host_thread.joinable() && !c.host_busy) {
            c.host_thread.join();
            Json result;std::string error;std::uint64_t origin;
            {std::lock_guard lock(c.host_result_mutex);result=std::move(c.host_result);error=std::move(c.host_error);origin=c.host_result_origin;}
            if(!error.empty()){c.host_origin.reset();c.message=std::move(error);}
            else {
                try {if(!apply_host(c,row,std::move(result),origin))return false;}
                catch(const std::exception& e){c.host_origin.reset();c.message=bounded(e.what(),512);}
            }
        }
        const auto now=now_ms();
        if(!c.host_thread.joinable() && (!c.last_host || (now>=c.last_host && now-c.last_host>=5000))) {
            c.last_host=now;c.host_busy=true;
            try {c.host_thread=std::thread([this,&c,row,now]{
                Json result;std::string error;
                try {result=host_read(c,row);}catch(const std::exception& e){error=bounded(e.what(),512);}
                catch(...){error="Pi takeover observation failed";}
                {std::lock_guard lock(c.host_result_mutex);c.host_result=std::move(result);c.host_error=std::move(error);c.host_result_origin=now;}
                c.host_busy=false;
            });}catch(...){c.host_busy=false;throw;}
        }
        return c.host_origin && now>=*c.host_origin && now-*c.host_origin<15000;
    }
    bool apply_host(FleetContext& c,const Json& row,Json state,std::uint64_t start) {
        auto t=target(c);
        if(state.at("device_id")!=c.id || state.value("revocation_unavailable",true))throw std::runtime_error("Pi takeover storage or identity unavailable");
        const auto status=t->app->status();
        if(state.contains("boot_id") && status.identity && state.at("boot_id")!=status.identity->boot_id)
            throw std::runtime_error("Pi takeover boot differs from WTP observation");
        if(state.at("revocation_generation")!=row.at("revocation_generation")) {
            c.permit->revoke();
            if(!c.removed){std::lock_guard lock(mutex_);if(c.control==UINT64_MAX)accepting_=false;else ++c.control;++publication_;}
            retire(c,"takeover");
            store_.revoke(c.id,state.at("revocation_generation").get<std::string>());
            sync_saved();c.removed=true;c.recovery=true;c.message="Local takeover removed the saved assignment";
            return false;
        }
        if(state.value("local_requested",true) || state.value("output_unknown",true))throw std::runtime_error("Target under local control or output unresolved");
        c.host=std::move(state);c.host_origin=start;return true;
    }
    void sync_saved(){auto saved=store_.snapshot();std::lock_guard lock(mutex_);config_=saved.first;etag_=saved.second;++publication_;}
    bool eligible(FleetContext& c,const Json& row,std::string& reason) {
        try {
            if(c.quit || c.recovery || c.removed || !c.retirement.is_null())throw std::runtime_error("Reconcile unresolved work before resuming");
            {std::lock_guard lock(mutex_);if(!journal_error_.empty())throw std::runtime_error(journal_error_);}
            auto t=target(c);if(!t)throw std::runtime_error("Output has not been inspected");
            const auto s=t->app->status();const auto now=t->clock.now_ms();
            if(!observed_healthy(s,row) || !observed_idle(s) || !t->app->ready() || !s.status_age_origin_ms ||
               now<*s.status_age_origin_ms || now-*s.status_age_origin_ms>=15000 || row.at("in_flight").get<bool>())
                throw std::runtime_error("Fresh matching idle evidence and a healthy session are required");
            if(row.at("product")=="WsprryPi") {
                const auto host_now=now_ms();
                if(!c.host_origin || host_now<*c.host_origin || host_now-*c.host_origin>=15000 ||
                   !c.host.contains("boot_id") || c.host.at("boot_id")!=s.identity->boot_id)
                    throw std::runtime_error("Fresh matching Pi takeover observation required");
            }
            check_caps(row,s);reason.clear();return true;
        }catch(const std::exception& e){reason=bounded(e.what(),512);return false;}
    }
    void inspect(FleetContext& c) {
        auto t=target(c);const auto started=now_ms();c.initial=true;
        const auto result=t->app->inspect();const auto s=t->app->status();
        if(now_ms()-started>=45000 || !observed_healthy(s,c.row))
            latch(c,result.error.empty()?"Output inspection failed or exceeded its deadline":result.error);
    }
    std::string storage_error() {std::lock_guard lock(mutex_);return journal_error_;}
    void put_retirement(const Json& entry) {
        try {journal_.put(entry);}
        catch(const std::exception& e){
            std::lock_guard lock(mutex_);journal_error_=bounded(e.what(),512);
            for(auto& [id,c]:contexts_){(void)id;c->permit->revoke();if(c->control==UINT64_MAX)accepting_=false;else ++c->control;}
            ++publication_;throw;
        }
    }
    void erase_retirement(const std::string& id) {
        try {journal_.erase(id);}
        catch(const std::exception& e){
            std::lock_guard lock(mutex_);journal_error_=bounded(e.what(),512);
            for(auto& [id,c]:contexts_){(void)id;c->permit->revoke();if(c->control==UINT64_MAX)accepting_=false;else ++c->control;}
            ++publication_;throw;
        }
    }
    void retire(FleetContext& c,const std::string& kind) {
        c.permit->revoke();
        if(!c.retirement.is_null()) {
            if(kind=="takeover" && c.retirement.at("kind")=="operator_remove") {
                auto takeover=c.retirement;takeover["kind"]="takeover";
                put_retirement(takeover);c.retirement=std::move(takeover);
            }
            return;
        }
        const auto saved=store_.snapshot();auto t=target(c);auto s=t?t->app->status():WtpRuntimeStatus{};
        auto entry=Json{{"retirement_id",wtp_random_identity()},{"kind",kind},{"assignment",c.row},
            {"source_etag",saved.second},{"last_boot",s.identity?Json(s.identity->boot_id):Json(nullptr)},
            {"last_job",s.job_id.empty()?Json(nullptr):Json(s.job_id)},
            {"disposition","pending"},{"unresolved",true}};
        put_retirement(entry);c.retirement=std::move(entry);
    }
    void clear_barrier(FleetContext& c,bool paused=true) {
        update_config({},[&](Json& d){if(row_for(d,c.id))modify_row(d,c.id,[&](Json& r){r["in_flight"]=false;if(paused)r["enabled"]=false;});});
        if(c.row.is_object()){c.row["in_flight"]=false;if(paused)c.row["enabled"]=false;}
        c.dispatched=false;
    }
    bool stop_owned(FleetContext& c,bool new_retirement=false) {
        const bool retained=!c.retirement.is_null();
        const bool frozen_job=retained && c.retirement.at("assignment").value("in_flight",false);
        auto t=target(c);const bool tracked=c.dispatched || frozen_job || (c.row.is_object() && c.row.value("in_flight",false));
        if(c.binding_conflict || (retained && !new_retirement && !tracked))return false;
        if(!t)return !tracked;
        const auto stopped=t->app->stop();
        // A restored empty context cannot clear a persisted previous job.
        if(tracked && !c.dispatched)return false;
        if(!stopped.ok)return false;
        if(tracked) {
            // An already-idle completion may predate Pause acceptance. Obtain
            // new read-only evidence on the existing healthy session; this
            // path never reconnects a failed/changed session.
            const auto before=t->app->status();
            if(before.session_phase!=wtp::SessionPhase::Ready || before.phase!=WtpSchedulePhase::Idle ||
               !t->app->inspect().ok)return false;
            const auto s=t->app->status();
            if(s.uncertain || s.safety_fault || !s.remote || s.remote->output_active || s.remote->owner_id)return false;
            clear_barrier(c,false);
        }
        return true;
    }
    void complete(FleetContext& c,FleetAction& a,std::string state,std::string reason,Json post=Json::object()) {
        const auto t=target(c);const auto s=t?t->app->status():WtpRuntimeStatus{};
        std::lock_guard lock(mutex_);
        if(a.record.at("state")=="superseded" || (a.fence!=c.control && a.operation!="pause" && state=="confirmed"))state="superseded";
        a.pending=false;a.completed=now_ms();a.record["state"]=state;a.record["work_pending"]=false;
        a.record["completed_ms"]=std::to_string(a.completed);a.record["reason"]=bounded(std::move(reason),128);
        a.record["result_boot_id"]=s.identity?Json(s.identity->boot_id):Json(nullptr);
        a.record["postconditions"]=std::move(post);
        if(a.record.dump().size()>2048)throw std::runtime_error("Action record exceeds serialized budget");
        a.command=nullptr; // Terminal records retain a digest, not request settings.
        ++publication_;
    }
    void perform(FleetContext& c,FleetAction& a) {
        if(!current(c,a) && a.operation!="pause"){complete(c,a,"superseded","A newer intent revoked this action");return;}
        {std::lock_guard lock(mutex_);a.record["state"]="pending";++publication_;}
        bool effects=a.operation=="pause";
        try {
            const auto& cmd=a.command;
            if(a.operation=="assign") {
                const auto settings=parse_wtp_settings(cmd.at("settings"),true);
                make_owner(c,settings);auto t=target(c);
                c.row={{"device_id",c.id},{"settings",cmd.at("settings")},{"name",cmd.at("name")},
                    {"schedule",cmd.at("schedule")},{"enabled",false},{"product","WsprryPico"},
                    {"management_port",cmd.at("management_port")},{"revocation_generation",nullptr},{"last_start_ns","0"},{"in_flight",false}};
                const auto started=now_ms();const auto result=t->app->inspect();c.initial=true;
                const auto s=t->app->status();
                if(!s.identity || now_ms()-started>=45000)throw std::runtime_error("Output inspection unavailable or expired");
                c.row["product"]=s.identity->product;
                if(!observed_healthy(s,c.row))throw std::runtime_error(result.error);
                if(c.row.at("product")=="WsprryPi") {
                    auto state=revocation_status(hooks_,settings,c.row.at("management_port").get<unsigned>());
                    c.row["revocation_generation"]=state.at("revocation_generation");
                    if(!refresh_host(c,c.row,true))throw std::runtime_error("Pi assignment was revoked");
                }
                check_caps(c.row,s);
                if(cmd.at("enabled").get<bool>()){std::string why;if(!eligible(c,c.row,why))throw std::runtime_error(why);}
                if(!current(c,a))throw std::runtime_error("Action superseded before assignment persistence");
                c.row["enabled"]=cmd.at("enabled");
                update_config(a.etag,[&](Json& d){if(!current(c,a))throw std::runtime_error("Action superseded");effects=true;d["assignments"].push_back(c.row);});
            } else {
                auto saved=store_.snapshot();auto row=row_for(saved.first,c.id);
                if(!row && a.operation!="recover" && a.operation!="pause")throw std::runtime_error("Assignment no longer exists");
                if(row)c.row=*row;
                if(saved.second!=a.etag)throw std::runtime_error("revision_conflict");
                if(a.operation=="enable") {
                    std::string why;if(!eligible(c,c.row,why))throw std::runtime_error(why);
                    const auto s=target(c)->app->status();if(cmd.at("reviewed_boot_id")!=s.identity->boot_id)throw std::runtime_error("Boot changed");
                    update_config(a.etag,[&](Json& d){if(!current(c,a))throw std::runtime_error("Enable superseded");effects=true;modify_row(d,c.id,[](Json& r){r["enabled"]=true;});});
                    effects=true;c.row["enabled"]=true;
                } else if(a.operation=="pause") {
                    pause_saved(c);effects=true;const bool tracked=c.row.value("in_flight",false)||c.dispatched||!c.retirement.is_null();
                    const bool clean=stop_owned(c);c.cleanup_ok=clean;
                    if(!clean)c.recovery=true;
                    complete(c,a,clean?"confirmed":"outcome_unconfirmed",clean?"Schedule paused; device output remains a separate observation":"Owned cleanup remains unconfirmed",
                        {{"schedule_paused",true},{"controller_work_quiescent",true},{"tracked_cleanup_confirmed",tracked?Json(clean):Json(nullptr)}});
                    return;
                } else if(a.operation=="schedule") {
                    if(c.row.at("enabled").get<bool>() || c.row.at("in_flight").get<bool>() || c.recovery || !c.retirement.is_null())throw std::runtime_error("Pause and reconcile before editing");
                    auto candidate=c.row;candidate["schedule"]=cmd.at("schedule");
                    if(auto t=target(c);t && t->app->status().capabilities)check_caps(candidate,t->app->status());
                    update_config(a.etag,[&](Json& d){if(!current(c,a))throw std::runtime_error("Schedule superseded");effects=true;modify_row(d,c.id,[&](Json& r){r["schedule"]=cmd.at("schedule");});});
                    effects=true;c.row=std::move(candidate);
                } else if(a.operation=="remove") {
                    const bool fresh_intent=c.retirement.is_null();
                    retire(c,"operator_remove");effects=true;pause_saved(c);
                    if(!stop_owned(c,fresh_intent))throw std::runtime_error("Owned cleanup unconfirmed; assignment retained paused");
                    if(!current(c,a))throw std::runtime_error("Removal superseded");
                    update_config({},[&](Json& d){if(!current(c,a))throw std::runtime_error("Removal superseded before deletion");auto& rows=d["assignments"];for(auto it=rows.begin();it!=rows.end();++it)if(it->at("device_id")==c.id){rows.erase(it);break;}});
                    c.removed=true;c.retirement["disposition"]="removed";put_retirement(c.retirement);
                    erase_retirement(c.retirement.at("retirement_id"));c.retirement=nullptr;
                } else if(a.operation=="recover") {
                    if(c.binding_conflict)throw std::runtime_error("Retained assignment binding conflict requires explicit maintenance review");
                    if(!storage_error().empty())throw std::runtime_error("Journal requires explicit maintenance; reconciliation cannot repair it");
                    const bool had_barrier=c.recovery || c.dispatched || !c.retirement.is_null() || c.row.value("in_flight",false);
                    if(row)pause_saved(c);
                    effects=true;
                    if(!target(c))make_owner(c,parse_wtp_settings(c.row.at("settings"),true));
                    auto t=target(c);(void)t->app->stop();auto result=t->app->recover();
                    if(!result.ok && t->app->status().session_phase==wtp::SessionPhase::IdentityChanged) {
                        const auto previous=t->app->status();t->plain.close();t->tls.close();t->stream.close();
                        auto fresh=make_target(hooks_,parse_wtp_settings(c.row.at("settings"),true),[this,&c]{return dispatch_allowed(c);},t->output_lease_snapshot());
                        const auto inspected=fresh->app->inspect();
                        if(inspected.ok && wtp_fleet_restarted_output_safe(previous,fresh->app->status(),c.id,c.row.at("product"))) {
                            set_target(c,std::move(fresh));t=target(c);result={true,{}};
                        }
                    }
                    const auto observed=t->app->status();
                    if(!had_barrier && observed_healthy(observed,c.row) && !observed_idle(observed)) {
                        complete(c,a,"refused","Other job on device; schedule paused without taking ownership",
                            {{"schedule_paused",true},{"controller_work_quiescent",true},{"tracked_cleanup_confirmed",nullptr}});
                        return;
                    }
                    if(!result.ok || !observed_healthy(observed,c.row) || !observed_idle(observed))throw std::runtime_error(result.error.empty()?"Safe inactive/unowned state is unconfirmed":result.error);
                    if(!current(c,a))throw std::runtime_error("Reconciliation superseded");
                    if(!refresh_host(c,c.row,true) && !c.removed)throw std::runtime_error("Output revoked");
                    if(t->settings.transport=="usb") {
                        (void)t->reconcile_usb_binding();
                        if(t->has_usb_binding_fault())throw std::runtime_error("USB management binding remains unresolved");
                    }
                    if(row)clear_barrier(c);
                    if(!c.retirement.is_null()) {
                        c.retirement["disposition"]=c.removed?"removed":"retain_paused";put_retirement(c.retirement);
                        erase_retirement(c.retirement.at("retirement_id"));c.retirement=nullptr;
                    }
                    c.recovery=false;c.message.clear();c.initial=true;
                }
            }
            if(!current(c,a)) {
                c.permit->revoke();if(effects && !c.removed)pause_saved(c);
                complete(c,a,"superseded","Newer intent revoked activation");return;
            }
            if((a.operation=="enable" || a.operation=="assign") && c.row.value("enabled",false)) {
                std::lock_guard lock(mutex_);
                std::lock_guard permit(c.permit->mutex);
                if(a.fence==c.control && !c.quit && !c.recovery)c.permit->allowed=true;
            }
            complete(c,a,"confirmed","Requested postconditions committed");
        }catch(const std::exception& e) {
            c.permit->revoke();
            if(effects)latch(c,e.what());
            complete(c,a,effects?"outcome_unconfirmed":"refused",e.what());
            if(a.operation=="assign" && !row_for(store_.snapshot().first,c.id)) {
                c.removed=true;c.retirement=nullptr;
            }
        }
    }
    void publish(FleetContext& c) {
        auto t=target(c);const auto s=t?t->app->status():WtpRuntimeStatus{};
        Json p={{"state",c.removed?"removed":(c.recovery || !c.retirement.is_null())?"recovery_required":"paused"},
            {"message",bounded(c.message,512)},{"revoked",c.removed},{"can_resume",false},
            {"boot_id",s.identity?Json(s.identity->boot_id):Json(nullptr)},
            {"identity",s.identity?Json{{"device_id",s.identity->device_id},{"product",s.identity->product},
                {"boot_id",s.identity->boot_id},{"firmware_version",bounded(s.identity->firmware_version,512)}}:Json(nullptr)},
            {"age_origin_ms",nullptr},{"age_origin_kind",s.status_age_origin_kind},
            {"pending_start",nullptr},{"forecast",nullptr},{"host_observation_age_ms",nullptr},{"host_age_origin_ms",nullptr},
            {"retained_name",c.row.is_object()?c.row.value("name",c.id):c.id},
            {"transport",t?t->settings.transport:""},{"trust",!s.identity?"Identity unverified":t && t->settings.transport=="network"?
                (s.session_phase==wtp::SessionPhase::Ready?"TLS verified":"TLS last verified"):"Expected identity match"}};
        if(const auto error=storage_error();!error.empty()){p["state"]="recovery_required";p["message"]=error;}
        if(t && s.status_age_origin_ms && s.status_observed_ms) {
            // Fixture clocks may differ from the controller publication domain.
            const auto elapsed=t->clock.now_ms()>=*s.status_age_origin_ms?t->clock.now_ms()-*s.status_age_origin_ms:UINT64_MAX;
            const auto now=now_ms();if(elapsed<now)p["age_origin_ms"]=std::to_string(now-elapsed-1);
        }
        if(t) {
            auto status=Json::parse(wtp_runtime_status_json(s));status.erase("capabilities");
            // Bounded summaries, never full per-event arrays.
            Json pairs=Json::array();std::set<std::pair<std::uint64_t,std::uint64_t>> unique;
            for(const auto& a:s.adjustments)if(c.dispatched && s.remote && s.remote->job_id && *s.remote->job_id==s.job_id && unique.emplace(a.requested_frequency_nhz,a.realized_frequency_nhz).second && pairs.size()<16)
                pairs.push_back({{"requested_nhz",std::to_string(a.requested_frequency_nhz)},{"realized_nhz",std::to_string(a.realized_frequency_nhz)}});
            status["adjustments"]=std::move(pairs);status["adjustments_omitted"]=unique.size()>16;
            if(status["last_report"].is_object()){status["last_report"]["adjustments"]=Json::array();status["last_report"]["error"]=bounded(status["last_report"].value("error",""),512);}
            bool truncated=false;
            std::function<void(Json&)> bound_text=[&](Json& value){
                if(value.is_string()){const auto text=value.get<std::string>();if(text.size()>512){value=bounded(text,512);truncated=true;}}
                else if(value.is_object() || value.is_array())for(auto& child:value)bound_text(child);
            };
            bound_text(status);status["diagnostics_truncated"]=truncated;
            p["wtp"]=std::move(status);
            if(s.status_observed_ms && (s.status_sequence!=c.last_sequence || *s.status_observed_ms!=c.last_status || (s.status_age_origin_ms && *s.status_age_origin_ms!=c.last_origin))) {
                std::lock_guard lock(mutex_);++c.observation;c.last_status=*s.status_observed_ms;c.last_sequence=s.status_sequence;c.last_origin=s.status_age_origin_ms.value_or(0);
            }
            if(!s.remote && c.last_status){std::lock_guard lock(mutex_);++c.observation;c.last_status=0;}
            if(!c.row.is_null()) {
                std::string why;p["can_resume"]=eligible(c,c.row,why);p["resume_reason"]=why;
                if(!c.recovery && !c.removed && c.retirement.is_null() && storage_error().empty()) {
                    if(!observed_healthy(s,c.row))p["state"]="unavailable";
                    else if(!observed_idle(s) && (!s.job || !s.job->authoritative))p["state"]="foreign";
                    else if(s.job && s.job->state)p["state"]=p["wtp"]["job"]["state"];
                    else if(c.dispatched && t->app->active())p["state"]="waiting";
                    else p["state"]="idle";
                }
                if(c.dispatched && t->app->active() &&
                   !(s.job && s.job->state && *s.job->state!=wtp::State::Loaded && *s.job->state!=wtp::State::Armed))
                    p["pending_start"]={{"job_id",s.job_id},{"request_id",std::to_string(s.request_id.value)},{"start_utc_ns",std::to_string(s.start_utc_ns)}};
                if(!c.dispatched && !c.removed && !c.recovery && p["can_resume"]==true) {
                    try {const auto utc=t->clock.utc_now_ns();const auto lead=t->app->preparation_lead_ns();
                        if(utc && *utc<=INT64_MAX-lead)p["forecast"]={{"if_resumed",!c.row.value("enabled",false)},
                            {"start_utc_ns",std::to_string(wtp_fleet_next_slot(c.row.at("schedule"),*utc+lead,wtp_fleet_decimal(c.row.at("last_start_ns"))))}};
                    }catch(...){}
                }
            }
            if(const auto utc=t->clock.utc_now_ns())p["utc_anchor_ns"]=std::to_string(*utc);else p["utc_anchor_ns"]=nullptr;
            if(c.host_origin){p["host_age_origin_ms"]=std::to_string(*c.host_origin);p["host_observation_age_ms"]=std::to_string(now_ms()-*c.host_origin);}
        }
        p["utc_anchor_ms"]=std::to_string(now_ms());
        std::lock_guard lock(mutex_);if(p!=c.published){c.published=std::move(p);++publication_;}
    }
    void tick(FleetContext& c) {
        if(c.row.is_null() || c.removed)return;
        if(!storage_error().empty() || !c.retirement.is_null())return;
        if(!c.initial && c.recovery)return; // Restored crash barrier: no initial carrier work.
        if(!target(c))make_owner(c,parse_wtp_settings(c.row.at("settings"),true));
        auto t=target(c);
        if(!c.initial && !c.recovery && now_ms()>=c.initial_due) {
            inspect(c);
            if(!c.recovery && c.row.value("enabled",false)) {
                std::lock_guard permit(c.permit->mutex);c.permit->allowed=true;
            }
        }
        if(!c.initial || c.recovery){if(c.initial && !t->app->active())(void)refresh_host(c,c.row);return;}
        if(!t->app->active()) {
            (void)t->app->poll_idle();
            if(!observed_healthy(t->app->status(),c.row)){latch(c,"Observation failed; explicit Reconcile required");return;}
            (void)refresh_host(c,c.row);
            if(c.removed)return;
        }
        if(auto completion=t->app->take_completion()) {
            const auto s=t->app->status();
            if(!s.uncertain && !s.safety_fault && observed_idle(s)) {
                update_config({},[&](Json& d){if(row_for(d,c.id))modify_row(d,c.id,[](Json& r){r["in_flight"]=false;});});
                c.row["in_flight"]=false;c.dispatched=false;
            }
            if(completion->outcome!=WtpScheduleOutcome::Complete){latch(c,completion->error);return;}
        }
        if(!c.row.value("enabled",false))return;
        {std::lock_guard permit(c.permit->mutex);if(!c.permit->allowed)return;}
        if(t->app->active())return;
        std::string why;if(!eligible(c,c.row,why)){c.message=why;return;}
        t->refresh_usb_time(true); // Independently authorized scheduler activity.
        const auto utc=t->clock.utc_now_ns();const auto lead=t->app->preparation_lead_ns();
        if(!utc || *utc>INT64_MAX-lead)throw std::runtime_error("Controller UTC unavailable or overflow");
        const auto next=wtp_fleet_next_slot(c.row.at("schedule"),*utc+lead,wtp_fleet_decimal(c.row.at("last_start_ns")));
        auto request=wtp_fleet_request(c.row.at("schedule"));const auto policy=current_test_tone_planning_config_snapshot();
        request.policy.allow_unqualified_frequency=policy.allow_unqualified_frequency;request.policy.allow_non_amateur_frequency=policy.allow_non_amateur_frequency;
        request.slot.start_time=std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(next)));
        t->app->prepare(std::move(request));
        try {
            update_config({},[&](Json& d){modify_row(d,c.id,[&](Json& r){
                std::lock_guard permit(c.permit->mutex);
                if(!c.permit->allowed || c.quit || !r.value("enabled",false) || r.at("schedule")!=c.row.at("schedule"))throw std::runtime_error("Dispatch revoked");
                r["last_start_ns"]=std::to_string(next);r["in_flight"]=true;
            });});
            c.row["last_start_ns"]=std::to_string(next);c.row["in_flight"]=true;c.dispatched=true;
            {std::lock_guard permit(c.permit->mutex);if(!c.permit->allowed)throw std::runtime_error("Dispatch revoked after storage");}
            t->app->start();
        }catch(...){(void)t->app->stop();throw;}
    }
    void run(FleetContext& c) noexcept {
        std::uint64_t next_tick=0;
        while(!c.quit) {
            try {
                std::string notice;
                {std::lock_guard lock(c.notice_mutex);notice.swap(c.external_fault);}
                if(!notice.empty())latch(c,notice);
                std::shared_ptr<FleetAction> action;
                {std::lock_guard lock(mutex_);
                 if(c.ordinary && c.ordinary->pending)action=c.ordinary;
                 else if(c.pause && c.pause->pending)action=c.pause;}
                if(action){perform(c,*action);publish(c);}
                else if(now_ms()>=next_tick) {
                    tick(c);publish(c);next_tick=now_ms()+1000;
                }
                if(c.removed && c.retirement.is_null()) {
                    std::lock_guard lock(mutex_);
                    if((!c.ordinary || !c.ordinary->pending) && (!c.pause || !c.pause->pending)){c.reclaimable=true;break;}
                }
            }catch(const std::exception& e){try{latch(c,e.what());publish(c);}catch(...) {}}
            catch(...){c.permit->revoke();c.recovery=true;}
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        try {
            c.cleanup_ok=stop_owned(c);
            // Existing in-flight/retirement authority survives unconfirmed cleanup.
            publish(c);
        }catch(...){c.cleanup_ok=false;}
        if(c.host_thread.joinable())c.host_thread.join();
        // Old HTTP references retain projections, not live sessions or identity
        // leases. Release only after owned work and callbacks have quiesced.
        std::shared_ptr<WtpTargetRuntime> retired;
        {std::lock_guard lock(c.target_mutex);retired.swap(c.target);}
        retired.reset();
        c.finished=true;
        std::lock_guard lock(mutex_);
        for(auto& a:{c.ordinary,c.pause})if(a && a->pending){a->pending=false;a->completed=now_ms();a->record["state"]="outcome_unconfirmed";a->record["work_pending"]=false;a->record["reason"]="Controller owner retired; review restored barriers";}
    }
    WtpFleetStore store_;WtpFleetJournal journal_;
    std::string self_id_;WtpFleetTestHooks hooks_;std::string incarnation_;
    WtpSteadyClock clock_;std::uint64_t last_clock_{},publication_{1};bool accepting_{true},clock_fault_{};
    std::atomic_bool exiting_{false};std::thread reaper_;
    std::mutex mutex_;Json config_;std::string etag_,journal_error_;
    std::map<std::string,std::shared_ptr<FleetContext>> contexts_;
    std::map<std::string,std::shared_ptr<FleetAction>> actions_;
};
std::mutex manager_mutex,lifecycle_mutex;
std::shared_ptr<Fleet> manager;
std::string startup_error;
void replace_manager(const std::string& ini,const std::string& self,WtpFleetTestHooks hooks={}) {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Fleet> old;{std::lock_guard lock(manager_mutex);old=manager;}
    if(old)old->shutdown(); // No candidate sessions/writers before old quiescence.
    auto candidate=std::make_shared<Fleet>(ini,self,std::move(hooks));
    {std::lock_guard lock(manager_mutex);manager=std::move(candidate);startup_error.clear();}
}
} // namespace
void start_wtp_fleet(const std::string& ini,const std::string& self) {
    try{replace_manager(ini,self.empty()?wtp_pi_device_id():self);}
    catch(const std::exception& e){std::lock_guard lock(manager_mutex);startup_error=e.what();}
}
void start_wtp_fleet_for_test(const std::string& ini,const std::string& self,WtpFleetTestHooks hooks) {
    if(!hooks.create_target || !hooks.revocation_status)throw std::runtime_error("Explicit Fleet fixture providers required");
    replace_manager(ini,self,std::move(hooks));
}
void stop_wtp_fleet() noexcept {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Fleet> old;{std::lock_guard lock(manager_mutex);old=manager;}
    if(old)old->shutdown();
    {std::lock_guard lock(manager_mutex);manager.reset();}
}
PicoHttpResponse wtp_fleet_api(const std::string& method,const std::string& body,const std::string& revision,
                              const std::string& resource,const std::string& ids) {
    std::shared_ptr<Fleet> current;std::string error;
    {std::lock_guard lock(manager_mutex);current=manager;error=startup_error;}
    if(!current)return {503,Json{{"error",{{"code","fleet_unavailable"},{"message",error}}}}.dump(),{}};
    try{return current->request(method,body,revision,resource,ids);}
    catch(const std::exception& e){const std::string reason=e.what();return {reason=="revision_conflict"?412U:409U,
        Json{{"error",{{"code",reason=="revision_conflict"?"revision_conflict":"fleet_request_rejected"},{"message",bounded(reason,512)}}}}.dump(),{}};}
}
} // namespace wsprrypi
