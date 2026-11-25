// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_http.hpp"
#include <iostream>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair HTTP check "+std::to_string(checks));}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused);}
PiPairHttpDecoder::State decode(const std::string &wire,std::size_t chunk=4096){
    PiPairHttpDecoder parser;
    for(std::size_t pos=0;pos<wire.size();pos+=chunk){
        const auto count=std::min(chunk,wire.size()-pos);
        parser.append(std::span(reinterpret_cast<const std::uint8_t *>(wire.data()+pos),count));
    }
    return parser.state();
}
std::string raw(std::string method="POST",std::string path="/api/pair/v1/rotate/commit",std::string fields={},std::string body="{}"){
    return method+" "+path+" HTTP/1.1\r\nHost: target.local:31418\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\n"+fields+"\r\n"+body;
}
}
int main(){try{
    using State=PiPairHttpDecoder::State;
    const auto wire=pi_pair_http_request("POST","/api/pair/v1/rotate/commit","target.local:31418","{}",{{"X-WsprryPi-Operation-ID",std::string(32,'a')}});
    for(const auto chunk:{1U,2U,17U,4096U})check(decode(wire,chunk)==State::Ready);
    PiPairHttpDecoder parser;refuses([&]{(void)parser.request();});check(parser.append({})==State::More);
    check(parser.append(std::span(reinterpret_cast<const std::uint8_t *>(wire.data()),wire.size()))==State::Ready);
    check(parser.request().method=="POST"&&parser.request().path=="/api/pair/v1/rotate/commit"&&parser.request().body=="{}"&&
        parser.request().get_header_value("X-WsprryPi-Operation-ID")==std::string(32,'a'));
    check(parser.append({})==State::Ready);const std::uint8_t extra='x';check(parser.append(std::span(&extra,1))==State::Refused);
    for(const auto *header:{"Host: target.local\r\n","host: target.local\r\n","Content-Length: 2\r\n","content-length: 2\r\n",
        "Transfer-Encoding: chunked\r\n","Content-Encoding: gzip\r\n","Cookie: browser\r\n","Authorization: Bearer fake\r\n",
        "Origin: http://target.local\r\n","X-WsprryPi-Client-Address: 192.168.1.1\r\n","Expect: 100-continue\r\n",
        "Connection: keep-alive\r\n","User-Agent: arbitrary\r\n"," folded: value\r\n","Bad Header: value\r\n"})check(decode(raw("POST","/api/pair/v1/rotate/commit",header))==State::Refused);
    check(decode(raw("POST","/api/pair/v1/rotate/commit","X-WsprryPi-Operation-ID: a\r\nx-wsprrypi-operation-id: b\r\n"))==State::Refused);
    for(const auto *method:{"HEAD","DELETE","OPTIONS","PATCH","post"})check(decode(raw(method))==State::Refused);
    for(const auto *path:{"http://target.local/api/pair/v1/rotate/commit","//api/pair/v1/rotate/commit","/api/pair/v1/../commit",
        "/api/pair/v1/rotate/commit?x=1","/api/pair/v1/%63ommit","/api/pair/v1/rotate/commit#x","/api/pair/v1/rotate\\commit"})check(decode(raw("POST",path))==State::Refused);
    for(const auto *host:{"","evil@target.local","target.local/path","target.local:1:2","[fd00::1","[bad]:31418","[fd00::1]:",
        "target.local:031418","target.local:65536","target.local:0","target.local?x","target..local"})
        refuses([&]{(void)pi_pair_http_request("GET","/api/pair/v1/operations/"+std::string(32,'a'),host);});
    for(const auto *host:{"target.local","target.local:31418","[fd00::1]:31418","[::1]:12345","192.168.1.20:31418"})
        check(decode(pi_pair_http_request("GET","/api/pair/v1/operations/"+std::string(32,'a'),host))==State::Ready);
    auto wrong=raw();wrong.replace(wrong.find("HTTP/1.1"),8,"HTTP/1.0");check(decode(wrong)==State::Refused);
    wrong=raw();wrong.replace(wrong.find("Content-Length: 2"),17,"Content-Length: 02");check(decode(wrong)==State::Refused);
    check(decode(raw()+wire)==State::Refused);check(decode(raw()+"x")==State::Refused);
    check(decode(raw("GET"))==State::Refused);
    check(decode(raw("POST","/api/pair/v1/rotate/commit",{},"{\"x\":1,\"x\":2}"))==State::Refused);
    check(decode(raw("POST","/api/pair/v1/rotate/commit",{},std::string(4097,' ')))==State::Refused);
    check(decode(raw("POST","/api/control/v1/targets/a/settings",{},std::string(32769,' ')))==State::Refused);
    check(decode("GET /api/pair/v1/operations/"+std::string(32,'a')+" HTTP/1.1\r\nHost: "+std::string(8193,'a'))==State::Refused);
    refuses([&]{(void)pi_pair_http_request("POST","/api/pair/v1/rotate/commit","target.local","{}",{{"If-Match","a\r\nInjected: yes"}});});
    refuses([&]{(void)pi_pair_http_request("POST","/api/pair/v1/rotate/commit","target.local","{}",{{"Cookie","not authority"}});});
    PiPairHttpReply reply{{200,"{}","\"fixture\""},"/api/pair/v1/operations/"+std::string(32,'a'),"settings"};
    const auto response=pi_pair_http_response(reply);check(response.starts_with("HTTP/1.1 200 ")&&response.ends_with("\r\n\r\n{}")&&
        response.find("Cache-Control: no-store\r\n")!=std::string::npos&&response.find("X-WsprryPi-Recovery-Resource: settings\r\n")!=std::string::npos);
    auto response_state=[&](const std::string &wire,std::size_t chunk=4096){
        PiPairHttpResponseDecoder decoder("/api/pair/v1/operations/"+std::string(32,'a'));
        for(std::size_t pos=0;pos<wire.size();pos+=chunk)decoder.append(std::span(reinterpret_cast<const std::uint8_t *>(wire.data()+pos),std::min(chunk,wire.size()-pos)));
        if(decoder.state()==State::Ready){const auto &got=decoder.reply();check(got.payload.status==200&&got.payload.body=="{}"&&got.payload.etag==reply.payload.etag&&got.location==reply.location&&got.recovery_resource=="settings");}
        return decoder.state();
    };
    for(const auto chunk:{1U,2U,17U,4096U})check(response_state(response,chunk)==State::Ready);
    PiPairHttpResponseDecoder response_decoder("/api/pair/v1/operations/"+std::string(32,'a'));refuses([&]{(void)response_decoder.reply();});
    check(response_decoder.append({})==State::More);
    check(response_decoder.append(std::span(reinterpret_cast<const std::uint8_t *>(response.data()),response.size()))==State::Ready);
    check(response_decoder.append(std::span(&extra,1))==State::Refused);
    for(const auto *header:{"content-length: 2\r\n","ETag: duplicate\r\n","Transfer-Encoding: chunked\r\n","Content-Encoding: gzip\r\n",
        "Set-Cookie: x=y\r\n","Server: arbitrary\r\n","X-Unknown: value\r\n","Connection: close\r\n"}){
        auto changed=response;changed.insert(changed.find("\r\n\r\n")+2,header);check(response_state(changed)==State::Refused);
    }
    for(const auto *status:{"100","199","302","600","2a0"}){auto changed=response;changed.replace(9,3,status);check(response_state(changed)==State::Refused);}
    auto changed=response;changed.replace(changed.find("no-store"),8,"public");check(response_state(changed)==State::Refused);
    changed=response;changed.replace(changed.find("Content-Length: 2"),17,"Content-Length: 02");check(response_state(changed)==State::Refused);
    changed=response;changed.replace(changed.find("application/json"),16,"text/plain");check(response_state(changed)==State::Refused);
    check(response_state(response+"x")==State::Refused);
    changed=response;changed.replace(changed.size()-2,2,"{\"x\":1,\"x\":2}");changed.replace(changed.find("Content-Length: 2"),17,"Content-Length: 13");check(response_state(changed)==State::Refused);
    auto bad=reply;bad.location="http://metadata.local/";refuses([&]{(void)pi_pair_http_response(bad);});
    bad=reply;bad.payload.status=302;refuses([&]{(void)pi_pair_http_response(bad);});
    bad=reply;bad.payload.etag="a\r\nInjected: x";refuses([&]{(void)pi_pair_http_response(bad);});
    bad=reply;bad.recovery_resource="../jobs";refuses([&]{(void)pi_pair_http_response(bad);});
    auto pico=reply;pico.context=std::string(32,'a');pico.operation=std::string(32,'b');pico.readback_revision=std::string(64,'c');
    const auto removal=pi_pair_http_request("DELETE","/api/control/v1/targets/"+std::string(32,'a')+"/pico/wifi","target.local","{}");
    PiPairHttpDecoder removal_decoder;check(removal_decoder.append(std::span(reinterpret_cast<const std::uint8_t *>(removal.data()),removal.size()))==State::Ready);
    check(removal_decoder.request().method=="DELETE"&&removal_decoder.request().body=="{}");
    const auto pico_wire=pi_pair_http_response(pico);PiPairHttpResponseDecoder pico_decoder("/api/control/v1/targets/"+std::string(32,'a')+"/pico/hostname");
    check(pico_decoder.append(std::span(reinterpret_cast<const std::uint8_t *>(pico_wire.data()),pico_wire.size()))==State::Ready);
    check(pico_decoder.reply().context==pico.context&&pico_decoder.reply().operation==pico.operation&&pico_decoder.reply().readback_revision==pico.readback_revision);
    for(auto member:{&PiPairHttpReply::context,&PiPairHttpReply::operation,&PiPairHttpReply::readback_revision}){
        bad=pico;bad.*member="malformed";refuses([&]{(void)pi_pair_http_response(bad);});
    }
    for(const auto *header:{"X-WsprryPi-Target-Context: bad\r\n","X-WsprryPi-Operation-ID: bad\r\n","X-WsprryPi-Readback-Revision: bad\r\n"}){
        auto duplicate=pico_wire;duplicate.insert(duplicate.find("\r\n\r\n")+2,header);check(response_state(duplicate)==State::Refused);
        auto malformed=response;malformed.insert(malformed.find("\r\n\r\n")+2,header);check(response_state(malformed)==State::Refused);
    }
    std::cout<<"PASS closed bounded paired HTTP framing and metadata isolation: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
