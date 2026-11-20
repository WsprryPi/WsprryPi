// SPDX-License-Identifier: MIT
#include "web_service.hpp"
#include "web_bootstrap.hpp"
#include "runtime_options.hpp"
#include "http_contract.hpp"
#include "wire_bytes.hpp"
#include "application_recovery/maintenance_service.hpp"
#include "web_socket.hpp"
#include "version.hpp"
#include "httplib.hpp"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
volatile std::sig_atomic_t stopped=0;
void stop_signal(int){stopped=1;}
void require(bool value){if(!value)throw LocalControlError(LocalControlError::Code::Protocol);}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto *name:names)if(!value.contains(name))return false;
    return true;
}
Json value(const Json &result){
    if(result.at("ok")!=true){
        if(result.value("error",std::string{})=="storage_unavailable")throw LocalControlError(LocalControlError::Code::Unavailable);
        if(result.value("error",std::string{})=="busy")throw LocalCommandRefused(LocalRefusal::Busy);
        throw LocalCommandRefused(LocalRefusal::InvalidRequest);
    }
    return result.at("value");
}
Json parsed_command(const std::string &bytes){
    require(bytes.size()<=65536);std::vector<std::set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        require(depth<=32);if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(item.get<std::string>()).second);
        else if(event==Json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
        return true;
    });
}
class RemoteHttpResource {
public:
    RemoteHttpResource(LocalTransportOptions options,const std::string &boot,const httplib::Request &request,const BrowserHttpOperation &operation)
        :connection_(options){
        require(boot.empty()||connection_.boot()==boot);auto headers=Json::array();
        for(const auto &[name,content]:request.headers){
            if(name=="REMOTE_ADDR"||name=="REMOTE_PORT"||name=="LOCAL_ADDR"||name=="LOCAL_PORT")continue;
            headers.push_back({name,content});
        }
        value(connection_.request({LocalOperation::HttpOpen,{{"operation",operation.name},{"method",request.method},{"path",request.path},
            {"target",request.target},{"peer",request.remote_addr},{"headers",headers},{"body_size",std::to_string(request.body.size())}}}));
        for(std::size_t offset=0;offset<request.body.size();){
            const auto bytes=request.body.substr(offset,std::min<std::size_t>(65536,request.body.size()-offset));
            const auto reply=value(connection_.request({LocalOperation::HttpBody,{{"offset",std::to_string(offset)},{"bytes",encode_wire_chunk(bytes)}}}));
            offset+=bytes.size();require(fields(reply,{"offset"})&&reply["offset"]==std::to_string(offset));
        }
        metadata_=value(connection_.request({LocalOperation::HttpDispatch,Json::object()}));
        require(fields(metadata_,{"schema","status","headers","size"})&&metadata_["schema"]=="browser-http-response/1"&&
            metadata_["status"].is_number_integer()&&metadata_["status"]>=100&&metadata_["status"]<=599&&metadata_["size"].is_string()&&metadata_["headers"].is_array());
        size_=wire_offset(metadata_["size"].get<std::string>());require(size_<=operation.maximum_resource&&metadata_["headers"].size()<=64);
    }
    const Json &metadata()const{return metadata_;}
    std::uint64_t size()const{return size_;}
    std::string read(std::uint64_t offset,std::size_t maximum){
        require(offset<=size_&&maximum&&maximum<=65536);
        const auto reply=value(connection_.request({LocalOperation::HttpRead,{{"offset",std::to_string(offset)},{"maximum",maximum}}}));
        require(fields(reply,{"offset","bytes"})&&reply["offset"]==std::to_string(offset)&&reply["bytes"].is_string());
        auto bytes=decode_wire_chunk(reply["bytes"].get<std::string>());require(bytes.size()<=maximum&&bytes.size()<=size_-offset);return bytes;
    }
    void complete(bool delivered)noexcept{
        try{value(connection_.request({LocalOperation::HttpComplete,{{"delivered",delivered}}}));}catch(...){/* No delivery replay. */}
    }
private:
    LocalControlConnection connection_;Json metadata_;std::uint64_t size_{};
};
class WebService {
public:
    explicit WebService(LocalTransportOptions options):options_(std::move(options)){}
    ~WebService(){stop();}
    void start(const Json &bootstrap){
        require(valid_web_bootstrap(bootstrap));bootstrap_=bootstrap;
        auto server=std::make_unique<httplib::Server>();
        server->set_payload_max_length(1024*1024);server->set_read_timeout(5,0);server->set_write_timeout(5,0);
        server->new_task_queue=[] {return new httplib::ThreadPool(4,8);};
        const auto handler=[this](const auto &request,auto &response){http(request,response);};
        server->Get(R"(/.*)",handler);server->Post(R"(/.*)",handler);server->Put(R"(/.*)",handler);
        server->Patch(R"(/.*)",handler);server->Delete(R"(/.*)",handler);server->Options(R"(/.*)",handler);
        websocket_=std::make_unique<WebSocketServer>();
        websocket_->set_backend_session_factory([this](const auto &upgrade,const auto &peer){return browser(upgrade,peer);});
        server_=std::move(server);
        try{
            if(bootstrap_["http_enabled"]==true){
                require(server_->bind_to_port(bootstrap_["http_loopback_only"]==true?"127.0.0.1":"0.0.0.0",bootstrap_["web_port"].get<int>()));
                http_thread_=std::thread([this]{server_->listen_after_bind();});
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                while(!server_->is_running()&&std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                require(server_->is_running());
            }
            if(bootstrap_["websocket_enabled"]==true){
                const auto family=bootstrap_["socket_loopback_family"]=="ipv4"?WebSocketLoopbackFamily::IPv4:
                    bootstrap_["socket_loopback_family"]=="ipv6"?WebSocketLoopbackFamily::IPv6:WebSocketLoopbackFamily::Auto;
                require(websocket_->start(bootstrap_["socket_port"].get<std::uint16_t>(),30,bootstrap_["socket_loopback_only"].get<bool>(),family));
            }
        }catch(...){stop();throw;}
    }
    void stop()noexcept{
        if(server_)server_->stop();
        if(websocket_)websocket_->stop();
        if(http_thread_.joinable())http_thread_.join();
        websocket_.reset();server_.reset();
    }
    void retain_maintenance_http(){
        maintenance_only_.store(true,std::memory_order_release);
        if(websocket_)websocket_->stop();
        websocket_.reset();
    }
    bool same_listeners(const Json &candidate)const{
        for(const auto *key:{"web_port","socket_port","http_enabled","websocket_enabled","http_loopback_only","socket_loopback_only","socket_loopback_family"})
            if(bootstrap_.at(key)!=candidate.at(key))return false;
        return true;
    }
    void context(const Json &bootstrap){std::lock_guard lock(context_mutex_);bootstrap_=bootstrap;}
    std::string boot(){std::lock_guard lock(context_mutex_);return bootstrap_.at("boot").get<std::string>();}
    void refresh(){if(websocket_)websocket_->refresh_backend_sessions();}
    void events(std::string &event_boot,std::uint64_t &cursor){
        auto options=options_;options.channel=LocalChannel::Events;
        LocalControlConnection connection(options);require(connection.boot()==boot());
        const auto result=value(connection.request({LocalOperation::EventsRead,{{"boot",event_boot},{"after",std::to_string(cursor)}}}));
        require(fields(result,{"schema","boot","sequence","resync_required","events"})&&result["schema"]=="local-events/1"&&result["boot"]==connection.boot()&&
            result["sequence"].is_string()&&result["resync_required"].is_boolean()&&result["events"].is_array()&&result["events"].size()<=1);
        const auto sequence=wire_offset(result["sequence"].get<std::string>());
        if(result["resync_required"]==true){
            require(result["events"].empty());event_boot=result["boot"].get<std::string>();cursor=sequence;
            if(websocket_)websocket_->resynchronize_backend_sessions();
            return;
        }
        require(event_boot==result["boot"].get<std::string>()&&sequence>=cursor);
        for(const auto &event:result["events"]){
            require(fields(event,{"sequence","message"})&&event["sequence"].is_string()&&event["message"].is_string()&&event["message"].get_ref<const std::string &>().size()<=65536);
            const auto number=wire_offset(event["sequence"].get<std::string>());require(number>cursor&&number==sequence);
            if(websocket_)websocket_->sendAllClients(event["message"].get<std::string>());
        }
        cursor=sequence;
    }
private:
    void http(const httplib::Request &request,httplib::Response &response){
        response.set_header("Cache-Control","no-store");
        const auto operation=browser_http_operation(request.method,request.path);
        if(!operation){response.status=404;response.set_content("Not found","text/plain");return;}
        if(maintenance_only_.load(std::memory_order_acquire)&&
            (!operation->name.starts_with("recovery.")||operation->name=="recovery.session")){
            response.status=410;response.set_content(R"({"error":"listener_moved","message":"Use the current WsprryPi address. This endpoint retains admitted recovery jobs only."})","application/json");return;
        }
        if(request.body.size()>operation->maximum_body){response.status=413;response.set_content("Request is too large","text/plain");return;}
        try{
            auto options=options_;options.channel=LocalChannel::Http;
            const bool maintenance=operation->name.starts_with("recovery.")&&operation->name!="recovery.session";
            if(maintenance){options=maintenance_control_options(true);options.channel=LocalChannel::Http;}
            auto resource=std::make_shared<RemoteHttpResource>(options,maintenance?std::string{}:boot(),request,*operation);
            response.status=resource->metadata()["status"].get<int>();
            // The backend deliberately captures the full pinned resource. This
            // outer listener alone applies a client range and its matching 206
            // length; retaining explicit 200 makes httplib send a partial body
            // with the full Content-Length.
            if(response.status==200&&resource->size()&&!request.ranges.empty())response.status=206;
            std::size_t header_bytes=0;std::string content_type="application/octet-stream";
            for(const auto &header:resource->metadata()["headers"]){
                require(header.is_array()&&header.size()==2&&header[0].is_string()&&header[1].is_string());
                const auto name=header[0].get<std::string>(),content=header[1].get<std::string>();header_bytes+=name.size()+content.size()+4;
                require(header_bytes<=8192&&name.find_first_of("\r\n\0",0,3)==std::string::npos&&content.find_first_of("\r\n\0",0,3)==std::string::npos);
                auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                if(lower=="content-type")content_type=content;
                else response.set_header(name,content);
            }
            if(!resource->size()){response.set_content("",content_type);resource->complete(false);return;}
            response.set_content_provider(static_cast<std::size_t>(resource->size()),content_type,
                [resource](std::size_t offset,std::size_t length,httplib::DataSink &sink){try{
                    const auto bytes=resource->read(offset,std::min<std::size_t>(65536,length));return !bytes.empty()&&sink.write(bytes.data(),bytes.size());
                }catch(...){return false;}},[resource](bool delivered){resource->complete(delivered);});
        }catch(const LocalCommandRefused &error){response.status=error.reason()==LocalRefusal::Busy?409:400;
            response.set_content(Json{{"error",error.reason()==LocalRefusal::Busy?"busy":"invalid_request"}}.dump(),"application/json");}
        catch(const LocalControlError &error){response.status=503;response.set_content(Json{{"error",error.code()==LocalControlError::Code::UnknownOutcome?"outcome_unknown":"backend_unavailable"},
            {"message",error.code()==LocalControlError::Code::UnknownOutcome?"The result is unknown. Read current state before retrying.":"The WsprryPi backend is unavailable."}}.dump(),"application/json");}
        catch(...){response.status=503;response.set_content(R"({"error":"backend_unavailable"})","application/json");}
    }
    std::shared_ptr<WebSocketBackendSession> browser(const std::string &upgrade,const std::string &peer){
        auto options=options_;options.channel=LocalChannel::Browser;
        auto connection=std::make_shared<LocalControlConnection>(options);require(connection->boot()==boot());
        const auto result=connection->request({LocalOperation::BrowserOpen,{{"upgrade",upgrade},{"peer",peer}}});
        if(result.at("ok")!=true)return {};
        const auto admission=result.at("value");
        require(fields(admission,{"schema","connected","key"})&&admission["schema"]=="browser-session/1"&&admission["connected"]==true&&admission["key"].is_string());
        auto session=std::make_shared<WebSocketBackendSession>();session->key=admission["key"].get<std::string>();
        session->current=[connection]{try{const auto result=connection->request({LocalOperation::BrowserCheck,Json::object()});
            return result.at("ok")==true&&fields(result.at("value"),{"connected"})&&result.at("value")["connected"]==true;}catch(...){return false;}};
        session->command=[connection](const std::string &message){
            Json command;bool attempted=false;
            const auto failure=[&](const char *code,const char *message){
                Json reply{{"status","error"},{"error",code},{"message",message}};
                if(command.is_object()){
                    if(command.contains("command")&&command["command"].is_string())reply["command"]=command["command"];
                    if(command.contains("client_request_id")&&command["client_request_id"].is_string()&&command["client_request_id"].get_ref<const std::string &>().size()<=128)
                        reply["client_request_id"]=command["client_request_id"];
                }
                return reply.dump();
            };
            try{
                command=parsed_command(message);require(command.is_object());
                attempted=true;
                const auto result=connection->request({LocalOperation::BrowserMessage,{{"message",message}}});
                if(result.at("ok")!=true)throw LocalCommandRefused(LocalRefusal::InvalidRequest);
                const auto &reply=result.at("value");if(!(fields(reply,{"schema","message"})&&reply["schema"]=="browser-reply/1"&&reply["message"].is_string()))
                    throw LocalControlError(LocalControlError::Code::UnknownOutcome);
                return reply["message"].get<std::string>();
            }catch(const LocalCommandRefused &){return failure("invalid_request","The backend refused the request before making a change.");}
            catch(const LocalControlError &error){return error.code()==LocalControlError::Code::UnknownOutcome?
                failure("outcome_unknown","The result is unknown. Read current state before retrying."):
                failure(attempted?"backend_unavailable":"invalid_request",attempted?"The WsprryPi backend is unavailable.":"The request is invalid.");}
            catch(...){return attempted?failure("outcome_unknown","The result is unknown. Read current state before retrying."):failure("invalid_request","The request is invalid.");}
        };
        return session;
    }
    LocalTransportOptions options_;Json bootstrap_;std::mutex context_mutex_;
    std::atomic_bool maintenance_only_{false};
    std::unique_ptr<httplib::Server> server_;std::unique_ptr<WebSocketServer> websocket_;std::thread http_thread_;
};
struct Signals {
    struct sigaction previous_term{},previous_int{},previous_pipe{};
    Signals(){stopped=0;struct sigaction action{};action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);
        require(!sigaction(SIGTERM,&action,&previous_term)&&!sigaction(SIGINT,&action,&previous_int));
        action.sa_handler=SIG_IGN;require(!sigaction(SIGPIPE,&action,&previous_pipe));}
    ~Signals(){sigaction(SIGTERM,&previous_term,nullptr);sigaction(SIGINT,&previous_int,nullptr);sigaction(SIGPIPE,&previous_pipe,nullptr);}
};
}
std::optional<int> web_service_main(int argc,char *argv[]){
    bool selected=false;for(int index=1;index<argc;++index)if(std::string_view(argv[index]).starts_with("--web-service"))selected=true;
    if(!selected)return {};
    try{
        std::optional<std::string> runtime;std::string cache="/var/lib/wsprrypi-web/listeners.json";bool role=false,help=false,cache_selected=false;
        for(int index=1;index<argc;++index){
            const std::string option=argv[index];
            if(option=="--web-service"&&!role)role=true;
            else if((option=="--help"||option=="-h")&&!help)help=true;
            else if((option=="--runtime-directory"&&!runtime)||(option=="--listener-cache"&&!cache_selected)){
                require(index+1<argc);const std::string path=argv[++index];require(std::filesystem::path(path).is_absolute());
                if(option=="--runtime-directory")runtime=path;else{cache=path;cache_selected=true;}
            }else throw LocalControlError(LocalControlError::Code::Protocol);
        }
        require(role);
        if(help){std::cout<<"wsprrypi --web-service [--runtime-directory <absolute directory>] [--listener-cache <absolute file>]\n"
            <<"Runs browser HTTP and WebSocket listeners through the local backend. No database, hardware, or identity initialization.\n";return 0;}
        const auto options=web_control_options(runtime);Signals signals;Json bootstrap;
        while(!stopped&&bootstrap.is_null()){
            try{LocalControlConnection connection(options);bootstrap=value(connection.request({LocalOperation::WebBootstrap,Json::object()}));require(valid_web_bootstrap(bootstrap)&&bootstrap["boot"]==connection.boot());}
            catch(...){bootstrap=nullptr;try{bootstrap=read_web_bootstrap_cache(cache);}catch(...){std::this_thread::sleep_for(std::chrono::milliseconds(100));}}
        }
        if(stopped)return 0;
        auto service=std::make_unique<WebService>(options);service->start(bootstrap);
        bool listeners_ready=true;
        std::unique_ptr<WebService> retained;
        const auto retention_cache=cache+".recovery.web-listeners.json";
        try{
            auto previous=read_web_bootstrap_cache(retention_cache);
            if(previous["device_id"]==bootstrap["device_id"]&&previous["role"]==bootstrap["role"]&&previous["http_enabled"]==true&&
                (bootstrap["http_enabled"]!=true||previous["web_port"]!=bootstrap["web_port"])){
                previous["websocket_enabled"]=false;
                retained=std::make_unique<WebService>(options);retained->retain_maintenance_http();retained->start(previous);
            }
        }catch(...){retained.reset();}
        std::unique_ptr<LocalControlConnection> presence;
        std::string event_boot;std::uint64_t cursor=0;
        auto heartbeat=std::chrono::steady_clock::time_point{};
        std::cout<<"WsprryPi browser service "<<get_exe_version()<<" source="<<get_exe_commit()<<" dirty="<<get_exe_build_dirty()<<" listening; backend state remains private.\n"<<std::flush;
        while(!stopped){
            const auto now=std::chrono::steady_clock::now();
            if(now>=heartbeat){
                heartbeat=now+std::chrono::seconds(1);
                try{
                    if(!presence){
                        LocalControlConnection control(options);auto current=value(control.request({LocalOperation::WebBootstrap,Json::object()}));
                        require(valid_web_bootstrap(current)&&current["boot"]==control.boot());
                        if(!listeners_ready||!service->same_listeners(current)){
                            const bool keep_http=listeners_ready&&bootstrap["http_enabled"]==true&&
                                (current["http_enabled"]!=true||current["web_port"]!=bootstrap["web_port"]);
                            if(keep_http){
                                auto previous=bootstrap;previous["websocket_enabled"]=false;
                                write_web_bootstrap_cache(retention_cache,previous);
                                service->retain_maintenance_http();retained=std::move(service);
                            }else service->stop();
                            if(retained&&current["http_enabled"]==true){
                                try{const auto previous=read_web_bootstrap_cache(retention_cache);
                                    if(previous["web_port"]==current["web_port"])retained.reset();
                                }catch(...){}
                            }
                            listeners_ready=false;bootstrap=current;
                            service=std::make_unique<WebService>(options);service->start(current);listeners_ready=true;
                        }
                        service->context(current);bootstrap=current;
                        auto service_options=options;service_options.channel=LocalChannel::WebService;
                        presence=std::make_unique<LocalControlConnection>(service_options);require(presence->boot()==bootstrap["boot"].get<std::string>());
                        value(presence->request({LocalOperation::WebAttach,{{"web_port",bootstrap["http_enabled"]==true?bootstrap["web_port"]:Json(nullptr)},
                            {"socket_port",bootstrap["websocket_enabled"]==true?bootstrap["socket_port"]:Json(nullptr)}}}));
                        write_web_bootstrap_cache(cache,bootstrap);
                    }else value(presence->request({LocalOperation::WebHeartbeat,Json::object()}));
                }catch(...){presence.reset();}
                service->refresh();
            }
            if(presence)try{service->events(event_boot,cursor);}catch(...){presence.reset();service->refresh();}
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        service->stop();retained.reset();presence.reset();return 0;
    }catch(const std::exception &){std::cerr<<"Browser service failed; inspect listener availability, local IPC, and its listener cache.\n";return 2;}
}
}
