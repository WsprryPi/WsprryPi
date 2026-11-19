// SPDX-License-Identifier: MIT
#include "pi_management_service.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include "config_handler.hpp"
#include "config_handler_deserialization.hpp"
#include "solar_configuration.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include "native_recurrence.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_endpoint/identity.hpp"
#include "pi_pair_history.hpp"
#include "local_browser_principal.hpp"
#include "pi_pair_owner.hpp"
#include "recurrence_authority.hpp"
#include "pi_pair_contract.hpp"
#include "privileged_network_runtime.hpp"
#include <openssl/rand.h>
#include <limits>
#include <charconv>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
class PairedAdmissionHeld final:public std::runtime_error {public:PairedAdmissionHeld():std::runtime_error("paired_admission_held"){};};
bool identity(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string connection(){std::uint64_t value{};do{if(RAND_bytes(reinterpret_cast<unsigned char *>(&value),sizeof(value))!=1)throw std::runtime_error("management_entropy_unavailable");}while(!value);return std::to_string(value);}
PicoHttpResponse document(const Json &value,const std::string &family,const std::string &revision={}){
    if(!valid_controller_document(value,family))return controller_target_error(503,"invalid_shape");
    const auto bytes=value.dump();
    if(bytes.size()>32768)return controller_target_error(503,"payload_too_large");
    return {200,bytes,revision.empty()?std::string{}:"\""+revision+"\""};
}
bool writable(const PiManagementObservation &observation){
    const auto &state=observation.authority;
    return !state.remote.owner_id&&!state.remote.output_active&&!state.output_unknown&&!state.stop_reserved&&
        !state.revocation_unavailable&&!state.finishing_remote_job&&
        bool(observation.reserve_configuration)&&bool(observation.release_configuration);
}
bool local_migration_inventory(const Json &authority,const std::string &self){
    if(authority.contains("management")){
        if(!authority["management"]["complete"].get<bool>())return false;
        for(const auto &row:authority["management"]["authorities"])if(!row["assignments"].empty())return false;
    }
    return authority["controllers"].size()==1&&authority["controllers"][0]["controller_id"]==self;
}
std::optional<Json> remote_migration_plan(const Json &authority,const std::string &transition){
    try{auto plan=recurrence_retirement_plan(authority,transition);if(!plan["requests"].empty())return plan;}catch(...){}
    return {};
}
bool migration_idle(const PiManagementObservation &state){
    return writable(state)&&!state.authority.local_work_active&&!state.output.owner_id&&!state.output.job_id&&
        legacy_recurrence_transition_idle()&&!native_occurrence_active();
}
PicoHttpResponse operation_document(const Json &receipt){
    auto response=document(receipt,"target-operation",receipt["revision"].is_string()?receipt["revision"].get<std::string>():std::string{});
    if(response.status==200&&receipt["state"]=="pending")response.status=202;
    return response;
}
}
PiLocalManagementService::PolicyInstallation PiLocalManagementService::install_local_scheduling_policy(){
    std::lock_guard operation(mutex_);
    std::lock_guard configuration(config_update_mutex());
    const auto authority=store_.recurrence_authority();
    // Durable authority is the installation marker. Reinstall/restart must not
    // disable an enabled native schedule or undo explicit local restoration.
    if(authority["state"]=="staged"||authority["state"]=="native"||authority["state"]=="rollback")
        return PolicyInstallation::Current;
    if(authority["state"]!="legacy"||!local_migration_inventory(authority,self_)||!authority["jobs"].empty())
        return PolicyInstallation::Held;
    const auto saved=store_.settings();
    if(saved["configuration"]["Operation"]["Transmit Backend"]=="wtp")return PolicyInstallation::Held;
    const auto state=observe();
    if(!state||!migration_idle(*state)||!state->reserve_configuration())return PolicyInstallation::Held;
    struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
    const auto current=observe();
    if(!current||binding(*current)!=binding(*state)||!migration_idle(*current)||
        store_.recurrence_authority()!=authority||store_.settings()!=saved)return PolicyInstallation::Held;
    const auto route="/api/control/v1/targets/"+self_+"/migration";
    // This principal is the target's trusted installation owner, not a browser
    // or paired controller. The current closed contract and WAL still apply.
    const PiManagedStore::Operation intent{self_,"0",route,{
        {"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",binding(*current)},
        {"capability_revision",PiManagementJournal::digest(canonical_pi_management_json(capabilities(*current)))},
        {"operation_id",WtpPiBootIdentity{}.new_boot_id()},{"expected_revision",saved["revision"]},
        {"expected_authority_generation",authority["generation"]},{"controllers",authority["controllers"]}}};
    store_.persist_migration(jConfig,[]{},intent,false);
    const auto receipt=store_.replay(intent);
    const auto after=store_.recurrence_authority(),settings=store_.settings();
    if(!receipt||(*receipt)["state"]!="committed"||after["state"]!="staged"||
        settings["scheduling"]["enabled"]!=false||after["staged_revision"]!=settings["revision"])
        throw std::runtime_error("scheduling_policy_installation_unconfirmed");
    return PolicyInstallation::Completed;
}
PiLocalManagementService::PiLocalManagementService(PiManagedStore &store,std::string self,std::string name,Observe observe)
    :store_(store),self_(std::move(self)),name_(std::move(name)),connection_(connection()),observe_(std::move(observe)){
    if(!identity(self_)||self_!=store_.device_id()||!observe_||!valid_controller_document(Json{{"contract","Wsprry-Control/1"},{"schema","targets"},
        {"targets",Json::array({{{"device_id",self_},{"product","WsprryPi"},{"name",name_},{"profile_generation","1"},{"connection_generation",connection_}}})}},"targets"))
        throw std::invalid_argument("Valid Pi identity, name and native observation owner required");
    epoch_=store_.context_epoch();
}
Json PiLocalManagementService::target(){
    std::lock_guard lock(mutex_);
    refresh_epoch();
    return {{"device_id",self_},{"product","WsprryPi"},{"name",name_},{"profile_generation","1"},{"connection_generation",connection_}};
}
Json PiLocalManagementService::binding(const PiManagementObservation &state) const {
    return {{"device_id",self_},{"boot_id",state.authority.remote.boot_id},{"profile_generation","1"},{"connection_generation",connection_}};
}
Json PiLocalManagementService::output(const PiManagementObservation &state) const {
    return {{"generation",std::to_string(state.output.generation)},
        {"owner_id",state.output.owner_id?Json(*state.output.owner_id):Json(nullptr)},
        {"job_id",state.output.job_id?Json(*state.output.job_id):Json(nullptr)}};
}
std::optional<PiManagementObservation> PiLocalManagementService::observe(){
    refresh_epoch();
    auto result=observe_();
    if(!result||result->device_id!=self_||!identity(result->authority.remote.boot_id)||!result->output.generation)return {};
    if(result->destination_id&&!identity(*result->destination_id))return {};
    return result;
}
void PiLocalManagementService::refresh_epoch(){
    const auto epoch=store_.context_epoch();if(epoch==epoch_)return;
    std::uint64_t current{};
    const auto parsed=std::from_chars(connection_.data(),connection_.data()+connection_.size(),current);
    if(parsed.ec!=std::errc{}||parsed.ptr!=connection_.data()+connection_.size()||epoch<epoch_||
        std::numeric_limits<std::uint64_t>::max()-current<epoch-epoch_)
        throw std::runtime_error("management_context_capacity");
    connection_=std::to_string(current+epoch-epoch_);epoch_=epoch;
}
Json PiLocalManagementService::capabilities(const PiManagementObservation &state) const {
    Json result=Json::array();
    for(const auto *resource:{"pi-config","pi-scheduling","stop","migration"}){
        auto capability=Json::parse(pi_resource_descriptor(resource));
        const bool config=std::string_view(resource)=="pi-config",stop=std::string_view(resource)=="stop",migration=std::string_view(resource)=="migration";
        const bool cancellable=stop&&!state.authority.stop_reserved&&state.reserve_stop&&state.release_stop&&state.cancel_reserved_stop;
        const auto authority=store_.recurrence_authority();
        bool native_rollback=false;
        if(authority["state"]=="native"&&store_.settings()["scheduling"]["enabled"]==false&&!native_occurrence_active()){
            try{native_rollback=!store_.recurrence_progress()->held()&&store_.recurrence_progress()->snapshot()["in_flight"].is_null();}catch(...){}
        }
        const bool migratable=migration&&writable(state)&&(authority["jobs"].empty()||authority["state"]=="retiring")&&
            (local_migration_inventory(authority,self_)||remote_migration_plan(authority,std::string(32,'0')).has_value()||store_.remote_handover().has_value())&&
            (authority["state"]=="legacy"||authority["state"]=="rollback"||authority["state"]=="staged"||authority["state"]=="retiring"||native_rollback);
        const bool scheduling=std::string_view(resource)=="pi-scheduling";
        capability.update({{"resource",resource},{"read",!stop},{"write",(config&&writable(state))||scheduling||cancellable||migratable},
            {"activation",config||scheduling?"next_occurrence":cancellable||migratable?"immediate":"unsupported"},{"maximum_bytes",stop&&!cancellable?0:32768}});
        if(!valid_pi_resource_capability(capability))throw std::logic_error("Pi descriptor mismatch");
        result.push_back(std::move(capability));
    }
    return result;
}
PicoHttpResponse PiLocalManagementService::handle(const BrowserRequest &request,bool authorized_local){
    if(!authorized_local)return controller_target_error(403,"unauthorized");
    if(request.method!="GET"&&!request.mutation_context)return controller_target_error(403,"csrf_failed");
    return handle_authorized(request,{self_,"1",nullptr,nullptr,nullptr});
}
PicoHttpResponse PiLocalManagementService::handle(const BrowserRequest &request,const LocalBrowserPrincipal &principal,PiPairHistory &history){
    if(!principal.matches(request))return controller_target_error(403,"unauthorized");
    return handle_authorized(request,{self_,"1",&history,nullptr,nullptr,&principal});
}
PicoHttpResponse PiLocalManagementService::handle(const BrowserRequest &request,const PiPairPrincipal &principal,PiPairHistory &history,std::string *readback_revision){
    if(readback_revision)readback_revision->clear();
    if(request.body.size()>32768)return controller_target_error(413,"payload_too_large");
    std::optional<Json> metadata;
    {
        std::lock_guard admission(privileged_network_admission_mutex());const auto policy=privileged_network_runtime_state();
        if(!policy.active_known||policy.active!=PrivilegedNetworkMode::enforced||policy.configured!=PrivilegedNetworkMode::enforced)
            return controller_target_error(403,"unauthorized");
        metadata=history.management_authorization(principal);
    }
    if(!metadata)return controller_target_error(403,"unauthorized");
    if(request.path.starts_with("/api/pair/v1/"))return handle_pair(request,principal,history);
    return handle_authorized(request,{principal.identity().pair_id,principal.identity().authorization_generation,&history,&principal,*metadata},readback_revision);
}
PicoHttpResponse PiLocalManagementService::handle_pair(const BrowserRequest &request,const PiPairPrincipal &principal,PiPairHistory &history){
    const auto refuse=[&](unsigned status,const char *code){return PicoHttpResponse{status,Json{{"contract","Wsprry-Pair/1"},{"schema","result"},
        {"pair_id",principal.identity().pair_id},{"state","rejected"},{"code",code},{"retryable",false}}.dump(),{}};};
    std::unique_lock operation(mutex_,std::try_to_lock);if(!operation.owns_lock())return refuse(409,"busy");
    std::unique_lock configuration(config_update_mutex(),std::try_to_lock);if(!configuration.owns_lock())return refuse(409,"busy");
    try{
        if(!history.management_authorization(principal)||principal.self_id()!=self_)return refuse(403,"invalid_proof");
        constexpr std::string_view prefix="/api/pair/v1/operations/";
        if(request.method=="GET"&&request.path.starts_with(prefix)&&request.body.empty()){
            const auto id=request.path.substr(prefix.size());if(!identity(id))return refuse(400,"invalid_shape");
            const auto receipt=store_.authority_operation(id,principal);
            return receipt?PicoHttpResponse{200,receipt->dump(),{}}:refuse(404,"operation_unknown");
        }
        if(request.method=="GET"&&request.path=="/api/pair/v1/resume"&&request.body.empty()){
            const auto grant=store_.remote_resume(principal);
            return grant?PicoHttpResponse{200,grant->dump(),{}}:refuse(404,"operation_unknown");
        }
        if(request.method=="POST"&&request.path=="/api/pair/v1/resume/ack"){
            const auto body=parse_pi_pair_document(request.body,"resume-ack");
            const auto proof=history.resume_outcome_proof(principal,body);if(!proof)return refuse(403,"invalid_proof");
            auto admission=history.admit_resume_outcome(*proof);if(!admission)return refuse(409,"busy");
            return {200,store_.confirm_remote_resume(*proof,*admission).dump(),{}};
        }
        if(request.method!="POST"||request.path!="/api/pair/v1/authorities/register")return refuse(404,"invalid_shape");
        const auto body=parse_pi_pair_document(request.body,"authority-register");
        if(body["target_id"]!=self_||body["controller_id"]!=principal.peer_id()||body["pair_id"]!=principal.identity().pair_id||
            body["authorization_generation"]!=principal.identity().authorization_generation)return refuse(403,"invalid_proof");
        if(history.pair_operation(principal,body["operation_id"].get<std::string>()))return refuse(409,"request_id_reuse");
        // Final pairing revocation/renewal admission spans only this bounded
        // local WAL. No DNS, network wait or output action is inside the guard.
        auto admission=history.admit_mutation(principal);if(!admission)return refuse(409,"busy");
        const auto receipt=store_.register_authority(body,principal,*admission);
        return {200,receipt.dump(),{}};
    }catch(const std::invalid_argument &){return refuse(400,"invalid_shape");}
    catch(const std::exception &error){
        if(std::string(error.what())=="request_id_reuse")return refuse(409,"request_id_reuse");
        return refuse(503,"storage_unavailable");
    }
}
PicoHttpResponse PiLocalManagementService::handle_authorized(const BrowserRequest &request,const Authorization &authorization,std::string *readback_revision){
    const auto refuse=[](unsigned status,const char *code){return controller_target_error(status,code);};
    if(request.body.size()>32768)return refuse(413,"payload_too_large");
    if(request.method=="GET"&&!request.body.empty())return refuse(400,"invalid_shape");
    const auto prefix="/api/control/v1/targets/"+self_+"/";
    if(!request.path.starts_with(prefix))return refuse(404,"target_mismatch");
    const auto resource=request.path.substr(prefix.size());
    std::unique_lock operation_lock(mutex_,std::try_to_lock);
    if(!operation_lock.owns_lock())return refuse(409,"capacity");
    std::unique_lock tone(test_tone_lifecycle_mutex(),std::try_to_lock);
    if(!tone.owns_lock())return refuse(409,"capacity");
    std::unique_lock configuration(config_update_mutex(),std::try_to_lock);
    if(!configuration.owns_lock())return refuse(409,"capacity");
    const bool reviewing=request.method=="POST"&&resource.size()==50&&resource.starts_with("operations/")&&resource.ends_with("/review")&&identity(resource.substr(11,32));
    const auto prior_read=readback_;
    readback_.reset();
    const auto admit=[&](){
        auto guard=authorization.paired?authorization.history->admit_mutation(*authorization.paired):std::optional<PiPairMutationGuard>{};
        if(authorization.paired&&!guard)throw PairedAdmissionHeld();
        return guard;
    };
    try{
        if(authorization.paired){
            const auto scope=authorization.principal+"/"+authorization.generation;
            if(paired_context_!=scope){
                // Reuse the existing server-owned connection retirement domain.
                // A pre-pair/replaced-manager draft cannot acquire fresh TLS
                // authority merely by retaining the same settings revision.
                std::uint64_t current{};
                const auto parsed=std::from_chars(connection_.data(),connection_.data()+connection_.size(),current);
                if(parsed.ec!=std::errc{}||parsed.ptr!=connection_.data()+connection_.size()||current==std::numeric_limits<std::uint64_t>::max())
                    throw std::runtime_error("management_context_capacity");
                connection_=std::to_string(current+1);paired_context_=scope;
            }
        }
        const auto saved=store_.settings();
        if(request.method=="GET"&&resource.starts_with("operations/")&&identity(resource.substr(11))){
            const auto receipt=store_.receipt(authorization.principal,authorization.generation,resource.substr(11));
            return receipt?document(*receipt,"target-operation"):refuse(404,"operation_unknown");
        }
        const auto state=observe();if(!state)return refuse(503,"authority_pending");
        const auto bound=binding(*state),caps=capabilities(*state);
        const auto capability_revision=PiManagementJournal::digest(canonical_pi_management_json(caps));
        const auto revision=saved["revision"].get<std::string>();
        const auto readback=[&](PicoHttpResponse response){
            if(authorization.paired&&response.status==200){
                const auto content=PiManagementJournal::digest(Json{{"scope",authorization.principal+"/"+authorization.generation},
                    {"binding",bound},{"resource",resource},{"capability_revision",capability_revision},
                    {"etag",response.etag},{"document",strict_browser_json(response.body)}}.dump());
                if(read_sequence_==std::numeric_limits<std::uint64_t>::max())return refuse(503,"capacity");
                const auto token=PiManagementJournal::digest(Json{{"content",content},{"sequence",std::to_string(++read_sequence_)}}.dump());
                readback_=Readback{token,content,resource,authorization.principal+"/"+authorization.generation,bound,std::chrono::steady_clock::now()};
                if(readback_revision)*readback_revision=token;
            }
            return response;
        };
        if(reviewing){
            const auto authority=store_.recurrence_authority();
            if(authority.contains("management")&&authority["management"].contains("resume")&&authority["management"]["resume"]["state"]=="pending")return refuse(409,"authority_pending");
            if(!authorization.paired||!prior_read)return refuse(409,"recovery_required");
            Json body;try{body=strict_browser_json(request.body,16);}catch(...){return refuse(400,"invalid_json");}
            if(!valid_controller_document(body,"target-operation-review")||body["operation_id"]!=resource.substr(11,32))return refuse(400,"invalid_shape");
            if(body["binding"]!=bound||prior_read->binding!=bound||body["readback_revision"]!=prior_read->token||
                prior_read->scope!=authorization.principal+"/"+authorization.generation||
                std::chrono::steady_clock::now()-prior_read->at>std::chrono::seconds(30)||
                state->authority.output_unknown||state->authority.stop_reserved)return refuse(409,"recovery_required");
            if(const auto retained=store_.receipt(authorization.principal,authorization.generation,body["operation_id"].get<std::string>());
                retained&&((*retained)["state"]=="pending"||(*retained)["state"]=="unknown"))return refuse(409,"recovery_required");
            // Refresh the exact fixed resource under the same native operation,
            // configuration and output locks. A changed document cannot pass.
            BrowserRequest fresh{"GET",prefix+prior_read->resource,{},{}};
            const auto current_read=handle_authorized(fresh,authorization);
            if(current_read.status!=200||!readback_||readback_->content!=prior_read->content){readback_.reset();return refuse(409,"recovery_required");}
            auto admission=admit();
            std::uint64_t current{};const auto parsed=std::from_chars(connection_.data(),connection_.data()+connection_.size(),current);
            if(parsed.ec!=std::errc{}||parsed.ptr!=connection_.data()+connection_.size()||current==std::numeric_limits<std::uint64_t>::max())return refuse(503,"capacity");
            connection_=std::to_string(current+1);readback_.reset();
            return document({{"contract","Wsprry-Control/1"},{"schema","target-operation-reviewed"},{"binding",binding(*state)},
                {"operation_id",body["operation_id"]},{"readback_revision",prior_read->token}},"target-operation-reviewed");
        }
        const auto settings_document=[&](const Json &record){
            auto result=Json{{"contract","Wsprry-Control/1"},{"schema","target-settings"},{"binding",bound},
                {"capability_revision",capability_revision},{"revision",record["revision"]},{"candidate_digest",record["candidate_digest"]},
                {"saved",{{"configuration",record["configuration"]},{"scheduling",record["scheduling"]}}},
                {"active_revision",nullptr},{"pending_application",state->authority.output_unknown||state->authority.stop_reserved?"held":"unknown"},
                {"observed_output",output(*state)}};
            const auto observed=native_recurrence_observation(self_);
            if(store_.recurrence_authority()["state"]=="native"&&!observed.empty()){
                result["active_revision"]=observed["active_revision"];
                result["pending_application"]=state->authority.output_unknown||state->authority.stop_reserved||
                    observed["waiting_reason"]=="storage"||observed["waiting_reason"]=="recovery"?"held":
                    result["active_revision"]==record["revision"]?"none":"next_occurrence";
            }
            return result;
        };
        if(request.method=="GET"&&resource=="context")
            return document({{"contract","Wsprry-Control/1"},{"schema","target-context"},{"target",target()},
                {"binding",bound},{"capability_revision",capability_revision},{"capabilities",caps},{"configuration_revision",revision},
                {"authorization",authorization.metadata},{"gateway",nullptr}},"target-context");
        if(request.method=="GET"&&resource=="settings")
            return readback(document(settings_document(saved),"target-settings",revision));
        if(request.method=="GET"&&resource=="migration"){
            const auto authority=store_.recurrence_authority();Json wtp_binding=nullptr,controllers=authority["controllers"];
            if(!store_.remote_handover())if(const auto plan=remote_migration_plan(authority,std::string(32,'0')))controllers=(*plan)["controllers"];
            if(state->wtp_binding&&!state->authority.revocation_unavailable&&!state->authority.output_unknown){const auto &actual=*state->wtp_binding;
                if(actual.revocation_generation==state->authority.revocation_generation&&actual.wtp_port&&actual.wtp_port<=65535&&
                    actual.management_port&&actual.management_port<=65535)
                    wtp_binding={{"revocation_generation",std::to_string(actual.revocation_generation)},
                        {"wtp_port",actual.wtp_port},{"management_port",actual.management_port}};
            }
            return readback(document({{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",bound},
                {"authority_generation",authority["generation"]},{"transition_id",authority["transition_id"]},{"state",authority["state"]},
                {"controllers",controllers},{"retirements",authority["retirements"]},{"jobs",authority["jobs"]},
                {"staged_revision",authority["staged_revision"]},{"wtp_binding",wtp_binding}},"target-migration-read",revision));
        }
        if(request.method=="GET"&&resource=="status"){
            Json result{{"contract","Wsprry-Control/1"},{"schema","target-status"},{"binding",bound},{"revision",revision},
                {"active_revision",nullptr},{"observed_output",output(*state)},{"observation_age_ms",0},
                {"schedule_enabled",saved["scheduling"]["enabled"]},{"solar_enabled",saved["scheduling"]["solar"]["enabled"]},
                {"transmit_effective",state->authority.effective_local_enable&&!state->authority.stop_reserved},{"execution_owner",self_},
                {"destination_id",state->destination_id?Json(*state->destination_id):Json(nullptr)},
                {"authority_state",store_.recurrence_authority()["state"]},{"phase","unknown"},{"waiting_reason","authority"},
                {"clock",{{"state","unknown"},{"uncertainty_ns",nullptr},{"holdover_age_ns",nullptr},{"generation","0"}}},
                {"selection","unknown"},{"selected_frequencies_hz",Json::array()},{"next_start_utc_ns",nullptr},
                {"forecast","unknown"},{"prolonged_window",nullptr}};
            const auto observed=native_recurrence_observation(self_);
            if(store_.recurrence_authority()["state"]=="native"&&!observed.empty())result.update(observed);
            if(native_occurrence_active()&&!observed.empty()){
                if(saved["scheduling"]["enabled"]==false)result["phase"]="draining";
                else if(transmitter_state()==WsprTransmitState::TRANSMITTING)result["phase"]="running";
            }else if(saved["scheduling"]["enabled"]==false){result["phase"]="waiting";result["waiting_reason"]="disabled";}
            else if(!state->authority.effective_local_enable){result["phase"]="waiting";result["waiting_reason"]="transmit_off";}
            return readback(document(result,"target-status",revision));
        }
        const bool stopping=resource=="stop"&&request.method=="POST";
        const bool enabling=resource=="enable"&&request.method=="POST";
        const bool disabling=resource=="disable"&&request.method=="POST";
        const bool rollback=resource=="migration/rollback"&&request.method=="POST";
        const bool migration=rollback||(resource=="migration"&&request.method=="POST");
        if(!stopping&&!migration&&!enabling&&!disabling&&(resource!="settings"||request.method!="PUT"))return refuse(404,"unsupported_resource");
        Json body;
        try{body=strict_browser_json(request.body,16);}catch(...){return refuse(400,"invalid_json");}
        if(!valid_controller_document(body,migration?(rollback?"target-migration-rollback":"target-migration"):stopping?"target-stop":enabling?"target-enable":disabling?"target-action":"target-save"))return refuse(400,"invalid_shape");
        if(body["binding"]["device_id"]!=self_)return refuse(409,"target_mismatch");
        if(request.revision.empty())return refuse(428,"revision_required");
        if(request.revision!="\""+body["expected_revision"].get<std::string>()+"\"")return refuse(412,"revision_conflict");
        const PiManagedStore::Operation operation{authorization.principal,authorization.generation,request.path,body};
        // Exact retained replies grant no new mutation. The accepted write may
        // itself retire the connection context through bounded receipt eviction.
        // Absent/reused operations still require all current bindings below.
        if(const auto replay=store_.replay(operation))return operation_document(*replay);
        if(body["binding"]["boot_id"]!=bound["boot_id"])return refuse(409,"boot_changed");
        if(body["binding"]["profile_generation"]!=bound["profile_generation"])return refuse(409,"profile_changed");
        if(body["binding"]["connection_generation"]!=bound["connection_generation"])return refuse(409,"connection_changed");
        // A successful Stop can itself change writable capabilities by ending
        // remote ownership. Exact historical replies grant no new mutation.
        if(body["capability_revision"]!=capability_revision)return refuse(409,"capability_changed");
        if(body["expected_revision"]!=revision)return refuse(412,"revision_conflict");
        if(enabling||disabling){
            if(enabling){
                const auto authority=store_.recurrence_authority();
                if((authority["state"]!="staged"&&authority["state"]!="native")||!authority["jobs"].empty()||!writable(*state))return refuse(409,"authority_pending");
                if(body["candidate_digest"]!=saved["candidate_digest"])return refuse(412,"revision_conflict");
                try{(void)parse_solar_configuration(saved["scheduling"],config,nullptr,true);}
                catch(...){return refuse(400,"candidate_invalid");}
                if(!state->reserve_configuration())return refuse(409,"authority_pending");
                struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
                const auto current=observe();if(!current||binding(*current)!=bound||!writable(*current))return refuse(409,"authority_pending");
                const auto accepted=active_native_occurrence();
                if(authority["state"]=="native"){
                    const auto progress=store_.recurrence_progress()->snapshot();
                    if(!progress["in_flight"].is_null()&&(!accepted||!accepted->accepted||progress["in_flight"]["occurrence_id"]!=accepted->id))return refuse(409,"recovery_required");
                }
                auto admission=admit();store_.persist_scheduling(operation,true,accepted&&accepted->accepted?accepted->id:std::string{});
            }else {auto admission=admit();store_.persist_scheduling(operation,false);}
            const auto receipt=store_.replay(operation);if(!receipt)return refuse(503,"recovery_required");
            return operation_document(*receipt);
        }
        if(migration){
            // A device-owned replacement must execute on this device. Keep
            // explicit old-software rollback available, but never turn a
            // controller's remote output into a native recurrence authority.
            if(!rollback&&(config.transmit_backend==TransmitBackendKind::WTP||
                saved["configuration"]["Operation"]["Transmit Backend"]=="wtp"))
                return refuse(409,"unsupported_resource");
            const auto authority=store_.recurrence_authority();
            if(body["expected_authority_generation"]!=authority["generation"])return refuse(412,"revision_conflict");
            const bool draining=!rollback&&authority["state"]!="retiring"&&writable(*state)&&legacy_recurrence_has_accepted_work();
            if((!migration_idle(*state)&&!draining)||(!authority["jobs"].empty()&&authority["state"]!="retiring"))return refuse(409,"authority_pending");
            if(!rollback)if(const auto plan=remote_migration_plan(authority,body["operation_id"].get<std::string>())){
                if(authority["state"]!="legacy"&&authority["state"]!="rollback"&&authority["state"]!="staged")return refuse(409,"authority_pending");
                if(!authority["jobs"].empty()||body["controllers"]!=(*plan)["controllers"])return refuse(409,"authority_pending");
                // The local browser and paired editor use this target's actual
                // owner. Neither supplies keys, callback endpoints or proofs.
                const auto selected=authorization.history?std::shared_ptr<PiPairOwner>{}:pi_pair_owner();
                auto retained=selected?selected->history():std::shared_ptr<PiPairHistory>{};
                auto *history=authorization.history?authorization.history:retained.get();
                if(!history)return refuse(409,"authority_pending");
                Json signed_requests=Json::array();
                for(const auto &row:(*plan)["requests"]){
                    const auto retained=recurrence_reusable_retirement(authority,row["request"]);
                    if(!retained.is_null()){signed_requests.push_back(retained["request"]);continue;}
                    const auto signed_request=history->retirement_request(row["request"]);
                    if(!signed_request)return refuse(409,"authority_pending");
                    signed_requests.push_back(*signed_request);}
                if(!state->reserve_configuration())return refuse(409,"authority_pending");
                struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
                const auto current=observe();if(!current||binding(*current)!=bound||
                    (!migration_idle(*current)&&!(draining&&writable(*current)&&legacy_recurrence_has_accepted_work())))return refuse(409,"authority_pending");
                std::optional<Json> job;if(!migration_idle(*current))job=Json{{"controller_id",self_},
                    {"job_id",current->output.job_id.value_or(WtpPiBootIdentity{}.new_boot_id())},{"boot_id",bound["boot_id"]},{"state","accepted"}};
                auto admission=admit();store_.prepare_remote_migration(operation,authority,signed_requests,job);
                const auto receipt=store_.replay(operation);return receipt?operation_document(*receipt):refuse(503,"recovery_required");
            }
            if(rollback&&body["resume_controller_id"]!=self_){
                if(!authorization.history)return refuse(409,"authority_pending");
                const auto proof=authorization.paired?authorization.history->incoming_resume_target(request,*authorization.paired):
                    authorization.local?authorization.history->incoming_resume_target(request,*authorization.local):std::optional<PiPairResumeTargetProof>{};
                if(!proof||saved["scheduling"]["enabled"]!=false||saved["configuration"]["Operation"]["Transmit"]!=false)return refuse(409,"authority_pending");
                if(!state->reserve_configuration())return refuse(409,"authority_pending");
                struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
                const auto current=observe();
                if(!current||binding(*current)!=bound||!migration_idle(*current)||!current->wtp_binding||
                    current->authority.revocation_unavailable||current->authority.output_unknown)return refuse(409,"authority_pending");
                const auto &actual=*current->wtp_binding;
                if(actual.revocation_generation!=current->authority.revocation_generation||!actual.wtp_port||!actual.management_port)return refuse(409,"authority_pending");
                const Json actual_binding{{"revocation_generation",std::to_string(actual.revocation_generation)},
                    {"wtp_port",actual.wtp_port},{"management_port",actual.management_port}};
                auto admission=authorization.history->admit_resume_target(*proof);if(!admission)return refuse(409,"authority_pending");
                store_.prepare_remote_resume(operation,authority,*proof,actual_binding,*admission);
                const auto receipt=store_.replay(operation);return receipt?operation_document(*receipt):refuse(503,"recovery_required");
            }
            if(!local_migration_inventory(authority,self_))return refuse(409,"authority_pending");
            if(!rollback&&body["controllers"]!=authority["controllers"])return refuse(409,"authority_pending");
            if(rollback&&(body["resume_controller_id"]!=self_||body["resume_assignment_id"]!=authority["controllers"][0]["assignment_id"]))return refuse(409,"authority_pending");
            if(rollback&&authority["state"]=="native"&&(saved["scheduling"]["enabled"]!=false||native_occurrence_active()||
                store_.recurrence_progress()->held()||!store_.recurrence_progress()->snapshot()["in_flight"].is_null()))return refuse(409,"authority_pending");
            if((rollback&&authority["state"]!="staged"&&authority["state"]!="native")||(!rollback&&authority["state"]!="legacy"&&authority["state"]!="rollback"&&authority["state"]!="retiring"))return refuse(409,"authority_pending");
            if(!state->reserve_configuration())return refuse(409,"authority_pending");
            struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
            const auto current=observe();if(!current||binding(*current)!=bound||
                (!migration_idle(*current)&&!(draining&&writable(*current)&&legacy_recurrence_has_accepted_work())))return refuse(409,"authority_pending");
            if(!rollback){
                std::optional<Json> job;
                if(!migration_idle(*current))job=Json{{"controller_id",self_},{"job_id",current->output.job_id.value_or(WtpPiBootIdentity{}.new_boot_id())},
                    {"boot_id",bound["boot_id"]},{"state","accepted"}};
                auto admission=admit();store_.persist_migration(jConfig,[]{},operation,false,job);
                const auto receipt=store_.replay(operation);if(!receipt)return refuse(503,"recovery_required");
                return operation_document(*receipt);
            }
            const auto candidate=rollback?store_.rollback_configuration():saved["configuration"];
            const auto legacy=get_public_config_snapshot();configuration.unlock();tone.unlock();
            patch_config_with_persistence_coordinator(candidate,legacy.second,[&](const auto &internal,const auto &writer){
                if(!legacy_recurrence_transition_idle())throw std::runtime_error("authority_pending");
                auto admission=admit();
                store_.persist_migration(internal,writer,operation,rollback);
            });
            const auto receipt=store_.replay(operation);if(!receipt)return refuse(503,"recovery_required");
            return document(*receipt,"target-operation",(*receipt)["revision"]);
        }
        if(stopping){
            if(!state->reserve_stop||!state->release_stop||!state->cancel_reserved_stop)return refuse(409,"unsupported_resource");
            if(body["observed_output"]!=output(*state))return refuse(409,"output_changed");
            if(!state->reserve_stop(state->output))return refuse(409,"output_changed");
            struct ReleaseStop {std::function<void()> release;~ReleaseStop(){release();}} reservation{state->release_stop};
            // Commit acceptance and the existing Transmit-off after-image
            // before cancellation. A lost reply can never dispatch Stop again
            // against a later occurrence, even if the process dies here.
            if(!persist_owned_transmit_disabled([&](const auto &internal,const auto &writer){
                auto admission=admit();
                store_.persist_stop(internal,writer,operation);
            }))return refuse(503,"storage_unavailable");
            const auto receipt=store_.replay(operation);
            if(!receipt)return refuse(503,"recovery_required");
            configuration.unlock();
            try{state->cancel_reserved_stop();}catch(...){
                // The accepted request is durable. Its receipt does not assert
                // physical cleanup; status remains the output observation.
                // Preserve the native admission fence until process recovery
                // instead of allowing another occurrence after failed dispatch.
                reservation.release=[]{};
            }
            return document(*receipt,"target-operation",(*receipt)["revision"]);
        }
        if(!writable(*state))return refuse(409,"authority_pending");
        const auto &candidate=body["candidate"];
        if(candidate["scheduling"]["enabled"]!=saved["scheduling"]["enabled"])return refuse(409,"unsupported_resource");
        const auto authority=store_.recurrence_authority();
        if(candidate["scheduling"]["solar"]["enabled"]==true&&authority["state"]!="staged"&&authority["state"]!="native")return refuse(409,"authority_pending");
        for(const auto *key:{"Web Port","Socket Port"})
            if(candidate["configuration"]["Operation"][key]!=saved["configuration"]["Operation"][key])return refuse(409,"read_only_field");
        if(!state->reserve_configuration())return refuse(409,"confirmation_required");
        struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
        const auto current=observe();
        if(!current||binding(*current)!=bound)return refuse(409,"connection_changed");
        const auto legacy=get_public_config_snapshot();
        // The parent owns validation's lock/revision lifecycle, including its
        // deliberate unlock around slow backend validation. Keep the native
        // admission reservation, but do not retain duplicate recursive locks.
        configuration.unlock();tone.unlock();
        try{
            patch_config_with_persistence_coordinator(candidate["configuration"],legacy.second,
                [&](const auto &internal,const auto &persist){
                    ArgParserConfig policy_config=config;
                    config_handler_deserialization::deserialize_json_to_runtime_config(internal,policy_config);
                    if(config.simulated_backend_override)policy_config.transmit_backend=TransmitBackendKind::SIMULATED;
                    (void)parse_solar_configuration(candidate["scheduling"],policy_config,&saved["scheduling"],true);
                    auto prospective=saved;
                    prospective["configuration"]=config_handler_serialization::saved_configuration_from_internal_json(internal);
                    prospective["scheduling"]=candidate["scheduling"];
                    // A valid bounded request must not commit a candidate whose
                    // complete negotiated readback cannot fit the response cap.
                    auto readback=settings_document(prospective);
                    readback["observed_output"]={{"generation","18446744073709551615"},
                        {"owner_id",std::string(32,'f')},{"job_id",std::string(32,'f')}};
                    readback["binding"]["connection_generation"]="18446744073709551615";
                    readback["pending_application"]="unknown";
                    if(readback.dump().size()>32768)
                        throw std::runtime_error("candidate_exceeds_settings_capacity");
                    auto admission=admit();store_.persist(internal,persist,operation);
                });
        }catch(const PairedAdmissionHeld &){return refuse(409,"authority_pending");}
        catch(const ConfigValidationError &){return refuse(400,"candidate_invalid");}
        catch(const std::exception &error){
            if(std::string(error.what())=="revision_conflict")return refuse(412,"revision_conflict");
            // A committed operation can outlive runtime-application failure.
            if(const auto receipt=store_.replay(operation))return document(*receipt,"target-operation",(*receipt)["revision"]);
            return refuse(400,"candidate_invalid");
        }
        const auto receipt=store_.replay(operation);
        if(!receipt)return refuse(503,"recovery_required");
        return document(*receipt,"target-operation",(*receipt)["revision"]);
    }catch(const PairedAdmissionHeld &){return refuse(409,"authority_pending");}
    catch(const std::exception &error){
        if(std::string(error.what())=="request_id_reuse")return refuse(409,"request_id_reuse");
        return refuse(503,"storage_unavailable");
    }
}
void PiLocalManagementService::advance_remote_handover(PiPairRetirementClient &client,const std::function<bool()> &cancelled) noexcept {
    // Called only by the dedicated pairing network supervisor, never the RF
    // scheduler. No request/configuration/store lock spans peer I/O.
    std::unique_lock worker(remote_worker_,std::try_to_lock);if(!worker.owns_lock()||!cancelled||cancelled())return;
    try{
        const auto saved=store_.remote_handover();if(!saved)return;
        const auto &requests=(*saved)["requests"];if(requests.empty()||requests.size()>32)return;
        if(remote_transition_!=(*saved)["transition_id"].get<std::string>()){remote_transition_=(*saved)["transition_id"].get<std::string>();remote_cursor_=0;remote_backoff_.assign(requests.size(),{});}
        if(remote_backoff_.size()!=requests.size())return;
        const auto now=std::chrono::steady_clock::now();
        for(std::size_t visited=0;visited<requests.size();++visited){
            const auto index=remote_cursor_++%requests.size();const auto &row=requests[index];auto &backoff=remote_backoff_[index];
            if(row["state"]=="confirmed"||now<backoff.next)continue;
            if(cancelled())return;
            bool handed=false;
            const PiPairEndpoint endpoint{row["endpoint"]["host"].get<std::string>(),row["endpoint"]["port"].get<unsigned>()};
            const auto result=client.exchange(endpoint,row["request"],row["state"]=="prepared"?PiPairRetirementClient::Mode::Request:PiPairRetirementClient::Mode::Status,
                [&](const auto &guard){if(cancelled())return false;if(handed)return true;handed=store_.handover_handoff(row["request"],guard);return handed;},cancelled);
            bool confirmed=false;
            if(!cancelled()&&result.proof){
                std::unique_lock configuration(config_update_mutex(),std::try_to_lock);
                if(configuration.owns_lock())confirmed=store_.confirm_handover(*result.proof);
            }
            backoff.failures=confirmed?0:std::min(backoff.failures+1,6U);
            backoff.next=std::chrono::steady_clock::now()+std::chrono::seconds(confirmed?1U:1U<<(backoff.failures-1));
            return; // At most one bounded exchange per fair supervisor turn.
        }
    }catch(...){/* Storage and absent/unverified outcomes retain the target hold. */}
}
void PiLocalManagementService::advance_migration() noexcept {
    try{
        if(!store_.migration_pending())return;
        std::unique_lock operation(mutex_,std::try_to_lock);if(!operation.owns_lock())return;
        std::unique_lock tone(test_tone_lifecycle_mutex(),std::try_to_lock);if(!tone.owns_lock())return;
        std::unique_lock configuration(config_update_mutex(),std::try_to_lock);if(!configuration.owns_lock())return;
        const auto authority=store_.recurrence_authority();if(authority["state"]!="retiring")return;
        const auto state=observe();if(!state||!migration_idle(*state))return;
        // A reboot cannot manufacture the outcome of an earlier accepted job.
        // It needs explicit, current-bound reconciliation through POST instead.
        for(const auto &job:authority["jobs"])if(job["boot_id"]!=binding(*state)["boot_id"])return;
        if(!state->reserve_configuration())return;
        struct Reservation {std::function<void()> release;~Reservation(){release();}} reservation{state->release_configuration};
        const auto current=observe();if(!current||binding(*current)!=binding(*state)||!migration_idle(*current))return;
        store_.complete_migration();
    }catch(...){}
}
} // namespace wsprrypi
