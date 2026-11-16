// SPDX-License-Identifier: MIT
#include "pi_management_service.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include "config_handler.hpp"
#include "config_handler_deserialization.hpp"
#include "solar_configuration.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include <openssl/rand.h>
#include <limits>
#include <charconv>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
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
    for(const auto *resource:{"pi-config","pi-scheduling","stop"}){
        auto capability=Json::parse(pi_resource_descriptor(resource));
        const bool config=std::string_view(resource)=="pi-config",stop=std::string_view(resource)=="stop";
        const bool cancellable=stop&&!state.authority.stop_reserved&&state.reserve_stop&&state.release_stop&&state.cancel_reserved_stop;
        capability.update({{"resource",resource},{"read",!stop},{"write",((config||std::string_view(resource)=="pi-scheduling")&&writable(state))||cancellable},
            {"activation",config?"next_occurrence":std::string_view(resource)=="pi-scheduling"?"stored_only":cancellable?"immediate":"unsupported"},{"maximum_bytes",stop&&!cancellable?0:32768}});
        if(!valid_pi_resource_capability(capability))throw std::logic_error("Pi descriptor mismatch");
        result.push_back(std::move(capability));
    }
    return result;
}
PicoHttpResponse PiLocalManagementService::handle(const BrowserRequest &request,bool authorized_local){
    const auto refuse=[](unsigned status,const char *code){return controller_target_error(status,code);};
    if(!authorized_local)return refuse(403,"unauthorized");
    if(request.body.size()>32768)return refuse(413,"payload_too_large");
    if(request.method!="GET"&&!request.mutation_context)return refuse(403,"csrf_failed");
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
    try{
        const auto saved=store_.settings();
        if(request.method=="GET"&&resource.starts_with("operations/")&&identity(resource.substr(11))){
            const auto receipt=store_.receipt(self_,"1",resource.substr(11));
            return receipt?document(*receipt,"target-operation"):refuse(404,"operation_unknown");
        }
        const auto state=observe();if(!state)return refuse(503,"authority_pending");
        const auto bound=binding(*state),caps=capabilities(*state);
        const auto capability_revision=PiManagementJournal::digest(canonical_pi_management_json(caps));
        const auto revision=saved["revision"].get<std::string>();
        const auto settings_document=[&](const Json &record){
            return Json{{"contract","Wsprry-Control/1"},{"schema","target-settings"},{"binding",bound},
                {"capability_revision",capability_revision},{"revision",record["revision"]},{"candidate_digest",record["candidate_digest"]},
                {"saved",{{"configuration",record["configuration"]},{"scheduling",record["scheduling"]}}},
                {"active_revision",nullptr},{"pending_application",state->authority.output_unknown||state->authority.stop_reserved?"held":"unknown"},
                {"observed_output",output(*state)}};
        };
        if(request.method=="GET"&&resource=="context")
            return document({{"contract","Wsprry-Control/1"},{"schema","target-context"},{"target",target()},
                {"binding",bound},{"capability_revision",capability_revision},{"capabilities",caps},{"configuration_revision",revision}},"target-context");
        if(request.method=="GET"&&resource=="settings")
            return document(settings_document(saved),"target-settings",revision);
        if(request.method=="GET"&&resource=="status")
            return document({{"contract","Wsprry-Control/1"},{"schema","target-status"},{"binding",bound},{"revision",revision},
                {"active_revision",nullptr},{"observed_output",output(*state)},{"observation_age_ms",0},
                {"schedule_enabled",saved["scheduling"]["enabled"]},{"solar_enabled",saved["scheduling"]["solar"]["enabled"]},
                {"transmit_effective",state->authority.effective_local_enable&&!state->authority.stop_reserved},{"execution_owner",self_},
                {"destination_id",state->destination_id?Json(*state->destination_id):Json(nullptr)},
                {"authority_state","unclassified"},{"phase","unknown"},{"waiting_reason","authority"},
                {"clock",{{"state","unknown"},{"uncertainty_ns",nullptr},{"holdover_age_ns",nullptr},{"generation","0"}}},
                {"selection","unknown"},{"selected_frequencies_hz",Json::array()},{"next_start_utc_ns",nullptr},
                {"forecast","unknown"},{"prolonged_window",nullptr}},"target-status",revision);
        const bool stopping=resource=="stop"&&request.method=="POST";
        if(!stopping&&(resource!="settings"||request.method!="PUT"))return refuse(404,"unsupported_resource");
        Json body;
        try{body=strict_browser_json(request.body,16);}catch(...){return refuse(400,"invalid_json");}
        if(!valid_controller_document(body,stopping?"target-stop":"target-save"))return refuse(400,"invalid_shape");
        if(body["binding"]["device_id"]!=self_)return refuse(409,"target_mismatch");
        if(body["binding"]["boot_id"]!=bound["boot_id"])return refuse(409,"boot_changed");
        if(body["binding"]["profile_generation"]!=bound["profile_generation"])return refuse(409,"profile_changed");
        if(body["binding"]["connection_generation"]!=bound["connection_generation"])return refuse(409,"connection_changed");
        if(request.revision.empty())return refuse(428,"revision_required");
        if(request.revision!="\""+body["expected_revision"].get<std::string>()+"\"")return refuse(412,"revision_conflict");
        const PiManagedStore::Operation operation{self_,"1",request.path,body};
        if(const auto replay=store_.replay(operation))return document(*replay,"target-operation",(*replay)["revision"]);
        // A successful Stop can itself change writable capabilities by ending
        // remote ownership. Exact historical replies grant no new mutation.
        if(body["capability_revision"]!=capability_revision)return refuse(409,"capability_changed");
        if(body["expected_revision"]!=revision)return refuse(412,"revision_conflict");
        if(stopping){
            if(!state->reserve_stop||!state->release_stop||!state->cancel_reserved_stop)return refuse(409,"unsupported_resource");
            if(body["observed_output"]!=output(*state))return refuse(409,"output_changed");
            if(!state->reserve_stop(state->output))return refuse(409,"output_changed");
            struct ReleaseStop {std::function<void()> release;~ReleaseStop(){release();}} reservation{state->release_stop};
            // Commit acceptance and the existing Transmit-off after-image
            // before cancellation. A lost reply can never dispatch Stop again
            // against a later occurrence, even if the process dies here.
            if(!persist_owned_transmit_disabled([&](const auto &internal,const auto &writer){
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
        if(candidate["scheduling"]["enabled"]!=saved["scheduling"]["enabled"]||candidate["scheduling"]["solar"]["enabled"]!=false)return refuse(409,"unsupported_resource");
        for(const auto *key:{"Web Port","Socket Port"})
            if(candidate["configuration"]["Operation"][key]!=saved["configuration"]["Operation"][key])return refuse(409,"read_only_field");
        for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})
            if(candidate["configuration"]["WTP"][key]!=saved["configuration"]["WTP"][key])return refuse(409,"read_only_field");
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
                    store_.persist(internal,persist,operation);
                });
        }catch(const ConfigValidationError &){return refuse(400,"candidate_invalid");}
        catch(const std::exception &error){
            if(std::string(error.what())=="revision_conflict")return refuse(412,"revision_conflict");
            // A committed operation can outlive runtime-application failure.
            if(const auto receipt=store_.replay(operation))return document(*receipt,"target-operation",(*receipt)["revision"]);
            return refuse(400,"candidate_invalid");
        }
        const auto receipt=store_.replay(operation);
        if(!receipt)return refuse(503,"recovery_required");
        return document(*receipt,"target-operation",(*receipt)["revision"]);
    }catch(const std::exception &error){
        if(std::string(error.what())=="request_id_reuse")return refuse(409,"request_id_reuse");
        return refuse(503,"storage_unavailable");
    }
}
} // namespace wsprrypi
