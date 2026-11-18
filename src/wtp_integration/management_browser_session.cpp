// SPDX-License-Identifier: MIT
#include "management_browser_session.hpp"
#include "browser_http.hpp"
#include "backend_http_guard.hpp"
#include <charconv>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <chrono>
#include <optional>
namespace wsprrypi {
namespace {
constexpr auto cookie="WSPRRYPI_MANAGEMENT_SESSION";
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string random(unsigned count) {
    unsigned char bytes[32];if(count>sizeof(bytes)||RAND_bytes(bytes,count)!=1)throw std::runtime_error("management_entropy_unavailable");
    std::string value;for(unsigned i=0;i<count;++i){value+="0123456789abcdef"[bytes[i]>>4];value+="0123456789abcdef"[bytes[i]&15];}return value;
}
std::optional<std::string> session_id(const httplib::Request &request) {
    if(request.get_header_value_count("Cookie")>1)return {};
    const auto text=request.get_header_value("Cookie");std::string result;bool found=false;std::size_t start=0;
    while(start<text.size()) {
        const auto end=text.find(';',start);auto item=text.substr(start,end==std::string::npos?end:end-start);
        const auto first=item.find_first_not_of(' ');if(first!=std::string::npos)item.erase(0,first);
        const auto equal=item.find('=');
        if(equal!=std::string::npos&&item.substr(0,equal)==cookie) {
            if(found)return {};
            found=true;result=item.substr(equal+1);
            if(result.size()!=32||result.find_first_not_of("0123456789abcdef")!=std::string::npos)return {};
        }
        if(end==std::string::npos)break;
        start=end+1;
    }
    return result;
}
bool headers(const httplib::Request &request) {
    for(const auto *name:{"Host","Origin","Cookie","Content-Type","X-WsprryPico-Request","X-WsprryPi-CSRF"})
        if(request.get_header_value_count(name)>1)return false;
    const auto parsed=wtp_browser_http_request(request);
    return parsed&&parsed->mutation_context;
}
}
bool ManagementBrowserSessions::establish(const httplib::Request &request,httplib::Response &response) {
    if(request.method!="POST"||!headers(request))return false;
    const auto selected=session_id(request);if(!selected)return false;
    const auto current=now();std::lock_guard lock(mutex_);
    for(auto it=sessions_.begin();it!=sessions_.end();){
        if(current<it->second.seen||current-it->second.seen>=1800000||current-it->second.started>=28800000){
            it->second.alive->store(false);it=sessions_.erase(it);
        }else ++it;
    }
    auto found=sessions_.find(*selected);std::string id=*selected;
    if(found==sessions_.end()||found->second.host!=request.get_header_value("Host")) {
        if(sessions_.size()>=64)return false;
        id=random(16);found=sessions_.emplace(id,Session{request.get_header_value("Host"),random(32),current,current}).first;
    }
    found->second.seen=current;
    // Origin has already passed the exact same-origin guard. A bootstrap POST
    // supplies its scheme even behind the existing HTTP reverse proxy.
    const bool secure=request.get_header_value("Origin").starts_with("https://");
    response.set_header("Set-Cookie",std::string(cookie)+"="+id+"; Path=/; HttpOnly; SameSite=Strict"+(secure?"; Secure":""));
    response.set_header("X-WsprryPi-CSRF",found->second.token);return true;
}
bool ManagementBrowserSessions::permits(const httplib::Request &request) {
    if(!headers(request))return false;
    const auto selected=session_id(request);if(!selected||selected->empty())return false;
    const auto token=request.get_header_value("X-WsprryPi-CSRF");if(token.size()!=64)return false;
    const auto current=now();std::lock_guard lock(mutex_);const auto found=sessions_.find(*selected);
    if(found==sessions_.end())return false;
    if(current<found->second.seen||current-found->second.seen>=1800000||current-found->second.started>=28800000){
        found->second.alive->store(false);return false;
    }
    if(!found->second.alive->load()||found->second.host!=request.get_header_value("Host")||
        CRYPTO_memcmp(token.data(),found->second.token.data(),64))return false;
    found->second.seen=current;return true;
}
std::optional<LocalBrowserPrincipal> ManagementBrowserSessions::authorize_local_mutation(
    const httplib::Request &request,const SupportRequestGuardSnapshot &snapshot){
    if((request.method!="POST"&&request.method!="PUT"&&request.method!="DELETE")||
        classify_privileged_http_operation(request.method,request.path)!=PrivilegedOperationClass::protected_operation||
        request.path.size()>256||request.get_header_value_count("Content-Length")>1||
        request.get_header_value_count("Transfer-Encoding")>1||
        (request.has_header("Content-Length")&&request.has_header("Transfer-Encoding")))return {};
    std::size_t header_bytes=0;for(const auto &[name,value]:request.headers)header_bytes+=name.size()+value.size()+4;
    if(header_bytes>8192)return {};
    if(request.path.starts_with("/api/control/v1/host/pairing")&&
        (request.body.size()>4096||request.has_header("Content-Encoding")||request.has_header("Transfer-Encoding")))return {};
    if(request.has_header("Content-Length")){
        const auto text=request.get_header_value("Content-Length");std::uint64_t length{};
        const auto value=std::from_chars(text.data(),text.data()+text.size(),length);
        if(value.ec!=std::errc{}||value.ptr!=text.data()+text.size()||length!=request.body.size())return {};
    }
    const auto parsed=wtp_browser_http_request(request);if(!parsed||!parsed->mutation_context)return {};
    std::vector<std::string> proxy;
    const std::string proxy_header(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER);
    for(std::size_t i=0;i<request.get_header_value_count(proxy_header);++i)proxy.push_back(request.get_header_value(proxy_header,"",i));
    const auto origin=request.has_header("Origin")?std::optional(request.get_header_value("Origin")):std::nullopt;
    if(evaluate_backend_http_request(request.method,request.path,request.remote_addr,request.get_header_value("Host"),
        origin,snapshot,PrivilegedNetworkMode::enforced,proxy)!=BackendHttpGuardDecision::allowed||!permits(request))return {};
    const auto scope=session_id(request);if(!scope||scope->empty())return {};
    std::lock_guard lock(mutex_);const auto found=sessions_.find(*scope);
    if(found==sessions_.end()||!found->second.alive->load())return {};
    return LocalBrowserPrincipal(*parsed,*scope,found->second.alive);
}
ManagementBrowserSessions &management_browser_sessions(){
    static ManagementBrowserSessions sessions;
    return sessions;
}
void ManagementBrowserSessions::retire(){
    std::lock_guard lock(mutex_);for(const auto &[id,session]:sessions_){(void)id;session.alive->store(false);}sessions_.clear();
}
bool ManagementBrowserSessions::owns(const LocalBrowserPrincipal &principal){
    std::lock_guard lock(mutex_);const auto found=sessions_.find(principal.session_);
    const auto lease=principal.lease_.lock();
    return found!=sessions_.end()&&lease&&lease==found->second.alive&&lease->load()&&
        std::chrono::steady_clock::now()<principal.deadline_;
}
bool ManagementBrowserSessions::within_rate(const httplib::Request &request) {
    const auto current=now();std::lock_guard lock(mutex_);
    for(auto it=rates_.begin();it!=rates_.end();)it=current>=it->second.seen&&current-it->second.seen>60000?rates_.erase(it):std::next(it);
    const auto key=request.remote_addr;
    auto found=rates_.find(key);
    if(found==rates_.end()) {
        if(rates_.size()>=256)return false;
        found=rates_.emplace(key,Rate{current,20000}).first;
    }
    auto &rate=found->second;if(current<rate.seen)return false;
    const auto elapsed=current-rate.seen;
    rate.credit=elapsed>=2000?20000:std::min<std::uint64_t>(20000,rate.credit+elapsed*10);
    rate.seen=current;if(rate.credit<1000)return false;rate.credit-=1000;return true;
}
} // namespace wsprrypi
