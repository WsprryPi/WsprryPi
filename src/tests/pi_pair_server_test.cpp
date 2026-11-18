// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_server.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <source_location>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
using namespace wsprrypi;
namespace {
std::atomic_uint checks{0};
void check(bool value,const std::source_location at=std::source_location::current()){const auto count=++checks;if(!value)throw std::runtime_error("pair server check "+std::to_string(count)+" at "+std::to_string(at.line()));}
std::string id(char c){return std::string(32,c);}
PiPairSecret secret(unsigned char byte){std::array<unsigned char,32> bytes{};bytes.fill(byte);return PiPairSecret(bytes);}
const PiPairTlsIdentity management{PiPairTlsPurpose::Management,id('c'),"1","1"},retirement{PiPairTlsPurpose::Retirement,id('d'),"7",""};
std::shared_ptr<const PiPairTlsCredential> credential(const PiPairTlsIdentity &identity,bool client){
    if(identity!=management&&identity!=retirement)return {};
    auto key=identity.purpose==PiPairTlsPurpose::Management?secret(1):pi_pair_retirement_tls_key(secret(3),id('b'),id('a'),id('d'),"7");
    return std::make_shared<const PiPairTlsCredential>(PiPairTlsCredential{identity,id(client?'b':'a'),id(client?'a':'b'),std::move(key)});
}
struct Socket {
    int fd{-1};
    Socket()=default;explicit Socket(int value):fd(value){}
    Socket(Socket &&other) noexcept:fd(std::exchange(other.fd,-1)){}
    Socket(const Socket &)=delete;
    ~Socket(){if(fd>=0)::close(fd);}
};
Socket connect_to(unsigned port){
    Socket socket(::socket(AF_INET,SOCK_STREAM,0));check(socket.fd>=0);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(!connect(socket.fd,reinterpret_cast<const sockaddr *>(&address),sizeof(address)));
    check(!fcntl(socket.fd,F_SETFL,fcntl(socket.fd,F_GETFL)|O_NONBLOCK));
#ifdef SO_NOSIGPIPE
    const int yes=1;check(!setsockopt(socket.fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes)));
#endif
    return socket;
}
PicoHttpResponse exchange(PiPairServer &server,const PiPairTlsIdentity &identity,const std::string &path,const std::string &method="GET",std::string wire={}){
    auto socket=connect_to(server.port());const auto fd=std::exchange(socket.fd,-1);
    PiPairTlsChannel channel(fd,[&](const auto &requested){return requested==identity?credential(requested,true):nullptr;},identity);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);bool ready=false;
    while(std::chrono::steady_clock::now()<deadline){
        const auto state=channel.handshake();if(state==PiPairTlsChannel::Handshake::Failed)break;
        if(state==PiPairTlsChannel::Handshake::Ready){ready=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(ready);
    const auto now=[] {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    if(wire.empty())wire=pi_pair_http_request(method,path,"127.0.0.1:"+std::to_string(server.port()),method=="GET"?"":"{}");
    return wtp_json_http_exchange(channel,now,wire,now()+1500,method!="GET");
}
}
int main(){try{
    // Route coverage must not consume the rate budget intended for the later
    // connection-capacity assertion. Rate policy itself has its own clocked
    // tests; only this quota clock advances, never TLS/I/O worker deadlines.
    auto request_time=std::chrono::steady_clock::time_point{};
    PiPairAdmission admission([&]{return request_time+=std::chrono::milliseconds(100);});
    std::atomic_uint lookups{0},finished{0},dispatches{0};PiPairServer *listener{};
    check(!setenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS","1",1));
    PiPairServer production(admission,[](const auto &){return std::shared_ptr<const PiPairTlsCredential>{};},
        [](const auto &){return false;},[](const auto &,const auto &,auto &,const auto &){return PiPairHttpReply{};});
    check(!production.start()&&!production.ready()&&!production.port());
    PiPairServer server(admission,[&](const auto &identity){++lookups;return credential(identity,false);},
        [&](const auto &principal){++finished;return principal.self_id()==id('a')&&principal.peer_id()==id('b');},
        [&](const auto &principal,const httplib::Request &request,auto &,const std::function<bool()> &cancelled){
            check(principal.self_id()==id('a')&&principal.peer_id()==id('b')&&request.remote_addr=="127.0.0.1"&&!cancelled());++dispatches;
            if(request.path.ends_with(id('e')))listener->stop();
            return PiPairHttpReply{{200,nlohmann::json{{"fixture",true},{"purpose",principal.identity().purpose==PiPairTlsPurpose::Management?"management":"retirement"}}.dump(),{}},{},{}};
        },PiPairServer::Access::LoopbackTest);listener=&server;
    check(!server.ready()&&!server.port()&&server.start()&&server.ready()&&server.port()&&!server.start());
    const auto normal=exchange(server,management,"/api/pair/v1/operations/"+id('a'));check(normal.status==200&&nlohmann::json::parse(normal.body)["purpose"]=="management");
    const auto retire=exchange(server,retirement,"/api/pair/v1/retirement/status","POST");check(retire.status==200&&nlohmann::json::parse(retire.body)["purpose"]=="retirement");
    const auto before_collection=dispatches.load();
    for(const auto &[resource,method]:std::initializer_list<std::pair<const char *,const char *>>{
        {"hostname","GET"},{"wifi","PUT"},{"wifi","DELETE"},{"restart","POST"},{"ap","POST"}})
        check(exchange(server,management,"/api/control/v1/targets/"+id('a')+"/pico/"+resource,method).status==200);
    check(exchange(server,management,"/api/control/v1/targets").status==200);
    const auto accepted=dispatches.load();check(accepted==before_collection+6);
    check(exchange(server,management,"/api/control/v1/targets","POST").status==403);
    check(exchange(server,retirement,"/api/control/v1/targets").status==403);
    for(const auto &path:{"/api/pair/v1/window","/api/control/v1/host/pairing/window","/api/control/v1/host/fleet","/api/jobs",
        "/api/control/v2/targets/"})check(exchange(server,management,path).status==403);
    check(exchange(server,retirement,"/api/control/v1/targets/"+id('a')+"/context").status==403);
    check(exchange(server,management,"/api/control/v1/host/pairing/operations/"+id('a')).status==403);
    check(exchange(server,retirement,"/api/control/v1/host/pairing/operations/"+id('a')).status==403);
    check(exchange(server,management,"/api/control/v1/targets/"+id('a')+"/pico/jobs","POST").status==403);
    check(dispatches==accepted);
    const auto malformed=pi_pair_http_request("GET","/api/pair/v1/operations/"+id('a'),"target.local");
    auto ambiguous=malformed;ambiguous.insert(ambiguous.find("\r\n\r\n"),"\r\nCookie: not-a-machine-principal");
    check(exchange(server,management,"/api/pair/v1/operations/"+id('a'),"GET",ambiguous).status!=200&&dispatches==accepted);
    {
        auto plaintext=connect_to(server.port());const std::string text="GET /api/pair/v1/window HTTP/1.1\r\nHost: target.local\r\n\r\n";
        int flags=0;
#ifdef MSG_NOSIGNAL
        flags=MSG_NOSIGNAL;
#endif
        (void)send(plaintext.fd,text.data(),text.size(),flags);pollfd ready{plaintext.fd,POLLIN|POLLHUP|POLLERR,0};check(poll(&ready,1,2000)>0);
        std::array<char,256> bytes{};const auto count=recv(plaintext.fd,bytes.data(),bytes.size(),0);
        check(count<=0||!std::string_view(bytes.data(),count).starts_with("HTTP/"));check(dispatches==accepted);
    }
    // A stop requested by a handler signals its owner without joining itself.
    check(exchange(server,management,"/api/pair/v1/operations/"+id('e')).status!=200);
    server.stop();check(!server.ready()&&!server.port());check(server.start());
    std::vector<Socket> held;for(unsigned i=0;i<4;++i)held.push_back(connect_to(server.port()));
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);bool full=false;
    while(std::chrono::steady_clock::now()<end){if(!admission.try_incoming()){full=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    check(full);
    auto refused=connect_to(server.port());pollfd ready{refused.fd,POLLIN|POLLHUP|POLLERR,0};check(poll(&ready,1,2000)>0);
    std::array<char,32> bytes{};check(recv(refused.fd,bytes.data(),bytes.size(),0)<=0);
    const auto before_stop=std::chrono::steady_clock::now();server.stop();
    check(std::chrono::steady_clock::now()-before_stop<std::chrono::seconds(2)&&!server.ready());held.clear();server.stop();
    check(admission.try_incoming().has_value()&&finished>=10&&lookups>=finished);
    std::cout<<"PASS actual paired TLS listener, purpose routing, HTTP refusal, shared capacity and bounded shutdown: "<<checks.load()<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
