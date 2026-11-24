// SPDX-License-Identifier: MIT
#include "http_domain.hpp"
#include "backend_capabilities.hpp"
#include "http_contract.hpp"
#include "backend_services.hpp"
#include "local_transport.hpp"
#include "application_recovery/maintenance_service.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "config_handler.hpp"
#include "application_storage/application_state.hpp"
#include "privileged_network_admin.hpp"
#include "privileged_network_managed_ini.hpp"
#include "rp1_gpclk_route_service.hpp"
#include "support_bundle_http.hpp"
#include "support_bundle_intake_production.hpp"
#include "web_server_routes.hpp"
#include <algorithm>
#include "wtp_integration/native_socket.hpp"
#include <cstring>
#include <mutex>
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
bool token(const std::string &value){return !value.empty()&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'*+-.^_`|~")==std::string::npos;}
void validate(const BrowserHttpInput &input){
    const auto operation=browser_http_operation(input.method,input.path);
    require(operation&&operation->name==input.operation&&input.body.size()<=operation->maximum_body);
    require(input.target.size()<=4096&&!input.target.empty()&&input.target.front()=='/'&&
        input.target.find_first_of("\r\n\0",0,3)==std::string::npos&&input.target.find(' ') == std::string::npos);
    in_addr v4{};in6_addr v6{};require(inet_pton(AF_INET,input.peer.c_str(),&v4)==1||inet_pton(AF_INET6,input.peer.c_str(),&v6)==1);
    std::size_t bytes=0,hosts=0,lengths=0;
    require(input.headers.size()<=64);
    for(const auto &[name,value]:input.headers){
        bytes+=name.size()+value.size()+4;require(token(name)&&value.find_first_of("\r\n\0",0,3)==std::string::npos);
        auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(lower=="host")++hosts;
        if(lower=="content-length"){++lengths;require(value==std::to_string(input.body.size()));}
        require(lower!="transfer-encoding"&&lower!="expect"&&lower!="content-encoding");
    }
    require(bytes<=8192&&hosts==1&&lengths<=1);
}
class DomainStream final:public httplib::Stream {
public:
    explicit DomainStream(const BrowserHttpInput &input):descriptor_(input.transport_descriptor),alive_(input.transport_alive),peer_(input.peer){
        request_=input.method+" "+input.target+" HTTP/1.1\r\n";
        auto headers=input.headers;headers.erase("Content-Length");headers.erase("Connection");
        headers.emplace("Content-Length",std::to_string(input.body.size()));headers.emplace("Connection","close");
        for(const auto &[name,value]:headers)request_+=name+": "+value+"\r\n";
        request_+="\r\n";request_+=input.body;
    }
    bool is_readable()const override{return offset_<request_.size();}
    bool wait_readable()const override{return is_readable();}
    bool wait_writable()const override{return true;}
    ssize_t read(char *output,std::size_t length)override{
        const auto count=std::min(length,request_.size()-offset_);std::memcpy(output,request_.data()+offset_,count);offset_+=count;return static_cast<ssize_t>(count);
    }
    ssize_t write(const char *,std::size_t length)override{discarded_+=length;require(discarded_<=8192);return static_cast<ssize_t>(length);}
    void get_remote_ip_and_port(std::string &ip,int &port)const override{ip=peer_;port=0;}
    void get_local_ip_and_port(std::string &ip,int &port)const override{ip="127.0.0.1";port=0;}
    socket_t socket()const override{
#ifdef _WIN32
        return INVALID_SOCKET; // A named-pipe handle is never a Winsock socket.
#else
        return descriptor_;
#endif
    }
    std::function<bool()> connection_closed_handler()const override{
        if(alive_)return [alive=alive_]{return !alive();};
        return httplib::Stream::connection_closed_handler();
    }
    time_t duration()const override{return 0;}
private:
    LocalTransportDescriptor descriptor_;std::function<bool()> alive_;std::string peer_,request_;std::size_t offset_{},discarded_{};
};
class DomainRouter final:public ApplicationHttpRouter {
public:
    DomainRouter():
#if !WSPRRYPI_CONTROLLER_ONLY
        jobs_(backend_support_jobs()),
#endif
        network_(PrivilegedNetworkAdminPaths{application_configuration_namespace()},run_privileged_network_command,
        PrivilegedNetworkIniIO{},managed_network_settings_io()){
        set_payload_max_length(1024*1024);
        const auto snapshot=[] {return SupportRequestGuard::discover_local_networks();};
        const web_server_routes::CorsHeaderSetter cors=[](auto &response){
            response.headers.erase("Access-Control-Allow-Origin");
            response.set_header("Access-Control-Allow-Methods","GET, POST, PUT, PATCH, DELETE, OPTIONS");
            response.set_header("Access-Control-Allow-Headers","Content-Type, Accept");
        };
#if !WSPRRYPI_CONTROLLER_ONLY
        register_support_bundle_http_routes(*this,*jobs_,snapshot,resolve_support_bundle_intake_production);
#endif
        web_server_routes::register_request_guard(*this,snapshot);
        web_server_routes::register_config(*this,cors);
        web_server_routes::register_pairing(*this,backend_pairing_service,snapshot);
        web_server_routes::register_control(*this,cors,backend_pairing_service,snapshot);
        web_server_routes::register_privileged_admin(*this,network_,productionRp1GpclkRouteService());
        web_server_routes::register_version(*this,cors);
        Post("/api/recovery/session",[snapshot](const auto &request,auto &response){
            response.set_header("Cache-Control","no-store");
            if(!management_browser_sessions().authorize_local_mutation(request,snapshot())){
                response.status=403;response.set_content(R"({"error":"csrf_failed"})","application/json");return;
            }
            try{
                require(request.body=="{}");
                const auto reply=local_control_request(maintenance_control_options(),{LocalOperation::RecoverySessionIssue,
                    {{"host",request.get_header_value("Host")},{"secure",request.get_header_value("Origin").starts_with("https://")}}});
                require(reply.at("ok")==true);const auto lease=reply.at("value");
                require(lease.is_object()&&lease.size()==3&&lease.value("schema",nlohmann::json(nullptr))=="recovery-browser-admission/1"&&
                    lease.contains("cookie")&&lease["cookie"].is_string()&&lease.contains("csrf")&&lease["csrf"].is_string());
                response.set_header("Set-Cookie",lease["cookie"].template get<std::string>());
                response.set_content(nlohmann::json{{"schema","recovery-browser-admission/1"},{"csrf",lease["csrf"]}}.dump(),"application/json");
            }catch(...){response.status=503;response.set_content(R"({"error":"maintenance_unavailable"})","application/json");}
        });
        initialize_domain_capture();
    }
private:
    std::shared_ptr<SupportBundleJobManager> jobs_;
    PrivilegedNetworkAdmin network_;
};

struct DomainOwner {std::mutex mutex;std::shared_ptr<DomainRouter> router;bool stopped{};};
DomainOwner &owner(){static DomainOwner value;return value;}
}
ApplicationHttpRouter::ApplicationHttpRouter(){set_payload_max_length(1024*1024);}
thread_local std::unique_ptr<httplib::Response> *ApplicationHttpRouter::capture_{};
void ApplicationHttpRouter::initialize_domain_capture(){
        set_exception_handler([](const auto &,auto &,auto){throw LocalControlError(LocalControlError::Code::UnknownOutcome);});
        set_post_routing_handler([](const auto &,auto &response){
            require(capture_&&!*capture_);
            *capture_=std::make_unique<httplib::Response>(std::move(response));
            response.content_provider_resource_releaser_=nullptr;
            response=httplib::Response{};response.status=204;response.set_header("Content-Length","0");
        });

}
std::unique_ptr<BrowserHttpResource> ApplicationHttpRouter::dispatch(BrowserHttpInput input){
        validate(input);require(!capture_);std::unique_ptr<httplib::Response> response;
        capture_=&response;struct Reset{~Reset(){capture_=nullptr;}}reset;
        DomainStream stream(input);bool closed=false;
        require(process_request(stream,input.peer,0,"127.0.0.1",0,true,closed,[&](auto &request){
            require(request.path==input.path&&request.target==input.target);
            const auto operation=browser_http_operation(request.method,request.path);
            require(operation&&operation->name==input.operation);
            // The outer HTTP server applies its client's ranges once. The
            // backend retains the whole pinned resource and bounded reads.
            request.ranges.clear();
        }));
        require(bool(response));return std::make_unique<BrowserHttpResource>(std::move(response),browser_http_operation(input.method,input.path)->maximum_resource);
    }
BrowserHttpResource::BrowserHttpResource(std::unique_ptr<httplib::Response> response,std::uint64_t maximum):response_(std::move(response)){
    require(response_&&response_->status>=100&&response_->status<=599&&response_->file_content_path_.empty()&&!response_->is_chunked_content_provider_);
    size_=response_->content_provider_?response_->content_length_:response_->body.size();
    require((maximum==128ULL*1024*1024||maximum==2ULL*1024*1024*1024)&&size_<=maximum&&response_->body.size()<=1024*1024);
    response_->headers.erase("EXCEPTION_WHAT");
}
nlohmann::json BrowserHttpResource::metadata() const{
    require(response_&&!completed_);
    auto headers=nlohmann::json::array();std::size_t bytes=0;
    for(const auto &[name,value]:response_->headers){
        auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(lower=="connection"||lower=="keep-alive"||lower=="content-length"||lower=="transfer-encoding")continue;
        bytes+=name.size()+value.size()+4;require(bytes<=8192&&token(name)&&value.find_first_of("\r\n\0",0,3)==std::string::npos);
        headers.push_back({name,value});
    }
    return {{"schema","browser-http-response/1"},{"status",response_->status},{"headers",headers},{"size",std::to_string(size_)}};
}
std::string BrowserHttpResource::read(std::uint64_t offset,std::size_t maximum){
    require(response_&&!completed_&&maximum&&maximum<=64*1024&&offset<=size_);
    const auto length=static_cast<std::size_t>(std::min<std::uint64_t>(maximum,size_-offset));
    if(!length)return {};
    std::string output;output.reserve(length);
    if(response_->content_provider_){
        httplib::DataSink sink;
        sink.is_writable=[] {return true;};sink.done=[] {};sink.done_with_trailer=[](const auto &){};
        sink.write=[&](const char *data,std::size_t size){require(size<=length-output.size());output.append(data,size);return true;};
        require(response_->content_provider_(offset,length,sink)&&!output.empty());
    }else output=response_->body.substr(static_cast<std::size_t>(offset),length);
    if(offset==contiguous_)contiguous_+=output.size();
    return output;
}
void BrowserHttpResource::complete(bool delivered){
    require(!completed_);completed_=true;
    response_->content_provider_success_=delivered&&contiguous_==size_;
    response_.reset();
}
std::unique_ptr<BrowserHttpResource> dispatch_backend_http(BrowserHttpInput input){
    std::shared_ptr<DomainRouter> router;auto &slot=owner();
    {std::lock_guard lock(slot.mutex);require(!slot.stopped);if(!slot.router)slot.router=std::make_shared<DomainRouter>();router=slot.router;}
    return router->dispatch(std::move(input));
}
void stop_backend_http_domain() noexcept{
    auto &slot=owner();std::lock_guard lock(slot.mutex);slot.stopped=true;slot.router.reset();
}
}
