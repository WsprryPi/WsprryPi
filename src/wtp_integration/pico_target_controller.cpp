// SPDX-License-Identifier: MIT
#include "pico_target_controller.hpp"
#include "application_recovery/restore_hold.hpp"
#include "pi_pair_history.hpp"
#include "wtp_settings_json.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <array>
#include <algorithm>
#include <charconv>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
bool identity(const std::string &s){return s.size()==32&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string generation() {
    std::uint64_t value{};do {if(RAND_bytes(reinterpret_cast<unsigned char *>(&value),sizeof(value))!=1)throw std::runtime_error("entropy_unavailable");}while(!value);
    return std::to_string(value);
}
std::string fingerprint(const WtpCatalogProfile &p) {
    return PicoManagementJournal::digest(Json{{"id",p.id},{"name",p.name},{"method",p.method},{"discovery",p.discovery_id},
        {"settings",wtp_settings_json(p.settings)},{"legacy_unverified",p.legacy_unverified},{"generation",p.generation}}.dump());
}
PicoTargetReply failure(unsigned status,const char *code) {
    return {controller_target_error(status,code),{},{},{},{}};
}
std::string carrier(const WtpSettings &s) {return s.transport=="usb"?"usb_physical":s.transport=="network"?"engineering_mtls":"consumer_station";}
}
std::shared_mutex &pico_management_catalog_gate(){static std::shared_mutex gate;return gate;}
PicoTargetController::PicoTargetController(WtpCatalog &catalog,Active active,std::string directory,std::string identity,Fixtures fixtures,std::string parent_boot)
    :catalog_(catalog),active_(std::move(active)),fixtures_(std::move(fixtures)),directory_(std::move(directory)),identity_(std::move(identity)),parent_boot_(std::move(parent_boot)) {
    if(!wsprrypi::identity(identity_))throw std::invalid_argument("Controller identity required");
}
std::shared_ptr<PicoTargetController::Entry> PicoTargetController::selected(const std::string &device,const std::string &profile,bool create) {
    // This map lock covers only local lookup/publication. Native I/O and
    // catalog reads run outside it; unrelated targets must not fail merely
    // because another request is looking up its entry at the same instant.
    std::unique_lock lock(mutex_);
    if(profile.empty()) {
        const auto found=entries_.find(device);return found==entries_.end()?nullptr:found->second;
    }
    if(!identity(profile))throw std::invalid_argument("Invalid profile identity");
    lock.unlock();
    const auto [snapshot,revision]=catalog_.snapshot(active_());(void)snapshot;
    const auto selected=catalog_.profile(profile,revision,active_());
    if(selected.legacy_unverified||selected.settings.device_id!=device)throw std::invalid_argument("Enrolled target identity required");
    if(!valid_controller_document(Json{{"contract","Wsprry-Control/1"},{"schema","targets"},
        {"targets",Json::array({{{"device_id",device},{"product","WsprryPico"},{"name",selected.name},
            {"profile_generation","1"},{"connection_generation","1"}}})}},"targets"))
        throw std::invalid_argument("Invalid target metadata");
    const auto digest=fingerprint(selected);
    lock.lock();
    const auto found=entries_.find(device);
    if(found!=entries_.end()&&found->second->profile.id==profile&&found->second->fingerprint==digest)return found->second;
    if(!create)return {};
    if(found==entries_.end()&&entries_.size()>=64)throw std::runtime_error("context_capacity");
    // A caller may establish another edit context after the old operation has
    // completed. Disconnection refuses while an accepted request owns its lock.
    const auto previous=found==entries_.end()?std::shared_ptr<Entry>{}:found->second;
    lock.unlock();
    if(previous&&previous->context->disconnect().status!=200)throw std::runtime_error("context_busy");
    auto entry=std::make_shared<Entry>();entry->profile=selected;entry->fingerprint=digest;
    entry->profile_generation=selected.generation;entry->connection_generation=generation();
    entry->context=std::make_shared<PicoManagementContext>(selected.settings,identity_,directory_,entry->profile_generation,
        entry->connection_generation,fixtures_?fixtures_(selected):PicoManagementTarget::Fixtures{},catalog_.management_history_authority(active_()));
    lock.lock();
    const auto existing=entries_.find(device);
    if((existing==entries_.end())!=(!previous)||(existing!=entries_.end()&&existing->second!=previous))throw std::runtime_error("context_changed");
    entries_[device]=entry;return entry;
}
bool PicoTargetController::current(const Entry &entry) {
    try {
        const auto [snapshot,revision]=catalog_.snapshot(active_());(void)snapshot;
        return fingerprint(catalog_.profile(entry.profile.id,revision,active_()))==entry.fingerprint;
    }catch(...){return false;}
}
bool PicoTargetController::negotiate_attached_usb(const std::string &profile,const PiPairPrincipal &principal,PiPairHistory &history,const std::function<bool()> &cancelled){
    try{
        if((cancelled&&cancelled())||!identity(parent_boot_)||!identity(profile)||principal.self_id()!=identity_||!history.management_authorization(principal))return false;
        std::shared_lock gate(pico_management_catalog_gate(),std::try_to_lock);if(!gate.owns_lock())return false;
        if(connections_.fetch_add(1)>=4){connections_.fetch_sub(1);return false;}
        struct Release {std::atomic<unsigned> &n;~Release(){n.fetch_sub(1);}} release{connections_};
        const auto [snapshot,revision]=catalog_.snapshot(active_());(void)snapshot;
        const auto candidate=catalog_.profile(profile,revision,active_());
        if(candidate.legacy_unverified||candidate.settings.transport!="usb"||candidate.settings.device_id==identity_)return false;
        auto target_admission=PicoManagementJournal::target_admission(directory_,candidate.settings.device_id);if(!target_admission)return false;
        const auto entry=selected(candidate.settings.device_id,profile,true);
        std::unique_lock operation(entry->operation,std::try_to_lock);if(!operation.owns_lock()||!current(*entry))return false;
        auto cancellation=entry->context->scoped_cancellation(cancelled);
        const auto &pair=principal.identity();
        const bool same=entry->gateway_parent==principal.self_id()&&entry->gateway_pair==pair.pair_id&&entry->gateway_generation==pair.authorization_generation;
        if(entry->enrolled.load()&&same){
            const bool verified=entry->context->authorized()&&history.management_authorization(principal).has_value()&&
                entry->context->management_state()["carrier"]=="usb_physical";
            if(!verified)entry->enrolled.store(false);
            return verified;
        }
        entry->enrolled.store(false);
        if(entry->context->disconnect().status!=200)return false;
        if(entry->context->connect("usb_physical").status!=200)return false;
        if(!history.management_authorization(principal)||!current(*entry)){
            entry->context->cancel();return false;
        }
        entry->gateway_parent=principal.self_id();entry->gateway_pair=pair.pair_id;entry->gateway_generation=pair.authorization_generation;
        entry->enrolled.store(true);return true;
    }catch(...){return false;}
}
void PicoTargetController::catalog_changed() {
    std::vector<std::shared_ptr<Entry>> retired;
    {
        std::lock_guard lock(mutex_);
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(current(*it->second)){++it;continue;}
            retired.push_back(it->second);it=entries_.erase(it);
        }
    }
    for(const auto &entry:retired) {
        try{(void)entry->context->disconnect();}catch(...){entry->context->cancel();}
    }
    PicoManagementJournal recovery(directory_,identity_,identity_,catalog_.management_history_authority(active_()));
    recovery.prune_retired();
}
PicoTargetReply PicoTargetController::handle(const BrowserRequest &request,const std::string &profile,
    const std::string &context,const std::string &operation) {
    return handle_impl(request,profile,context,operation,nullptr,nullptr,{});
}
PicoTargetReply PicoTargetController::handle(const BrowserRequest &request,const std::string &profile,
    const std::string &context,const std::string &operation,const PiPairPrincipal &principal,PiPairHistory &history,const std::function<bool()> &cancelled) {
    return handle_impl(request,profile,context,operation,&principal,&history,cancelled);
}
PicoTargetReply PicoTargetController::handle_impl(const BrowserRequest &request,const std::string &profile,
    const std::string &context,const std::string &operation,const PiPairPrincipal *principal,PiPairHistory *history,const std::function<bool()> &cancelled) {
    if(request.method!="GET"&&restored_remote_authority_held())return failure(503,"recovery_required");
    if(cancelled&&cancelled())return failure(503,"authority_pending");
    const bool paired=principal&&history;
    if(paired&&!history->management_authorization(*principal))return failure(403,"unauthorized");
    if(request.body.size()>32768)return failure(413,"payload_too_large");
    if((!profile.empty()&&!identity(profile))||(!context.empty()&&!identity(context))||
        (!operation.empty()&&!identity(operation))||(request.method=="GET"&&!request.body.empty()))return failure(400,"invalid_shape");
    if(request.method!="GET"&&!paired&&!request.mutation_context)return failure(403,"csrf_failed");
    const std::string prefix="/api/control/v1/targets/";
    std::shared_lock binding_gate(pico_management_catalog_gate(),std::try_to_lock);
    if(!binding_gate.owns_lock())return failure(409,"profile_changed");
    if(request.path=="/api/control/v1/targets"&&request.method=="GET") {
        Json targets=Json::array();std::map<std::string,std::shared_ptr<Entry>> entries;
        {std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return failure(429,"capacity");entries=entries_;}
        for(const auto &[device,entry]:entries)if(current(*entry)&&entry->enrolled.load()) {
            std::unique_lock operation_lock(entry->operation,std::try_to_lock);if(!operation_lock.owns_lock())return failure(429,"capacity");
            const auto bound=entry->context->binding();if(bound.is_null())continue;
            if(paired&&(entry->profile.settings.transport!="usb"||entry->context->management_state()["carrier"]!="usb_physical"||entry->context->management_state()["connected"]!=true||
                entry->gateway_parent!=principal->self_id()||entry->gateway_pair!=principal->identity().pair_id||entry->gateway_generation!=principal->identity().authorization_generation))continue;
            targets.push_back({{"device_id",device},{"product","WsprryPico"},{"name",entry->profile.name},
                {"profile_generation",bound["profile_generation"]},{"connection_generation",bound["connection_generation"]}});
        }
        const Json document{{"contract","Wsprry-Control/1"},{"schema","targets"},{"targets",targets}};
        if(!valid_controller_document(document,"targets"))return failure(409,"invalid_shape");
        return {{200,document.dump(),{}},{},{},{},{}};
    }
    if(!request.path.starts_with(prefix)||request.path.size()<prefix.size()+34)return failure(404,"unsupported_resource");
    const auto device=request.path.substr(prefix.size(),32);
    if(!identity(device)||request.path[prefix.size()+32]!='/')return failure(404,"unsupported_resource");
    const auto suffix=request.path.substr(prefix.size()+33);
    auto target_admission=PicoManagementJournal::target_admission(directory_,device);if(!target_admission)return failure(409,"capacity");
    const bool management=suffix=="pico/management";
    const bool gateway_context=paired&&request.method=="GET"&&suffix=="context";
    const bool review=paired&&request.method=="POST"&&suffix.size()==50&&suffix.starts_with("operations/")&&suffix.ends_with("/review")&&identity(suffix.substr(11,32));
    // The paired connection cannot select a carrier, log into another Pico or
    // turn this Pi into an arbitrary network forwarding endpoint.
    if(!gateway_context&&!suffix.starts_with("pico/")&&!suffix.starts_with("operations/"))return failure(404,"unsupported_resource");
    if(connections_.fetch_add(1)>=4){connections_.fetch_sub(1);return failure(429,"capacity");}
    struct Release {std::atomic<unsigned> &n;~Release(){n.fetch_sub(1);}} release{connections_};
    try {
        Json action;
        if(review){
            action=strict_browser_json(request.body,16);
            if(!valid_controller_document(action,"target-operation-review")||action["operation_id"]!=suffix.substr(11,32)||action["binding"]["device_id"]!=device)
                return failure(400,"invalid_shape");
        }
        if(management&&request.method=="POST") {
            if(request.body.size()>512)return failure(413,"payload_too_large");
            action=strict_browser_json(request.body,16);
            if(!valid_controller_document(action,"controller-pico-selection"))return failure(400,"invalid_shape");
            if(!action.is_object()||action.value("contract",Json(nullptr))!="Wsprry-Control/1"||!action.contains("action")||!action["action"].is_string())return failure(400,"invalid_shape");
            action.erase("contract");
            const auto name=action["action"].get<std::string>();
            if(paired&&name!="reconcile")return failure(404,"unsupported_resource");
            if(name=="connect") {
                if(action.size()!=4||!action.contains("carrier")||!action["carrier"].is_string()||!action.contains("host")||!action["host"].is_string()||!action.contains("password")||!action["password"].is_string())return failure(400,"invalid_shape");
            } else if(name=="reconcile") {
                if(action.size()!=3||!action.contains("operation_id")||!action["operation_id"].is_string()||!action.contains("readback_revision")||!action["readback_revision"].is_string())return failure(400,"invalid_shape");
            } else if(name=="inspect_logins"||name=="retire_login") {
                // The authoritative closed request schema has already checked
                // carrier, endpoint, password, selection and proof scope types.
            } else if((name!="usb_physical"&&name!="engineering_mtls"&&name!="disconnect")||action.size()!=1)return failure(400,"invalid_shape");
        } else if(management&&request.method!="GET")return failure(405,"unsupported_resource");
        if(management&&!paired&&context.empty()&&profile.empty())return failure(428,"profile_changed");
        const auto entry=selected(device,profile,!paired&&management&&context.empty());
        if(!entry)return failure(409,"connection_changed");
        std::unique_lock operation_lock(entry->operation,std::try_to_lock);
        if(!operation_lock.owns_lock())return failure(409,"capacity");
        auto cancellation=entry->context->scoped_cancellation(cancelled);
        if(!current(*entry)){entry->context->cancel();return failure(409,"profile_changed");}
        if(paired&&(entry->profile.settings.transport!="usb"||!entry->enrolled.load()||entry->context->management_state()["carrier"]!="usb_physical"||entry->context->management_state()["connected"]!=true||
            entry->gateway_parent!=principal->self_id()||entry->gateway_pair!=principal->identity().pair_id||entry->gateway_generation!=principal->identity().authorization_generation))return failure(403,"unauthorized");
        // Fresh context observation is authorized by the actual parent pair,
        // not by an old editor token. Ordinary resource calls still fence it.
        if(!gateway_context&&!context.empty()&&context!=entry->context->id())return failure(409,"connection_changed");
        if(!gateway_context&&!management&&context.empty())return failure(428,"connection_changed");
        PicoTargetReply reply;reply.context=entry->context->id();
        const auto metadata=[&](bool verified) {
            const bool connected=entry->enrolled.load()&&(verified||entry->context->authorized());
            const auto native=entry->context->management_state();
            const auto selected_carrier=native.is_object()&&native.value("carrier","")!=""?native["carrier"].get<std::string>():carrier(entry->profile.settings);
            return PicoHttpResponse{200,Json{{"contract","Wsprry-Control/1"},{"carrier",selected_carrier},
                {"connected",connected},{"host",selected_carrier=="consumer_ap"?"192.168.4.1":entry->profile.settings.hostname},
                {"selection_id",entry->context->id()},{"session",native.is_object()?native["session"]:Json(nullptr)}}.dump(),{}};
        };
        if(gateway_context){
            if(!identity(parent_boot_))return failure(503,"authority_pending");
            const auto resources=entry->context->gateway_resources();
            if(std::find(resources.begin(),resources.end(),Json("configuration"))==resources.end())return failure(404,"unsupported_resource");
            auto configuration=request;configuration.path=prefix+device+"/pico/configuration";
            const auto saved=entry->context->request(configuration,entry->context->id(),{},*principal,*history);
            if(saved.status!=200)return {saved,{},{},{},{}};
            const auto authorization=history->management_authorization(*principal);
            if(!authorization||!current(*entry))return failure(403,"unauthorized");
            const auto native=entry->context->management_state(),child=entry->context->binding();
            if(native["connected"]!=true||native["carrier"]!="usb_physical"||child.is_null())return failure(409,"connection_changed");
            const Json parent{{"device_id",identity_},{"boot_id",parent_boot_},{"profile_generation",principal->identity().authorization_generation},{"connection_generation","1"}};
            const Json gateway{{"parent",parent},{"pair_id",principal->identity().pair_id},{"authorization_generation",principal->identity().authorization_generation},{"child",child},{"resources",resources}};
            const Json target{{"device_id",device},{"product","WsprryPico"},{"name",entry->profile.name},{"profile_generation",child["profile_generation"]},{"connection_generation",child["connection_generation"]}};
            const auto capability=PicoManagementJournal::digest(canonical_pi_management_json(Json{{"native",native["capability_revision"]},{"gateway",gateway}}));
            const auto document=strict_browser_json(saved.body);
            const auto revision=PicoManagementJournal::digest(canonical_pi_management_json(Json{{"configuration",document.at("config")},{"native_revision",saved.etag}}));
            const Json result{{"contract","Wsprry-Control/1"},{"schema","target-context"},{"target",target},{"binding",child},
                {"capability_revision",capability},{"capabilities",Json::array()},{"configuration_revision",revision},{"authorization",*authorization},{"gateway",gateway}};
            if(!valid_controller_document(result,"target-context"))return failure(503,"invalid_shape");
            reply.response={200,result.dump(),{}};reply.context=entry->context->id();return reply;
        }
        if(review){
            reply.response=entry->context->review(action,*principal,*history);reply.context=entry->context->id();return reply;
        }
        if(management) {
            if(request.method=="GET") {
                // Fresh establishment performs one bounded negotiation. Do not
                // follow it with another 24-second authorization exchange.
                const bool established=context.empty()&&entry->enrolled.load();
                const bool verified=established&&entry->context->refresh();
                reply.context=entry->context->id();
                if(established&&!verified)return failure(409,"connection_changed");
                reply.response=metadata(verified);return reply;
            }
            auto &password=action["password"];
            struct Wipe {Json &v;~Wipe(){if(v.is_string()){auto &s=v.get_ref<std::string&>();if(!s.empty())OPENSSL_cleanse(s.data(),s.size());}}} wipe{password};
            const auto name=action["action"].get<std::string>();
            if(name=="disconnect") {
                reply.response=entry->context->disconnect();
                if(reply.response.status==200) {
                    std::lock_guard lock(mutex_);const auto found=entries_.find(device);
                    if(found!=entries_.end()&&found->second==entry)entries_.erase(found);
                    reply.context.clear();
                }
            }
            else if(name=="inspect_logins"||name=="retire_login") {
                const auto selected_carrier=action["carrier"].get<std::string>();
                const auto expected=selected_carrier=="consumer_ap"?"192.168.4.1":entry->profile.settings.hostname;
                if(canonical_network_identity(action["host"].get<std::string>())!=canonical_network_identity(expected))return failure(400,"target_mismatch");
                if(name=="retire_login"&&action["selection_id"]!=entry->context->id())return failure(409,"connection_changed");
                reply.response=entry->context->recovery(selected_carrier,password.is_string()?password.get<std::string>():std::string{},
                    name=="retire_login"?action["scope"]:Json(nullptr));
                reply.context=entry->context->id();
            } else if(name=="reconcile"){
                reply.response=paired?entry->context->reconcile(action["operation_id"],action["readback_revision"],*principal,*history):entry->context->reconcile(action["operation_id"],action["readback_revision"]);
                reply.context=entry->context->id();
                if(reply.response.status==200){reply.operation=action["operation_id"];reply.location=prefix+device+"/operations/"+reply.operation;reply.recovery_resource=entry->context->receipt_resource(reply.operation);}
            }
            else {
                const auto selected_carrier=name=="connect"?action["carrier"].get<std::string>():name;
                if(name=="connect") {
                    const auto expected=selected_carrier=="consumer_ap"?"192.168.4.1":entry->profile.settings.hostname;
                    if((selected_carrier!="consumer_ap"&&selected_carrier!="consumer_station")||
                        canonical_network_identity(action["host"].get<std::string>())!=canonical_network_identity(expected))return failure(400,"target_mismatch");
                }
                reply.response=entry->context->connect(selected_carrier,password.is_string()?password.get<std::string>():std::string{});
                if(reply.response.status==200){reply.context=entry->context->id();entry->enrolled.store(true);reply.response={200,R"({"contract":"Wsprry-Control/1","ok":true})",{}};}
            }
            return reply;
        }
        if(suffix.starts_with("operations/")) {
            const auto id=suffix.substr(11);if(request.method!="GET"||!identity(id))return failure(404,"unsupported_resource");
            if(!entry->context->authorized())return failure(401,"unauthenticated");
            try {
                const auto receipt=paired?entry->context->receipt(id,*principal,*history):entry->context->receipt(id);
                if(!receipt.is_null()){reply.operation=id;reply.location=prefix+device+"/operations/"+id;reply.recovery_resource=entry->context->receipt_resource(id);}
                reply.response=receipt.is_null()?failure(404,"operation_unknown").response:PicoHttpResponse{200,receipt.dump(),{}};return reply;
            } catch(...) {return failure(503,"storage_unavailable");}
        }
        reply.response=paired?entry->context->request(request,context,operation,*principal,*history):entry->context->request(request,context,operation);
        reply.context=entry->context->id();
        reply.readback_revision=entry->context->readback_revision();
        if(!operation.empty()) {
            const auto [held,resource]=entry->context->recovery_hint();
            reply.operation=operation;reply.location=prefix+device+"/operations/"+(held.empty()?operation:held);reply.recovery_resource=resource;
        }
        return reply;
    } catch(const nlohmann::json::exception &) {return failure(400,"invalid_shape");}
      catch(...) {return failure(409,"recovery_required");}
}
} // namespace wsprrypi
