#include "wtp_retirement.hpp"
// SPDX-License-Identifier: MIT
#include "pi_recovery_service.hpp"
#include "pi_configuration_recovery.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr const char *recovery_path="/api/control/v1/host/configuration-recovery";
const char *state_name(PiHostReadiness state){
    switch(state){
    case PiHostReadiness::Starting:return "starting";
    case PiHostReadiness::Ready:return "ready";
    case PiHostReadiness::ConfigurationRecovery:return "configuration_recovery";
    case PiHostReadiness::StorageRecovery:return "storage_recovery";
    case PiHostReadiness::OutputRecovery:return "output_recovery";
    }
    throw std::runtime_error("invalid_host_readiness");
}
bool identity(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
PicoHttpResponse document(const Json &value,const char *family,const std::string &revision={}){
    if(!valid_controller_document(value,family))return controller_target_error(503,"invalid_shape");
    const auto bytes=value.dump();if(bytes.size()>32768)return controller_target_error(503,"payload_too_large");
    return {200,bytes,revision.empty()?std::string{}:"\""+revision+"\""};
}
}
PiRecoveryService::PiRecoveryService(PiManagedStartup &owner,std::string self,std::string boot,Observe observe)
    :owner_(owner),self_(std::move(self)),boot_(std::move(boot)),observe_(std::move(observe)){
    if(!identity(self_)||self_!=owner_.device_id()||!identity(boot_)||!observe_)throw std::invalid_argument("Recovery requires verified host and process identities");
    capability_revision_=PiManagementJournal::digest(canonical_pi_management_json(Json{
        {"adapter",Json::parse(controller_adapter_contract())},{"definitions",Json::parse(controller_document_definitions())}}));
}
Json PiRecoveryService::binding() const {
    return {{"device_id",self_},{"boot_id",boot_},{"profile_generation","1"},
        {"connection_generation",std::to_string(owner_.store().context_epoch())}};
}
PicoHttpResponse PiRecoveryService::handle(const BrowserRequest &request,bool authorized){
    const auto refuse=[](unsigned status,const char *code){return controller_target_error(status,code);};
    if(!authorized)return refuse(403,"unauthorized");
    if(request.body.size()>32768)return refuse(413,"payload_too_large");
    if(request.method!="GET"&&!request.mutation_context)return refuse(403,"csrf_failed");
    if(request.method=="GET"&&!request.body.empty())return refuse(400,"invalid_shape");
    if(request.path!=recovery_path&&request.path!="/api/control/v1/host/readiness")return refuse(404,"unsupported_resource");
    std::unique_lock operation(mutex_,std::try_to_lock);
    if(!operation.owns_lock())return refuse(409,"capacity");
    std::unique_lock tone(test_tone_lifecycle_mutex(),std::try_to_lock);
    if(!tone.owns_lock())return refuse(409,"capacity");
    std::unique_lock configuration(config_update_mutex(),std::try_to_lock);
    if(!configuration.owns_lock())return refuse(409,"capacity");
    bool validating_candidate=false;
    try{
        auto state=observe_();bool healthy=true;std::string revision;
        try{revision=owner_.store().recovery_revision();}catch(...){healthy=false;state=PiHostReadiness::StorageRecovery;}
        if(state==PiHostReadiness::Ready&&!owner_.admission_ready())state=PiHostReadiness::Starting;
        const auto saved=owner_.draft();
        const bool installing=healthy&&owner_.store().installation_pending();
        const bool legacy_active=saved&&wtp_retirement_source_active(saved->source);
        const bool editable=!legacy_active&&healthy&&(saved.has_value()||installing)&&state==PiHostReadiness::ConfigurationRecovery&&!owner_.configuration_validated();
        if(request.path=="/api/control/v1/host/readiness"){
            if(request.method!="GET")return refuse(405,"unsupported_resource");
            return document({{"contract","Wsprry-Control/1"},{"schema","host-readiness"},{"device_id",self_},
                {"boot_id",boot_},{"role","transmitter"},{"state",state_name(state)},{"recovery_editable",editable}},"host-readiness");
        }
        if(request.method=="GET"){
            if(!healthy)revision=PiManagementJournal::digest("storage-recovery:"+self_+":"+boot_);
            // Initial publication may retire the parser's older file stamp.
            // A verified complete managed record still supplies known saved
            // values for a read-only output/startup hold, never safe defaults.
            Json known=installing?initial_configuration_draft():saved?saved->source:Json::object();
            if(installing){
                known["Operation"]["Web Port"]=config.web_port;
                known["Operation"]["Socket Port"]=config.socket_port;
            }
            if(!saved&&healthy&&owner_.configuration_validated()&&owner_.store().initialized())
                known=owner_.store().settings()["configuration"];
            Json defaults=Json::array();
            static const auto definitions=Json::parse(controller_document_definitions());
            for(const auto &[section,group]:definitions["pi-config"]["properties"].items())
                for(const auto &[key,rule]:group["properties"].items()){
                    (void)rule;
                    if(!known.contains(section)||!known[section].contains(key))defaults.push_back(section+"."+key);
                }
            Json result{{"contract","Wsprry-Control/1"},{"schema","configuration-recovery"},{"binding",healthy?binding():Json(nullptr)},
                {"capability_revision",capability_revision_},{"revision",revision},{"writable",editable},{"draft",Json::object()},
                {"defaulted_fields",defaults},{"validation",healthy?(legacy_active?"migration_required":installing?"initialization":owner_.configuration_validated()?"unvalidated":"invalid"):"storage_unavailable"}};
            const auto budget=32768-result.dump().size();
            result["draft"]=saved&&!installing?pi_configuration_recovery_draft(*saved,budget):pi_configuration_recovery_draft(known,budget);
            return document(result,"configuration-recovery",revision);
        }
        if(legacy_active)return refuse(409,"migration_required");
        const bool save=request.method=="PUT",action=request.method=="POST";
        if(!save&&!action)return refuse(405,"unsupported_resource");
        Json body;try{body=strict_browser_json(request.body,16);}catch(...){return refuse(400,"invalid_json");}
        if(!valid_controller_document(body,save?"configuration-recovery-save":"configuration-recovery-action"))return refuse(400,"invalid_shape");
        const auto bound=binding();
        for(const auto &[field,code]:std::initializer_list<std::pair<const char *,const char *>>{
                {"device_id","target_mismatch"},{"boot_id","boot_changed"},{"profile_generation","profile_changed"},{"connection_generation","connection_changed"}})
            if(body["binding"][field]!=bound[field])return refuse(409,code);
        if(body["capability_revision"]!=capability_revision_)return refuse(409,"capability_changed");
        if(request.revision.empty())return refuse(428,"revision_required");
        if(request.revision!="\""+body["expected_revision"].get<std::string>()+"\"")return refuse(412,"revision_conflict");
        const PiManagedStore::Operation mutation{self_,"1",request.path,body};
        if(healthy&&owner_.store().initialized())if(const auto receipt=owner_.store().replay(mutation))return document(*receipt,"target-operation",(*receipt)["revision"]);
        if(!editable)return refuse(503,"authority_pending");
        if(body["expected_revision"]!=revision)return refuse(412,"revision_conflict");
        if(installing&&(action||body["configuration"]["Operation"]["Transmit"]!=false||
            body["configuration"]["Operation"]["Enable on Boot"]!="Never"))return refuse(400,"candidate_invalid");
        const auto receipt=owner_.execute_recovery(mutation,[&]{
            validating_candidate=true;
            if(action){persist_stock_configuration(body["action"]=="repair");return;}
            auto candidate=body["configuration"];
            persist_recovery_configuration(candidate,saved?saved->source:Json::object());
        });
        return document(receipt,"target-operation",receipt["revision"]);
    }catch(const std::exception &error){
        if(std::string(error.what())=="revision_conflict")return refuse(412,"revision_conflict");
        if(std::string(error.what())=="request_id_reuse")return refuse(409,"request_id_reuse");
        try{(void)owner_.store().recovery_revision();}catch(...){return refuse(503,"authority_pending");}
        if(!validating_candidate)return refuse(503,"authority_pending");
        return refuse(400,"candidate_invalid");
    }
}
} // namespace wsprrypi
