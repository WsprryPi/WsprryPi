// SPDX-License-Identifier: MIT
#include "pi_managed_host_runtime.hpp"
#include "config_handler.hpp"
#include "wtp_pi_control.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/control_contract.hpp"
#include "wtp_integration/pi_configuration_recovery.hpp"
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
struct Slot {std::mutex mutex;bool selected{};std::weak_ptr<PiManagedHost> host;std::string self,boot;};
Slot &slot(){static Slot value;return value;}
void refuse(){throw std::runtime_error("managed_host_storage_hold");}
PicoHttpResponse closed(const Json &value,const char *family){
    if(!valid_controller_document(value,family)||value.dump().size()>32768)return controller_target_error(503,"invalid_shape");
    return {200,value.dump(),{}};
}
}
std::optional<std::string> pi_managed_ini_argument(int argc,char *argv[]){
    for(int i=1;i+1<argc;++i)if(std::string_view(argv[i])=="-i"||std::string_view(argv[i])=="--ini-file")return std::string(argv[i+1]);
    return {};
}
std::shared_ptr<PiManagedHost> prepare_pi_managed_host(const std::string &ini){
    auto &registry=slot();std::lock_guard lock(registry.mutex);
    if(registry.selected)throw std::logic_error("managed_host_already_selected");
    registry.selected=true;
    // Even constructor/identity failure cannot reopen a legacy uncoordinated
    // writer or allow safe defaults to become a physical startup configuration.
    set_wtp_pi_management_readiness([]{return false;});
    iniFile.set_file_reader([](const std::string &)->std::string{refuse();return {};});
    iniFile.set_file_writer([](const std::string &,const std::string &){refuse();});
    set_config_persistence_owner([](const auto &,const auto &){refuse();},[](const auto &){refuse();},
        [](const auto &,const auto &){refuse();},{},[](const std::string &){refuse();});
    WtpPiBootIdentity identity;registry.boot=identity.new_boot_id();
    try{registry.self=wtp_pi_device_id();}catch(...){return {};}
    try{
        auto host=std::make_shared<PiManagedHost>(ini,registry.self,registry.boot,wtp_pi_management_observation);
        host->prepare();registry.host=host;return host;
    }catch(...){return {};}
}
std::shared_ptr<PiManagedHost> pi_managed_host(){auto &registry=slot();std::lock_guard lock(registry.mutex);return registry.host.lock();}
bool pi_managed_host_selected(){auto &registry=slot();std::lock_guard lock(registry.mutex);return registry.selected;}
bool pi_managed_host_protected_recovery_path(std::string_view path){
    return pi_managed_host_selected()&&(path=="/api/control/v1/host/readiness"||
        path=="/api/control/v1/host/configuration-recovery"||path=="/api/control/v1/host/management-session");
}
PiHostReadiness pi_managed_host_state(){const auto host=pi_managed_host();return host?host->state():PiHostReadiness::StorageRecovery;}
bool pi_managed_host_held(){return pi_managed_host_selected()&&pi_managed_host_state()!=PiHostReadiness::Ready;}
bool pi_managed_host_http_allowed(std::string_view method,std::string_view path){
    if(!pi_managed_host_held())return true;
    if(method=="GET"&&(path=="/"||path=="/version"||path=="/config"||
        path=="/api/control/v1/host/readiness"||path=="/api/control/v1/host/configuration-recovery"))return true;
    if(path=="/api/control/v1/host/management-session")return method=="POST";
    if(path=="/api/control/v1/host/configuration-recovery")return method=="PUT"||method=="POST";
    return false;
}
bool pi_managed_host_target_path(const std::string &path){
    auto &registry=slot();std::lock_guard lock(registry.mutex);
    return registry.selected&&!registry.self.empty()&&path.starts_with("/api/control/v1/targets/"+registry.self+"/");
}
PicoHttpResponse pi_managed_host_recovery(const BrowserRequest &request,bool authorized){
    if(!authorized)return controller_target_error(403,"unauthorized");
    if(const auto host=pi_managed_host())return host->recovery(request,true);
    std::string self,boot;
    {auto &registry=slot();std::lock_guard lock(registry.mutex);
        if(!registry.selected)return controller_target_error(404,"unsupported_resource");
        self=registry.self;boot=registry.boot;}
    if(request.body.size()>32768)return controller_target_error(413,"payload_too_large");
    if(request.method!="GET")return controller_target_error(503,"storage_unavailable");
    if(!request.body.empty())return controller_target_error(400,"invalid_shape");
    if(request.path=="/api/control/v1/host/readiness")return closed({{"contract","Wsprry-Control/1"},{"schema","host-readiness"},
        {"device_id",self.empty()?Json(nullptr):Json(self)},{"boot_id",boot},{"role","transmitter"},{"state","storage_recovery"},{"recovery_editable",false}},"host-readiness");
    if(request.path!="/api/control/v1/host/configuration-recovery")return controller_target_error(404,"unsupported_resource");
    const auto definitions=Json::parse(controller_document_definitions());Json defaults=Json::array();
    for(const auto &[section,group]:definitions["pi-config"]["properties"].items())
        for(const auto &[key,rule]:group["properties"].items()){(void)rule;defaults.push_back(section+"."+key);}
    const auto capability=PiManagementJournal::digest(canonical_pi_management_json(Json{
        {"adapter",Json::parse(controller_adapter_contract())},{"definitions",definitions}}));
    return closed({{"contract","Wsprry-Control/1"},{"schema","configuration-recovery"},{"binding",nullptr},{"capability_revision",capability},
        {"revision",PiManagementJournal::digest("unavailable:"+self+":"+boot)},{"writable",false},{"draft",pi_configuration_recovery_draft(Json::object())},
        {"defaulted_fields",defaults},{"validation","storage_unavailable"}},"configuration-recovery");
}
} // namespace wsprrypi
