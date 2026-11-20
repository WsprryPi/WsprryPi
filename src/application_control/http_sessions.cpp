// SPDX-License-Identifier: MIT
#include "http_sessions.hpp"
#include "http_contract.hpp"
#include "http_domain.hpp"
#include "wire_bytes.hpp"
#include "scheduling.hpp"
#include <map>
#include <mutex>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto *name:names)if(!value.contains(name))return false;
    return true;
}
std::string text(const Json &value,std::size_t maximum){require(value.is_string()&&value.get_ref<const std::string &>().size()<=maximum);return value.get<std::string>();}
struct Session {
    std::mutex mutex;
    BrowserHttpInput input;
    std::uint64_t expected{};
    bool dispatched{},closed{};
    std::unique_ptr<BrowserHttpResource> resource;
};
}
struct LocalHttpSessions::Impl {
    std::mutex mutex;std::map<std::string,std::shared_ptr<Session>> values;
    Dispatch dispatch;std::function<bool()> admission;
};
LocalHttpSessions::LocalHttpSessions(Dispatch dispatch,std::function<bool()> admission):impl_(std::make_unique<Impl>()){
    require(bool(dispatch)&&bool(admission));impl_->dispatch=std::move(dispatch);impl_->admission=std::move(admission);
}
LocalHttpSessions::~LocalHttpSessions()=default;
Json LocalHttpSessions::request(const LocalRequest &request){
    auto &owner=*impl_;require(request.transport_connection.size()==32&&owner.admission());
    if(request.operation==LocalOperation::HttpOpen){
        const auto &value=request.arguments;
        require(fields(value,{"operation","method","path","target","peer","headers","body_size"}));
        auto session=std::make_shared<Session>();auto &input=session->input;
        input.operation=text(value["operation"],64);input.method=text(value["method"],8);input.path=text(value["path"],4096);
        input.target=text(value["target"],4096);input.peer=text(value["peer"],64);input.transport_descriptor=request.transport_descriptor;
        session->expected=wire_offset(text(value["body_size"],20));
        const auto operation=browser_http_operation(input.method,input.path);
        require(operation&&operation->name==input.operation&&session->expected<=operation->maximum_body);
        require(value["headers"].is_array()&&value["headers"].size()<=64);std::size_t bytes=0;
        for(const auto &header:value["headers"]){
            require(header.is_array()&&header.size()==2);
            const auto name=text(header[0],128),content=text(header[1],8192);bytes+=name.size()+content.size()+4;
            require(bytes<=8192);input.headers.emplace(name,content);
        }
        input.body.reserve(static_cast<std::size_t>(session->expected));
        std::lock_guard lock(owner.mutex);require(owner.values.size()<4&&!owner.values.contains(request.transport_connection));
        owner.values.emplace(request.transport_connection,std::move(session));
        return {{"schema","browser-http-input/1"},{"accepted",true}};
    }
    std::shared_ptr<Session> session;
    {std::lock_guard lock(owner.mutex);const auto found=owner.values.find(request.transport_connection);require(found!=owner.values.end());session=found->second;}
    std::lock_guard lock(session->mutex);require(!session->closed);
    if(request.operation==LocalOperation::HttpBody){
        require(!session->dispatched&&fields(request.arguments,{"offset","bytes"}));
        const auto offset=wire_offset(text(request.arguments["offset"],20));
        const auto bytes=decode_wire_chunk(text(request.arguments["bytes"],87384));
        require(offset==session->input.body.size()&&offset<=session->expected&&!bytes.empty()&&bytes.size()<=session->expected-offset);
        session->input.body+=bytes;return {{"offset",std::to_string(session->input.body.size())}};
    }
    if(request.operation==LocalOperation::HttpDispatch){
        require(request.arguments.empty()&&!session->dispatched&&session->input.body.size()==session->expected);
        session->dispatched=true;
        session->resource=owner.dispatch(std::move(session->input));
        return session->resource->metadata();
    }
    require(session->dispatched&&bool(session->resource));
    if(request.operation==LocalOperation::HttpRead){
        require(fields(request.arguments,{"offset","maximum"})&&request.arguments["maximum"].is_number_unsigned());
        const auto maximum=request.arguments["maximum"].get<std::uint64_t>();require(maximum&&maximum<=65536);
        const auto offset=wire_offset(text(request.arguments["offset"],20));
        const auto bytes=session->resource->read(offset,static_cast<std::size_t>(maximum));
        return {{"offset",std::to_string(offset)},{"bytes",encode_wire_chunk(bytes)}};
    }
    require(request.operation==LocalOperation::HttpComplete&&fields(request.arguments,{"delivered"})&&request.arguments["delivered"].is_boolean());
    session->resource->complete(request.arguments["delivered"].get<bool>());session->resource.reset();session->closed=true;
    return {{"complete",true}};
}
void LocalHttpSessions::disconnected(const std::string &connection) noexcept{
    try{
        std::shared_ptr<Session> session;auto &owner=*impl_;
        {std::lock_guard lock(owner.mutex);const auto found=owner.values.find(connection);if(found==owner.values.end())return;
            session=std::move(found->second);owner.values.erase(found);}
        std::lock_guard lock(session->mutex);session->closed=true;session->resource.reset();
    }catch(...){/* Missing delivery acknowledgement remains unsuccessful. */}
}
namespace {
LocalHttpSessions &backend_sessions(){static LocalHttpSessions owner(dispatch_backend_http,[]{return !exiting_wspr.load();});return owner;}
}
Json backend_http_request(const LocalRequest &request){return backend_sessions().request(request);}
void backend_http_disconnected(const std::string &connection) noexcept{backend_sessions().disconnected(connection);}

}
