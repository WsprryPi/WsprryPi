// SPDX-License-Identifier: MIT
#include "pico_management_target.hpp"
#include "management_resolver.hpp"
#include "browser_api.hpp"
#include "control_contract.hpp"
#include "controller_contract.hpp"
#include "pico_management_journal.hpp"
#include "consumer_radio.hpp"
#include "udp_logging/management.hpp"
#include <openssl/crypto.h>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace wsprrypi {
namespace {
PicoHttpResponse refusal(unsigned status,const char *code) {
    return {status,nlohmann::json{{"error",{{"code",code}}}}.dump(),{}};
}
bool identity(const std::string &s) {return s.size()==32 && s.find_first_not_of("0123456789abcdef")==std::string::npos;}
class LoopbackResolver final:public TlsResolver {
    bool begin(const std::string &,unsigned)override{return true;}
    std::optional<std::vector<std::string>> poll()override{return std::vector<std::string>{"127.0.0.1"};}
    void cancel()noexcept override{}
};
}
std::uint64_t PicoManagementTarget::now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
PicoManagementTarget::PicoManagementTarget(WtpSettings settings)
    : PicoManagementTarget(std::move(settings),Fixtures{}) {}
PicoManagementTarget::PicoManagementTarget(WtpSettings settings,Fixtures fixtures)
    : settings_(std::move(settings)),fixtures_(std::move(fixtures)),
      usb_(fixtures_.usb ? fixtures_.usb : pico_usb_management_channel(settings_)),
      tls_(now,fixtures_.loopback_tls?TlsStream::Access::LoopbackTest:TlsStream::Access::Production,
          fixtures_.loopback_tls?std::unique_ptr<TlsResolver>(std::make_unique<LoopbackResolver>()):management_tls_resolver()),
      tcp_(now,PlainTcpStream::Access::Production,management_tls_resolver()) {
    validate_wtp_settings(settings_,true);
    if (!identity(settings_.device_id)) throw std::invalid_argument("Verified enrolled Pico identity required");
}
PicoManagementTarget::~PicoManagementTarget() {
    cancel();
    std::lock_guard lock(mutex_);
    if (consumer_) consumer_->clear();
}
bool PicoManagementTarget::cancel_requested() const noexcept{
    try{return cancelled_->load()||(operation_cancelled_&&operation_cancelled_());}catch(...){return true;}
}
void PicoManagementTarget::cancel() noexcept {cancelled_->store(true);tls_.cancel();}
void PicoManagementTarget::mutation_gate(MutationGate gate) {
    std::lock_guard lock(mutex_);mutation_gate_=std::move(gate);
}
nlohmann::json PicoManagementTarget::binding() const {
    std::lock_guard lock(mutex_);
    return {{"device_id",settings_.device_id},{"boot_id",boot_},{"carrier",carrier_},
        {"authorization_generation",generation_},{"connected",connected_ && !cancel_requested()},
        {"session",consumer_?consumer_->lost_session():nlohmann::json(nullptr)},
        {"capability_revision",PicoManagementJournal::digest(capabilities_+"\n"+management_)}};
}
nlohmann::json PicoManagementTarget::gateway_resources() const {
    std::lock_guard lock(mutex_);nlohmann::json result=nlohmann::json::array();
    if(!connected_||cancel_requested()||carrier_!="usb_physical")return result;
    const auto caps=strict_browser_json(capabilities_,16);
    if(!pico_management_capabilities_valid(caps,carrier_))return result;
    const auto feature=[&](const char *name){return caps["features"].contains(name)&&caps["features"][name]==true;};
    const auto add=[&](const char *resource,bool supported){if(supported)result.push_back(resource);};
    add("management",true);add("status",true);add("configuration",feature("config")&&caps.contains("configuration_updates"));
    add("application",feature("application"));add("station",feature("station"));add("hardware",feature("hardware"));
    add("pins",feature("hardware"));add("pins/validate",feature("hardware"));
    for(const auto &[descriptor,resource]:std::initializer_list<std::pair<const char *,const char *>>{
        {"gpio_drive","gpio-drive"},{"amplifier_timing","amplifier-timing"},{"filter_selector","filter-selector"},
        {"si5351","si5351"},{"gps","gps"},{"gray_line","gray-line"}})add(resource,caps.contains(descriptor));
    add("gpio-drive-progress",caps.contains("gpio_drive")&&caps.contains("saved_progress"));
    for(const auto *resource:{"progress","progress/update","progress/apply"})add(resource,caps.contains("saved_progress"));
    for(const auto *resource:{"udp-logging","udp-logging/capabilities","udp-logging/test","udp-logging/test/capabilities"})add(resource,true);
    for(const auto *resource:{"network","wifi","hostname"})add(resource,feature("network"));
    add("ap",feature("softap"));add("restart",feature("restart"));add("logging",caps.contains("logging"));
    add("schedules/stop",caps.contains("schedule_stop"));
    return result;
}
PicoHttpResponse PicoManagementTarget::exchange(const std::string &resource,const std::string &method,
    const std::string &body,const std::string &revision,const UsbManagementClient::WriteAdmission &admit,bool guarded) {
    if (cancel_requested() || now()>=deadline_) return refusal(503,"management_deadline_or_cancelled");
    if (method!="GET" && resource!="pins/validate" && mutation_gate_)
        if (auto refusal=mutation_gate_()) return *refusal;
    if(cancel_requested()||now()>=deadline_)return refusal(503,"management_deadline_or_cancelled");
    if (carrier_=="usb_physical") {
        usb_->cancellation=cancelled_;
        if (!usb_->ensure(settings_.device_id,false,deadline_,[this]{return cancel_requested();})) return refusal(503,"usb_management_unavailable");
        if(guarded&&method!="GET"&&(usb_->client->device()!=settings_.device_id||usb_->client->boot()!=boot_||
            !usb_connection_||usb_->client->connection_generation()!=usb_connection_)){
            // ensure may have reopened the shared physical channel after a
            // disconnect. Resource aliases such as network have no boot in
            // their native body, so never rebase an old edit onto that boot.
            connected_=false;return refusal(409,"management_context_changed");
        }
        const auto current=now();
        if (current>=deadline_) return refusal(503,"management_deadline_or_cancelled");
        const auto path=resource.starts_with('/') ? resource : "/api/control/v1/"+resource;
        const auto timeout=static_cast<unsigned>(std::min<std::uint64_t>(10000,deadline_-current));
        return guarded?usb_->client->request_guarded(path,method,body,revision,timeout,admit,[this]{return cancel_requested();}):
            usb_->client->request(path,method,body,revision,timeout,[this]{return cancel_requested();});
    }
    if(guarded)return refusal(409,"unauthorized");
    if (carrier_=="consumer_station" || carrier_=="consumer_ap")
        return consumer_ ? consumer_->request(resource,method,body,revision) : refusal(401,"consumer_login_required");
    if (carrier_!="engineering_mtls") return refusal(401,"management_carrier_required");
    if (fixtures_.engineering) return fixtures_.engineering(resource,method,body,revision);
    const TlsSelection selection{settings_.hostname,settings_.tls_identity,settings_.tls_ca,
        settings_.tls_certificate,settings_.tls_key,static_cast<unsigned>(settings_.tcp_port)};
    if (!credentials_ || !credentials_->matches(TlsCredentials(selection)))
        return refusal(409,"credentials_changed_read_required");
    return pico_http_request(tls_,selection,credentials_,now,resource,method,body,revision,deadline_);
}
bool PicoManagementTarget::negotiate(bool initial) {
    const auto response=exchange("management","GET",{},{});
    if (response.status!=200 || response.body.size()>16384) return false;
    const auto value=strict_browser_json(response.body);
    if (!value.is_object() || !value.contains("target") || !value["target"].is_object() ||
        !value["target"].contains("boot_id") || !value["target"]["boot_id"].is_string()) return false;
    const auto boot=value["target"]["boot_id"].get<std::string>();
    if(!initial&&value.dump()!=management_)return false;
    if (!identity(boot) || (!initial && boot!=boot_) ||
        !valid_management_contract(value,carrier_,settings_.device_id,boot,
            consumer_ ? consumer_->generation() : std::string{})) return false;
    const auto caps_response=exchange("capabilities","GET",{},{});
    if (caps_response.status!=200 || caps_response.body.size()>16384) return false;
    const auto caps=strict_browser_json(caps_response.body);
    if (!pico_management_capabilities_valid(caps,carrier_) || caps["management_carriers"]["schema"]!=value["schema"]) return false;
    const auto capability_snapshot=caps.dump();
    if (!initial && capability_snapshot!=capabilities_) return false;
    const auto gen=value["profile_generation"].is_null() ? std::string{} : value["profile_generation"].get<std::string>();
    if (!initial && gen!=generation_) return false;
    if (carrier_=="usb_physical"){
        const auto connection=usb_->client->connection_generation();
        if(usb_->client->device()!=settings_.device_id||usb_->client->boot()!=boot||!connection||(!initial&&connection!=usb_connection_))return false;
        usb_connection_=connection;
    }
    boot_=boot;generation_=gen;capabilities_=capability_snapshot;management_=value.dump();
    return true;
}
PicoHttpResponse PicoManagementTarget::connect(const std::string &carrier,std::string password) {
    struct Wipe {std::string &s;~Wipe(){if(!s.empty())OPENSSL_cleanse(s.data(),s.size());}} wipe{password};
    std::unique_lock lock(mutex_,std::try_to_lock);
    if (!lock.owns_lock()) return refusal(409,"management_busy");
    deadline_=now()+24000;
    if (connected_) return refusal(409,"management_context_already_connected");
    cancelled_->store(false);
    try {
        if(consumer_) {consumer_->logout();consumer_.reset();}
        credentials_.reset();carrier_=carrier;
        if (carrier=="usb_physical") {
            if (settings_.transport!="usb") return refusal(409,"usb_transport_required");
        } else if (carrier=="engineering_mtls") {
            if (settings_.transport!="network") return refusal(409,"engineering_profile_required");
            if (!fixtures_.engineering)
                credentials_=std::make_shared<TlsCredentials>(TlsSelection{settings_.hostname,settings_.tls_identity,
                    settings_.tls_ca,settings_.tls_certificate,settings_.tls_key,static_cast<unsigned>(settings_.tcp_port)});
        } else if (carrier=="consumer_station" || carrier=="consumer_ap") {
            const auto host=carrier=="consumer_ap" ? "192.168.4.1" : settings_.hostname;
            if (host.empty() || (carrier=="consumer_station" && host=="192.168.4.1")) return refusal(400,"invalid_consumer_endpoint");
            consumer_=std::make_unique<ConsumerRadioClient>([this,host](const auto &path,const auto &verb,const auto &payload,const auto &media) {
                if (fixtures_.consumer) return fixtures_.consumer(path,verb,payload,media);
                if (cancel_requested() || now()>=deadline_) return refusal(503,"management_deadline_or_cancelled");
                return consumer_http_request(tcp_,now,host,path,verb,payload,media,deadline_);
            },now);
            auto login=consumer_->login(settings_.device_id,carrier,password);
            if (login.status!=200) {consumer_->clear();return login;}
        } else return refusal(400,"unsupported_management_carrier");
        std::unique_lock<std::recursive_mutex> usb_lock(usb_->mutex,std::defer_lock);
        if (carrier=="usb_physical") {
            while (!usb_lock.try_lock()) {
                if (cancel_requested() || now()>=deadline_) return refusal(503,"management_deadline_or_cancelled");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        connected_=negotiate(true);
        return connected_ ? PicoHttpResponse{200,binding().dump(),{}} : refusal(409,"management_negotiation_failed");
    } catch (...) {connected_=false;return refusal(503,"management_unavailable");}
}
PicoHttpResponse PicoManagementTarget::disconnect() {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return refusal(409,"management_busy");
    deadline_=now()+24000;
    if(consumer_) {consumer_->logout();consumer_.reset();}
    connected_=false;cancelled_->store(true);tls_.close();tcp_.close();credentials_.reset();
    // Closing an editor never closes the shared management CDC of an executor.
    return {200,R"({"ok":true})",{}};
}
bool PicoManagementTarget::refresh() {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return false;
    deadline_=now()+24000;
    if(carrier_.empty()||cancel_requested()||(consumer_&&!consumer_->active()))return false;
    std::unique_lock<std::recursive_mutex> usb_lock(usb_->mutex,std::defer_lock);
    if(carrier_=="usb_physical")while(!usb_lock.try_lock()) {
        if(cancel_requested()||now()>=deadline_)return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    try {connected_=negotiate(true);}catch(...){connected_=false;}
    return connected_;
}
PicoHttpResponse PicoManagementTarget::recovery(const std::string &carrier,std::string password,const nlohmann::json &scope) {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return refusal(409,"management_busy");
    struct Wipe {std::string &s;~Wipe(){if(!s.empty())OPENSSL_cleanse(s.data(),s.size());}} wipe{password};
    if(carrier!="consumer_station"&&carrier!="consumer_ap")return refusal(400,"unsupported_management_carrier");
    const auto host=carrier=="consumer_ap"?"192.168.4.1":settings_.hostname;
    if(host.empty()||(carrier=="consumer_station"&&host=="192.168.4.1"))return refusal(400,"invalid_consumer_endpoint");
    deadline_=now()+24000;cancelled_->store(false);
    ConsumerRadioClient proof([this,host,&scope](const auto &path,const auto &verb,const auto &body,const auto &media) {
        if(path=="/api/consumer/v1/recovery/finish"&&consumer_&&scope.is_object()) {
            const auto &selected=consumer_->lost_session();
            if(selected.is_object()&&selected.value("selector",nlohmann::json(nullptr))==scope.value("target_selector",nlohmann::json(nullptr))&&
                selected.value("boot_id",nlohmann::json(nullptr))==scope.value("boot_id",nlohmann::json(nullptr))) {
                consumer_->clear();connected_=false;
            }
        }
        if(cancel_requested()||now()>=deadline_)return refusal(503,"management_deadline_or_cancelled");
        if(fixtures_.consumer)return fixtures_.consumer(path,verb,body,media);
        return consumer_http_request(tcp_,now,host,path,verb,body,media,deadline_);
    },now);
    return scope.is_null()?proof.recovery(settings_.device_id,carrier,host):proof.retire(settings_.device_id,carrier,host,password,scope);
}
PicoHttpResponse PicoManagementTarget::request(const std::string &resource,const std::string &method,
    const std::string &body,const std::string &revision) {
    return request_impl(resource,method,body,revision,{},false);
}
PicoHttpResponse PicoManagementTarget::request_guarded(const std::string &resource,const std::string &method,
    const std::string &body,const std::string &revision,const UsbManagementClient::WriteAdmission &admit) {
    return request_impl(resource,method,body,revision,admit,true);
}
PicoHttpResponse PicoManagementTarget::request_impl(const std::string &resource,const std::string &method,
    const std::string &body,const std::string &revision,const UsbManagementClient::WriteAdmission &admit,bool guarded) {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if (!lock.owns_lock()) return refusal(409,"management_busy");
    if(guarded&&(carrier_!="usb_physical"||(method!="GET"&&!admit)))return refusal(409,"unauthorized");
    deadline_=now()+24000;
    if (!connected_ || cancel_requested()) return refusal(409,"management_context_read_required");
    if (body.size()>32768) return refusal(413,"body_too_large");
    std::unique_lock<std::recursive_mutex> usb_lock(usb_->mutex,std::defer_lock);
    if (carrier_=="usb_physical") {
        while (!usb_lock.try_lock()) {
            if (cancel_requested() || now()>=deadline_) return refusal(503,"management_deadline_or_cancelled");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    try {
        if (!negotiate(false)) {connected_=false;return refusal(409,"management_context_changed");}
        if(resource=="management"&&method=="GET")return {200,management_,{}};
        const auto io=[this,&admit,guarded](const auto &path,const auto &verb,const auto &payload,const auto &etag) {return exchange(path,verb,payload,etag,admit,guarded);};
        const auto generation=consumer_ ? consumer_->generation() : std::string{};
        const auto result=[&]() -> PicoHttpResponse {
        if(resource=="network") {
            if(strict_browser_json(capabilities_,16)["features"]["network"]!=true)
                return refusal(409,"unsupported_resource");
            if(method!="GET"&&method!="PUT")return refusal(405,"unsupported_resource");
            if(method=="PUT") {
                const auto input=strict_browser_json(body,16);
                if(!input.is_object()||input.size()!=2||input.value("contract",nlohmann::json(nullptr))!="Wsprry-Control/1"||
                    !input.contains("enabled")||!input["enabled"].is_boolean())return refusal(400,"invalid_network");
                if(revision.empty())return refusal(428,"revision_required");
            }
            const auto valid=[](const PicoHttpResponse &reply) {
                if(reply.status!=200||reply.body.size()>32768||reply.etag.size()<2||reply.etag.size()>128||
                    reply.etag.front()!='"'||reply.etag.back()!='"')return false;
                for(std::size_t i=1;i+1<reply.etag.size();++i)
                    if(static_cast<unsigned char>(reply.etag[i])<33||static_cast<unsigned char>(reply.etag[i])>=127||reply.etag[i]=='"')return false;
                try{return valid_pico_network_observation(strict_browser_json(reply.body,16));}catch(...){return false;}
            };
            const auto current=exchange(resource,"GET",{},{});
            if(current.status!=200)return refusal(current.status==401?401:503,"network_unavailable");
            if(!valid(current))return refusal(502,"invalid_remote_resource");
            // Unlike shared settings, native diagnostics have no target field.
            // Fence a reboot/session change spanning this observation before
            // publishing it or admitting a mutation under the old context.
            if(!negotiate(false)){connected_=false;return refusal(409,"management_context_changed");}
            if(method=="GET")return current;
            if(current.etag!=revision)return refusal(412,"revision_conflict");
            const auto changed=exchange(resource,method,body,revision,admit,guarded);
            if(changed.status>=400&&changed.status<500)
                return refusal(changed.status,changed.status==412?"revision_conflict":"network_change_refused");
            if(!valid(changed))return refusal(503,"management_outcome_unknown");
            const auto observed=strict_browser_json(changed.body,16);
            const auto requested=strict_browser_json(body,16)["enabled"];
            if(observed["requested_enabled"]!=requested&&
                !(observed["requested_enabled"].is_null()&&observed["enabled"]==requested&&!observed["withdrawal_pending"].get<bool>()))
                return refusal(503,"management_outcome_unknown");
            // Native acceptance records the request, not completed withdrawal.
            // Preserve enabled, requested_enabled, and withdrawal_pending so
            // the UI can distinguish the observed state from the transition.
            return changed;
        }
        if(resource=="wifi"||resource=="hostname"||resource=="logging"||resource=="restart"||resource=="ap") {
            if(carrier_!="usb_physical")return refusal(409,"usb_management_required");
            return pico_usb_settings_request(io,settings_.device_id,boot_,resource,method,body,revision);
        }
        if (resource=="configuration") return pico_configuration_request(io,settings_.device_id,boot_,method,body,revision,carrier_,generation);
        if (resource=="status" || resource=="application" || resource=="station" || resource=="hardware" || resource=="gpio-drive" ||
            resource=="amplifier-timing" || resource=="filter-selector" || resource=="si5351" || resource=="gps" ||
            resource=="gray-line" || resource=="gpio-drive-progress" || resource=="pins" || resource=="pins/validate" || resource=="schedules/stop")
            return pico_resource_request(io,settings_.device_id,boot_,resource,method,body,revision,carrier_,generation);
        if (resource=="progress" || resource=="progress/update" || resource=="progress/apply")
            return pico_progress_request(io,settings_.device_id,boot_,resource,method,body,revision,carrier_,generation);
        if (resource=="udp-logging" || resource=="udp-logging/capabilities" || resource=="udp-logging/test" || resource=="udp-logging/test/capabilities")
            return pico_udp_logging_request(io,settings_.device_id,boot_,resource,method,body,revision);
        return refusal(404,"unsupported_resource");
        }();
        if(method!="GET"&&resource!="pins/validate"&&result.status>=200&&result.status<300) {
            // An authenticated station-only profile transfer retains the native
            // crypto session. Refresh policy without logging in or replaying a
            // write; a failed refresh retires editing but not a confirmed result.
            try {connected_=negotiate(true);}catch(...){connected_=false;}
        }
        return result;
    } catch (...) {connected_=false;return refusal(503,"management_unavailable");}
}
} // namespace wsprrypi
