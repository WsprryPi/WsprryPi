// SPDX-License-Identifier: MIT
#include "local_transport.hpp"
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
using Json=nlohmann::json;
using Code=LocalControlError::Code;
// Browser messages retain the existing 64 KiB frame limit. JSON wrapping can
// double quotes/escapes; keep explicit envelope headroom on that channel only.
constexpr std::size_t maximum=144*1024;
constexpr std::string_view protocol="wsprrypi-local/1";
using Deadline=std::chrono::steady_clock::time_point;
void require(bool value,Code code=Code::Protocol){if(!value)throw LocalControlError(code);}
bool fields(const Json &v,std::initializer_list<const char *> names){
    if(!v.is_object()||v.size()!=names.size())return false;
    for(const auto *name:names)if(!v.contains(name))return false;
    return true;
}
bool identity(const Json &v){return v.is_string()&&v.get_ref<const std::string &>().size()==32&&
    v.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string token(){
    std::array<unsigned char,16> bytes{};
    const int descriptor=::open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    require(descriptor>=0,Code::Unavailable);
    std::size_t offset=0;
    while(offset<bytes.size()){
        const auto size=::read(descriptor,bytes.data()+offset,bytes.size()-offset);
        if(size<0&&errno==EINTR)continue;
        if(size<=0){::close(descriptor);throw LocalControlError(Code::Unavailable);}
        offset+=static_cast<std::size_t>(size);
    }
    ::close(descriptor);std::string result;
    for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}
    return result;
}
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
void send_frame(int fd,const Json &value,Deadline deadline){
    auto bytes=value.dump();require(bytes.size()<=maximum);
    std::array<unsigned char,4> header{static_cast<unsigned char>(bytes.size()>>24),static_cast<unsigned char>(bytes.size()>>16),
        static_cast<unsigned char>(bytes.size()>>8),static_cast<unsigned char>(bytes.size())};
    transfer(fd,header.data(),header.size(),true,deadline);transfer(fd,bytes.data(),bytes.size(),true,deadline);
}
Json receive_frame(int fd,Deadline deadline,std::size_t bound=32768){
    std::array<unsigned char,4> header{};transfer(fd,header.data(),header.size(),false,deadline);
    const auto size=(std::uint32_t(header[0])<<24)|(std::uint32_t(header[1])<<16)|(std::uint32_t(header[2])<<8)|header[3];
    require(size>0&&size<=bound);
    std::string bytes(size,'\0');transfer(fd,bytes.data(),bytes.size(),false,deadline);
    std::vector<std::set<std::string>> keys;
    try{
        return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &parsed){
            require(depth<=32);
            if(event==Json::parse_event_t::object_start)keys.emplace_back();
            else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second);
            else if(event==Json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
            return true;
        });
    }catch(const LocalControlError &){throw;}catch(...){throw LocalControlError(Code::Protocol);}
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
const char *name(LocalOperation operation){
    switch(operation){case LocalOperation::DiagnosticRead:return "diagnostic.read";case LocalOperation::SettingsRead:return "settings.read";case LocalOperation::SettingsWrite:return "settings.write";case LocalOperation::RouteRead:return "maintenance.route.read";case LocalOperation::ListenerRead:return "listeners.read";case LocalOperation::EventsRead:return "events.read";case LocalOperation::BrowserOpen:return "browser.open";case LocalOperation::BrowserMessage:return "browser.message";case LocalOperation::HttpOpen:return "http.open";case LocalOperation::HttpBody:return "http.body";case LocalOperation::HttpDispatch:return "http.dispatch";case LocalOperation::HttpRead:return "http.read";case LocalOperation::HttpComplete:return "http.complete";case LocalOperation::WebBootstrap:return "web.bootstrap";case LocalOperation::WebAttach:return "web.attach";case LocalOperation::WebHeartbeat:return "web.heartbeat";case LocalOperation::BrowserCheck:return "browser.check";case LocalOperation::RecoveryCapture:return "recovery.capture";case LocalOperation::RecoveryObserve:return "recovery.observe";case LocalOperation::RecoveryValidate:return "recovery.validate";case LocalOperation::RecoveryPrepare:return "recovery.prepare";case LocalOperation::RecoveryReview:return "recovery.review";case LocalOperation::RecoveryCommand:return "recovery.command";case LocalOperation::RecoverySessionIssue:return "recovery.session.issue";}
    throw LocalControlError(Code::Protocol);
}
LocalOperation operation(const Json &value){
    require(value.is_string());for(const auto item:{LocalOperation::SettingsRead,LocalOperation::SettingsWrite,LocalOperation::DiagnosticRead,LocalOperation::RouteRead,LocalOperation::ListenerRead,LocalOperation::EventsRead,LocalOperation::BrowserOpen,LocalOperation::BrowserMessage,LocalOperation::HttpOpen,LocalOperation::HttpBody,LocalOperation::HttpDispatch,LocalOperation::HttpRead,LocalOperation::HttpComplete,LocalOperation::WebBootstrap,LocalOperation::WebAttach,LocalOperation::WebHeartbeat,LocalOperation::BrowserCheck,LocalOperation::RecoveryCapture,LocalOperation::RecoveryObserve,LocalOperation::RecoveryValidate,LocalOperation::RecoveryPrepare,LocalOperation::RecoveryReview,LocalOperation::RecoveryCommand,LocalOperation::RecoverySessionIssue})if(value==name(item))return item;
    throw LocalControlError(Code::Protocol);
}
bool http_operation(LocalOperation value){
    return value==LocalOperation::HttpOpen||value==LocalOperation::HttpBody||value==LocalOperation::HttpDispatch||value==LocalOperation::HttpRead||value==LocalOperation::HttpComplete;
}
bool mutation(LocalOperation value){return value==LocalOperation::SettingsWrite||value==LocalOperation::BrowserMessage||value==LocalOperation::HttpDispatch||value==LocalOperation::HttpComplete||value==LocalOperation::RecoveryCapture||value==LocalOperation::RecoveryValidate||value==LocalOperation::RecoveryPrepare||value==LocalOperation::RecoveryReview||value==LocalOperation::RecoveryCommand||value==LocalOperation::RecoverySessionIssue;}
const char *refusal_name(LocalRefusal value){
    switch(value){case LocalRefusal::InvalidRequest:return "invalid_request";case LocalRefusal::RevisionConflict:return "revision_conflict";
        case LocalRefusal::Busy:return "busy";case LocalRefusal::SetupRequired:return "setup_required";case LocalRefusal::StorageUnavailable:return "storage_unavailable";}
    return "command_refused";
}
bool refusal_value(const Json &value){
    if(!value.is_string())return false;
    for(const auto *name:{"command_refused","invalid_request","revision_conflict","busy","setup_required","storage_unavailable","outcome_unknown"})if(value==name)return true;
    return false;
}
}
LocalControlError::LocalControlError(Code code):std::runtime_error(code==Code::UnknownOutcome?
    "Local command outcome is unknown; read saved state before deciding whether to retry":code==Code::Unauthorized?
    "Local control peer is not authorized":code==Code::Protocol?"Local control protocol is unsupported or malformed":"Local backend is unavailable"),code_(code){}

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
                require(uid==options.backend_uid||uid==options.web_uid,Code::Unauthorized);
                const auto principal=uid==options.backend_uid?LocalPeer::Administrator:LocalPeer::WebService;
                {std::lock_guard lock(mutex);if(stopping.load())continue;clients.insert(client.value);}
                struct Remove {Impl &owner;int fd;~Remove(){std::lock_guard lock(owner.mutex);owner.clients.erase(fd);}}remove{*this,client.value};
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                const auto connection=token();
                send_frame(client.value,Json{{"protocol",protocol},{"boot",boot},{"connection",connection}},deadline);
                struct Retire {
                    Disconnected &closed;LocalPeer peer;const std::string &connection;
                    ~Retire(){if(closed)try{closed(peer,connection);}catch(...){}}
                }retire{disconnected,principal,connection};
                const bool browser=options.channel==LocalChannel::Browser;
                const bool http=options.channel==LocalChannel::Http;
                const bool service=options.channel==LocalChannel::WebService;
                const bool recovery=options.channel==LocalChannel::Recovery;
                bool opened=false;
                for(std::uint64_t sequence=1;!stopping.load();++sequence){
                    const auto request=receive_frame(client.value,std::chrono::steady_clock::now()+std::chrono::seconds(5),(browser||http||recovery)?maximum:32768);
                    require(fields(request,{"protocol","boot","connection","sequence","operation","arguments"})&&request["protocol"]==protocol&&
                        request["boot"]==boot&&request["connection"]==connection&&request["sequence"]==std::to_string(sequence)&&request["arguments"].is_object());
                    const LocalRequest parsed{operation(request["operation"]),request["arguments"],connection,client.value};
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
                    send_frame(client.value,Json{{"protocol",protocol},{"boot",boot},{"connection",connection},{"sequence",std::to_string(sequence)},{"result",result}},
                        std::chrono::steady_clock::now()+std::chrono::seconds(5));
                    if((!browser&&!http&&!service)||(!opened&&result["ok"]!=true)||parsed.operation==LocalOperation::HttpComplete||sequence==std::numeric_limits<std::uint64_t>::max())break;
                    opened=true;
                }
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
    const auto &greeting=owner.greeting;const auto sequence=std::to_string(owner.sequence);
    const Json envelope{{"protocol",protocol},{"boot",greeting["boot"]},{"connection",greeting["connection"]},
        {"sequence",sequence},{"operation",name(request.operation)},{"arguments",request.arguments}};
    require(envelope.dump().size()<=((owner.options.channel==LocalChannel::Browser||owner.options.channel==LocalChannel::Http||owner.options.channel==LocalChannel::Recovery)?maximum:32768));
    try{
        send_frame(owner.socket.value,envelope,std::chrono::steady_clock::now()+std::chrono::seconds(5));
        const auto response=receive_frame(owner.socket.value,std::chrono::steady_clock::now()+std::chrono::seconds(5),owner.options.channel==LocalChannel::Control?32768:maximum);
        require(fields(response,{"protocol","boot","connection","sequence","result"})&&response["protocol"]==protocol&&
            response["boot"]==greeting["boot"]&&response["connection"]==greeting["connection"]&&response["sequence"]==sequence);
        const auto &result=response["result"];
        require((fields(result,{"ok","value"})&&result["ok"]==true)||(fields(result,{"ok","error"})&&result["ok"]==false&&refusal_value(result["error"])));
        if(owner.sequence==std::numeric_limits<std::uint64_t>::max())owner.failed=true;
        else ++owner.sequence;
        if(result["ok"]==false&&result["error"]=="outcome_unknown")throw LocalControlError(Code::UnknownOutcome);
        return result;
    }catch(...){
        owner.failed=true;::shutdown(owner.socket.value,SHUT_RDWR);
        if(mutation(request.operation))throw LocalControlError(Code::UnknownOutcome);
        throw;
    }
}
Json local_control_request(const LocalTransportOptions &options,const LocalRequest &request){
    LocalControlConnection connection(options);return connection.request(request);
}
}
