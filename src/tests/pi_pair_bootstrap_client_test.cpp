// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_bootstrap_client.hpp"
#include "wtp_integration/pi_pair_guarded_http.hpp"
#include <atomic>
#include <iostream>
#include <thread>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
std::atomic_uint checks{0};
void check(bool value){const auto count=++checks;if(!value)throw std::runtime_error("bootstrap client check "+std::to_string(count));}
std::string id(char c){return std::string(32,c);}
}
int main(){try{
    using State=PiPairBootstrapResult::State;
    PiPairAdmission incoming,outgoing;PiPairGuardedHttpServer server(incoming);std::atomic_uint dispatches{0},fault{0};
    const Json window{{"contract","Wsprry-Pair/1"},{"schema","window"},{"suite","SPAKE2+-P256-SHA256-HKDF-HMAC"},{"device_id",id('a')},
        {"boot_id",id('b')},{"open",false},{"window_id",nullptr},{"pair_id",nullptr},{"authorization_generation","0"},{"action",nullptr}};
    server.Get("/api/pair/v1/window",[&](const auto &request,auto &response){
        ++dispatches;check(!request.has_header("Cookie")&&!request.has_header("Authorization"));auto body=window;
        if(fault==1){response.status=302;response.set_header("Location","http://169.254.169.254/");}
        if(fault==2)response.set_header("Content-Encoding","gzip");
        if(fault==3)body.erase("action");
        response.set_content(body.dump(),"application/json");
    });
    server.Post("/api/pair/v1/hello",[&](const auto &request,auto &response){
        ++dispatches;check(request.body.find("\"code\"")==std::string::npos&&!request.has_header("Cookie")&&!request.has_header("Origin"));
        response.set_content(Json{{"contract","Wsprry-Pair/1"},{"schema","challenge"},{"suite","SPAKE2+-P256-SHA256-HKDF-HMAC"},
            {"pair_id",id('b')},{"target_nonce",std::string(43,'A')}}.dump(),"application/json");
    });
    server.Post("/api/pair/v1/commit",[&](const auto &,auto &response){++dispatches;response.set_content(window.dump(),"application/json");});
    const auto port=server.bind_to_any_port("127.0.0.1");check(port>0);std::thread listening([&]{server.listen_after_bind();});
    struct Join {PiPairGuardedHttpServer &server;std::thread &thread;~Join(){server.stop();if(thread.joinable())thread.join();}} join{server,listening};server.wait_until_ready();
    PiPairBootstrapClient client(PiPairSocket::Access::LoopbackTest);PiPairEndpoint endpoint{"127.0.0.1",static_cast<unsigned>(port)};
    auto connection=outgoing.try_outgoing();check(connection.has_value());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    auto result=client.request(endpoint,"window",nullptr,*connection,deadline);check(result.state==State::Replied&&result.reply&&Json::parse(result.reply->payload.body)==window);
    const Json hello{{"contract","Wsprry-Pair/1"},{"schema","hello"},{"suite","SPAKE2+-P256-SHA256-HKDF-HMAC"},
        {"window_id",id('c')},{"pair_id",id('b')},{"action","pair"},{"controller_id",id('d')},{"controller_boot_id",id('e')},
        {"target_id",id('a')},{"target_boot_id",id('b')},{"authorization_generation","0"},{"controller_nonce",std::string(43,'A')}};
    result=client.request(endpoint,"hello",hello,*connection,deadline);check(result.state==State::Replied&&Json::parse(result.reply->payload.body)["schema"]=="challenge");
    const auto accepted=dispatches.load();auto invalid=hello;invalid["code"]="2345ABCD";
    check(client.request(endpoint,"hello",invalid,*connection,deadline).state==State::Refused&&dispatches==accepted);
    for(const auto *resource:{"settings","rotate/prepare","../control/v1/targets","window?code=not-public"})
        check(client.request(endpoint,resource,hello,*connection,deadline).state==State::Refused&&dispatches==accepted);
    check(client.request(endpoint,"window",Json::object(),*connection,deadline).state==State::Refused&&dispatches==accepted);
    check(client.request(endpoint,"window",nullptr,*connection,std::chrono::steady_clock::now()-std::chrono::seconds(1)).state==State::Refused&&dispatches==accepted);
    check(client.request(endpoint,"window",nullptr,*connection,deadline,[]{return true;}).state==State::Refused&&dispatches==accepted);
    PiPairBootstrapClient production;
    check(production.request(endpoint,"window",nullptr,*connection,deadline).state==State::Unavailable&&dispatches==accepted);
    for(unsigned mode=1;mode<=3;++mode){fault=mode;check(client.request(endpoint,"window",nullptr,*connection,deadline).state==State::Unavailable);}
    fault=0;const auto before=dispatches.load();
    const Json commit{{"contract","Wsprry-Pair/1"},{"schema","commit"},{"pair_id",id('b')},{"request_digest",std::string(64,'a')},{"proof",std::string(43,'A')}};
    unsigned gates=0;
    result=client.request(endpoint,"commit",commit,*connection,deadline,[]{return false;},[&]()->std::optional<PiPairMutationGuard>{++gates;return {};});
    check(result.state==State::Unavailable&&!result.reply&&dispatches==before&&gates==1);
    result=client.request(endpoint,"commit",commit,*connection,deadline);check(result.state==State::Uncertain&&!result.reply&&dispatches==before+1);
    connection->cancel();check(client.request(endpoint,"window",nullptr,*connection,deadline).state==State::Refused);
    std::cout<<"PASS explicit bounded bootstrap transport, no code/settings forwarding, strict action and no fallback/retry: "<<checks.load()<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
