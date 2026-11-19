// SPDX-License-Identifier: MIT
#include "pi_pair_server.hpp"
#include "management_resolver.hpp"
#include "pi_pair_contract.hpp"
#include "control_contract.hpp"
#include "controller_contract.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <algorithm>
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value){if(!value)throw std::runtime_error("pairing_listener_unavailable");}
struct Socket {int fd{-1};~Socket(){if(fd>=0)::close(fd);}};
thread_local const void *active_server{};
struct ActiveServer {const void *previous;explicit ActiveServer(const void *value):previous(active_server){active_server=value;}~ActiveServer(){active_server=previous;}};
void nonblocking(int fd){
    const auto flags=fcntl(fd,F_GETFL),descriptor=fcntl(fd,F_GETFD);require(flags>=0&&descriptor>=0&&
        !fcntl(fd,F_SETFL,flags|O_NONBLOCK)&&!fcntl(fd,F_SETFD,descriptor|FD_CLOEXEC));
}
std::string peer_address(const sockaddr_storage &peer){
    std::array<char,INET6_ADDRSTRLEN> text{};
    if(peer.ss_family==AF_INET){const auto &address=reinterpret_cast<const sockaddr_in &>(peer);
        require(inet_ntop(AF_INET,&address.sin_addr,text.data(),text.size()));}
    else {require(peer.ss_family==AF_INET6);const auto &address=reinterpret_cast<const sockaddr_in6 &>(peer);
        require(inet_ntop(AF_INET6,&address.sin6_addr,text.data(),text.size()));}
    return text.data();
}
bool loopback(const std::string &peer){in_addr a{};in6_addr b{};return (inet_pton(AF_INET,peer.c_str(),&a)==1&&(ntohl(a.s_addr)>>24)==127)||
    (inet_pton(AF_INET6,peer.c_str(),&b)==1&&IN6_IS_ADDR_LOOPBACK(&b));}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool route(const PiPairPrincipal &principal,const httplib::Request &request){
    const std::string pair="/api/pair/v1/",control="/api/control/v1/targets/";
    if(request.path=="/api/control/v1/targets")return request.method=="GET"&&principal.identity().purpose==PiPairTlsPurpose::Management;
    if(request.path.starts_with(pair))return pi_pair_route(principal.identity().purpose==PiPairTlsPurpose::Retirement?
        PiPairRoutePurpose::Retirement:PiPairRoutePurpose::Management,request.method,request.path.substr(pair.size()));
    if(principal.identity().purpose!=PiPairTlsPurpose::Management||!request.path.starts_with(control))return false;
    const auto suffix=request.path.substr(control.size());if(suffix.size()<34||suffix[32]!='/'||!id(suffix.substr(0,32)))return false;
    const auto resource=suffix.substr(33);
    if(resource.starts_with("pico/"))return controller_pico_route(resource.substr(5),request.method);
    if(resource.starts_with("operations/"))return (request.method=="GET"&&id(resource.substr(11)))||
        (request.method=="POST"&&resource.size()==50&&resource.ends_with("/review")&&id(resource.substr(11,32)));
    if(request.method=="GET")return resource=="context"||resource=="settings"||resource=="status"||resource=="migration";
    if(request.method=="PUT")return resource=="settings";
    return request.method=="POST"&&(resource=="enable"||resource=="disable"||resource=="stop"||resource=="migration"||resource=="migration/rollback");
}
PiPairHttpReply refused(const PiPairPrincipal &principal,const httplib::Request &request){
    if(principal.identity().purpose==PiPairTlsPurpose::Retirement||request.path.starts_with("/api/pair/"))
        return {{403,nlohmann::json{{"contract","Wsprry-Pair/1"},{"schema","result"},{"pair_id",principal.identity().pair_id},
            {"state","rejected"},{"code","invalid_proof"},{"retryable",false}}.dump(),{}},{},{}};
    return {controller_target_error(403,"unauthorized"),{}, {}};
}
}
struct PiPairServer::State:std::enable_shared_from_this<State> {
    struct Worker {std::thread thread;std::atomic_bool done{false};};
    PiPairAdmission &admission;
    PiPairTlsChannel::Lookup lookup;
    Finished finished;
    Handler handler;
    Access access;
    std::mutex lifecycle;
    std::thread accepting;
    std::vector<int> listeners;
    std::array<std::unique_ptr<Worker>,4> workers;
    std::atomic_bool stopping{true},running{false};
    std::atomic_uint bound_port{0};
    State(PiPairAdmission &budget,PiPairTlsChannel::Lookup keys,Finished admit,Handler dispatch,Access mode)
        :admission(budget),lookup(std::move(keys)),finished(std::move(admit)),handler(std::move(dispatch)),access(mode){require(bool(lookup)&&bool(finished)&&bool(handler));}
    void serve(int fd,PiPairAdmission::Connection connection,const std::string &peer){
        ActiveServer active(this);
        struct Cancel {PiPairAdmission::Connection &connection;~Cancel(){connection.cancel();}} cancel{connection};
        try{
            const auto owned=fd;fd=-1;PiPairTlsChannel channel(owned,lookup);
            const auto started=Clock::now(),deadline=started+std::chrono::seconds(24);
            const auto own_cancel=connection.cancellation();const auto self=shared_from_this();
            const auto cancelled=[self,own_cancel,deadline]{return self->stopping.load()||own_cancel()||Clock::now()>=deadline;};
            while(!cancelled()){
                const auto state=channel.handshake();if(state==PiPairTlsChannel::Handshake::Failed)return;
                if(state==PiPairTlsChannel::Handshake::Ready)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if(cancelled()||!channel.principal()||!finished(*channel.principal()))return;
            PiPairHttpDecoder decoder;std::array<std::uint8_t,4096> bytes{};
            while(!cancelled()&&decoder.state()==PiPairHttpDecoder::State::More){
                const auto read=channel.read(bytes);
                if(read.state==wtp::IoState::Progress){require(read.count&&read.count<=bytes.size());decoder.append(std::span(bytes.data(),read.count));}
                else if(read.state!=wtp::IoState::WouldBlock)return;
                else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if(cancelled()||decoder.state()!=PiPairHttpDecoder::State::Ready)return;
            auto request=decoder.request();request.remote_addr=peer;
            const auto response=route(*channel.principal(),request)?handler(*channel.principal(),request,connection,cancelled):refused(*channel.principal(),request);
            if(cancelled())return;
            const auto wire=pi_pair_http_response(response);std::size_t sent=0;
            while(!cancelled()&&sent<wire.size()){
                const auto wrote=channel.write(std::span(reinterpret_cast<const std::uint8_t *>(wire.data()+sent),wire.size()-sent));
                if(wrote.state==wtp::IoState::Progress){require(wrote.count&&wrote.count<=wire.size()-sent);sent+=wrote.count;}
                else if(wrote.state!=wtp::IoState::WouldBlock)return;
                else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }catch(...){if(fd>=0)::close(fd);}
    }
    void reap(){for(auto &worker:workers)if(worker&&worker->done.load()){worker->thread.join();worker.reset();}}
    void accept_loop() noexcept{
        ActiveServer active(this);
        try{
        while(!stopping.load()){
            reap();std::vector<pollfd> fds;for(const auto fd:listeners)fds.push_back({fd,POLLIN,0});
            const auto count=poll(fds.data(),fds.size(),50);if(count<0){if(errno==EINTR)continue;break;}
            for(const auto &source:fds){
                if(!(source.revents&POLLIN)||stopping.load())continue;
                sockaddr_storage peer{};socklen_t size=sizeof(peer);Socket accepted{accept(source.fd,reinterpret_cast<sockaddr *>(&peer),&size)};
                if(accepted.fd<0)continue;
                try{
                    nonblocking(accepted.fd);const auto address=peer_address(peer);
                    if(access==Access::LoopbackTest?!loopback(address):!private_management_address(address,{}))continue;
                    if(!admission.allow_request(address))continue;
                    auto connection=admission.try_incoming();if(!connection)continue;
                    auto free=std::find_if(workers.begin(),workers.end(),[](const auto &worker){return !worker;});if(free==workers.end())continue;
                    auto worker=std::make_unique<Worker>();auto *completed=worker.get();const auto fd=accepted.fd;const auto self=shared_from_this();
                    worker->thread=std::thread([self,completed,fd,address,connection=std::move(*connection)]() mutable {
                        self->serve(fd,std::move(connection),address);completed->done.store(true);
                    });accepted.fd=-1;*free=std::move(worker);
                }catch(...){}
            }
        }
        }catch(...){}
        stopping.store(true);running.store(false);bound_port.store(0);
        for(const auto fd:listeners)::close(fd);
        listeners.clear();
    }
};
PiPairServer::PiPairServer(PiPairAdmission &admission,PiPairTlsChannel::Lookup lookup,Finished finished,Handler handler,Access access)
    :state_(std::make_shared<State>(admission,std::move(lookup),std::move(finished),std::move(handler),access)){}
PiPairServer::~PiPairServer(){stop();}
bool PiPairServer::ready() const noexcept{return state_->running.load();}
unsigned PiPairServer::port() const noexcept{return state_->bound_port.load();}
bool PiPairServer::start(){
    auto &state=*state_;std::lock_guard lock(state.lifecycle);if(state.accepting.joinable())return false;
    if(const auto disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");state.access==Access::PrivateLan&&disabled&&std::string_view(disabled)=="1")return false;
    try{
        unsigned port=state.access==Access::LoopbackTest?0:31418;
        for(const auto family:{AF_INET,AF_INET6}){
            if(family==AF_INET6&&state.access==Access::LoopbackTest)continue;
            Socket listener{socket(family,SOCK_STREAM,IPPROTO_TCP)};
            if(listener.fd<0&&family==AF_INET6&&(errno==EAFNOSUPPORT||errno==EPROTONOSUPPORT))continue;
            require(listener.fd>=0);nonblocking(listener.fd);const int yes=1;require(!setsockopt(listener.fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)));
            if(family==AF_INET){
                sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
                address.sin_addr.s_addr=htonl(state.access==Access::LoopbackTest?INADDR_LOOPBACK:INADDR_ANY);
                require(!bind(listener.fd,reinterpret_cast<const sockaddr *>(&address),sizeof(address))&&!listen(listener.fd,4));
                socklen_t size=sizeof(address);require(!getsockname(listener.fd,reinterpret_cast<sockaddr *>(&address),&size));port=ntohs(address.sin_port);
            }else {
                require(!setsockopt(listener.fd,IPPROTO_IPV6,IPV6_V6ONLY,&yes,sizeof(yes)));
                sockaddr_in6 address{};address.sin6_family=AF_INET6;address.sin6_port=htons(port);
                require(!bind(listener.fd,reinterpret_cast<const sockaddr *>(&address),sizeof(address))&&!listen(listener.fd,4));
            }
            state.listeners.push_back(listener.fd);listener.fd=-1;
        }
        require(!state.listeners.empty()&&port);state.stopping.store(false);state.bound_port.store(port);state.running.store(true);
        state.accepting=std::thread([self=state_]{self->accept_loop();});return true;
    }catch(...){for(const auto fd:state.listeners)::close(fd);state.listeners.clear();state.running.store(false);state.bound_port.store(0);return false;}
}
void PiPairServer::stop() noexcept{
    auto &state=*state_;state.stopping.store(true);if(active_server==state_.get())return;
    std::lock_guard lock(state.lifecycle);
    state.stopping.store(true);
    if(state.accepting.joinable())state.accepting.join();
    for(auto &worker:state.workers)if(worker){if(worker->thread.joinable())worker->thread.join();worker.reset();}
    state.running.store(false);state.bound_port.store(0);
}
} // namespace wsprrypi
