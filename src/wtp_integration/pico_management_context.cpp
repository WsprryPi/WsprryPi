// SPDX-License-Identifier: MIT
#include "pico_management_context.hpp"
#include "control_contract.hpp"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <charconv>
#include <set>
namespace wsprrypi {
namespace {
PicoHttpResponse refuse(unsigned status,const char *code) {return {status,nlohmann::json{{"error",{{"code",code}}}}.dump(),{}};}
std::string generation(std::string value) {
    std::uint64_t n{};const auto r=std::from_chars(value.data(),value.data()+value.size(),n);
    if(value.empty()||value[0]=='0'||!n||r.ec!=std::errc{}||r.ptr!=value.data()+value.size())throw std::invalid_argument("invalid_context_generation");
    return value;
}
std::string random_generation() {
    std::uint64_t n{};do{if(RAND_bytes(reinterpret_cast<unsigned char *>(&n),sizeof(n))!=1)throw std::runtime_error("context_entropy_unavailable");}while(!n);return std::to_string(n);
}
std::string context_id() {
    unsigned char bytes[16];if(RAND_bytes(bytes,sizeof(bytes))!=1)throw std::runtime_error("context_entropy_unavailable");
    std::string result;for(auto b:bytes){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
}
bool identity(const std::string &s){return s.size()==32&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
}
PicoManagementContext::PicoManagementContext(WtpSettings settings,std::string controller,std::string directory,
    std::string profile,std::string connection,PicoManagementTarget::Fixtures fixtures,PicoManagementHistoryAuthority authority)
    :target_(settings,std::move(fixtures)),journal_(std::move(directory),controller,settings.device_id,std::move(authority)),
     browser_([]{return nlohmann::json::object();},[this](const auto &r,const auto &m,const auto &b,const auto &v,const auto &s){return forward(r,m,b,v,s);},
        [](const auto &){return refuse(404,"unsupported_resource");}),controller_(std::move(controller)),device_(settings.device_id),
     profile_(generation(std::move(profile))),connection_(generation(std::move(connection))),id_(context_id()) {
    if(!journal_.authority_bound())throw std::invalid_argument("Catalog management-history authority required");
    target_.mutation_gate([this]() -> std::optional<PicoHttpResponse> {
        if(operation_.empty()||reserved_)return refuse(409,"recovery_required");
        // This callback runs after native validation/revision checks, immediately
        // before the sole outbound mutation. Failure cannot dispatch a write.
        std::optional<nlohmann::json> replay;
        try {replay=journal_.reserve(controller_,route_,method_,binding_,authorization_,operation_,request_digest_);}
        catch(const std::runtime_error &error) {
            const std::string code=error.what();
            gate_error_=code=="management_operation_capacity"||code=="management_storage_capacity"?
                refuse(429,"capacity"):refuse(503,"storage_unavailable");
            return gate_error_;
        }
        if(replay)return refuse(409,"recovery_required");
        reserved_=true;read_revision_.clear();read_resource_.clear();return {};
    });
}
PicoHttpResponse PicoManagementContext::connect(const std::string &carrier,std::string password) {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return refuse(409,"capacity");
    struct Wipe {std::string &s;~Wipe(){if(!s.empty())OPENSSL_cleanse(s.data(),s.size());}} wipe{password};
    try {(void)journal_.snapshot();}catch(...){return refuse(503,"storage_unavailable");}
    const auto response=target_.connect(carrier,password);
    if(response.status==200) {
        browser_.retire_management_reads();read_revision_.clear();read_resource_.clear();
        if(!binding_.is_null()){id_=context_id();connection_=random_generation();}
        const auto native=target_.binding();
        binding_={{"device_id",device_},{"boot_id",native.at("boot_id")},{"profile_generation",profile_},{"connection_generation",connection_}};
        authorization_=native.at("authorization_generation").get<std::string>();if(authorization_.empty())authorization_="1";
        capability_=native.at("capability_revision").get<std::string>();
    }
    return response;
}
nlohmann::json PicoManagementContext::binding() const {
    std::unique_lock lock(mutex_,std::try_to_lock);return lock.owns_lock()?binding_:nlohmann::json(nullptr);
}
bool PicoManagementContext::authorized() {
    std::unique_lock lock(mutex_,std::try_to_lock);return lock.owns_lock()&&target_.request("management","GET").status==200;
}
bool PicoManagementContext::refresh() {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||!target_.refresh())return false;
    const auto native=target_.binding();id_=context_id();connection_=random_generation();
    binding_={{"device_id",device_},{"boot_id",native["boot_id"]},{"profile_generation",profile_},{"connection_generation",connection_}};
    authorization_=native["authorization_generation"].get<std::string>();if(authorization_.empty())authorization_="1";
    capability_=native["capability_revision"].get<std::string>();browser_.retire_management_reads();read_revision_.clear();read_resource_.clear();return true;
}
nlohmann::json PicoManagementContext::management_state() {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return nullptr;
    return target_.binding();
}
PicoHttpResponse PicoManagementContext::recovery(const std::string &carrier,std::string password,const nlohmann::json &scope) {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return refuse(409,"capacity");
    struct Wipe {std::string &s;~Wipe(){if(!s.empty())OPENSSL_cleanse(s.data(),s.size());}} wipe{password};
    const auto response=target_.recovery(carrier,password,scope);
    if(response.status==200&&!scope.is_null()) {
        id_=context_id();connection_=random_generation();
        if(!binding_.is_null())binding_["connection_generation"]=connection_;
        browser_.retire_management_reads();read_revision_.clear();read_resource_.clear();
    }
    if(response.status==200) {auto body=strict_browser_json(response.body);body["selection_id"]=id_;body["contract"]="Wsprry-Control/1";return {200,body.dump(),response.etag};}
    return response;
}
PicoHttpResponse PicoManagementContext::disconnect() {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return refuse(409,"capacity");
    const auto response=target_.disconnect();
    if(response.status==200){binding_=nullptr;read_revision_.clear();read_resource_.clear();}
    return response;
}
nlohmann::json PicoManagementContext::receipt(const std::string &operation) {
    std::lock_guard lock(mutex_);if(!identity(operation))return nullptr;return journal_.receipt(operation);
}
std::string PicoManagementContext::receipt_resource(const std::string &operation) {
    std::lock_guard lock(mutex_);if(!identity(operation))return {};
    const auto prefix="/api/control/v1/targets/"+device_+"/pico/";
    const auto history=journal_.snapshot();
    for(const auto &entry:history["receipts"])if(entry["receipt"]["operation_id"]==operation) {
        const auto route=entry["route"].get<std::string>();
        if(route.starts_with(prefix))return route.substr(prefix.size());
    }
    return {};
}
PicoHttpResponse PicoManagementContext::forward(const std::string &resource,const std::string &method,
    const std::string &body,const std::string &revision,const std::string &selection) {
    if(resource=="member/management"&&method=="GET") {
        const auto native=target_.binding();
        return {200,nlohmann::json{{"selection_id",id_},{"carrier",native["carrier"]},{"connected",native["connected"]}}.dump(),{}};
    }
    if(resource=="network") {
        if(!selection.empty()&&selection!=id_)return refuse(409,"connection_changed");
        return target_.request(resource,method,body,revision);
    }
    if(!resource.starts_with("member/")||(!selection.empty()&&selection!=id_))return refuse(409,"connection_changed");
    return target_.request(resource.substr(7),method,body,revision);
}
PicoHttpResponse PicoManagementContext::request(const BrowserRequest &request,const std::string &context,const std::string &operation) {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return refuse(409,"capacity");
    recovery_operation_.clear();recovery_resource_.clear();gate_error_.reset();
    if(context!=id_||binding_.is_null()||target_.binding()["connected"]!=true)return refuse(409,"connection_changed");
    const auto prefix="/api/control/v1/targets/"+device_+"/pico/";
    if(!request.path.starts_with(prefix)||request.path.size()==prefix.size())return refuse(404,"unsupported_resource");
    const auto resource=request.path.substr(prefix.size());
    if(resource=="management"||!controller_pico_route(resource,request.method))return refuse(404,"unsupported_resource");
    const bool mutation=request.method!="GET"&&resource!="pins/validate";
    if(request.method!="GET"&&!request.mutation_context)return refuse(403,"csrf_failed");
    if(mutation&&(operation.empty()||request.revision.empty()))return refuse(428,"revision_required");
    if(mutation&&!identity(operation))return refuse(400,"invalid_shape");
    nlohmann::json parsed_body;
    if(request.method!="GET") {try {parsed_body=strict_browser_json(request.body,16);}catch(...){return refuse(400,"invalid_json");}}
    auto adapted=request;adapted.path="/api/control/v1/"+(resource=="network"?resource:"member/"+resource);
    operation_=mutation?operation:std::string{};route_=request.path;method_=request.method;reserved_=false;
    struct Clear {PicoManagementContext &s;~Clear(){s.operation_.clear();s.request_digest_.clear();s.reserved_=false;s.gate_error_.reset();}} clear{*this};
    bool retire_retention=false;
    try {
        if(mutation) {
            const auto &body=parsed_body;
            request_digest_=PicoManagementJournal::digest(nlohmann::json{{"principal",controller_},{"route",route_},{"method",method_},
                {"binding",binding_},{"authorization_generation",authorization_},{"revision",request.revision},{"body",body}}.dump());
            // Replay lookup precedes volatile read-context consumption. It never
            // retries a native mutation, including after process restart.
            const auto history=journal_.snapshot();
            for(const auto &entry:history["receipts"])if(entry["receipt"]["operation_id"]==operation_) {
                // A stored response is not continuing management authority.
                // Revalidate the live native authority before disclosing a
                // replay; this read never resends the original mutation.
                if(target_.request("management","GET").status!=200)return refuse(401,"unauthenticated");
                if(entry["principal_id"]!=controller_||entry["route"]!=route_||entry["method"]!=method_||entry["authorization_generation"]!=authorization_||
                    entry["receipt"]["binding"]!=binding_||entry["receipt"]["request_digest"]!=request_digest_)return refuse(409,"request_id_reuse");
                recovery_operation_=operation_;recovery_resource_=resource;
                if(entry["response"].is_null())return {409,journal_.receipt(operation_).dump(),{}};
                return {entry["response"]["status"].get<unsigned>(),entry["response"]["body"].dump(),entry["response"]["etag"].get<std::string>()};
            }
            for(const auto &entry:history["receipts"])if((entry["receipt"]["state"]=="pending"||entry["receipt"]["state"]=="unknown")&&entry["reconciled_revision"].is_null()) {
                if(target_.request("management","GET").status!=200)return refuse(401,"unauthenticated");
                recovery_operation_=entry["receipt"]["operation_id"].get<std::string>();
                recovery_resource_=entry["route"].get<std::string>().substr(prefix.size());
                return refuse(503,"recovery_required");
            }
        }
        const auto response=browser_.handle(adapted);
        if(gate_error_&&!reserved_)return *gate_error_;
        if(reserved_) {
            retire_retention=journal_.finish(operation_,response);
            if(journal_.receipt(operation_)["state"]=="unknown"){
                recovery_operation_=operation_;recovery_resource_=resource;
                return {503,journal_.receipt(operation_).dump(),{}};
            }
        }
        if(reserved_&&retire_retention) {
            // Once a terminal ID leaves bounded retention, an old captured
            // request must not become a new mutation. Fence its entire browser
            // context; historical receipts remain available through lookup.
            id_=context_id();connection_=random_generation();binding_["connection_generation"]=connection_;
            browser_.retire_management_reads();read_revision_.clear();read_resource_.clear();
        }
        if(reserved_&&response.status>=200&&response.status<300) {
            const auto native=target_.binding();
            auto authorization=native["authorization_generation"].get<std::string>();if(authorization.empty())authorization="1";
            const auto capability=native["capability_revision"].get<std::string>();
            if(native["connected"]==true&&(authorization!=authorization_||capability!=capability_)) {
                id_=context_id();connection_=random_generation();binding_["connection_generation"]=connection_;
                authorization_=authorization;capability_=capability;browser_.retire_management_reads();read_revision_.clear();read_resource_.clear();
            }
        }
        if(!mutation&&resource!="pins/validate"&&response.status==200&&!response.etag.empty()) {
            read_resource_=resource;read_revision_=PicoManagementJournal::digest(nlohmann::json{{"binding",binding_},{"resource",resource},{"etag",response.etag},{"body",strict_browser_json(response.body)}}.dump());
        }
        return response;
    } catch(...) {
        if(reserved_){recovery_operation_=operation_;recovery_resource_=resource;}
        return refuse(503,"storage_unavailable");
    }
}
PicoHttpResponse PicoManagementContext::reconcile(const std::string &operation,const std::string &revision) {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return refuse(409,"capacity");
    if(read_revision_.empty()||revision!=read_revision_||target_.binding()["connected"]!=true)return refuse(409,"recovery_required");
    try {
        const auto history=journal_.snapshot();bool matches=false;
        for(const auto &entry:history["receipts"])if(entry["receipt"]["operation_id"]==operation) {
            const auto route=entry["route"].get<std::string>();
            const auto resource=route.substr(route.find("/pico/")+6);
            matches=read_resource_==controller_pico_recovery_read(resource);
        }
        if(!matches)return refuse(409,"recovery_required");
        // Readback is evidence, not continuing authorization. A revoked native
        // session must not release the durable hold after that read.
        if(target_.request("management","GET").status!=200)return refuse(401,"unauthenticated");
        if(journal_.reconcile(operation,binding_,revision)) {
            id_=context_id();connection_=random_generation();binding_["connection_generation"]=connection_;
            browser_.retire_management_reads();
        }
        read_revision_.clear();read_resource_.clear();return {200,journal_.receipt(operation).dump(),{}};}
    catch(...){return refuse(503,"storage_unavailable");}
}
} // namespace wsprrypi
