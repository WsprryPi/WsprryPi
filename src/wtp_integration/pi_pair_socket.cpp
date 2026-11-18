// SPDX-License-Identifier: MIT
#include "pi_pair_socket.hpp"
#include "management_resolver.hpp"
#include "pi_pair_http.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <thread>
#include <utility>
#include <stdexcept>
#include <cstdlib>
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value){if(!value)throw std::runtime_error("pairing_connection_unavailable");}
struct Socket {int fd{-1};~Socket(){if(fd>=0)::close(fd);}};
struct ResolverCancel {TlsResolver &resolver;~ResolverCancel(){resolver.cancel();}};
bool loopback(const std::string &text){in_addr a{};in6_addr b{};return (inet_pton(AF_INET,text.c_str(),&a)==1&&(ntohl(a.s_addr)>>24)==127)||
    (inet_pton(AF_INET6,text.c_str(),&b)==1&&IN6_IS_ADDR_LOOPBACK(&b));}
}
PiPairSocket::PiPairSocket(int fd,std::string address,Time deadline,std::function<bool()> cancelled)
    :fd_(fd),address_(std::move(address)),deadline_(deadline),cancelled_(std::move(cancelled)){}
std::unique_ptr<PiPairSocket> PiPairSocket::open(const PiPairEndpoint &endpoint,Access access,Time whole,std::function<bool()> cancel){
    require(bool(cancel)&&endpoint.port&&endpoint.port<=65535);
    if(const auto disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");access==Access::PrivateLan&&disabled&&std::string_view(disabled)=="1")
        throw std::runtime_error("pairing_connection_unavailable");
    const auto host=(endpoint.host.find(':')==std::string::npos?endpoint.host:"["+endpoint.host+"]")+":"+std::to_string(endpoint.port);
    (void)pi_pair_http_request("GET","/api/pair/v1/window",host); // Reject URL/credential/path injection before DNS.
    const auto cancelled=[&]{return cancel()||Clock::now()>=whole;};require(!cancelled());
    auto resolver=access==Access::PrivateLan?management_tls_resolver():system_tls_resolver();ResolverCancel cleanup{*resolver};
    require(resolver->begin(endpoint.host,endpoint.port));std::optional<std::vector<std::string>> addresses;
    const auto resolving=std::min(whole,Clock::now()+std::chrono::seconds(3));
    while(!cancelled()&&Clock::now()<resolving){addresses=resolver->poll();if(addresses)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(!cancelled()&&addresses&&addresses->size()==1);const auto &address=addresses->front();if(access==Access::LoopbackTest)require(loopback(address));
    sockaddr_storage destination{};socklen_t size{};auto &v4=reinterpret_cast<sockaddr_in &>(destination);auto &v6=reinterpret_cast<sockaddr_in6 &>(destination);
    if(inet_pton(AF_INET,address.c_str(),&v4.sin_addr)==1){v4.sin_family=AF_INET;v4.sin_port=htons(endpoint.port);size=sizeof(v4);}
    else {require(inet_pton(AF_INET6,address.c_str(),&v6.sin6_addr)==1);v6.sin6_family=AF_INET6;v6.sin6_port=htons(endpoint.port);size=sizeof(v6);}
    Socket socket{::socket(destination.ss_family,SOCK_STREAM,0)};require(socket.fd>=0);
    const auto flags=fcntl(socket.fd,F_GETFL),descriptor=fcntl(socket.fd,F_GETFD);require(flags>=0&&descriptor>=0&&
        !fcntl(socket.fd,F_SETFL,flags|O_NONBLOCK)&&!fcntl(socket.fd,F_SETFD,descriptor|FD_CLOEXEC));
#ifdef SO_NOSIGPIPE
    const int yes=1;require(!setsockopt(socket.fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes)));
#endif
    const auto connected=connect(socket.fd,reinterpret_cast<const sockaddr *>(&destination),size);require(!connected||errno==EINPROGRESS);
    const auto connecting=std::min(whole,Clock::now()+std::chrono::seconds(3));bool ready=!connected;
    while(!cancelled()&&!ready&&Clock::now()<connecting){pollfd source{socket.fd,POLLOUT,0};const auto count=poll(&source,1,2);
        if(count<0){require(errno==EINTR);continue;}
        if(count){int error{};socklen_t length=sizeof(error);require(!getsockopt(socket.fd,SOL_SOCKET,SO_ERROR,&error,&length)&&!error);ready=true;}
    }
    require(ready&&!cancelled());
    // Finish allocation before releasing the descriptor guard.
    auto result=std::unique_ptr<PiPairSocket>(new PiPairSocket(socket.fd,address,std::min(whole,Clock::now()+std::chrono::seconds(8)),std::move(cancel)));
    socket.fd=-1;return result;
}
PiPairSocket::~PiPairSocket(){close();}
bool PiPairSocket::live() const{return fd_>=0&&Clock::now()<deadline_&&!cancelled_();}
int PiPairSocket::release_fd() noexcept{return std::exchange(fd_,-1);}
void PiPairSocket::close() noexcept{if(fd_>=0)::close(std::exchange(fd_,-1));}
wtp::IoResult PiPairSocket::read(std::span<std::uint8_t> bytes){
    if(!live())return {wtp::IoState::Failed,0};
    if(bytes.empty())return {wtp::IoState::WouldBlock,0};
    const auto count=recv(fd_,bytes.data(),bytes.size(),0);
    if(count>0)return {wtp::IoState::Progress,static_cast<std::size_t>(count)};
    if(!count)return {wtp::IoState::Closed,0};
    return {errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};
}
wtp::IoResult PiPairSocket::write(std::span<const std::uint8_t> bytes){
    if(!live())return {wtp::IoState::Failed,0};
    if(bytes.empty())return {wtp::IoState::WouldBlock,0};
#ifdef MSG_NOSIGNAL
    constexpr int flags=MSG_NOSIGNAL;
#else
    constexpr int flags=0;
#endif
    const auto count=send(fd_,bytes.data(),bytes.size(),flags);
    if(count>0)return {wtp::IoState::Progress,static_cast<std::size_t>(count)};
    return {count<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};
}
} // namespace wsprrypi
