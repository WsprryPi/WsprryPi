// SPDX-License-Identifier: MIT
#include "pi_pair_http_principals.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_local_routes.hpp"
#include "pi_management_journal.hpp"
#include "browser_http.hpp"
#include "backend_http_guard.hpp"
#include <charconv>
namespace wsprrypi {
namespace {
bool guarded(const httplib::Request &request,const SupportRequestGuardSnapshot &snapshot){
    if(request.path.size()>256||request.target!=request.path||request.get_header_value_count("Host")!=1||
        request.get_header_value_count("Origin")>1||request.get_header_value_count("Content-Type")>1||
        request.get_header_value_count("Content-Length")>1||request.has_header("Transfer-Encoding")||
        request.has_header("Content-Encoding"))return false;
    std::size_t size=0;for(const auto &[name,value]:request.headers)size+=name.size()+value.size()+4;
    if(size>8192)return false;
    if(request.has_header("Content-Length")){
        const auto text=request.get_header_value("Content-Length");std::uint64_t count{};
        const auto value=std::from_chars(text.data(),text.data()+text.size(),count);
        if(value.ec!=std::errc{}||value.ptr!=text.data()+text.size()||count!=request.body.size())return false;
    }
    if(request.method=="GET"&&(!request.body.empty()||request.has_header("Transfer-Encoding")))return false;
    std::vector<std::string> proxies;const std::string proxy_header(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER);
    for(std::size_t i=0;i<request.get_header_value_count(proxy_header);++i)proxies.push_back(request.get_header_value(proxy_header,"",i));
    const auto origin=request.has_header("Origin")?std::optional(request.get_header_value("Origin")):std::nullopt;
    // The bootstrap namespace is independent of browser privilege classes; it
    // always gets the actual same enforced LAN/Host/Origin/proxy-client guard.
    return SupportRequestGuard(snapshot).evaluate(request.remote_addr,request.get_header_value("Host"),origin,true,proxies).allowed();
}
}
PiPairBootstrapPrincipal::PiPairBootstrapPrincipal(const BrowserRequest &request)
    :method_(request.method),path_(request.path),digest_(PiManagementJournal::digest(request.body)){}
bool PiPairBootstrapPrincipal::matches(const BrowserRequest &request) const {
    return request.method==method_&&request.path==path_&&PiManagementJournal::digest(request.body)==digest_;
}
LocalPairingReadPrincipal::LocalPairingReadPrincipal(const BrowserRequest &request):path_(request.path){}
bool LocalPairingReadPrincipal::matches(const BrowserRequest &request) const{return request.method=="GET"&&request.path==path_&&request.body.empty();}
std::optional<PiPairBootstrapPrincipal> authorize_pi_pair_bootstrap(const httplib::Request &request,const SupportRequestGuardSnapshot &snapshot){
    constexpr std::string_view prefix="/api/pair/v1/";
    if(!request.path.starts_with(prefix)||request.body.size()>4096||!guarded(request,snapshot))return {};
    const auto resource=std::string_view(request.path).substr(prefix.size());
    if(!pi_pair_route(PiPairRoutePurpose::Bootstrap,request.method,resource))return {};
    const auto parsed=wtp_browser_http_request(request);if(!parsed)return {};
    if(request.method=="POST"){
        if(request.get_header_value("Content-Type")!="application/json")return {};
        const auto family=resource=="exchange"?std::string_view("exchange-request"):resource;
        try{(void)parse_pi_pair_document(request.body,family);}catch(...){return {};}
    }
    return PiPairBootstrapPrincipal(*parsed);
}
std::optional<LocalPairingReadPrincipal> authorize_local_pairing_read(const httplib::Request &request,const SupportRequestGuardSnapshot &snapshot){
    if(request.method!="GET"||!local_pairing_read_path(request.path)||!guarded(request,snapshot))return {};
    const auto parsed=wtp_browser_http_request(request);if(!parsed)return {};return LocalPairingReadPrincipal(*parsed);
}
} // namespace wsprrypi
