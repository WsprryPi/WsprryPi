// SPDX-License-Identifier: MIT
#include "browser_admission.hpp"
#include "application_control/local_transport.hpp"
#include "backend_http_guard.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <chrono>
#include <filesystem>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/rand.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr const char *cookie="WSPRRYPI_RECOVERY_SESSION";
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string random(){unsigned char bytes[32];require(RAND_bytes(bytes,sizeof(bytes))==1);std::string result;
    for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;}
bool hex(const Json &value){return value.is_string()&&value.get_ref<const std::string &>().size()==64&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
bool equal(const std::string &a,const std::string &b){return a.size()==b.size()&&!CRYPTO_memcmp(a.data(),b.data(),a.size());}
std::string selected_cookie(const httplib::Request &request){
    if(request.get_header_value_count("Cookie")!=1)return {};
    const auto text=request.get_header_value("Cookie");std::size_t offset{};std::string result;bool found{};
    while(offset<text.size()){
        const auto end=text.find(';',offset);auto item=text.substr(offset,end==std::string::npos?end:end-offset);
        const auto first=item.find_first_not_of(' ');if(first!=std::string::npos)item.erase(0,first);
        const auto separator=item.find('=');
        if(separator!=std::string::npos&&item.substr(0,separator)==cookie){if(found)return {};found=true;result=item.substr(separator+1);}
        if(end==std::string::npos)break;
        offset=end+1;
    }
    return hex(Json(result))?result:std::string{};
}
}
struct RecoveryBrowserAdmission::Impl {
    std::mutex mutex;std::string path,identity;Snapshot snapshot;
    Json read(){
        const auto source=PiManagementJournal::read_private_record(path);
        if(!source)return {{"schema","recovery-browser-leases/1"},{"identity",identity},{"leases",Json::array()}};
        auto value=strict_browser_json(source->bytes,8);
        require(value.is_object()&&value.size()==3&&value.value("schema",Json(nullptr))=="recovery-browser-leases/1"&&
            value.value("identity",Json(nullptr))==identity&&value.contains("leases")&&value["leases"].is_array()&&value["leases"].size()<=64);
        for(const auto &lease:value["leases"]){
            require(lease.is_object()&&lease.size()==4&&lease.contains("cookie_sha256")&&lease.contains("csrf_sha256")&&
                hex(lease["cookie_sha256"])&&hex(lease["csrf_sha256"])&&lease.contains("host")&&lease["host"].is_string()&&
                lease["host"].get_ref<const std::string &>().size()<=512&&lease.contains("created_utc")&&lease["created_utc"].is_number_unsigned());
        }
        return value;
    }
};
RecoveryBrowserAdmission::RecoveryBrowserAdmission(std::string root,std::string identity,Snapshot snapshot):impl_(std::make_unique<Impl>()){
    require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos&&bool(snapshot));
    impl_->path=(std::filesystem::path(root)/"browser-leases.json").string();impl_->identity=std::move(identity);impl_->snapshot=std::move(snapshot);
}
RecoveryBrowserAdmission::~RecoveryBrowserAdmission()=default;
Json RecoveryBrowserAdmission::issue(const std::string &host,bool secure){
    // Host was validated by the actual backend boundary, and the worker repeats
    // the network/Host/Origin boundary for every subsequent request.
    require(!host.empty()&&host.size()<=512&&host.find_first_of("\r\n\0 /\\",0,6)==std::string::npos);
    std::lock_guard lock(impl_->mutex);auto value=impl_->read();const auto timestamp=now();auto retained=Json::array();
    for(const auto &lease:value["leases"]){const auto started=lease["created_utc"].get<std::uint64_t>();if(timestamp>=started&&timestamp-started<86400)retained.push_back(lease);}
    if(retained.size()>=64)throw LocalCommandRefused(LocalRefusal::Busy);
    const auto id=random(),csrf=random();retained.push_back({{"host",host},{"created_utc",timestamp},
        {"cookie_sha256",PiManagementJournal::digest(id)},{"csrf_sha256",PiManagementJournal::digest(csrf)}});value["leases"]=std::move(retained);
    const auto expected=PiManagementJournal::read_private_record(impl_->path);
    (void)PiManagementJournal::publish_private_record(impl_->path,expected,value.dump());
    return {{"schema","recovery-browser-admission/1"},{"csrf",csrf},{"cookie",std::string(cookie)+"="+id+
        "; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400"+(secure?"; Secure":"")}};
}
bool RecoveryBrowserAdmission::permits(const httplib::Request &request){
    for(const auto *header:{"Host","Origin","Cookie","Content-Type","X-WsprryPi-Recovery-CSRF"})if(request.get_header_value_count(header)>1)return false;
    const auto id=selected_cookie(request);if(id.empty())return false;
    const bool read=request.method=="GET"||request.method=="HEAD";
    if(!read&&(request.method!="POST"||request.get_header_value("Content-Type")!="application/json"||
        !request.has_header("Origin")||!hex(Json(request.get_header_value("X-WsprryPi-Recovery-CSRF")))))return false;
    const auto origin=request.has_header("Origin")?std::optional(request.get_header_value("Origin")):std::nullopt;
    std::vector<std::string> proxy;const std::string trusted(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER);
    for(std::size_t index=0;index<request.get_header_value_count(trusted);++index)proxy.push_back(request.get_header_value(trusted,"",index));
    if(evaluate_backend_http_request(request.method,request.path,request.remote_addr,request.get_header_value("Host"),origin,
        impl_->snapshot(),PrivilegedNetworkMode::enforced,proxy)!=BackendHttpGuardDecision::allowed)return false;
    const auto id_hash=PiManagementJournal::digest(id),csrf_hash=PiManagementJournal::digest(request.get_header_value("X-WsprryPi-Recovery-CSRF"));
    std::lock_guard lock(impl_->mutex);const auto value=impl_->read();const auto timestamp=now();
    for(const auto &lease:value["leases"]){
        const auto started=lease["created_utc"].get<std::uint64_t>();
        if(timestamp>=started&&timestamp-started<86400&&lease["host"]==request.get_header_value("Host")&&
            equal(id_hash,lease["cookie_sha256"].get<std::string>())&&(read||equal(csrf_hash,lease["csrf_sha256"].get<std::string>())))return true;
    }
    return false;
}
}
