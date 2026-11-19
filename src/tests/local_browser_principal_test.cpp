// SPDX-License-Identifier: MIT
#include "wtp_integration/management_browser_session.hpp"
#include "wtp_integration/browser_http.hpp"
#include "wtp_integration/pi_pair_http_principals.hpp"
#include "pairing_provider/profile_vector.hpp"
#include <type_traits>
#include <iostream>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("local principal check "+std::to_string(checks));}
httplib::Request request(){
    httplib::Request value;value.method="POST";value.path="/api/control/v1/host/pairing/window";value.target=value.path;
    value.remote_addr="192.168.1.20";value.headers={{"Host","controller.local"},{"Origin","http://controller.local"},
        {"Content-Type","application/json"},{"X-WsprryPico-Request","1"},{"Sec-Fetch-Site","same-origin"}};
    value.body=R"({"operation_id":"artificial-fixture"})";return value;
}
}
int main(){try{
    static_assert(!std::is_convertible_v<PiPairBootstrapPrincipal,LocalBrowserPrincipal>);
    static_assert(!std::is_convertible_v<LocalPairingReadPrincipal,LocalBrowserPrincipal>);
    const SupportRequestGuardSnapshot network{true,"controller.local",{},{{"192.168.1.10","255.255.255.0"}}};
    ManagementBrowserSessions sessions;auto command=request();
    check(!sessions.authorize_local_mutation(command,network));
    auto bootstrap=command;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;
    httplib::Response response;check(sessions.establish(bootstrap,response));
    const auto cookie=response.get_header_value("Set-Cookie");
    command.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));
    command.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
    const auto principal=sessions.authorize_local_mutation(command,network);check(principal.has_value());
    const auto parsed=wtp_browser_http_request(command);check(parsed&&principal->matches(*parsed)&&!principal->session_scope().empty());
    for(const auto field:{"body","method","path","revision","member_context","mutation_context"}){
        auto changed=*parsed;
        if(std::string(field)=="body")changed.body+=" ";
        else if(std::string(field)=="method")changed.method="GET";
        else if(std::string(field)=="path")changed.path+="/extra";
        else if(std::string(field)=="revision")changed.revision="different";
        else if(std::string(field)=="member_context")changed.member_context="different";
        else changed.mutation_context=false;
        check(!principal->matches(changed));
    }
    for(const auto *header:{"Host","Origin","Cookie","Content-Type","X-WsprryPico-Request","X-WsprryPi-CSRF","Sec-Fetch-Site"}){
        auto changed=command;changed.headers.emplace(header,changed.get_header_value(header));check(!sessions.authorize_local_mutation(changed,network));
        changed=command;changed.headers.erase(header);
        if(std::string(header)!="Sec-Fetch-Site")check(!sessions.authorize_local_mutation(changed,network));
    }
    for(const auto &pair:std::initializer_list<std::pair<const char *,const char *>>{
        {"Origin","http://evil.local"},{"Host","evil.local"},{"X-WsprryPi-CSRF","wrong"},
        {"Sec-Fetch-Site","cross-site"},{"Content-Type","text/plain"},{"X-WsprryPico-Request","0"}}){
        auto changed=command;changed.headers.erase(pair.first);changed.headers.emplace(pair.first,pair.second);check(!sessions.authorize_local_mutation(changed,network));
    }
    auto changed=command;changed.remote_addr="8.8.8.8";check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.remote_addr="127.0.0.1";changed.headers.emplace("X-WsprryPi-Client-Address","8.8.8.8");check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.remote_addr="8.8.8.8";changed.headers.emplace("X-WsprryPi-Client-Address","192.168.1.20");check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.headers.emplace("X-WsprryPi-Client-Address","8.8.8.8");check(sessions.authorize_local_mutation(changed,network).has_value());
    changed=command;changed.headers.emplace("Content-Length","0");check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.headers.emplace("Content-Length",std::to_string(changed.body.size()));check(sessions.authorize_local_mutation(changed,network).has_value());
    changed.headers.emplace("Transfer-Encoding","chunked");check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.target+="?extra=true";check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.method="GET";check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.path+="/extra";changed.target=changed.path;check(!sessions.authorize_local_mutation(changed,network));
    changed=command;changed.headers.emplace("X-Artificial",std::string(8192,'x'));check(!sessions.authorize_local_mutation(changed,network));
    auto unavailable=network;unavailable.discovery_succeeded=false;check(!sessions.authorize_local_mutation(command,unavailable));
    sessions.retire();check(!principal->matches(*parsed));check(!sessions.authorize_local_mutation(command,network));
    httplib::Response renewed;check(sessions.establish(bootstrap,renewed));
    const auto replacement=renewed.get_header_value("Set-Cookie");
    command.headers.erase("Cookie");command.headers.emplace("Cookie",replacement.substr(0,replacement.find(';')));
    command.headers.erase("X-WsprryPi-CSRF");command.headers.emplace("X-WsprryPi-CSRF",renewed.get_header_value("X-WsprryPi-CSRF"));
    const auto fresh=sessions.authorize_local_mutation(command,network);check(fresh&&fresh->matches(*parsed));
    check(fresh->session_scope()!=principal->session_scope()&&!principal->matches(*parsed));
    auto state=request();state.method="GET";state.path="/api/control/v1/host/pairing";state.target=state.path;state.body.clear();
    const auto read=authorize_local_pairing_read(state,network);check(read.has_value());
    check(read->matches(*wtp_browser_http_request(state))&&!read->matches(*parsed));
    auto discovery=state;discovery.path+="/discovery";discovery.target=discovery.path;
    const auto discovered=authorize_local_pairing_read(discovery,network);check(discovered.has_value());
    check(discovered->matches(*wtp_browser_http_request(discovery))&&!discovered->matches(*wtp_browser_http_request(state)));
    check(!read->matches(*wtp_browser_http_request(discovery)));
    discovery.path+="/extra";discovery.target=discovery.path;check(!authorize_local_pairing_read(discovery,network));
    check(!authorize_pi_pair_bootstrap(state,network));state.body="{}";check(!authorize_local_pairing_read(state,network));
    auto bootstrap_request=request();bootstrap_request.path="/api/pair/v1/hello";bootstrap_request.target=bootstrap_request.path;
    bootstrap_request.headers.erase("Origin");auto hello=nlohmann::json::parse(context);hello.erase("target_nonce");bootstrap_request.body=hello.dump();
    const auto public_request=authorize_pi_pair_bootstrap(bootstrap_request,network);check(public_request.has_value());
    check(public_request->matches(*wtp_browser_http_request(bootstrap_request))&&!public_request->matches(*parsed));
    check(!sessions.authorize_local_mutation(bootstrap_request,network));check(!authorize_local_pairing_read(bootstrap_request,network));
    for(const auto *header:{"Host","Origin","Content-Type","Content-Length","Transfer-Encoding"}){
        auto wrong=bootstrap_request;wrong.headers.emplace(header,"wrong");check(!authorize_pi_pair_bootstrap(wrong,network));
    }
    auto wrong=bootstrap_request;wrong.headers.emplace("Content-Encoding","gzip");check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;wrong.headers.emplace("Transfer-Encoding","chunked");check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;wrong.remote_addr="8.8.8.8";check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;wrong.remote_addr="127.0.0.1";wrong.headers.emplace("X-WsprryPi-Client-Address","8.8.8.8");check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;wrong.target+="?same=true";check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;hello["code"]="2345-ABCD";wrong.body=hello.dump();check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;wrong.path="/api/pair/v1/rotate/prepare";wrong.target=wrong.path;check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=bootstrap_request;wrong.path="/api/pair/v1/window";wrong.target=wrong.path;wrong.method="GET";wrong.body.clear();
    check(authorize_pi_pair_bootstrap(wrong,network).has_value());
    wrong.headers.emplace("Transfer-Encoding","chunked");check(!authorize_pi_pair_bootstrap(wrong,network));
    wrong=command;wrong.headers.emplace("Content-Encoding","gzip");check(!sessions.authorize_local_mutation(wrong,network));
    wrong=command;wrong.headers.erase("Content-Length");wrong.headers.emplace("Transfer-Encoding","chunked");check(!sessions.authorize_local_mutation(wrong,network));
    auto receipt=bootstrap_request;receipt.method="GET";receipt.body.clear();receipt.headers.erase("Content-Length");
    receipt.path="/api/control/v1/host/pairing/operations/"+std::string(32,'a');receipt.target=receipt.path;
    const auto read_receipt=authorize_local_pairing_read(receipt,network);check(read_receipt.has_value());
    check(read_receipt->matches(*wtp_browser_http_request(receipt)));
    auto other_receipt=receipt;other_receipt.path.back()='b';other_receipt.target=other_receipt.path;check(!read_receipt->matches(*wtp_browser_http_request(other_receipt)));
    for(const auto &id:{std::string(),std::string(31,'a'),std::string(32,'A'),std::string(33,'a'),std::string(32,'a')+"/extra"}){
        auto invalid=receipt;invalid.path="/api/control/v1/host/pairing/operations/"+id;invalid.target=invalid.path;check(!authorize_local_pairing_read(invalid,network));
    }
    std::cout<<"PASS real protected boundary mints request-bound local browser principals: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
