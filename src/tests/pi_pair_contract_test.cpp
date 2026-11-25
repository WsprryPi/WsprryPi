// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_contract.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using Json=nlohmann::json;
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair_contract_test_failed");}
template<class F>void refuses(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected);}
}
int main(){
    try{
        using namespace wsprrypi;
        const auto id=[](char c){return std::string(32,c);};
        const std::string nonce(43,'A');
        const Json hello{{"contract","Wsprry-Pair/1"},{"schema","hello"},{"suite","SPAKE2+-P256-SHA256-HKDF-HMAC"},
            {"window_id",id('a')},{"pair_id",id('b')},{"action","pair"},{"controller_id",id('c')},{"controller_boot_id",id('d')},
            {"target_id",id('e')},{"target_boot_id",id('f')},{"authorization_generation","0"},{"controller_nonce",nonce}};
        check(parse_pi_pair_document(hello.dump(),"hello")==hello);
        for(const auto &change:{Json{{"code","2345ABCD"}},Json{{"suite","SPAKE2-P256-SHA256-HKDF-HMAC"}},
                Json{{"contract","Wsprry-Pair/2"}},Json{{"target_id",id('c')}},Json{{"authorization_generation","00"}},
                Json{{"authorization_generation",0}},Json{{"authorization_generation","18446744073709551616"}},
                Json{{"controller_nonce",std::string(42,'A')+"B"}}}){
            auto value=hello;value.update(change);check(!valid_pi_pair_document(value,"hello"));
        }
        auto value=hello;value.erase("suite");check(!valid_pi_pair_document(value,"hello"));
        value=hello;value["authorization_generation"]="18446744073709551615";check(valid_pi_pair_document(value,"hello"));
        auto duplicate=hello.dump();duplicate.pop_back();duplicate+=",\"pair_id\":\""+id('b')+"\"}";
        refuses([&]{(void)parse_pi_pair_document(duplicate,"hello");});
        refuses([&]{(void)parse_pi_pair_document(std::string(4097,' ')+hello.dump(),"hello");});
        refuses([&]{(void)parse_pi_pair_document("{\"contract\":\"\xff\"}","hello");});
        Json window{{"contract","Wsprry-Pair/1"},{"schema","window"},{"suite",hello["suite"]},{"device_id",id('a')},
            {"boot_id",id('b')},{"open",false},{"window_id",nullptr},{"pair_id",nullptr},{"authorization_generation","0"},{"action",nullptr}};
        check(valid_pi_pair_document(window,"window"));window["open"]=true;check(!valid_pi_pair_document(window,"window"));
        window["window_id"]=id('c');window["pair_id"]=id('d');check(!valid_pi_pair_document(window,"window"));
        for(const auto *action:{"pair","replace","recover"}){window["action"]=action;check(valid_pi_pair_document(window,"window"));}
        window["action"]="unknown";check(!valid_pi_pair_document(window,"window"));window["action"]="pair";
        auto missing=window;missing.erase("action");check(!valid_pi_pair_document(missing,"window"));
        auto closed=window;closed["open"]=false;closed["window_id"]=nullptr;closed["pair_id"]=nullptr;check(!valid_pi_pair_document(closed,"window"));
        window["pair_id"]=nullptr;check(!valid_pi_pair_document(window,"window"));
        Json rotation{{"contract","Wsprry-Pair/1"},{"schema","rotate-commit"},{"operation_id",id('a')},
            {"pair_id",id('b')},{"expected_epoch","1"},{"next_epoch","2"}};
        check(valid_pi_pair_document(rotation,"rotate-commit"));
        for(const auto next:{"0","1","3","18446744073709551616"}){
            value=rotation;value["next_epoch"]=next;check(!valid_pi_pair_document(value,"rotate-commit"));
        }
        value=rotation;value["expected_epoch"]="18446744073709551615";value["next_epoch"]="1";
        check(!valid_pi_pair_document(value,"rotate-commit"));
        using Purpose=PiPairRoutePurpose;
        check(pi_pair_route(Purpose::Bootstrap,"POST","hello"));
        check(pi_pair_route(Purpose::Management,"GET","operations/"+id('a')));
        check(!pi_pair_route(Purpose::Management,"GET","operations/{operation_id}"));
        check(!pi_pair_route(Purpose::Management,"GET","operations/"+id('a')+"/"));
        for(const auto resource:{"retirement","retirement/status"}){
            check(pi_pair_route(Purpose::Retirement,"POST",resource));
            check(!pi_pair_route(Purpose::Bootstrap,"POST",resource));
            check(!pi_pair_route(Purpose::Management,"POST",resource));
        }
        for(const auto resource:{"hello","commit","rotate/prepare","rotate/commit","authorities/register","resume/ack","stop","settings","jobs/load"})
            check(!pi_pair_route(Purpose::Retirement,"POST",resource));
        check(!pi_pair_route(Purpose::Retirement,"GET","resume"));
        check(!pi_pair_route(Purpose::Management,"GET","resume"));
        check(!pi_pair_route(Purpose::Management,"POST","resume/ack"));
        check(!pi_pair_route(static_cast<Purpose>(99),"POST","retirement"));
        Json registration{{"contract","Wsprry-Pair/1"},{"schema","authority-register"},{"operation_id",id('a')},
            {"pair_id",id('b')},{"authorization_generation","1"},{"target_id",id('c')},{"controller_id",id('d')},
            {"controller_incarnation",id('e')},{"controller_generation","1"},{"assignments",Json::array()},
            {"endpoint",Json{{"host","192.168.1.2"},{"port",31418}}}};
        check(valid_pi_pair_document(registration,"authority-register"));
        const Json assignment{{"assignment_id",id('f')},{"assignment_revision",std::string(64,'a')},{"state","legacy"}};
        registration["assignments"].push_back(assignment);check(valid_pi_pair_document(registration,"authority-register"));
        registration["assignments"].push_back(assignment);check(!valid_pi_pair_document(registration,"authority-register"));
        const Json grant{{"contract","Wsprry-Pair/1"},{"schema","resume-grant"},{"operation_id",id('1')},{"pair_id",id('c')},
            {"authorization_generation","1"},{"target_id",id('a')},{"target_boot_id",id('f')},{"controller_id",id('b')},
            {"controller_incarnation",id('e')},{"assignment_id",id('4')},{"assignment_revision",std::string(64,'5')},
            {"transition_id",id('2')},{"retirement_receipt_digest",std::string(64,'6')},{"authority_generation","7"},
            {"disabled_revision",std::string(64,'8')},{"disable_receipt_digest",std::string(64,'9')},{"candidate_digest",std::string(64,'a')},
            {"wtp_binding",{{"revocation_generation","0"},{"wtp_port",31417},{"management_port",31415}}},
            {"grant_digest","ba7c1fb71a7e9ef1b76508f91328e7f97f595266a64c18911e2ccd98c128e9ba"}};
        // Public digest independently calculated with Python hashlib/canonical JSON.
        check(valid_pi_pair_document(grant,"resume-grant"));
        for(const auto &change:{Json{{"target_boot_id",id('0')}},Json{{"disabled_revision",std::string(64,'0')}},
                Json{{"authority_generation","8"}},Json{{"candidate_digest",std::string(64,'0')}}}){
            value=grant;value.update(change);check(!valid_pi_pair_document(value,"resume-grant"));
        }
        const Json acknowledgement{{"contract","Wsprry-Pair/1"},{"schema","resume-ack"},{"operation_id",id('a')},
            {"pair_id",id('b')},{"authorization_generation","1"},{"target_id",id('c')},{"controller_id",id('d')},
            {"assignment_id",id('e')},{"grant_digest",std::string(64,'a')},{"outcome","refused"},
            {"controller_generation","2"},{"receipt_digest",std::string(64,'b')},{"proof",nonce}};
        check(valid_pi_pair_document(acknowledgement,"resume-ack"));
        value=acknowledgement;value.erase("proof");check(!valid_pi_pair_document(value,"resume-ack"));
        value=acknowledgement;value["proof"]=std::string(42,'A')+"B";check(!valid_pi_pair_document(value,"resume-ack"));
        value=acknowledgement;value["certificate"]="unused";check(!valid_pi_pair_document(value,"resume-ack"));
        std::cout<<"PASS Pair/1 closed parsing, canonical encodings, counter bounds, exact rotation and purpose isolation: "<<checks<<" checks\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
