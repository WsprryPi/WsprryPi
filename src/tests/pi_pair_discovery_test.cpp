// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_discovery.hpp"
#include "wtp_integration/pi_pair_discovery_avahi.hpp"
#include "wtp_integration/controller_contract.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstdlib>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair discovery check "+std::to_string(checks));}
std::string id(char c){return std::string(32,c);}
std::string number(unsigned value){std::ostringstream out;out<<std::hex<<std::setw(32)<<std::setfill('0')<<value;return out.str();}
}
int main(){try{
    const auto disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");check(disabled&&std::string_view(disabled)=="1");
    PiPairDiscovery native_cache(id('a'));PiPairDiscoveryBrowser native_browser(native_cache);
    check(!native_cache.snapshot()["available"].get<bool>()&&!native_browser.start());native_browser.stop();native_browser.stop();
    PiPairDiscoveryPublisher publisher({id('a'),id('b'),"Transmitter Pi",8080},[]{return true;});
    check(!publisher.published()&&!publisher.start());publisher.stop();publisher.stop();
    bool rejected=false;try{PiPairDiscoveryPublisher invalid({id('a'),id('b'),std::string(64,'x'),8080},[]{return true;});}catch(...){rejected=true;}check(rejected);
    auto now=std::chrono::steady_clock::time_point{};PiPairDiscovery discovery(id('a'),[&]{return now;});
    const PiPairDiscovery::Key key{1,"Transmitter Pi"},other{2,key.instance},third{3,key.instance};
    const auto host="wsprry-management-"+id('b')+".local.";const auto txt=pi_pair_discovery_txt(id('b'),id('c'),8080);
#if defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
    std::vector<unsigned char> wire;
    for(const auto &field:txt){wire.push_back(static_cast<unsigned char>(field.size()));wire.insert(wire.end(),field.begin(),field.end());}
    AvahiStringList *native{};check(avahi_string_list_parse(wire.data(),wire.size(),&native)==0);
    struct Free {AvahiStringList *value;~Free(){avahi_string_list_free(value);}} free_native{native};
    check(pi_pair_discovery_avahi_fields(native)==txt);
    // The actual publisher's prepend/serialize recipe preserves this same
    // wire order, and callback decoding neither changes nor frees the list.
    std::vector<unsigned char> roundtrip(wire.size());
    check(avahi_string_list_serialize(native,roundtrip.data(),roundtrip.size())==wire.size()&&roundtrip==wire);
    check(pi_pair_discovery_avahi_fields(native)==txt);
#endif
    check(!discovery.snapshot()["available"].get<bool>()&&discovery.snapshot()["discoveries"].empty());
    check(!discovery.resolved(key,"local",host,31418,txt));discovery.available(true);
    check(discovery.resolved(key,"local",host,31418,txt));auto snapshot=discovery.snapshot();
    check(valid_controller_document(snapshot,"pair-discovery")&&snapshot["discoveries"].size()==1&&snapshot["discoveries"][0]["bootstrap_port"]==8080);
    const auto handle=snapshot["discoveries"][0]["discovery_id"].get<std::string>();
    check(discovery.candidate(handle).has_value()&&!discovery.candidate(id('0'))&&!discovery.candidate("not-a-selector"));
    auto spelling=host;spelling[0]='W';spelling.pop_back();
    check(discovery.resolved(other,"LOCAL.",spelling,31418,txt)&&discovery.snapshot()["discoveries"].size()==1&&discovery.candidate(handle));
    auto changed=txt;changed[3]="boot="+id('d');check(!discovery.resolved(third,"local",host,31418,changed));
    check(discovery.snapshot()["discoveries"].empty()&&!discovery.candidate(handle));
    check(discovery.resolved(key,"local",host,31418,txt));const auto refreshed=discovery.snapshot()["discoveries"][0]["discovery_id"].get<std::string>();check(refreshed!=handle);
    check(discovery.resolved(other,"local",host,31418,txt));discovery.removed(key);check(discovery.candidate(refreshed).has_value());discovery.removed(other);check(!discovery.candidate(refreshed));
    for(unsigned scenario=0;scenario<10;++scenario){
        check(discovery.resolved(key,"local",host,31418,txt));auto invalid=txt;
        if(scenario==0)std::swap(invalid[0],invalid[1]);
        if(scenario==1)invalid.push_back("code=not-public");
        if(scenario==2)invalid[4]="http-port=08080";
        if(scenario==3)invalid[4]="http-port=65536";
        if(scenario==4)invalid[2]="id="+id('a');
        if(scenario==5)invalid[3]="boot=bad";
        if(scenario==6)invalid[5]="suite=unsupported";
        if(scenario==7)invalid[6]="control-sha256="+std::string(64,'0');
        if(scenario==8)invalid[6]=invalid[2];
        if(scenario==9)invalid[1]="binding="+std::string(256,'x');
        check(!discovery.resolved(key,"local",host,31418,invalid)&&discovery.snapshot()["discoveries"].empty());
    }
    check(!discovery.resolved(key,"example.com",host,31418,txt));check(!discovery.resolved(key,"local",host,31418,txt,true));
    check(!discovery.resolved(key,"local",host,31419,txt));check(!discovery.resolved(key,"local","evil.local",31418,txt));
    check(!discovery.resolved({0,key.instance},"local",host,31418,txt));check(!discovery.resolved({1,"bad\nname"},"local",host,31418,txt));
    check(!discovery.resolved({1,std::string(65,'x')},"local",host,31418,txt));
    check(!discovery.resolved({1,std::string(64,'x')},"local",host,31418,txt));
    check(discovery.resolved(key,"local",host,31418,txt));now+=std::chrono::seconds(29);check(discovery.snapshot()["discoveries"].size()==1);
    check(discovery.resolved(key,"local",host,31418,txt));now+=std::chrono::seconds(29);check(discovery.snapshot()["discoveries"].size()==1);
    now+=std::chrono::seconds(1);check(discovery.snapshot()["discoveries"].empty());
    check(discovery.resolved(key,"local",host,31418,txt));now-=std::chrono::seconds(1);check(discovery.snapshot()["discoveries"].empty());
    for(unsigned i=1;i<=64;++i)check(discovery.resolved({i,std::string(60,'x')+std::to_string(i)},"local","wsprry-management-"+number(i)+".local.",31418,pi_pair_discovery_txt(number(i),id('c'),65535)));
    check(discovery.snapshot()["discoveries"].size()==64&&discovery.snapshot().dump().size()<=32768);
    check(!discovery.resolved({65,"Pi 65"},"local","wsprry-management-"+number(65)+".local.",31418,pi_pair_discovery_txt(number(65),id('c'),8080)));
    discovery.available(false);check(discovery.snapshot()["available"]==false&&discovery.snapshot()["discoveries"].empty());
    std::cout<<"PASS bounded separate management discovery, public metadata, conflicts, expiry and no authority: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
