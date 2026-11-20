// SPDX-License-Identifier: MIT
#include "http_contract.hpp"
namespace wsprrypi {
std::optional<BrowserHttpOperation> browser_http_operation(std::string_view method,std::string_view path){
    const bool read=method=="GET"||method=="HEAD";
    if(method=="OPTIONS"){
        if(browser_http_operation("GET",path)||browser_http_operation("POST",path)||browser_http_operation("PUT",path))
            return BrowserHttpOperation{"preflight",0};
        return {};
    }
    if(path=="/"&&read)return BrowserHttpOperation{"home.read",32768};
    if(path=="/version"&&read)return BrowserHttpOperation{"version.read",32768};
    if(path=="/status"&&read)return BrowserHttpOperation{"status.read",32768};
    if(path=="/config"){
        if(read)return BrowserHttpOperation{"configuration.read",32768};
        if(method=="PUT"||method=="PATCH")return BrowserHttpOperation{"configuration.write",1024*1024};
    }
    if(path=="/config/si5351-addresses"&&read)return BrowserHttpOperation{"configuration.addresses",32768};
    if(path=="/config/repair"&&method=="POST")return BrowserHttpOperation{"configuration.repair",1024*1024};
    if(path=="/control/stop"&&method=="POST")return BrowserHttpOperation{"control.stop",8192};
    if(path=="/api/network-safety"&&(read||method=="POST"))return BrowserHttpOperation{read?"network.read":"network.write",read?32768u:8192u};
    if(path=="/api/rp1-gpclk-route"&&(read||method=="POST"))return BrowserHttpOperation{read?"route.read":"route.write",read?32768u:8192u};
    if(path=="/api/wtp"&&read)return BrowserHttpOperation{"device.read",32768};
    if(path=="/api/wtp/recover"&&method=="POST")return BrowserHttpOperation{"device.recover",8192};
    if(path.starts_with("/api/control/v1/")&&(read||method=="POST"||method=="PUT"||method=="DELETE"))
        return BrowserHttpOperation{"management.request",32768u};
    if((path=="/api/pair/v1/window"&&read)||
        ((path=="/api/pair/v1/hello"||path=="/api/pair/v1/exchange"||path=="/api/pair/v1/confirm"||path=="/api/pair/v1/commit"||
          path=="/api/pair/v1/offer"||path=="/api/pair/v1/pair"||path=="/api/pair/v1/replace"||path=="/api/pair/v1/remove"||path=="/api/pair/v1/rotate/prepare")&&method=="POST"))
        return BrowserHttpOperation{"pairing.request",4096u};
    if(path=="/api/recovery/session"&&method=="POST")return BrowserHttpOperation{"recovery.session",8192};
    if(path=="/api/recovery/review"&&(read||method=="POST"))return BrowserHttpOperation{"recovery.review",read?32768u:8192u};
    if(path=="/api/recovery/jobs"&&(read||method=="POST"))return BrowserHttpOperation{"recovery.jobs",read?32768u:8192u};
    constexpr std::string_view recovery_prefix="/api/recovery/jobs/";
    if(path.starts_with(recovery_prefix)){
        const auto tail=path.substr(recovery_prefix.size());
        if(tail.size()<32||tail.substr(0,32).find_first_not_of("0123456789abcdef")!=std::string_view::npos)return {};
        const auto action=tail.substr(32);
        if(action=="/download"&&read)return BrowserHttpOperation{"recovery.download",32768,2ULL*1024*1024*1024};
        if(action.empty()&&read)return BrowserHttpOperation{"recovery.status",32768};
        if(method=="POST"&&(action=="/chunk"||action=="/finish"||action=="/validate"||action=="/confirm"||action=="/resume"||action=="/cancel"))
            return BrowserHttpOperation{"recovery.mutate",action=="/chunk"?128*1024u:8192u};
    }
    if(path=="/api/support-intake"&&read)return BrowserHttpOperation{"support.request",32768};
    if(path=="/api/support-bundles"&&(read||method=="POST"))return BrowserHttpOperation{"support.request",read?32768u:8192u};
    constexpr std::string_view prefix="/api/support-bundles/";
    if(path.starts_with(prefix)){
        const auto tail=path.substr(prefix.size());
        if(tail.size()<32||tail.substr(0,32).find_first_not_of("0123456789abcdef")!=std::string_view::npos)return {};
        const auto action=tail.substr(32);
        if((action.empty()&&(read||method=="DELETE"))||
            ((action=="/download"||action=="/encrypted"||action=="/receipt"||action=="/handoff")&&read)||
            ((action=="/finalize"||action=="/encrypt"||action=="/upload-reported-complete")&&method=="POST"))
            return BrowserHttpOperation{"support.request",read?32768u:8192u};
    }
    return {};
}
}
