// SPDX-License-Identifier: MIT
#include "local_transport.hpp"
namespace wsprrypi {
LocalControlError::LocalControlError(Code code):std::runtime_error(code==Code::UnknownOutcome?
    "Local command outcome is unknown; read saved state before deciding whether to retry":code==Code::Unauthorized?
    "Local control peer is not authorized":code==Code::Protocol?"Local control protocol is unsupported or malformed":"Local backend is unavailable"),code_(code){}

}
#ifndef _WIN32
#include "local_session.hpp"
#include "native_random.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <mutex>
#include <limits>
#include <poll.h>
#include <set>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
namespace wsprrypi {
namespace {
using namespace local_protocol;
struct Descriptor {
    int value{-1};explicit Descriptor(int descriptor=-1):value(descriptor){}
    ~Descriptor(){if(value>=0)::close(value);}
    Descriptor(const Descriptor &)=delete;
    Descriptor &operator=(const Descriptor &)=delete;
};
void ready(int fd,short events,Deadline deadline){
    while(true){
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        require(remaining>0,Code::Unavailable);
        pollfd item{fd,events,0};
        const auto result=::poll(&item,1,static_cast<int>(std::min<std::int64_t>(remaining,5000)));
        if(result<0&&errno==EINTR)continue;
        require(result>0&&(item.revents&events),Code::Unavailable);return;
    }
}
void transfer(int fd,void *buffer,std::size_t size,bool sending,Deadline deadline){
    auto *data=static_cast<unsigned char *>(buffer);
    while(size){
        ready(fd,sending?POLLOUT:POLLIN,deadline);
#ifdef MSG_NOSIGNAL
        const auto count=sending ? ::send(fd,data,size,MSG_NOSIGNAL) : ::recv(fd,data,size,0);
#else
        const auto count=sending ? ::send(fd,data,size,0) : ::recv(fd,data,size,0);
#endif
        if(count<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        require(count>0,Code::Unavailable);data+=count;size-=static_cast<std::size_t>(count);
    }
}
Json receive_frame(int fd,Deadline deadline,std::size_t bound=32768){
    auto io=[fd](void *data,std::size_t size,bool sending,Deadline limit){transfer(fd,data,size,sending,limit);};
    return local_protocol::receive_frame(io,deadline,bound);
}
uid_t peer_uid(int fd){
#ifdef __linux__
    struct ucred credential{};socklen_t size=sizeof credential;
    require(!::getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credential,&size)&&size==sizeof credential,Code::Unauthorized);
    return credential.uid;
#elif defined(__APPLE__)
    uid_t user{};gid_t group{};require(!::getpeereid(fd,&user,&group),Code::Unauthorized);return user;
#else
    (void)fd;throw LocalControlError(Code::Unavailable);
#endif
}
int socket_descriptor(){
    const int fd=::socket(AF_UNIX,SOCK_STREAM,0);require(fd>=0,Code::Unavailable);
    if(::fcntl(fd,F_SETFD,FD_CLOEXEC)||::fcntl(fd,F_SETFL,O_NONBLOCK)){::close(fd);throw LocalControlError(Code::Unavailable);}
#ifdef SO_NOSIGPIPE
    const int one=1;if(::setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one)){::close(fd);throw LocalControlError(Code::Unavailable);}
#endif
    return fd;
}
std::string socket_path(const LocalTransportOptions &options,bool create){
    require(!options.directory.empty()&&std::filesystem::path(options.directory).is_absolute()&&
        options.directory.find('\0')==std::string::npos,Code::Unavailable);
    auto parent=std::filesystem::path(options.directory).parent_path();
    while(!parent.empty()){
        struct stat info{};require(!::lstat(parent.c_str(),&info)&&S_ISDIR(info.st_mode)&&
            (info.st_uid==0||info.st_uid==options.backend_uid)&&
            (!(info.st_mode&0022)||(info.st_uid==0&&(info.st_mode&S_ISVTX))),Code::Unauthorized);
        const auto next=parent.parent_path();if(next==parent)break;parent=next;
    }
    if(create&&::mkdir(options.directory.c_str(),0750))require(errno==EEXIST,Code::Unavailable);
    struct stat info{};
    if(::lstat(options.directory.c_str(),&info))throw LocalControlError(errno==ENOENT?Code::Unavailable:Code::Unauthorized);
    require(S_ISDIR(info.st_mode)&&info.st_uid==options.backend_uid&&
        !(info.st_mode&0027),Code::Unauthorized);
    if(create){
        require(::geteuid()==options.backend_uid&& !::chown(options.directory.c_str(),options.backend_uid,options.socket_group)&&
            !::chmod(options.directory.c_str(),0750),Code::Unauthorized);
    }
    const auto path=options.directory+(options.channel==LocalChannel::Control?"/control.sock":options.channel==LocalChannel::Events?"/events.sock":options.channel==LocalChannel::Browser?"/browser.sock":options.channel==LocalChannel::Http?"/http.sock":options.channel==LocalChannel::Recovery?"/recovery.sock":"/service.sock");
    require(path.size()<sizeof(sockaddr_un{}.sun_path),Code::Unavailable);return path;
}
sockaddr_un address(const std::string &path){sockaddr_un value{};value.sun_family=AF_UNIX;std::memcpy(value.sun_path,path.c_str(),path.size()+1);return value;}

}

struct LocalControlServer::Impl {
    LocalTransportOptions options;std::string boot,path;Handler handler;Disconnected disconnected;
    std::atomic_bool stopping{true};int listener{-1};std::array<std::thread,4> workers;
    std::mutex mutex;std::set<int> clients;dev_t device{};ino_t inode{};
    Impl(LocalTransportOptions value,std::string id,Handler callback,Disconnected closed):options(std::move(value)),boot(std::move(id)),handler(std::move(callback)),disconnected(std::move(closed)){
        require(identity(boot)&&bool(handler),Code::Protocol);
    }
    void serve(){
        while(!stopping.load()){
            try{
                ready(listener,POLLIN,std::chrono::steady_clock::now()+std::chrono::seconds(1));
                Descriptor client(::accept(listener,nullptr,nullptr));if(client.value<0)continue;
                require(!::fcntl(client.value,F_SETFD,FD_CLOEXEC)&&!::fcntl(client.value,F_SETFL,O_NONBLOCK),Code::Unavailable);
#ifdef SO_NOSIGPIPE
                const int one=1;require(!::setsockopt(client.value,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one),Code::Unavailable);
#endif
                const auto uid=peer_uid(client.value);
                require(uid==options.backend_uid||uid==options.web_uid||(options.administrator_uid&&uid==*options.administrator_uid),Code::Unauthorized);
                const auto principal=uid==options.web_uid&&uid!=options.backend_uid?LocalPeer::WebService:LocalPeer::Administrator;
                {std::lock_guard lock(mutex);if(stopping.load())continue;clients.insert(client.value);}
                struct Remove {Impl &owner;int fd;~Remove(){std::lock_guard lock(owner.mutex);owner.clients.erase(fd);}}remove{*this,client.value};
                auto io=[fd=client.value](void *data,std::size_t size,bool sending,Deadline limit){transfer(fd,data,size,sending,limit);};
                serve_session(io,options,boot,principal,client.value,[fd=client.value]{
                    char byte{};const auto count=::recv(fd,&byte,1,MSG_PEEK);const auto error=errno;
                    return count>0||(count<0&&(error==EAGAIN||error==EWOULDBLOCK||error==EINTR));
                },handler,disconnected,[this]{return stopping.load();});
            }catch(...){/* A refused/disconnected client cannot affect backend owners. */}
        }
    }
};
LocalControlServer::LocalControlServer(LocalTransportOptions options,std::string boot,Handler handler,Disconnected disconnected)
    :impl_(std::make_unique<Impl>(std::move(options),std::move(boot),std::move(handler),std::move(disconnected))){}
LocalControlServer::~LocalControlServer(){stop();}
void LocalControlServer::start(){
    auto &owner=*impl_;require(owner.stopping.load()&&owner.listener<0,Code::Unavailable);
    owner.path=socket_path(owner.options,true);
    struct stat previous{};
    if(!::lstat(owner.path.c_str(),&previous)){
        require(S_ISSOCK(previous.st_mode)&&previous.st_uid==owner.options.backend_uid&&previous.st_nlink==1,Code::Unauthorized);
        Descriptor probe(socket_descriptor());auto endpoint=address(owner.path);
        require(::connect(probe.value,reinterpret_cast<sockaddr *>(&endpoint),sizeof endpoint)<0&&errno==ECONNREFUSED,Code::Unavailable);
        require(!::unlink(owner.path.c_str()),Code::Unavailable);
    }else require(errno==ENOENT,Code::Unavailable);
    Descriptor socket(socket_descriptor());auto endpoint=address(owner.path);
    require(!::bind(socket.value,reinterpret_cast<sockaddr *>(&endpoint),sizeof endpoint),Code::Unavailable);
    require(!::lstat(owner.path.c_str(),&previous),Code::Unavailable);owner.device=previous.st_dev;owner.inode=previous.st_ino;
    try{
        require(!::chown(owner.path.c_str(),owner.options.backend_uid,owner.options.socket_group)&&!::chmod(owner.path.c_str(),0660)&&
            !::listen(socket.value,4),Code::Unavailable);
        owner.listener=socket.value;socket.value=-1;owner.stopping.store(false);
        const auto count=(owner.options.channel==LocalChannel::Events||owner.options.channel==LocalChannel::WebService)?1:owner.workers.size();
        for(std::size_t index=0;index<count;++index)owner.workers[index]=std::thread([&owner]{owner.serve();});
    }catch(...){stop();throw;}
}
void LocalControlServer::stop() noexcept{
    auto &owner=*impl_;owner.stopping.store(true);
    if(owner.listener>=0)::shutdown(owner.listener,SHUT_RDWR);
    {std::lock_guard lock(owner.mutex);for(const auto fd:owner.clients)::shutdown(fd,SHUT_RDWR);}
    for(auto &worker:owner.workers)if(worker.joinable())worker.join();
    if(owner.listener>=0){::close(owner.listener);owner.listener=-1;}
    struct stat current{};
    if(!owner.path.empty()&&!::lstat(owner.path.c_str(),&current)&&current.st_dev==owner.device&&current.st_ino==owner.inode)::unlink(owner.path.c_str());
    owner.path.clear();owner.device=0;owner.inode=0;
}
struct LocalControlConnection::Impl {
    LocalTransportOptions options;
    Descriptor socket;
    Json greeting;
    std::uint64_t sequence{1};
    bool failed{};
    std::mutex mutex;
    explicit Impl(const LocalTransportOptions &value):options(value){
    const auto path=socket_path(options,false);
    struct stat info{};
    if(::lstat(path.c_str(),&info))throw LocalControlError(errno==ENOENT?Code::Unavailable:Code::Unauthorized);
    require(S_ISSOCK(info.st_mode)&&info.st_uid==options.backend_uid&&
        info.st_nlink==1&&!(info.st_mode&0007),Code::Unauthorized);
    Descriptor socket(socket_descriptor());const auto endpoint=address(path);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    if(::connect(socket.value,reinterpret_cast<const sockaddr *>(&endpoint),sizeof endpoint)){
        require(errno==EINPROGRESS,Code::Unavailable);ready(socket.value,POLLOUT,deadline);
        int error{};socklen_t size=sizeof error;require(!::getsockopt(socket.value,SOL_SOCKET,SO_ERROR,&error,&size)&&!error,Code::Unavailable);
    }
    require(peer_uid(socket.value)==options.backend_uid,Code::Unauthorized);
    const auto greeting=receive_frame(socket.value,deadline);
    require(fields(greeting,{"protocol","boot","connection"})&&greeting["protocol"]==protocol&&identity(greeting["boot"])&&identity(greeting["connection"]));
        this->socket.value=socket.value;socket.value=-1;this->greeting=greeting;
    }
};
LocalControlConnection::LocalControlConnection(const LocalTransportOptions &options):impl_(std::make_unique<Impl>(options)){}
LocalControlConnection::~LocalControlConnection()=default;
const std::string &LocalControlConnection::boot() const noexcept{return impl_->greeting["boot"].get_ref<const std::string &>();}
Json LocalControlConnection::request(const LocalRequest &request){
    auto &owner=*impl_;std::lock_guard lock(owner.mutex);require(!owner.failed&&request.arguments.is_object(),Code::Unavailable);
    auto io=[fd=owner.socket.value](void *data,std::size_t size,bool sending,Deadline limit){transfer(fd,data,size,sending,limit);};
    return session_request(io,owner.options.channel,owner.greeting,owner.sequence,owner.failed,request,
        [&owner]{::shutdown(owner.socket.value,SHUT_RDWR);});
}
Json local_control_request(const LocalTransportOptions &options,const LocalRequest &request){
    LocalControlConnection connection(options);return connection.request(request);
}
}

#endif
