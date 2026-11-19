// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_guarded_http.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <array>
#include <vector>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("guarded pairing HTTP check "+std::to_string(checks));}
struct Socket {int fd{-1};explicit Socket(int value):fd(value){}Socket(Socket &&other) noexcept:fd(std::exchange(other.fd,-1)){}Socket(const Socket &)=delete;~Socket(){if(fd>=0)::close(fd);}};
Socket connect_to(unsigned port){
    Socket socket(::socket(AF_INET,SOCK_STREAM,0));check(socket.fd>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(!connect(socket.fd,reinterpret_cast<const sockaddr *>(&address),sizeof(address)));check(!fcntl(socket.fd,F_SETFL,fcntl(socket.fd,F_GETFL)|O_NONBLOCK));
#ifdef SO_NOSIGPIPE
    const int yes=1;check(!setsockopt(socket.fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes)));
#endif
    return socket;
}
void send_to(Socket &socket,const std::string &wire){
    int flags=0;
#ifdef MSG_NOSIGNAL
    flags=MSG_NOSIGNAL;
#endif
    check(send(socket.fd,wire.data(),wire.size(),flags)==static_cast<ssize_t>(wire.size()));
}
std::string receive(Socket &socket){
    std::string result;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(std::chrono::steady_clock::now()<end){pollfd source{socket.fd,POLLIN|POLLHUP|POLLERR,0};const auto ready=poll(&source,1,10);if(ready<=0)continue;
        std::array<char,4096> bytes{};const auto count=recv(socket.fd,bytes.data(),bytes.size(),0);if(count<=0)break;result.append(bytes.data(),count);}
    return result;
}
std::string request(const std::string &path,const std::string &extra="Content-Length: 2\r\n",const std::string &body="{}"){
    return "POST "+path+" HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"+extra+"\r\n"+body;
}
}
int main(){try{
    std::atomic_uint64_t elapsed{0},requests{0};
    // Keep protocol/deadline fuzzing independent of the separately tested
    // real quota policy, without changing the listener's I/O deadline clock.
    PiPairAdmission budget([&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(requests.fetch_add(100));});
    PiPairGuardedHttpServer server(budget,[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(elapsed.load());});
    std::atomic_uint dispatched{0};server.Post("/api/pair/v1/hello",[&](const auto &,auto &response){++dispatched;response.set_content("{}","application/json");});
    server.Post("/api/control/v1/host/pairing/connect",[&](const auto &,auto &response){++dispatched;response.set_content("{}","application/json");});
    server.Post("/api/control/v1/targets/fixture/settings",[&](const auto &,auto &response){++dispatched;elapsed.fetch_add(9000);response.set_content("{}","application/json");});
    server.Post("/api/control/v1/targets/fixture/enable",[&](const auto &,auto &response){++dispatched;elapsed.fetch_add(25000);response.set_content("{}","application/json");});
    server.Get("/legacy",[](const auto &,auto &response){response.set_content("legacy","text/plain");});
    const auto port=server.bind_to_any_port("127.0.0.1");check(port>0);
    std::thread listening([&]{server.listen_after_bind();});struct Join {PiPairGuardedHttpServer &server;std::thread &thread;~Join(){server.stop();if(thread.joinable())thread.join();}} join{server,listening};
    server.wait_until_ready();
    for(const auto *path:{"/api/pair/v1/hello","/api/control/v1/host/pairing/connect","/api/control/v1/targets/fixture/settings"}){
        auto socket=connect_to(port);send_to(socket,request(path));const auto response=receive(socket);
        check(response.starts_with("HTTP/1.1 200")&&response.find("Connection: close")!=std::string::npos&&response.find("Cache-Control: no-store")!=std::string::npos&&response.ends_with("{}"));
    }
    {auto socket=connect_to(port);send_to(socket,request("/api/control/v1/targets/fixture/enable"));check(!receive(socket).starts_with("HTTP/1.1 200"));}
    {auto socket=connect_to(port);send_to(socket,request("/api/control/v1/host/pairing/connect","Content-Length: 4096\r\n",std::string(4096,'x')));check(receive(socket).starts_with("HTTP/1.1 200"));}
    const auto accepted=dispatched.load();
    for(const auto *path:{"/api/control/v1/host/pairing/connect","/api/control/v1/host/pairing/remove"}){
        auto socket=connect_to(port);send_to(socket,request(path,"Content-Length: 4097\r\n",""));
        pollfd source{socket.fd,POLLIN|POLLHUP|POLLERR,0};check(poll(&source,1,1000)>0);
        char byte{};check(recv(socket.fd,&byte,1,0)<=0&&dispatched==accepted);
    }
    for(const auto &wire:{std::string("POST  /api/pair/v1/hello HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 2\r\n\r\n{}"),
        std::string("POST /api/pair/v1/hello HTTP/1.1\r\nHost: 127.0.0.1\nContent-Length: 2\n\n{}")}){
        auto socket=connect_to(port);send_to(socket,wire);check(!receive(socket).starts_with("HTTP/1.1 200")&&dispatched==accepted);
    }
    for(const auto &extra:{std::string("Content-Length: 4097\r\n"),std::string("Transfer-Encoding: chunked\r\n"),std::string("Content-Length: 2\r\ncontent-length: 2\r\n"),std::string("Content-Length: 02\r\n"),"X-Overflow: "+std::string(8192,'x')+"\r\nContent-Length: 2\r\n"}){
        auto socket=connect_to(port);send_to(socket,request("/api/pair/v1/hello",extra));check(!receive(socket).starts_with("HTTP/1.1 200")&&dispatched==accepted);
    }
    std::vector<Socket> held;
    for(unsigned i=0;i<4;++i){held.push_back(connect_to(port));send_to(held.back(),request("/api/pair/v1/hello","Content-Length: 2\r\n",""));}
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);bool full=false;
    while(std::chrono::steady_clock::now()<end){if(!budget.try_incoming()){full=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}check(full);
    auto refused=connect_to(port);send_to(refused,request("/api/pair/v1/hello"));check(!receive(refused).starts_with("HTTP/1.1 200")&&dispatched==accepted);
    elapsed.fetch_add(8001);for(auto &socket:held)check(!receive(socket).starts_with("HTTP/1.1 200"));held.clear();
    check(budget.try_incoming().has_value());
    auto encoded=connect_to(port);send_to(encoded,request("/%61pi/pair/v1/hello","Content-Length: 2\r\n",""));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));elapsed.fetch_add(8001);check(!receive(encoded).starts_with("HTTP/1.1 200")&&dispatched==accepted);
    httplib::Client legacy("127.0.0.1",port);legacy.set_keep_alive(true);
    const auto first=legacy.Get("/legacy"),second=legacy.Get("/legacy");check(first&&second&&first->status==200&&second->body=="legacy");
    auto stopping=connect_to(port);send_to(stopping,request("/api/pair/v1/hello","Content-Length: 2\r\n",""));std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto before=std::chrono::steady_clock::now();server.stop();listening.join();check(std::chrono::steady_clock::now()-before<std::chrono::seconds(2)&&dispatched==accepted);
    std::cout<<"PASS guarded HTTP absolute bounds, early framing refusal, shared admission and legacy keep-alive: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
