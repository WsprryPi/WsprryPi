// SPDX-License-Identifier: MIT
// Actual WebSocket transport with an explicitly injected software session.
// Local transmitter dispatch is excluded and must never be reached here.
#include "web_socket.hpp"
#include "websocket_upgrade_guard.hpp"
#include "logging.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
#include <source_location>
namespace ns=wsprrypi::native_socket;
void WebSocketServer::stop_command_runtime(){if(command_runtime_.load())throw std::logic_error("Local command runtime entered the native transport fixture");}
void WebSocketServer::handleMessage(const std::string &){throw std::logic_error("Local dispatch entered the native transport fixture");}
void WebSocketServer::stopBoundedTone(){throw std::logic_error("Local tone lifecycle entered the native transport fixture");}
namespace {
void check(bool value,const std::source_location where=std::source_location::current()){if(!value)throw std::runtime_error("Native WebSocket transport fixture failed at line "+std::to_string(where.line()));}
struct Socket{ns::Handle value{ns::invalid};~Socket(){ns::close(value);}};
template<class F>void until(F ready,const std::source_location where=std::source_location::current()){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!ready()){check(std::chrono::steady_clock::now()<deadline,where);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
unsigned short port(ns::Handle socket){sockaddr_in address{};ns::Length length=sizeof(address);check(!::getsockname(socket,reinterpret_cast<sockaddr *>(&address),&length));return ntohs(address.sin_port);}
unsigned short free_port(){for(unsigned offset=0;offset<200;++offset){Socket socket{ns::open_stream(AF_INET)};check(socket.value!=ns::invalid);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(static_cast<unsigned short>(30000+(::GetCurrentProcessId()+offset)%18000));if(!::bind(socket.value,reinterpret_cast<sockaddr *>(&address),sizeof(address)))return port(socket.value);}throw std::runtime_error("No fixture port available within the application range");}
void connect(Socket &socket,unsigned short selected){socket.value=ns::open_stream(AF_INET);check(socket.value!=ns::invalid);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(selected);
    const auto result=::connect(socket.value,reinterpret_cast<sockaddr *>(&address),sizeof(address));check(result==0||ns::pending(ns::error()));until([&]{return ns::poll_write(socket.value)>0;});check(ns::connected(socket.value));}
void write(ns::Handle socket,const std::string &bytes){std::size_t offset{};until([&]{const auto sent=ns::send(socket,bytes.data()+offset,bytes.size()-offset);if(sent<0){check(ns::retry(ns::error()));return false;}check(sent>0);offset+=static_cast<std::size_t>(sent);return offset==bytes.size();});}
std::string read(ns::Handle socket,std::size_t minimum){std::string bytes;until([&]{char buffer[4096];const auto count=ns::receive(socket,buffer,sizeof buffer);if(count<0){check(ns::retry(ns::error()));return false;}check(count>0);bytes.append(buffer,static_cast<std::size_t>(count));return bytes.size()>=minimum;});return bytes;}
std::string request(unsigned short selected,bool valid=true){return "GET / HTTP/1.1\r\nHost: localhost:"+std::to_string(selected)+"\r\nOrigin: http://"+(valid?std::string("localhost"):std::string("foreign.invalid"))+":"+std::to_string(selected)+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";}
std::string frame(const std::string &bytes,unsigned char opcode=1){check(bytes.size()<126);std::string result{static_cast<char>(0x80|opcode),static_cast<char>(0x80|bytes.size()),'1','2','3','4'};for(std::size_t index=0;index<bytes.size();++index)result+=static_cast<char>(bytes[index]^"1234"[index%4]);return result;}
}
class WebSocketLifecycleTest{
public:static void native_transport(){
    std::atomic<unsigned> commands{};WebSocketServer server;
    server.set_backend_session_factory([&](const std::string &upgrade,const std::string &peer){
        const auto admitted=evaluate_websocket_upgrade(upgrade,peer,{true,"localhost",{},{}},PrivilegedNetworkMode::enforced);
        if(admitted.decision!=WebSocketUpgradeGuardDecision::allowed)return std::shared_ptr<WebSocketBackendSession>{};
        auto session=std::make_shared<WebSocketBackendSession>();session->key=admitted.key;session->current=[]{return true;};
        session->command=[&](const std::string &message){check(message=="{\"echo\":\"fixture\"}");++commands;return std::string("{\"fixture_ack\":true}");};return session;
    });
    const auto selected=free_port();check(server.start(selected,60,true,WebSocketLoopbackFamily::IPv4));
    DWORD flags{};check(::GetHandleInformation(reinterpret_cast<HANDLE>(server.listen_fd_),&flags)&&!(flags&HANDLE_FLAG_INHERIT));
    {Socket duplicate{ns::open_stream(AF_INET)};check(duplicate.value!=ns::invalid);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(selected);check(::bind(duplicate.value,reinterpret_cast<sockaddr *>(&address),sizeof(address))!=0);}
    {
        Socket client;connect(client,selected);std::cout<<"Native transport connected\n"<<std::flush;write(client.value,request(selected));const auto upgraded=read(client.value,129);
        check(upgraded.starts_with("HTTP/1.1 101 ")&&upgraded.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")!=std::string::npos);
        std::cout<<"Native transport upgraded\n"<<std::flush;write(client.value,frame("{\"echo\":\"fixture\"}"));const auto reply=read(client.value,22);check(static_cast<unsigned char>(reply[0])==0x81&&reply.substr(2)=="{\"fixture_ack\":true}"&&commands==1);
        std::cout<<"Native transport echoed\n"<<std::flush;write(client.value,frame("",9));const auto pong=read(client.value,2);check(static_cast<unsigned char>(pong[0])==0x8a&&pong.size()==2&&pong[1]==0);
        ns::shutdown(client.value);
    }
    until([&]{std::lock_guard lock(server.clients_mutex_);return !server.active_client_handlers_;});server.stop();check(server.listen_fd_==ns::invalid);
    check(server.start(selected,60,true,WebSocketLoopbackFamily::IPv4));
    {Socket denied;connect(denied,selected);write(denied.value,request(selected,false));const auto response=read(denied.value,12);check(response.starts_with("HTTP/1.1 403 "));check(commands==1);}
    Socket idle;connect(idle,selected);until([&]{std::lock_guard lock(server.clients_mutex_);return !server.handshaking_sockets_.empty();});
    auto stopped=std::async(std::launch::async,[&]{server.stop();});check(stopped.wait_for(std::chrono::seconds(3))==std::future_status::ready);stopped.get();
    check(server.listen_fd_==ns::invalid);server.stop();
    for(unsigned iteration=0;iteration<5;++iteration){
        check(server.start(selected,60,true,WebSocketLoopbackFamily::IPv4));
        auto immediate=std::async(std::launch::async,[&]{server.stop();});check(immediate.wait_for(std::chrono::seconds(3))==std::future_status::ready);immediate.get();
        check(!server.listening_port()&&server.listen_fd_==ns::invalid);
    }
    std::cout<<"PASS native WebSocket exclusive ownership, noninherited handles, upgrade/framing, origin refusal, echo/pong, same-port restart, and idle-handshake shutdown; injected software session only\n";
}};
void test_native_windows_websocket_transport(){WebSocketLifecycleTest::native_transport();}
