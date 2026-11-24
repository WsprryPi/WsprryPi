// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "local_pipe_windows.hpp"
#include "local_session.hpp"
#include <thread>
#include <mutex>

namespace wsprrypi {
namespace {
using namespace local_protocol;
namespace pipe=local_pipe;
namespace wf=windows_file;
}
struct LocalControlServer::Impl {
    LocalTransportOptions options;std::string boot;Handler handler;Disconnected disconnected;
    std::atomic_bool stopping{true};wf::Handle stop_event;
    std::array<wf::Handle,4> pipes;std::array<std::thread,4> workers;
    Impl(LocalTransportOptions value,std::string id,Handler callback,Disconnected closed)
        :options(std::move(value)),boot(std::move(id)),handler(std::move(callback)),disconnected(std::move(closed)){
        require(identity(boot)&&bool(handler),Code::Protocol);
    }
    void serve(HANDLE endpoint){
        while(!stopping.load()){
            try{
                pipe::Pending pending(endpoint);pipe::begin_connect(endpoint,pending);
                // A waiting instance remains available until shutdown. Connected
                // clients have the same five-second admission/session deadlines.
                while(pending.active()&&!stopping.load()){
                    const HANDLE events[]{pending.event_handle(),stop_event.get()};
                    const auto result=::WaitForMultipleObjects(2,events,FALSE,INFINITE);
                    require(result==WAIT_OBJECT_0,Code::Unavailable);
                    (void)pending.wait(std::chrono::steady_clock::now()+std::chrono::seconds(5),stop_event.get());
                }
                if(stopping.load())break;
                const auto peer=pipe::authenticate_client(endpoint,options,std::chrono::steady_clock::now()+std::chrono::seconds(5),stop_event.get());
                auto io=[this,endpoint](void *data,std::size_t size,bool sending,Deadline deadline){
                    pipe::transfer(endpoint,data,size,sending,deadline,stop_event.get());
                };
                serve_session(io,options,boot,peer.principal,pipe::descriptor(endpoint),[endpoint]{return pipe::alive(endpoint);},
                    handler,disconnected,[this]{return stopping.load();});
                // DisconnectNamedPipe discards unread replies. Wait for the
                // client to consume/close, bounded and cancellable, before reuse.
                unsigned char extra{};pipe::transfer(endpoint,&extra,1,false,
                    std::chrono::steady_clock::now()+std::chrono::seconds(5),stop_event.get());
            }catch(...){/* A refused/disconnected client cannot affect backend owners. */}
            (void)::DisconnectNamedPipe(endpoint);
        }
        (void)::DisconnectNamedPipe(endpoint);
    }
};
LocalControlServer::LocalControlServer(LocalTransportOptions options,std::string boot,Handler handler,Disconnected disconnected)
    :impl_(std::make_unique<Impl>(std::move(options),std::move(boot),std::move(handler),std::move(disconnected))){}
LocalControlServer::~LocalControlServer(){stop();}
void LocalControlServer::start(){
    auto &owner=*impl_;require(owner.stopping.load()&&owner.pipes[0].get()==INVALID_HANDLE_VALUE,Code::Unavailable);
    require(wf::current_sid()==owner.options.backend_sid,Code::Unauthorized);
    const auto name=pipe::name(owner.options);pipe::Security security(owner.options.backend_sid,pipe::clients(owner.options));
    const unsigned count=(owner.options.channel==LocalChannel::Events||owner.options.channel==LocalChannel::WebService)?1:4;
    try{
        owner.stop_event=pipe::event();
        for(unsigned index=0;index<count;++index)owner.pipes[index]=pipe::create(name,security,count,index==0);
        owner.stopping.store(false);
        for(unsigned index=0;index<count;++index)owner.workers[index]=std::thread([&owner,index]{owner.serve(owner.pipes[index].get());});
    }catch(...){stop();throw;}
}
void LocalControlServer::stop() noexcept{
    auto &owner=*impl_;owner.stopping.store(true);
    if(owner.stop_event.get()!=INVALID_HANDLE_VALUE)(void)::SetEvent(owner.stop_event.get());
    for(auto &worker:owner.workers)if(worker.joinable())worker.join();
    for(auto &endpoint:owner.pipes)endpoint=wf::Handle{};
    owner.stop_event=wf::Handle{};
}
struct LocalControlConnection::Impl {
    LocalTransportOptions options;wf::Handle endpoint;Json greeting;
    std::uint64_t sequence{1};bool failed{};std::mutex mutex;
    explicit Impl(const LocalTransportOptions &value):options(value){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        endpoint=pipe::open(pipe::name(options),deadline);
        auto authentication=pipe::authenticate_server(endpoint.get(),options,deadline);
        auto io=[this](void *data,std::size_t size,bool sending,Deadline limit){pipe::transfer(endpoint.get(),data,size,sending,limit);};
        greeting=receive_frame(io,deadline);
        require(fields(greeting,{"protocol","boot","connection"})&&greeting["protocol"]==std::string(protocol)&&identity(greeting["boot"])&&identity(greeting["connection"]));
    }
};
LocalControlConnection::LocalControlConnection(const LocalTransportOptions &options):impl_(std::make_unique<Impl>(options)){}
LocalControlConnection::~LocalControlConnection()=default;
const std::string &LocalControlConnection::boot() const noexcept{return impl_->greeting["boot"].get_ref<const std::string &>();}
nlohmann::json LocalControlConnection::request(const LocalRequest &request){
    auto &owner=*impl_;std::lock_guard lock(owner.mutex);require(!owner.failed&&request.arguments.is_object(),Code::Unavailable);
    auto io=[&owner](void *data,std::size_t size,bool sending,Deadline deadline){pipe::transfer(owner.endpoint.get(),data,size,sending,deadline);};
    return session_request(io,owner.options.channel,owner.greeting,owner.sequence,owner.failed,request,[&owner]{owner.endpoint=wf::Handle{};});
}
nlohmann::json local_control_request(const LocalTransportOptions &options,const LocalRequest &request){
    LocalControlConnection connection(options);return connection.request(request);
}
}
#endif
