// SPDX-License-Identifier: MIT
#pragma once
#include "local_protocol.hpp"
#include <limits>
#include <mutex>
namespace wsprrypi::local_protocol {
// Retained application callbacks cannot probe a descriptor after retirement or
// race descriptor reuse: probing and invalidation share the same lock.
struct Liveness {
    explicit Liveness(std::function<bool()> value):probe(std::move(value)){}
    bool check(){std::lock_guard lock(mutex);return live&&probe&&probe();}
    void retire(){std::lock_guard lock(mutex);live=false;probe={};}
    std::mutex mutex;bool live{true};std::function<bool()> probe;
};
template<class Transfer,class Stopping> void serve_session(Transfer &io,const LocalTransportOptions &options,
    const std::string &boot,LocalPeer principal,LocalTransportDescriptor descriptor,std::function<bool()> probe,
    const LocalControlServer::Handler &handler,LocalControlServer::Disconnected &disconnected,Stopping stopping){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    const auto connection=token();
    auto alive=std::make_shared<Liveness>(std::move(probe));
    send_frame(io,Json{{"protocol",protocol},{"boot",boot},{"connection",connection}},deadline);
    struct Retire {
        LocalControlServer::Disconnected &closed;LocalPeer peer;const std::string &connection;std::shared_ptr<Liveness> alive;
        ~Retire(){alive->retire();if(closed)try{closed(peer,connection);}catch(...){}}
    }retire{disconnected,principal,connection,alive};
    const bool browser=options.channel==LocalChannel::Browser;
    const bool http=options.channel==LocalChannel::Http;
    const bool service=options.channel==LocalChannel::WebService;
    const bool recovery=options.channel==LocalChannel::Recovery;
    bool opened=false;
    for(std::uint64_t sequence=1;!stopping();++sequence){
        const auto request=receive_frame(io,std::chrono::steady_clock::now()+std::chrono::seconds(5),(browser||http||recovery)?maximum:32768);
        require(fields(request,{"protocol","boot","connection","sequence","operation","arguments"})&&request["protocol"]==std::string(protocol)&&
            request["boot"]==boot&&request["connection"]==connection&&request["sequence"]==std::to_string(sequence)&&request["arguments"].is_object());
        const LocalRequest parsed{operation(request["operation"]),request["arguments"],connection,descriptor,[alive]{return alive->check();}};
        const bool browser_operation=parsed.operation==LocalOperation::BrowserOpen||parsed.operation==LocalOperation::BrowserMessage||parsed.operation==LocalOperation::BrowserCheck;
        const bool service_operation=parsed.operation==LocalOperation::WebAttach||parsed.operation==LocalOperation::WebHeartbeat;
        require((parsed.operation==LocalOperation::EventsRead)==(options.channel==LocalChannel::Events)&&browser_operation==browser&&http_operation(parsed.operation)==http&&service_operation==service&&(parsed.operation==LocalOperation::RecoveryCommand)==recovery);
        require(!browser||(opened?parsed.operation!=LocalOperation::BrowserOpen:parsed.operation==LocalOperation::BrowserOpen));
        require(!service||(opened?parsed.operation==LocalOperation::WebHeartbeat:parsed.operation==LocalOperation::WebAttach));
        require(!http||(opened?parsed.operation!=LocalOperation::HttpOpen:parsed.operation==LocalOperation::HttpOpen));
        require(principal==LocalPeer::Administrator||parsed.operation==LocalOperation::ListenerRead||parsed.operation==LocalOperation::EventsRead||browser||http||service||parsed.operation==LocalOperation::WebBootstrap,Code::Unauthorized);
        Json result;
        try{result={{"ok",true},{"value",handler(principal,parsed)}};}
        catch(const LocalCommandRefused &error){result={{"ok",false},{"error",refusal_name(error.reason())}};}
        catch(...){result={{"ok",false},{"error",mutation(parsed.operation)?"outcome_unknown":"command_refused"}};}
        require(result.dump().size()<=(options.channel==LocalChannel::Control?32000:options.channel==LocalChannel::Events?126000:142000));
        send_frame(io,Json{{"protocol",protocol},{"boot",boot},{"connection",connection},{"sequence",std::to_string(sequence)},{"result",result}},
            std::chrono::steady_clock::now()+std::chrono::seconds(5));
        if((!browser&&!http&&!service)||(!opened&&result["ok"]!=true)||parsed.operation==LocalOperation::HttpComplete||sequence==std::numeric_limits<std::uint64_t>::max())break;
        opened=true;
    }
}
template<class Transfer,class Disconnect> Json session_request(Transfer &io,LocalChannel channel,const Json &greeting,
    std::uint64_t &next_sequence,bool &failed,const LocalRequest &request,Disconnect disconnect){
    const auto sequence=std::to_string(next_sequence);
    const Json envelope{{"protocol",protocol},{"boot",greeting["boot"]},{"connection",greeting["connection"]},
        {"sequence",sequence},{"operation",name(request.operation)},{"arguments",request.arguments}};
    require(envelope.dump().size()<=((channel==LocalChannel::Browser||channel==LocalChannel::Http||channel==LocalChannel::Recovery)?maximum:32768));
    try{
        send_frame(io,envelope,std::chrono::steady_clock::now()+std::chrono::seconds(5));
        const auto response=receive_frame(io,std::chrono::steady_clock::now()+std::chrono::seconds(5),channel==LocalChannel::Control?32768:maximum);
        require(fields(response,{"protocol","boot","connection","sequence","result"})&&response["protocol"]==std::string(protocol)&&
            response["boot"]==greeting["boot"]&&response["connection"]==greeting["connection"]&&response["sequence"]==sequence);
        const auto &result=response["result"];
        require((fields(result,{"ok","value"})&&result["ok"]==true)||(fields(result,{"ok","error"})&&result["ok"]==false&&refusal_value(result["error"])));
        if(next_sequence==std::numeric_limits<std::uint64_t>::max())failed=true;
        else ++next_sequence;
        if(result["ok"]==false&&result["error"]=="outcome_unknown")throw LocalControlError(Code::UnknownOutcome);
        return result;
    }catch(...){
        failed=true;disconnect();
        if(mutation(request.operation))throw LocalControlError(Code::UnknownOutcome);
        throw;
    }
}
}
