#include "pi_managed_host.hpp"
// SPDX-License-Identifier: MIT
#include "pi_pair_local_service.hpp"
#include "pi_pair_local_routes.hpp"
#include "browser_http.hpp"
#include "controller_contract.hpp"
#include "privileged_network_runtime.hpp"
#include "backend_http_guard.hpp"
namespace wsprrypi {
namespace {
PicoHttpResponse refused(const httplib::Request &request,unsigned status,const char *code){
    if(!request.path.starts_with("/api/pair/"))return controller_target_error(status,code);
    const char *pair_code=status==400?"invalid_shape":status==403?"invalid_proof":"pairing_closed";
    return {status,nlohmann::json{{"contract","Wsprry-Pair/1"},{"schema","result"},{"pair_id",nullptr},
        {"state","rejected"},{"code",pair_code},{"retryable",false}}.dump(),{}};
}
bool enforced(){const auto policy=privileged_network_runtime_state();return policy.active_known&&
    policy.active==PrivilegedNetworkMode::enforced&&policy.configured==PrivilegedNetworkMode::enforced;}
}
PicoHttpResponse pi_pair_local_unavailable(const httplib::Request &request){return refused(request,503,"authority_pending");}
PiPairLocalService::PiPairLocalService(std::shared_ptr<PiPairHistory> history,ManagementBrowserSessions &sessions,
    PiPairAdmission &admission,PiPairDiscovery &discovery,std::string self,std::string boot,std::shared_ptr<PiPairRemoteController> remote)
    :history_(std::move(history)),sessions_(sessions),discovery_(discovery),boot_(std::move(boot)),
    lifetime_(history_,admission,sessions),window_(*history_,sessions,self,boot_,[this](const auto &approval){return history_->commit_pair(approval);}),
    connect_(*history_,sessions,discovery,admission,self,boot_),renewal_(*history_,sessions,admission,self),
    remote_(std::move(remote)),maintenance_(*history_,window_,connect_,renewal_,std::move(self),remote_?
        PiPairMaintenance::Observe{[this](const auto &profile,const auto &cancelled){return remote_->maintain(profile,cancelled);}}:PiPairMaintenance::Observe{}){
    if(remote_)remote_->receipt_eviction_observer([this]{sessions_.retire();});
    if(remote_)remote_->authorization_observer([this](const auto &profile,const auto &principal,const auto &context){
        (void)renewal_.target_requires(profile,principal,context);
    });
}
PiPairLocalService::PiPairLocalService(std::shared_ptr<PiPairHistory> history,ManagementBrowserSessions &sessions,
    PiPairAdmission &admission,PiPairDiscovery &discovery,std::string self,std::string boot,LoopbackTest test,std::shared_ptr<PiPairRemoteController> remote)
    :history_(std::move(history)),sessions_(sessions),discovery_(discovery),boot_(std::move(boot)),
    lifetime_(history_,admission,sessions),window_(*history_,sessions,self,boot_,[this](const auto &approval){return history_->commit_pair(approval);}),
    connect_(*history_,sessions,discovery,admission,self,boot_,test.endpoints),
    renewal_(*history_,sessions,admission,self,PiPairRenewal::LoopbackTest{test.endpoints.management}),
    remote_(std::move(remote)),maintenance_(*history_,window_,connect_,renewal_,std::move(self),remote_?
        PiPairMaintenance::Observe{[this](const auto &profile,const auto &cancelled){return remote_->maintain(profile,cancelled);}}:PiPairMaintenance::Observe{}){
    if(remote_)remote_->receipt_eviction_observer([this]{sessions_.retire();});
    if(remote_)remote_->authorization_observer([this](const auto &profile,const auto &principal,const auto &context){
        (void)renewal_.target_requires(profile,principal,context);
    });
}
PiPairLocalService::~PiPairLocalService(){
    // Destruction cannot attest that the process's listener workers joined.
    request_stop();maintenance_.stop();if(remote_){remote_->authorization_observer({});remote_->receipt_eviction_observer({});}
}
bool PiPairLocalService::begin(){
    std::lock_guard owner(owner_);
    if(begun_||!enforced())return false;
    begun_=history_->available()?lifetime_.open():lifetime_.open_for_recovery();return begun_;
}
bool PiPairLocalService::transport_ready(){
    std::lock_guard owner(owner_);
    if(!begun_||!enforced())return false;
    auto call=lifetime_.enter();if(!call)return false;
    if(history_->available()&&!maintaining_){
        if(!lifetime_.ensure_history()||!maintenance_.start())return false;
        maintaining_=true;
    }
    transport_.store(true);return true;
}
void PiPairLocalService::request_stop() noexcept{
    transport_.store(false);maintenance_.request_stop();lifetime_.close();window_.cancel();
}
void PiPairLocalService::stop_and_drain(){
    std::lock_guard owner(owner_);request_stop();maintenance_.stop();
    lifetime_.drain();maintaining_=false;begun_=false;
}
PicoHttpResponse PiPairLocalService::native(const BrowserRequest &request,const LocalBrowserPrincipal &principal,PiManagedHost &host){
    auto call=lifetime_.enter();
    if(!call||!enforced()||!transport_.load()||!principal.matches(request))return controller_target_error(403,"unauthorized");
    return host.management(request,principal,*history_);
}
std::optional<PiPairHttpReply> PiPairLocalService::remote(const httplib::Request &request,const SupportRequestGuardSnapshot &guard){
    const auto fail=[](unsigned status,const char *code){return PiPairHttpReply{controller_target_error(status,code),{},{}};};
    if(!remote_)return {};
    try{
        const auto parsed=wtp_browser_http_request(request);if(!parsed||request.target!=request.path||request.body.size()>32768)return fail(400,"invalid_shape");
        for(const auto *header:{"Host","Origin","Content-Length","Content-Type","X-WsprryPi-Profile-ID","X-WsprryPi-Target-Context","X-WsprryPi-Operation-ID"})
            if(request.get_header_value_count(header)>1)return fail(400,"invalid_shape");
        if(request.has_header("Transfer-Encoding")||request.has_header("Content-Encoding"))return fail(400,"invalid_shape");
        std::size_t bytes=0;for(const auto &[name,value]:request.headers)bytes+=name.size()+value.size()+4;
        if(bytes>8192||request.path.size()>256)return fail(431,"payload_too_large");
        std::vector<std::string> proxies;const std::string proxy_header(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER);
        for(std::size_t i=0;i<request.get_header_value_count(proxy_header);++i)proxies.push_back(request.get_header_value(proxy_header,"",i));
        const auto origin=request.has_header("Origin")?std::optional(request.get_header_value("Origin")):std::nullopt;
        if(evaluate_backend_http_request(request.method,request.path,request.remote_addr,request.get_header_value("Host"),origin,guard,
            PrivilegedNetworkMode::enforced,proxies)!=BackendHttpGuardDecision::allowed)return fail(403,"unauthorized");
        std::optional<LocalBrowserPrincipal> browser_principal;
        if(request.method!="GET"){
            browser_principal=sessions_.authorize_local_mutation(request,guard);
            if(!browser_principal)return fail(403,"csrf_failed");
        }
        const bool inventory=request.path=="/api/control/v1/targets";
        constexpr std::string_view prefix="/api/control/v1/targets/";
        std::optional<std::string> profile;
        if(!inventory){
            if(!request.path.starts_with(prefix)||request.path.size()<prefix.size()+34)return {};
            profile=remote_->resolve(request.path.substr(prefix.size(),32),request.get_header_value("X-WsprryPi-Profile-ID"));
            if(!profile)return {};
        }
        auto call=lifetime_.enter();
        if(!call||!transport_.load()||!enforced()){
            // Pairing/listener availability never disables independent local
            // management. Known remote identities still refuse here.
            if(inventory)return {};
            return fail(503,"authority_pending");
        }
        const auto cancelled=[&]{return call->cancelled()||!transport_.load()||!enforced()||(browser_principal&&!sessions_.owns(*browser_principal));};
        if(inventory)return PiPairHttpReply{remote_->targets(),{},{}};
        if(request.method=="GET")return remote_->read(*parsed,*profile,cancelled,request.get_header_value("X-WsprryPi-Target-Context"));
        // Automatic key maintenance is a separate bounded protocol step, never
        // a settings retry. Preserve the draft and require fresh context when
        // a rotation is pending or has retired this browser's session.
        if(renewal_.advance(*profile,cancelled)!=PiPairRenewal::Result::Ready||cancelled())return fail(503,"transport_unavailable");
        if(request.path.ends_with("/review"))return remote_->review(*parsed,*profile,cancelled);
        if(request.path.substr(prefix.size()+32).starts_with("/pico/"))return remote_->write_pico(*parsed,*profile,
            request.get_header_value("X-WsprryPi-Target-Context"),request.get_header_value("X-WsprryPi-Operation-ID"),cancelled);
        if(request.has_header("X-WsprryPi-Target-Context")||request.has_header("X-WsprryPi-Operation-ID"))return fail(400,"invalid_shape");
        return remote_->write(*parsed,*profile,cancelled);
    }catch(...){return fail(503,"authority_pending");}
}
std::optional<PicoHttpResponse> PiPairLocalService::handle(const httplib::Request &request,const SupportRequestGuardSnapshot &guard){
    constexpr std::string_view local="/api/control/v1/host/pairing";
    const bool bootstrap=request.path.starts_with("/api/pair/v1/");
    if(!bootstrap&&request.path!=local&&!request.path.starts_with(std::string(local)+"/"))return {};
    auto call=lifetime_.enter();
    if(!call||!enforced())return refused(request,503,"authority_pending");
    const auto parsed=wtp_browser_http_request(request);if(!parsed)return refused(request,400,"invalid_shape");
    if(bootstrap){
        if(!transport_.load())return refused(request,503,"authority_pending");
        const auto principal=authorize_pi_pair_bootstrap(request,guard);
        if(!principal)return refused(request,403,"unauthorized");
        return window_.bootstrap(*parsed,*principal);
    }
    if(request.method=="GET"&&local_pairing_read_path(request.path)){
        const auto principal=authorize_local_pairing_read(request,guard);
        if(!principal)return refused(request,403,"unauthorized");
        if(request.path==local)return window_.state(*parsed,*principal);
        if(local_pairing_operation_id(request.path))return history_->local_pairing_receipt(*parsed,*principal);
        return PicoHttpResponse{200,discovery_.snapshot().dump(),{}};
    }
    if(request.method!="POST"||(request.path!=std::string(local)+"/window"&&request.path!=std::string(local)+"/connect"&&request.path!=std::string(local)+"/remove"))
        return controller_target_error(404,"not_found");
    const auto principal=sessions_.authorize_local_mutation(request,guard);
    if(!principal)return controller_target_error(403,"csrf_failed");
    if(request.path==std::string(local)+"/remove"){
        auto result=history_->remove_access(*parsed,*principal,sessions_,boot_);
        if(result.status==200)window_.cancel();
        return result;
    }
    if(!transport_.load())return refused(request,503,"authority_pending");
    if(request.path==std::string(local)+"/window"){
        auto response=window_.open(*parsed,*principal);
        if(response.status==200&&(!lifetime_.ensure_history()||call->cancelled())){
            window_.cancel();return refused(request,503,"authority_pending");
        }
        return response;
    }
    if(!lifetime_.ensure_history())return controller_target_error(503,"storage_unavailable");
    return connect_.connect(*parsed,*principal,[&]{return call->cancelled()||!transport_.load()||!enforced();});
}
}
