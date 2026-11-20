#include "application_recovery/restore_hold.hpp"
// SPDX-License-Identifier: MIT
#include "pi_managed_host.hpp"
#include "application_storage/application_state.hpp"
#include "config_handler.hpp"
#include "wtp_integration/pi_pair_history.hpp"
#include "scheduling.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "logging.hpp"
namespace wsprrypi {
PiManagedHost::PiManagedHost(std::string ini,std::string self,std::string boot,
    PiLocalManagementService::Observe observe,std::function<void(PiPairInitializationProof)> pairing)
    :owner_(ini,self,std::move(pairing)),recovery_(owner_,self,std::move(boot),[this]{return state();}),
     management_(owner_.store(),self,"This device",std::move(observe)),ini_(std::move(ini)){}
PiManagedHost::PiManagedHost(std::shared_ptr<SqliteStateStore> database,std::string boot,
    PiLocalManagementService::Observe observe,std::function<void(PiPairInitializationProof)> pairing)
    :owner_(database,std::move(pairing)),recovery_(owner_,database->identity(),std::move(boot),[this]{return state();}),
     management_(owner_.store(),database->identity(),"This device",std::move(observe)),
     ini_(application_configuration_namespace()){}
void PiManagedHost::prepare(){
    if(prepared_)throw std::logic_error("host_already_prepared");
    prepared_=true;
    try{owner_.prepare();state_.store(PiHostReadiness::Starting);}catch(...){state_.store(PiHostReadiness::StorageRecovery);}
}
void PiManagedHost::parsed(){
    std::lock_guard lock(config_update_mutex());
    if(!prepared_||parsed_)throw std::logic_error("host_parse_lifecycle");
    parsed_=true;
    if(state()==PiHostReadiness::StorageRecovery)return;
    last_attempt_=owner_.store().recovery_revision();
    state_.store(owner_.configuration_validated()?PiHostReadiness::Starting:PiHostReadiness::ConfigurationRecovery);
}
PiHostReadiness PiManagedHost::state(){
    std::lock_guard lock(config_update_mutex());
    auto current=state_.load();
    if(current==PiHostReadiness::StorageRecovery)return current;
    try{(void)owner_.store().recovery_revision();}
    catch(...){state_.store(PiHostReadiness::StorageRecovery);return PiHostReadiness::StorageRecovery;}
    return current;
}
void PiManagedHost::signal_saved_configuration() noexcept {
    configuration_signal_.store(true,std::memory_order_release);exitwspr_cv.notify_all();
}
bool PiManagedHost::consume_configuration_signal() noexcept {
    return configuration_signal_.exchange(false,std::memory_order_acq_rel);
}
void PiManagedHost::restore_recovery_listener_ports(){
    std::lock_guard lock(config_update_mutex());
    if(owner_.configuration_validated()||state()==PiHostReadiness::StorageRecovery)return;
    const auto draft=owner_.draft();if(!draft)return;
    const auto section=draft->source.find("Operation");
    if(section==draft->source.end()||!section->is_object())return;
    const auto port=[&](const char *key,int fallback){
        const auto value=section->find(key);
        if(value==section->end()||!value->is_number_integer()||*value<1024||*value>49151)return fallback;
        return value->get<int>();
    };
    const auto web=port("Web Port",config.web_port),socket=port("Socket Port",config.socket_port);
    if(web==socket)return;
    config.web_port=web;config.socket_port=static_cast<uint16_t>(socket);
}
bool PiManagedHost::retry_saved_configuration(){
    try{
    {
        std::unique_lock tone(test_tone_lifecycle_mutex(),std::try_to_lock);
        // A consumed notification still owes owning-parser readback when a
        // concurrent request holds a gate. Retain it for the recovery loop's
        // next bounded poll; no input was inspected or adopted in this case.
        if(!tone.owns_lock()){signal_saved_configuration();return false;}
        std::unique_lock lock(config_update_mutex(),std::try_to_lock);
        if(!lock.owns_lock()){signal_saved_configuration();return false;}
        if(state()!=PiHostReadiness::ConfigurationRecovery||
            (!application_database_selected()&&iniFile.hasPendingChanges()))return false;
        const auto revision=owner_.store().recovery_revision();
        if(revision==last_attempt_)return false;
        last_attempt_=revision;
    }
    // Let the owning parser release its validation locks normally. It checks
    // both file and runtime revisions again before publishing a saved readback.
        std::string error;
        if(!load_json(ini_,&error,nullptr))return false;
        state_.store(PiHostReadiness::Starting);return true;
    }catch(...){(void)state();return false;}
}
void PiManagedHost::activate(PiManagedStartup::ConfigurationGate gate){
    if(!ready_for_native_startup())throw std::runtime_error("host_startup_not_ready");
    owner_.activate(std::move(gate));
    const auto policy=management_.install_local_scheduling_policy();
    if(policy==PiLocalManagementService::PolicyInstallation::Completed)
        llog.logS(INFO,"Migration completed; schedule disabled pending review.");
    else if(policy==PiLocalManagementService::PolicyInstallation::Held)
        llog.logS(WARN,"Migration required; scheduling held pending ownership or output recovery.");
    state_.store(PiHostReadiness::Ready);
}
bool PiManagedHost::ready_for_native_startup(){
    std::lock_guard lock(config_update_mutex());
    return parsed_&&owner_.configuration_validated()&&state()==PiHostReadiness::Starting;
}
void PiManagedHost::hold_output(){
    if(!parsed_||!owner_.configuration_validated()||state()!=PiHostReadiness::Starting)throw std::runtime_error("host_startup_not_ready");
    state_.store(PiHostReadiness::OutputRecovery);
}
PicoHttpResponse PiManagedHost::recovery(const BrowserRequest &request,bool authorized){
    auto response=recovery_.handle(request,authorized);
    if(request.method!="GET"&&response.status>=200&&response.status<300&&state()==PiHostReadiness::ConfigurationRecovery)
        signal_saved_configuration();
    return response;
}
PicoHttpResponse PiManagedHost::management(const BrowserRequest &request,bool authorized){
    if(!authorized)return controller_target_error(403,"unauthorized");
    const auto receipts="/api/control/v1/targets/"+device_id()+"/operations/";
    if(state()!=PiHostReadiness::Ready&&!(request.method=="GET"&&request.path.starts_with(receipts)))
        return controller_target_error(503,"authority_pending");
    return management_.handle(request,authorized);
}
nlohmann::json PiManagedHost::target(){return management_.target();}
PicoHttpResponse PiManagedHost::management(const BrowserRequest &request,const PiPairPrincipal &principal,PiPairHistory &history,std::string *readback_revision){
    if(readback_revision)readback_revision->clear();
    if(request.method!="GET"&&restored_remote_authority_held())return controller_target_error(503,"recovery_required");
    const auto receipts="/api/control/v1/targets/"+device_id()+"/operations/";
    if(state()!=PiHostReadiness::Ready&&!(request.method=="GET"&&request.path.starts_with(receipts)))
        return controller_target_error(503,"authority_pending");
    return management_.handle(request,principal,history,readback_revision);
}
PicoHttpResponse PiManagedHost::management(const BrowserRequest &request,const LocalBrowserPrincipal &principal,PiPairHistory &history){
    if(state()!=PiHostReadiness::Ready)return controller_target_error(503,"authority_pending");
    return management_.handle(request,principal,history);
}
void PiManagedHost::reclaim_pairing_retirements(PiPairHistory &history) noexcept{try{(void)history.reclaim_native_retired(owner_.store());}catch(...){}}
void PiManagedHost::advance_migration() noexcept {if(state_.load()==PiHostReadiness::Ready)management_.advance_migration();}
void PiManagedHost::advance_remote_handover(PiPairRetirementClient &client,const std::function<bool()> &cancelled) noexcept {
    if(state_.load()==PiHostReadiness::Ready)management_.advance_remote_handover(client,cancelled);
}
WtpCatalog &PiManagedHost::catalog(){return owner_.catalog();}
const std::string &PiManagedHost::device_id() const noexcept{return owner_.device_id();}
} // namespace wsprrypi
