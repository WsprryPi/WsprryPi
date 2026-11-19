// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/pi_pair_rotation.hpp"
#include "pairing_provider/rotation-vector.hpp"
#include <array>
#include <iostream>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair operations check "+std::to_string(checks));}
template<class F>void refuses(F action){bool held=false;try{action();}catch(...){held=true;}check(held);}
std::string id(char value){return std::string(32,value);}
std::string operation(unsigned number){const auto text=std::to_string(number);return std::string(32-text.size(),'0')+text;}
Json entry(){return {{"scope","device"},{"principal_id",id('a')},{"target_id",id('b')},{"pair_id",id('c')},
    {"authorization_generation","1"},{"operation_id",id('d')},{"request_digest",std::string(64,'e')},{"state","pending"},
    {"result_digest",nullptr},{"unresolved",true},{"receipt",nullptr},{"continuation",nullptr}};}
Json book(const Json &values){return {{"schema","pi-management-pair-operations/1"},{"self_id",id('a')},{"entries",values}};}
}
int main(){try{
    auto key=PiPairRotationKey::generate();const auto public_point=key.public_point();const auto private_key=key.export_private();
    auto restored=PiPairRotationKey::restore(private_key,public_point);check(restored.public_point()==public_point);
    check(restored.export_private().storage_encoding()==private_key.storage_encoding());
    auto other=PiPairRotationKey::generate();check(other.public_point()!=public_point);
    refuses([&]{(void)PiPairRotationKey::restore(private_key,other.public_point());});
    std::array<unsigned char,32> zero{},large{};large.fill(255);
    refuses([&]{(void)PiPairRotationKey::restore(PiPairSecret(zero),public_point);});
    refuses([&]{(void)PiPairRotationKey::restore(PiPairSecret(large),public_point);});
    check(!PiPairRotationKey::valid_private_pair(std::string(42,'A')+"B",public_point));
    auto moved=std::move(restored);check(moved.public_point()==public_point);refuses([&]{(void)restored.export_private();});
    const auto fixture_prepare=Json::parse(rotation_fixture::prepare_json),fixture_prepared=Json::parse(rotation_fixture::prepared_json);
    const auto one=PiPairSecret::from_storage(rotation_fixture::controller_private),two=PiPairSecret::from_storage(rotation_fixture::target_private);
    auto controller=PiPairRotationKey::restore(one,fixture_prepare["pA"].get<std::string>());
    auto target=PiPairRotationKey::restore(two,fixture_prepared["pB"].get<std::string>());
    const auto current=PiPairSecret::from_storage(rotation_fixture::current_psk);
    const auto controller_next=controller.derive_next(current,7,fixture_prepare,fixture_prepared,PiPairRotationKey::Role::Controller);
    const auto target_next=target.derive_next(current,7,fixture_prepare,fixture_prepared,PiPairRotationKey::Role::Target);
    check(controller_next.storage_encoding()==rotation_fixture::next_psk&&target_next.storage_encoding()==rotation_fixture::next_psk);
    refuses([&]{(void)controller.derive_next(current,8,fixture_prepare,fixture_prepared,PiPairRotationKey::Role::Controller);});
    refuses([&]{(void)controller.derive_next(current,7,fixture_prepare,fixture_prepared,PiPairRotationKey::Role::Target);});
    refuses([&]{(void)controller.derive_next(current,7,fixture_prepare,fixture_prepared,static_cast<PiPairRotationKey::Role>(99));});
    auto changed=fixture_prepared;changed["operation_id"]=id('e');
    refuses([&]{(void)controller.derive_next(current,7,fixture_prepare,changed,PiPairRotationKey::Role::Controller);});
    changed=fixture_prepared;changed["next_epoch"]="9";
    refuses([&]{(void)controller.derive_next(current,7,fixture_prepare,changed,PiPairRotationKey::Role::Controller);});
    changed=fixture_prepared;changed["nonce"]=std::string(43,'A');
    check(controller.derive_next(current,7,fixture_prepare,changed,PiPairRotationKey::Role::Controller).storage_encoding()!=rotation_fixture::next_psk);
    check(controller.derive_next(PiPairSecret(zero),7,fixture_prepare,fixture_prepared,PiPairRotationKey::Role::Controller).storage_encoding()!=rotation_fixture::next_psk);
    changed=fixture_prepare;changed["pA"]=other.public_point();
    refuses([&]{(void)controller.derive_next(current,7,changed,fixture_prepared,PiPairRotationKey::Role::Controller);});
    changed=fixture_prepared;changed["contract"]="Wsprry-Pair/2";
    refuses([&]{(void)controller.derive_next(current,7,fixture_prepare,changed,PiPairRotationKey::Role::Controller);});
    {
        auto pending=entry();
        Json request{{"contract","Wsprry-Pair/1"},{"schema","authority-register"},{"operation_id",id('d')},{"pair_id",id('c')},
            {"authorization_generation","1"},{"target_id",id('b')},{"controller_id",id('a')},{"controller_incarnation",id('f')},
            {"controller_generation","1"},{"assignments",Json::array()},{"endpoint",{{"host","wsprry-management-"+id('a')+".local."},{"port",31418}}}};
        pending["request_digest"]=PiManagementJournal::digest(canonical_pi_management_json(request));
        pending["continuation"]={{"kind","authority-registration-intent"},{"self_id",id('a')},{"peer_id",id('b')},
            {"profile_id",id('f')},{"profile_generation","1"},{"stage","prepared"},{"request",request}};
        const auto staged=book(Json::array({pending}));check(valid_pi_pair_operations(staged,id('a')));
        auto after=staged;after["entries"][0]["continuation"]["stage"]="awaiting_receipt";check(valid_pi_pair_operations(after,id('a')));
        for(const auto &change:{Json{{"profile_generation","0"}},Json{{"self_id",id('b')}},Json{{"stage","unconfirmed_retry"}},Json{{"extra",true}}}){
            auto bad=staged;bad["entries"][0]["continuation"].update(change);check(!valid_pi_pair_operations(bad,id('a')));
        }
        after=staged;after["entries"][0]["continuation"]["request"]["controller_generation"]="2";check(!valid_pi_pair_operations(after,id('a')));
        after=staged;after["entries"][0]["continuation"]["request"]["target_id"]=id('f');check(!valid_pi_pair_operations(after,id('a')));
    }
    auto value=entry();
    Json prepare{{"contract","Wsprry-Pair/1"},{"schema","rotate-prepare"},{"operation_id",id('d')},{"pair_id",id('c')},
        {"expected_epoch","1"},{"pA",public_point},{"nonce",std::string(43,'A')}};
    Json intent{{"kind","rotation-intent"},{"self_id",id('a')},{"peer_id",id('b')},{"profile_id",id('f')},
        {"pair_id",id('c')},{"authorization_generation","1"},{"operation_id",id('d')},{"expected_epoch","1"},
        {"stage","awaiting_prepare"},{"prepare",prepare},{"prepared",nullptr},{"private_key",private_key.storage_encoding()}};
    value["continuation"]=intent;const auto original=book(Json::array({value}));
    check(valid_pi_pair_operations(original,id('a')));check(parse_pi_pair_operations(original.dump(),id('a'))==original);
    for(const auto &change:{Json{{"self_id",id('b')}},Json{{"peer_id",id('a')}},Json{{"authorization_generation","2"}},
        Json{{"pair_id",id('f')}},Json{{"operation_id",id('f')}},Json{{"private_key",nullptr}},Json{{"private_key",std::string(43,'A')}},
        Json{{"stage","other"}},Json{{"manual_file","/private/key"}}}){
        auto malformed=original;malformed["entries"][0]["continuation"].update(change);check(!valid_pi_pair_operations(malformed,id('a')));
    }
    for(const auto &change:{Json{{"state","committed"},{"result_digest",std::string(64,'a')}},Json{{"state","rejected"}}}){
        auto malformed=original;malformed["entries"][0].update(change);check(!valid_pi_pair_operations(malformed,id('a')));
    }
    Json prepared{{"contract","Wsprry-Pair/1"},{"schema","rotate-prepared"},{"operation_id",id('d')},
        {"next_epoch","2"},{"pB",other.public_point()},{"nonce",std::string(43,'A')}};
    auto awaiting=original;auto &continuation=awaiting["entries"][0]["continuation"];
    continuation["stage"]="awaiting_commit";continuation["private_key"]=nullptr;continuation["prepared"]=prepared;
    check(valid_pi_pair_operations(awaiting,id('a')));
    continuation["prepared"]["next_epoch"]="3";check(!valid_pi_pair_operations(awaiting,id('a')));
    continuation["prepared"]=prepared;continuation["private_key"]=private_key.storage_encoding();check(!valid_pi_pair_operations(awaiting,id('a')));
    for(const auto &change:{Json{{"unresolved",false}},Json{{"principal_id",id('b')}},Json{{"target_id",id('f')}},
        Json{{"state","committed"}},Json{{"result_digest",std::string(64,'a')}},Json{{"scope","retirement"}},Json{{"code","2345ABCD"}}}){
        auto malformed=original;malformed["entries"][0].update(change);check(!valid_pi_pair_operations(malformed,id('a')));
    }
    auto wrong=original;wrong["schema"]="pi-management-pair-operations/2";check(!valid_pi_pair_operations(wrong,id('a')));
    wrong=original;wrong["entries"].push_back(value);check(!valid_pi_pair_operations(wrong,id('a')));
    auto duplicate=original.dump();duplicate.pop_back();duplicate+=",\"entries\":[]}";refuses([&]{(void)parse_pi_pair_operations(duplicate,id('a'));});
    refuses([&]{(void)parse_pi_pair_operations(std::string(1048577,' '),id('a'));});
    auto history=Json::array();
    for(unsigned i=0;i<289;++i){auto item=entry();item["operation_id"]=operation(i);
        if(i<257){item["state"]="committed";item["unresolved"]=false;item["result_digest"]=std::string(64,'a');}
        history.push_back(item);
    }
    check(!valid_pi_pair_operations(book(history),id('a')));
    const auto bounded=pi_pair_operations(id('a'),history);check(bounded.evicted&&bounded.document["entries"].size()==288);
    check(bounded.document["entries"][0]["operation_id"]==operation(1));
    unsigned pending=0;for(const auto &item:bounded.document["entries"])if(item["unresolved"]==true)++pending;
    check(pending==32&&valid_pi_pair_operations(bounded.document,id('a'))&&history.size()==289);
    auto overflow=bounded.document["entries"];auto extra=entry();extra["operation_id"]=operation(999);overflow.push_back(extra);
    refuses([&]{(void)pi_pair_operations(id('a'),overflow);});check(bounded.document["entries"].size()==288);
    std::cout<<"PASS OpenSSL temporary rotation keys and bounded private recovery intentions: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
