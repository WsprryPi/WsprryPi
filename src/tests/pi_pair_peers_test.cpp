// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_peers.hpp"
#include "wtp_integration/pi_pair_catalog_store.hpp"
#include "wtp_integration/pi_pair_history_record.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair peers check "+std::to_string(checks));}
template<class F>void refuses(F action){bool held=false;try{action();}catch(...){held=true;}check(held);}
std::string id(unsigned value){const auto text=std::to_string(value);return std::string(32-text.size(),'0')+text;}
const std::string self(32,'a');
Json profile(unsigned value=1){return {{"profile_id",id(1000+value)},{"profile_generation","1"},{"target_id",id(value)},
    {"endpoint",{{"host","target.local"},{"port",31418}}},{"stage","awaiting_commit"},{"initial_operation_id",id(3000+value)},
    {"initial_request_digest",std::string(64,'e')},{"pair",{{"schema","pi-management-pair/1"},{"self_id",self},{"peer_id",id(value)},
        {"pair_id",id(2000+value)},{"authorization_generation","7"},{"role","manager"},{"state","pending"},
        {"current",nullptr},{"previous",nullptr},{"pending",{{"epoch","1"},{"tls_psk",std::string(43,'A')},{"service_ms","0"},{"connections","0"}}},
        {"receipt",nullptr},{"retirement_verifier",std::string(42,'A')+"E"}}}};}
void put(const std::string &path,const std::string &value){std::ofstream file(path,std::ios::binary);file<<value;file.close();check(!chmod(path.c_str(),0600));}
std::string bytes(const std::string &path){return PiManagementJournal::read_private_record(path)->bytes;}
}
int main(){try{
    auto index=empty_pi_pair_peers(self);check(valid_pi_pair_peers(index,self));index["profiles"].push_back(profile());
    check(valid_pi_pair_peers(index,self)&&parse_pi_pair_peers(index.dump(),self)==index);
    auto superseded=index;auto newer=profile();newer["profile_generation"]="2";
    newer["pair"]["pair_id"]=id(2101);newer["initial_operation_id"]=id(3101);
    superseded["profiles"][0]=newer;superseded["uncertain"].push_back(profile());
    check(valid_pi_pair_peers(superseded,self)&&parse_pi_pair_peers(superseded.dump(),self)==superseded);
    const auto unchanged=index.dump();check(supersede_pi_pair_pending(index,self,newer)==superseded&&index.dump()==unchanged);
    auto leap=newer;leap["profile_generation"]="3";refuses([&]{(void)supersede_pi_pair_pending(index,self,leap);});
    auto stale=newer;stale["profile_generation"]="1";refuses([&]{(void)supersede_pi_pair_pending(index,self,stale);});
    for(const auto &change:{Json{{"stage","active"}},Json{{"profile_generation","2"}},Json{{"profile_generation","3"}},
        Json{{"profile_id",id(1002)}},Json{{"initial_operation_id",id(3101)}}}){
        auto invalid=superseded;invalid["uncertain"][0].update(change);check(!valid_pi_pair_peers(invalid,self));
    }
    auto alias=superseded;alias["uncertain"][0]["pair"]["pair_id"]=id(2101);check(!valid_pi_pair_peers(alias,self));
    alias=superseded;alias["uncertain"].push_back(profile());check(!valid_pi_pair_peers(alias,self));
    alias=superseded;alias.erase("uncertain");check(!valid_pi_pair_peers(alias,self));
    auto active=index;auto &row=active["profiles"][0];row["stage"]="active";row["pair"]["state"]="active";
    row["pair"]["current"]=row["pair"]["pending"];row["pair"]["pending"]=nullptr;check(valid_pi_pair_peers(active,self));
    auto removed=active;removed["profiles"][0]["stage"]="access_removed";removed["profiles"][0]["pair"]["state"]="revoked";
    removed["profiles"][0]["pair"]["current"]=nullptr;check(valid_pi_pair_peers(removed,self));
    removed["profiles"][0]["pair"]["retirement_verifier"]=nullptr;check(valid_pi_pair_peers(removed,self));
    for(const auto &change:{Json{{"profile_generation","0"}},Json{{"profile_generation","01"}},Json{{"profile_generation",true}},
        Json{{"profile_generation","18446744073709551616"}},Json{{"stage","active"}},Json{{"stage","removed"}},Json{{"target_id",self}},
        Json{{"target_id",id(2)}},Json{{"initial_operation_id",""}},Json{{"initial_request_digest",std::string(64,'z')}},
        Json{{"code","1234-ABCD"}},Json{{"key_file","/private/key"}},Json{{"registration_record","not permitted"}}}){
        auto wrong=index;wrong["profiles"][0].update(change);check(!valid_pi_pair_peers(wrong,self));
    }
    for(const auto *field:{"self_id","peer_id","pair_id","authorization_generation","role","state","current","pending","previous","receipt","retirement_verifier"}){
        auto wrong=index;wrong["profiles"][0]["pair"].erase(field);check(!valid_pi_pair_peers(wrong,self));
    }
    for(const auto &port:{Json(443),Json(31418.0),Json(true),Json("31418"),Json(65536)}){
        auto wrong=index;wrong["profiles"][0]["endpoint"]["port"]=port;check(!valid_pi_pair_peers(wrong,self));
    }
    for(const auto *host:{"","http://target.local","target.local/path","target.local?x","target.local#x","target.local:31418",
        "user@target.local","target local","target\nlocal","target..local","target..","-target.local","target-.local",".",
        "127.0.0.1","169.254.169.254","8.8.8.8","::1","fe80::1","::ffff:192.168.1.10","[fd00::1]"}){
        auto wrong=index;wrong["profiles"][0]["endpoint"]["host"]=host;check(!valid_pi_pair_peers(wrong,self));
    }
    for(const auto &host:{std::string("target.local"),std::string("target.local."),std::string("192.168.1.10"),std::string("10.1.2.3"),
        std::string("172.16.1.2"),std::string("fd00::1"),std::string("fc00::1"),std::string(63,'a')+".local"}){
        auto valid=index;valid["profiles"][0]["endpoint"]["host"]=host;check(valid_pi_pair_peers(valid,self));
    }
    for(const auto &host:{std::string(64,'a')+".local",std::string(254,'a'),std::string("target\0.local",13)}){
        auto wrong=index;wrong["profiles"][0]["endpoint"]["host"]=host;check(!valid_pi_pair_peers(wrong,self));
    }
    for(const auto *field:{"profile_id","target_id","initial_operation_id"}){
        auto wrong=index;wrong["profiles"].push_back(profile(2));wrong["profiles"][1][field]=wrong["profiles"][0][field];check(!valid_pi_pair_peers(wrong,self));
    }
    auto wrong=index;wrong["profiles"].push_back(profile(2));wrong["profiles"][1]["pair"]["pair_id"]=wrong["profiles"][0]["pair"]["pair_id"];
    check(!valid_pi_pair_peers(wrong,self));
    auto full=empty_pi_pair_peers(self);for(unsigned i=1;i<=64;++i)full["profiles"].push_back(profile(i));check(valid_pi_pair_peers(full,self));
    full["profiles"].push_back(profile(65));check(!valid_pi_pair_peers(full,self));
    auto retirement=profile(100);retirement.erase("profile_generation");retirement.erase("stage");retirement.erase("initial_operation_id");retirement.erase("initial_request_digest");
    retirement["pair"]["state"]="revoked";retirement["pair"]["pending"]=nullptr;
    auto retired=index;retired["retired"].push_back(retirement);check(valid_pi_pair_peers(retired,self));
    retired["retired"][0]["pair"]["current"]=active["profiles"][0]["pair"]["current"];check(!valid_pi_pair_peers(retired,self));
    retired=index;
    for(unsigned i=0;i<32;++i){auto record=retirement;record["pair"]["pair_id"]=id(5000+i);retired["retired"].push_back(record);}
    check(valid_pi_pair_peers(retired,self));retirement["pair"]["pair_id"]=id(6000);retired["retired"].push_back(retirement);check(!valid_pi_pair_peers(retired,self));
    auto mixed=retired;mixed["retired"].erase(mixed["retired"].end()-1);mixed["uncertain"].push_back(profile(99));
    check(!valid_pi_pair_peers(mixed,self));mixed["retired"].erase(mixed["retired"].end()-1);check(valid_pi_pair_peers(mixed,self));
    auto duplicate=index.dump();duplicate.pop_back();duplicate+=",\"profiles\":[]}";refuses([&]{(void)parse_pi_pair_peers(duplicate,self);});
    refuses([&]{(void)parse_pi_pair_peers(std::string(1048577,' '),self);});
    wrong=index;wrong["schema"]="pi-management-peers/2";check(!valid_pi_pair_peers(wrong,self));
    wrong=index;wrong["outgoing_keys"]=Json::array();check(!valid_pi_pair_peers(wrong,self));
    char pattern[]="/tmp/pi-pair-peers-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary,ns=root+"/selected",selection=root+"/selection.json";
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};check(!mkdir(ns.c_str(),0700));
    const Json catalog{{"schema","pi-management-pairs/1"},{"self_id",self},{"authorization_generation","0"},{"active",nullptr},{"retired",Json::array()}};
    const auto empty_operations=pi_pair_operations(self,Json::array()).document,empty_peers=empty_pi_pair_peers(self);
    const Json initialization{{"kind","first"},{"operation_id",id(9001)},{"installation_id",id(9002)},{"source","controller-establishment"}};
    put(ns+"/authority.json",catalog.dump());put(ns+"/operations.json",empty_operations.dump());put(ns+"/peers.json",empty_peers.dump());
    put(selection,pi_pair_history_record(self,id(9000),initialization,catalog,empty_operations,empty_peers,true).dump());
    const PiPairCatalogStore::Authority authority{selection,id(9000)};
    auto store=std::make_unique<PiPairCatalogStore>(ns,self,std::function<void(PiPairCatalogWritePoint)>{},authority);const auto before=store->snapshot();
    Json reservation{{"scope","device"},{"principal_id",self},{"target_id",id(1)},{"pair_id",id(2001)},{"authorization_generation","7"},
        {"operation_id",id(3001)},{"request_digest",std::string(64,'c')},{"state","pending"},{"result_digest",nullptr},
        {"unresolved",true},{"receipt",nullptr},{"continuation",nullptr}};
    const auto operations=pi_pair_operations(self,Json::array({reservation})).document;
    auto second=reservation;second["operation_id"]=id(3101);second["pair_id"]=id(2101);
    const auto both=pi_pair_operations(self,Json::array({reservation,second})).document;
    refuses([&]{store->publish(before,catalog,operations,superseded);});
    check(!std::filesystem::exists(ns+"/transactions"));
    refuses([&]{store->publish(before,catalog,empty_operations,index);});
    check(!std::filesystem::exists(ns+"/transactions")&&store->snapshot().authority->signature==before.authority->signature);
    for(const auto &change:{Json{{"target_id",id(2)}},Json{{"pair_id",id(2002)}},Json{{"authorization_generation","8"}},
        Json{{"state","committed"},{"unresolved",false},{"result_digest",std::string(64,'f')}}}){
        auto invalid=reservation;invalid.update(change);const auto book=pi_pair_operations(self,Json::array({invalid})).document;
        refuses([&]{store->publish(before,catalog,book,index);});check(!std::filesystem::exists(ns+"/transactions"));
    }
    store->publish(before,catalog,operations,index);const auto accepted=store->snapshot();
    check(accepted.peers&&accepted.peers->bytes==index.dump()&&Json::parse(accepted.catalog.bytes)["authorization_generation"]=="0");
    store->publish(accepted,catalog,both,superseded);const auto with_uncertain=store->snapshot();
    check(parse_pi_pair_peers(with_uncertain.peers->bytes,self)==superseded);
    store.reset();store=std::make_unique<PiPairCatalogStore>(ns,self,std::function<void(PiPairCatalogWritePoint)>{},authority);
    check(store->snapshot().peers->bytes==superseded.dump());
    store->publish(store->snapshot(),catalog,operations,index);
    store.reset();put(ns+"/peers.json",accepted.peers->bytes+"\n");
    refuses([&]{PiPairCatalogStore cold(ns,self,{},authority);});put(ns+"/peers.json",accepted.peers->bytes);
    // A digest-consistent committed WAL must still refuse a peer/operation
    // binding mismatch before applying any staged final image.
    store=std::make_unique<PiPairCatalogStore>(ns,self,[](auto stage){if(stage==PiPairCatalogWritePoint::Committed)throw std::runtime_error("fixture crash");},authority);
    auto next=index;next["profiles"][0]["profile_generation"]="2";
    refuses([&]{store->publish(store->snapshot(),catalog,operations,next);});store.reset();
    auto wal=Json::parse(bytes(ns+"/transactions/transaction.json"));next["profiles"][0]["target_id"]=id(2);next["profiles"][0]["pair"]["peer_id"]=id(2);
    check(valid_pi_pair_peers(next,self));const auto next_selector=pi_pair_history_record(self,id(9000),initialization,catalog,operations,next,true);
    for(auto &image:wal["records"]){
        std::string changed;
        if(image["kind"]=="peers")changed=next.dump();else if(image["kind"]=="authority")changed=next_selector.dump();else continue;
        put(ns+"/transactions/"+image["staged_name"].get<std::string>()+".new",changed);image["new_digest"]=PiManagementJournal::digest(changed);
    }
    put(ns+"/transactions/transaction.json",wal.dump());const auto exact_selector=bytes(selection),exact_peers=bytes(ns+"/peers.json"),exact_operations=bytes(ns+"/operations.json");
    refuses([&]{PiPairCatalogStore cold(ns,self,{},authority);});
    check(bytes(selection)==exact_selector&&bytes(ns+"/peers.json")==exact_peers&&bytes(ns+"/operations.json")==exact_operations&&
        bytes(ns+"/authority.json")==catalog.dump()&&std::filesystem::exists(ns+"/transactions/transaction.json"));
    std::cout<<"PASS closed bounded outgoing-pair index and endpoint/key isolation: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
