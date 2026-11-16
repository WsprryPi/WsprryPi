// SPDX-License-Identifier: MIT
#include "pico_target_controller.hpp"
#include "wtp_settings_json.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <array>
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
PicoTargetController::PicoTargetController(WtpCatalog &catalog,Active active,std::string directory,std::string identity,Fixtures fixtures)
    :catalog_(catalog),active_(std::move(active)),fixtures_(std::move(fixtures)),directory_(std::move(directory)),identity_(std::move(identity)) {
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
    if(request.body.size()>32768)return failure(413,"payload_too_large");
    if((!profile.empty()&&!identity(profile))||(!context.empty()&&!identity(context))||
        (!operation.empty()&&!identity(operation))||(request.method=="GET"&&!request.body.empty()))return failure(400,"invalid_shape");
    if(request.method!="GET"&&!request.mutation_context)return failure(403,"csrf_failed");
    const std::string prefix="/api/control/v1/targets/";
    std::shared_lock binding_gate(pico_management_catalog_gate(),std::try_to_lock);
    if(!binding_gate.owns_lock())return failure(409,"profile_changed");
    if(request.path=="/api/control/v1/targets"&&request.method=="GET") {
        Json targets=Json::array();std::map<std::string,std::shared_ptr<Entry>> entries;
        {std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return failure(429,"capacity");entries=entries_;}
        for(const auto &[device,entry]:entries)if(current(*entry)&&entry->enrolled.load()) {
            std::unique_lock operation_lock(entry->operation,std::try_to_lock);if(!operation_lock.owns_lock())return failure(429,"capacity");
            const auto bound=entry->context->binding();if(bound.is_null())continue;
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
    const bool management=suffix=="pico/management";
    if(!suffix.starts_with("pico/")&&!suffix.starts_with("operations/"))return failure(404,"unsupported_resource");
    if(connections_.fetch_add(1)>=4){connections_.fetch_sub(1);return failure(429,"capacity");}
    struct Release {std::atomic<unsigned> &n;~Release(){n.fetch_sub(1);}} release{connections_};
    try {
        Json action;
        if(management&&request.method=="POST") {
            if(request.body.size()>512)return failure(413,"payload_too_large");
            action=strict_browser_json(request.body,16);
            if(!valid_controller_document(action,"controller-pico-selection"))return failure(400,"invalid_shape");
            if(!action.is_object()||action.value("contract",Json(nullptr))!="Wsprry-Control/1"||!action.contains("action")||!action["action"].is_string())return failure(400,"invalid_shape");
            action.erase("contract");
            const auto name=action["action"].get<std::string>();
            if(name=="connect") {
                if(action.size()!=4||!action.contains("carrier")||!action["carrier"].is_string()||!action.contains("host")||!action["host"].is_string()||!action.contains("password")||!action["password"].is_string())return failure(400,"invalid_shape");
            } else if(name=="reconcile") {
                if(action.size()!=3||!action.contains("operation_id")||!action["operation_id"].is_string()||!action.contains("readback_revision")||!action["readback_revision"].is_string())return failure(400,"invalid_shape");
            } else if(name=="inspect_logins"||name=="retire_login") {
                // The authoritative closed request schema has already checked
                // carrier, endpoint, password, selection and proof scope types.
            } else if((name!="usb_physical"&&name!="engineering_mtls"&&name!="disconnect")||action.size()!=1)return failure(400,"invalid_shape");
        } else if(management&&request.method!="GET")return failure(405,"unsupported_resource");
        if(management&&context.empty()&&profile.empty())return failure(428,"profile_changed");
        const auto entry=selected(device,profile,management&&context.empty());
        if(!entry)return failure(409,"connection_changed");
        std::unique_lock operation_lock(entry->operation,std::try_to_lock);
        if(!operation_lock.owns_lock())return failure(409,"capacity");
        if(!current(*entry)){entry->context->cancel();return failure(409,"profile_changed");}
        if(!context.empty()&&context!=entry->context->id())return failure(409,"connection_changed");
        if(!management&&context.empty())return failure(428,"connection_changed");
        PicoTargetReply reply;reply.context=entry->context->id();
        const auto metadata=[&](bool verified) {
            const bool connected=entry->enrolled.load()&&(verified||entry->context->authorized());
            const auto native=entry->context->management_state();
            const auto selected_carrier=native.is_object()&&native.value("carrier","")!=""?native["carrier"].get<std::string>():carrier(entry->profile.settings);
            return PicoHttpResponse{200,Json{{"contract","Wsprry-Control/1"},{"carrier",selected_carrier},
                {"connected",connected},{"host",selected_carrier=="consumer_ap"?"192.168.4.1":entry->profile.settings.hostname},
                {"selection_id",entry->context->id()},{"session",native.is_object()?native["session"]:Json(nullptr)}}.dump(),{}};
        };
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
                reply.response=entry->context->reconcile(action["operation_id"],action["readback_revision"]);
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
                const auto receipt=entry->context->receipt(id);
                if(!receipt.is_null()){reply.operation=id;reply.location=prefix+device+"/operations/"+id;reply.recovery_resource=entry->context->receipt_resource(id);}
                reply.response=receipt.is_null()?failure(404,"operation_unknown").response:PicoHttpResponse{200,receipt.dump(),{}};return reply;
            } catch(...) {return failure(503,"storage_unavailable");}
        }
        reply.response=entry->context->request(request,context,operation);
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
