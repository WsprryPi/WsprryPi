// SPDX-License-Identifier: MIT
#include "fleet_runtime.hpp"
#include "runtime_config_bridge.hpp"
#include "fleet_store.hpp"
#include "fleet_schedule.hpp"
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
        {std::string(32,'1'),121000000000ULL,parse_wtp_settings(row.at("settings"),true).start_uncertainty_ns});
    if(!prepared) throw std::runtime_error(prepared.explanation);
}
class OutputWorker {
public:
    OutputWorker(WtpFleetStore& store,Json row,std::unique_ptr<WtpTargetRuntime> verified={})
        :store_(store),id_(row.at("device_id").get<std::string>()),settings_(parse_wtp_settings(row.at("settings"),true)) {
        // Use one native context per target; a temporary read-only inspection
        // can transfer its identity exclusion lease without transferring jobs.
        auto lease=verified?verified->output_lease:std::shared_ptr<void>{};
        verified.reset();
        target_=std::make_unique<WtpTargetRuntime>(settings_,[this]{return admit();},std::move(lease));
    }
    ~OutputWorker(){halt();}
    void start(){quit_=false;finished_=false;thread_=std::thread([this]{run();});}
    bool halt(){quit_=true;cv_.notify_all();if(thread_.joinable())thread_.join();return cleanup_ok_;}
    void recover(){recover_=true;cv_.notify_all();}
    bool retired() const {return revoked_ && finished_ && cleanup_ok_;}
    bool reconcile_retired() {
        if(!revoked_ || !finished_)return false;
        const auto result=target_->app->recover();
        cleanup_ok_=result.ok;
        publish(result.ok?"revoked":"recovery_required",result.error);
        return result.ok;
    }
    Json status() const {
        std::lock_guard lock(status_mutex_);
        auto result=status_;
        result["wtp"]=Json::parse(wtp_runtime_status_json(target_->app->status()));
        result["revoked"]=revoked_.load();
        return result;
    }
private:
    void publish(std::string state,std::string message={}) {
        std::lock_guard lock(status_mutex_);
        status_={{"state",std::move(state)},{"message",std::move(message)},{"observed_ms",target_->clock.now_ms()}};
    }
    bool reconcile(const Json& row) {
        if(row.at("product")!="WsprryPi") return true;
        const auto state=wtp_pi_revocation_status(settings_,row.at("management_port").get<unsigned>());
        if(state.at("revocation_generation")!=row.at("revocation_generation")) {
            store_.revoke(id_,state.at("revocation_generation").get<std::string>());
            revoked_=true;
            publish("revoked","Local takeover removed this output's saved schedule.");
            return false;
        }
        if(state.value("local_requested",true) || state.value("output_unknown",true))
            throw std::runtime_error("Target is under local control or its output is unresolved");
        return true;
    }
    bool admit() {
        try {
            const auto row=row_for(store_.snapshot().first,id_);
            if(quit_ || !row || !row->at("enabled").get<bool>() || !reconcile(*row))return false;
            check_caps(*row,target_->app->status());
            return true;
        } catch(const std::exception& e){publish("paused",e.what());return false;}
    }
    void clear_in_flight() {
        store_.update({},[&](Json& data){if(row_for(data,id_))modify_row(data,id_,[](Json& row){row["in_flight"]=false;});});
        dispatched_here_=false;
    }
    void tick() {
        auto row=row_for(store_.snapshot().first,id_);
        if(!row){revoked_=true;cleanup_ok_=target_->app->stop().ok;return;}
        // Paused and crash-retained assignments also observe offline takeover.
        // Do not interrupt a running job selected to finish locally.
        if((!target_->app->active() || target_->app->phase()==WtpSchedulePhase::Waiting) && !reconcile(*row))return;
        if(recover_.exchange(false)) {
            (void)target_->app->stop();
            const auto result=target_->app->recover();
            cleanup_ok_=result.ok;
            if(result.ok && reconcile(*row)) {clear_in_flight();publish("paused","Output reconciled. Resume to schedule future slots.");}
            else publish("recovery_required",result.error);
            return;
        }
        if(!row->at("enabled").get<bool>()) {
            if(target_->app->active()) {
                cleanup_ok_=target_->app->stop().ok;
                if(cleanup_ok_)clear_in_flight();
            }
            publish(row->at("in_flight").get<bool>()?"recovery_required":"paused");
            return;
        }
        if(auto completion=target_->app->take_completion()) {
            const auto state=target_->app->status();
            const bool safe=state.phase==WtpSchedulePhase::Idle && !state.uncertain && !state.safety_fault &&
                state.remote && !state.remote->output_active && !state.remote->owner_id;
            if(safe)clear_in_flight();
            if(completion->outcome!=WtpScheduleOutcome::Complete) {
                // A failed or cancelled slot is never automatically retried.
                store_.update({},[&](Json& data){if(row_for(data,id_))modify_row(data,id_,[](Json& r){r["enabled"]=false;});});
                publish(safe?"paused":"recovery_required",completion->error);
                return;
            }
            publish("complete");
            row=row_for(store_.snapshot().first,id_);
            if(!row)return;
        }
        if(target_->app->active()) {publish("scheduled");return;}
        if(row->at("in_flight").get<bool>()) {publish("recovery_required","A previous process may have dispatched this job. Reconcile before resuming.");return;}
        // Read-only reconnect and revocation observation precede each new slot.
        if(!reconcile(*row))return;
        const auto inspection=target_->app->inspect();
        if(!inspection.ok){publish("paused",inspection.error);return;}
        check_caps(*row,target_->app->status());
        const auto utc=target_->clock.utc_now_ns();
        if(!utc)throw std::runtime_error("Controller UTC is not synchronized");
        const auto lead=target_->app->preparation_lead_ns();
        if(*utc>INT64_MAX-lead)throw std::runtime_error("Controller UTC overflow");
        const auto next=wtp_fleet_next_slot(row->at("schedule"),*utc+lead,
                                           wtp_fleet_decimal(row->at("last_start_ns")));
        auto request=wtp_fleet_request(row->at("schedule"));
        const auto policy=current_test_tone_planning_config_snapshot();
        request.policy.allow_unqualified_frequency=policy.allow_unqualified_frequency;
        request.policy.allow_non_amateur_frequency=policy.allow_non_amateur_frequency;
        request.slot.start_time=std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(next)));
        target_->app->prepare(std::move(request));
        try {
            store_.update({},[&](Json& data){modify_row(data,id_,[&](Json& r){
                if(!r.at("enabled").get<bool>() || r.at("schedule")!=row->at("schedule"))
                    throw std::runtime_error("Assignment changed before dispatch");
                r["last_start_ns"]=std::to_string(next);r["in_flight"]=true;
            });});
        } catch(...){(void)target_->app->stop();throw;}
        dispatched_here_=true;
        target_->app->start();
        publish("scheduled");
    }
    void run() noexcept {
        while(!quit_ && !revoked_) {
            try{tick();}catch(const std::exception& e){publish("paused",e.what());}
            catch(...){publish("recovery_required","Unexpected output worker failure");}
            std::unique_lock lock(wait_mutex_);
            cv_.wait_for(lock,std::chrono::seconds(1),[&]{return quit_.load()||recover_.load();});
        }
        try {
            cleanup_ok_=target_->app->stop().ok;
            const auto row=row_for(store_.snapshot().first,id_);
            // An idle context created after a crash has never reconciled the
            // old process's job. Stopping that empty context proves nothing
            // about the persisted dispatch and must not erase its barrier.
            if(row && row->at("in_flight").get<bool>() && !dispatched_here_)
                cleanup_ok_=false;
            if(cleanup_ok_ && dispatched_here_)clear_in_flight();
        }catch(...){cleanup_ok_=false;}
        finished_=true;
    }
    WtpFleetStore& store_;
    std::string id_;
    WtpSettings settings_;
    std::unique_ptr<WtpTargetRuntime> target_;
    std::atomic<bool> quit_{false},recover_{false},revoked_{false},cleanup_ok_{true},finished_{false};
    std::thread thread_;
    mutable std::mutex status_mutex_;
    Json status_={{"state","starting"},{"message",""}};
    std::mutex wait_mutex_;
    std::condition_variable cv_;
    bool dispatched_here_{};
};
class Fleet {
public:
    Fleet(const std::string& path,const std::string& self):store_(path,self),self_id_(self) {
        const auto saved = store_.snapshot().first;
        for(const auto& row:saved.at("assignments")) {
            const auto id=row.at("device_id").get<std::string>();
            try{auto worker=std::make_unique<OutputWorker>(store_,row);worker->start();workers_.emplace(id,std::move(worker));}
            catch(const std::exception& e){errors_[id]=e.what();}
        }
    }
    PicoHttpResponse request(const std::string& method,const std::string& body,const std::string& revision) {
        std::lock_guard operation(mutex_);
        prune_retired();
        if(method=="GET")return snapshot();
        if(method!="POST")return {405,R"({"error":{"code":"method_not_allowed"}})",{}};
        if(revision.empty())return {428,R"({"error":{"code":"revision_required"}})",{}};
        auto [before,etag]=store_.snapshot();
        if(etag!=revision)throw std::runtime_error("revision_conflict");
        const auto command=strict_browser_json(body);
        const auto operation_name=command.at("operation").get<std::string>();
        if(operation_name=="assign") {
            if(command.size()!=7)throw std::runtime_error("Invalid assignment fields");
            const auto settings=parse_wtp_settings(command.at("settings"),true);
            if(settings.device_id.size()!=32)throw std::runtime_error("Confirm the full WTP device identity first");
            if(settings.transport=="network_plain" && command.at("consent_plain")!=true)
                throw std::runtime_error("Plain LAN requires explicit consent");
            if(before.at("assignments").size()>=8 || workers_.size()>=8)
                throw std::runtime_error("Fleet supports at most eight remote contexts; reconcile unresolved outputs first");
            if(row_for(before,settings.device_id))throw std::runtime_error("This output already has an assignment");
            if(auto old=workers_.find(settings.device_id);old!=workers_.end()) {
                if(!old->second->halt()) {old->second->start();throw std::runtime_error("Previous output cleanup is unresolved");}
                workers_.erase(old);
            }
            validate_wtp_fleet_schedule(command.at("schedule"));
            auto verified=std::make_unique<WtpTargetRuntime>(settings);
            const auto inspected=verified->app->inspect();
            if(!inspected.ok)throw std::runtime_error(inspected.error);
            const auto status=verified->app->status();
            if(!status.identity)throw std::runtime_error("Output identity unavailable");
            Json row={{"device_id",settings.device_id},{"settings",wtp_settings_json(settings)},
                {"name",command.at("name")},{"schedule",command.at("schedule")},{"enabled",command.at("enabled")},
                {"product",status.identity->product},{"management_port",command.at("management_port")},
                {"revocation_generation",nullptr},{"last_start_ns","0"},{"in_flight",false}};
            if(status.identity->product=="WsprryPi") {
                const auto state=wtp_pi_revocation_status(settings,row.at("management_port").get<unsigned>());
                if(state.value("local_requested",true)||state.value("output_unknown",true))
                    throw std::runtime_error("Release local control on the target before assigning it");
                row["revocation_generation"]=state.at("revocation_generation");
            }
            check_caps(row,status);
            // Validate self/duplicate/field policy before creating a worker.
            auto candidate=before;candidate["assignments"].push_back(row);WtpFleetStore::validate(candidate,self_id_);
            auto worker=std::make_unique<OutputWorker>(store_,row,std::move(verified));
            store_.update(revision,[&](Json& data){data["assignments"].push_back(row);});
            auto [it,inserted]=workers_.emplace(settings.device_id,std::move(worker));
            if(!inserted)throw std::runtime_error("Resolve the previous output context before reassigning it");
            try {it->second->start();}
            catch(...) {
                store_.update({},[&](Json& data){modify_row(data,settings.device_id,[](Json& r){r["enabled"]=false;});});
                workers_.erase(it);
                throw;
            }
        } else {
            const auto id=command.at("device_id").get<std::string>();
            auto worker=workers_.find(id);
            const auto row=row_for(before,id);
            if(!row) {
                if(operation_name=="recover" && command.size()==2 && worker!=workers_.end()) {
                    if(!worker->second->reconcile_retired())throw std::runtime_error("Removed output cleanup remains unresolved");
                    workers_.erase(worker);
                    return snapshot();
                }
                throw std::runtime_error("Output assignment no longer exists");
            }
            if(operation_name=="recover" && command.size()==2) {
                store_.update(revision,[&](Json& data){modify_row(data,id,[](Json& r){r["enabled"]=false;});});
                if(worker==workers_.end()) {
                    auto fresh=std::make_unique<OutputWorker>(store_,*row);fresh->start();
                    worker=workers_.emplace(id,std::move(fresh)).first;errors_.erase(id);
                }
                worker->second->recover();
            } else if(operation_name=="remove" && command.size()==2) {
                store_.update(revision,[&](Json& data){modify_row(data,id,[](Json& r){r["enabled"]=false;});});
                if(worker!=workers_.end()&&!worker->second->halt()) {
                    worker->second->start();
                    throw std::runtime_error("Output cleanup unresolved; reconcile before removal");
                }
                store_.update({},[&](Json& data){auto& rows=data["assignments"];for(auto it=rows.begin();it!=rows.end();++it)if(it->at("device_id")==id){rows.erase(it);break;}});
                workers_.erase(id);errors_.erase(id);
            } else if((operation_name=="enable"||operation_name=="pause") && command.size()==2) {
                if(operation_name=="enable"&&worker==workers_.end())throw std::runtime_error("Reconcile this unavailable output before resuming");
                if(operation_name=="enable"&&row->at("in_flight").get<bool>())throw std::runtime_error("Reconcile the previous job before resuming");
                store_.update(revision,[&](Json& data){modify_row(data,id,[&](Json& r){r["enabled"]=operation_name=="enable";});});
            } else if(operation_name=="schedule"&&command.size()==3) {
                if(row->at("enabled").get<bool>()||row->at("in_flight").get<bool>())throw std::runtime_error("Pause and reconcile before changing the schedule");
                validate_wtp_fleet_schedule(command.at("schedule"));
                store_.update(revision,[&](Json& data){modify_row(data,id,[&](Json& r){r["schedule"]=command.at("schedule");});});
            } else throw std::runtime_error("Invalid fleet operation");
        }
        return snapshot();
    }
private:
    void prune_retired() {
        for(auto it=workers_.begin();it!=workers_.end();) {
            if(it->second->retired())it=workers_.erase(it);
            else ++it;
        }
    }
    PicoHttpResponse snapshot(){
        auto [data,revision]=store_.snapshot();
        data["scope"]="wsprrypi-wtp-fleet/1";data["maximum_outputs"]=8;
        data["now_ms"]=std::to_string(WtpSteadyClock{}.now_ms());
        data["outputs"]=Json::object();
        for(const auto& [id,worker]:workers_)data["outputs"][id]=worker->status();
        for(const auto& [id,error]:errors_)data["outputs"][id]={{"state","unavailable"},{"message",error}};
        return {200,data.dump(),revision};
    }
    WtpFleetStore store_;
    std::string self_id_;
    std::mutex mutex_;
    std::map<std::string,std::unique_ptr<OutputWorker>> workers_;
    std::map<std::string,std::string> errors_;
};
std::mutex manager_mutex;
std::shared_ptr<Fleet> manager;
std::string startup_error;
}
void start_wtp_fleet(const std::string& ini_path, const std::string& self_id) {
    try {
        auto candidate=std::make_shared<Fleet>(ini_path+".wtp-assignments.json",
                                              self_id.empty()?wtp_pi_device_id():self_id);
        std::lock_guard lock(manager_mutex);manager=std::move(candidate);startup_error.clear();
    }catch(const std::exception& e){std::lock_guard lock(manager_mutex);startup_error=e.what();}
}
void stop_wtp_fleet() noexcept {
    std::shared_ptr<Fleet> old;{std::lock_guard lock(manager_mutex);old.swap(manager);}old.reset();
}
PicoHttpResponse wtp_fleet_api(const std::string& method,const std::string& body,const std::string& revision) {
    std::shared_ptr<Fleet> current;std::string error;
    {std::lock_guard lock(manager_mutex);current=manager;error=startup_error;}
    if(!current)return {503,Json{{"error",{{"code","fleet_unavailable"},{"message",error}}}}.dump(),{}};
    try{return current->request(method,body,revision);}
    catch(const std::exception& e){const std::string reason=e.what();return {reason=="revision_conflict"?412U:409U,
        Json{{"error",{{"code",reason=="revision_conflict"?"revision_conflict":"fleet_request_rejected"},{"message",reason}}}}.dump(),{}};}
}
}
