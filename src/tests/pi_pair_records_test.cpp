// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/pi_pair_catalog_store.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_rotation.hpp"
#include "wtp_endpoint/runtime.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair record check "+std::to_string(checks));}
template<class F>void refuses(F action){bool held=false;try{action();}catch(...){held=true;}check(held);}
std::string id(char c){return std::string(32,c);}
const std::string key(43,'A');
Json epoch(const char *number){return {{"epoch",number},{"tls_psk",key},{"service_ms","0"},{"connections","0"}};}
Json record(){return {{"schema","pi-management-pair/1"},{"self_id",id('a')},{"peer_id",id('b')},{"pair_id",id('c')},
    {"authorization_generation","1"},{"role","manager"},{"state","active"},{"current",epoch("1")},
    {"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},{"retirement_verifier",key}};}
Json catalog(){return {{"schema","pi-management-pairs/1"},{"self_id",id('a')},{"authorization_generation","1"},
    {"active",record()},{"retired",Json::array()}};}
Json rotation_book(){
    auto key=PiPairRotationKey::generate();
    Json prepare{{"contract","Wsprry-Pair/1"},{"schema","rotate-prepare"},{"operation_id",id('d')},{"pair_id",id('c')},
        {"expected_epoch","1"},{"pA",key.public_point()},{"nonce",std::string(43,'A')}};
    Json intent{{"kind","rotation-intent"},{"self_id",id('a')},{"peer_id",id('b')},{"profile_id",id('f')},{"pair_id",id('c')},
        {"authorization_generation","1"},{"operation_id",id('d')},{"expected_epoch","1"},{"stage","awaiting_prepare"},
        {"prepare",prepare},{"prepared",nullptr},{"private_key",key.export_private().storage_encoding()}};
    Json entry{{"scope","device"},{"principal_id",id('a')},{"target_id",id('b')},{"pair_id",id('c')},{"authorization_generation","1"},
        {"operation_id",id('d')},{"request_digest",std::string(64,'e')},{"state","pending"},{"result_digest",nullptr},
        {"unresolved",true},{"receipt",nullptr},{"continuation",intent}};
    return pi_pair_operations(id('a'),Json::array({entry})).document;
}
void prepared_projection(Json &catalog,Json &operations){
    auto peer=PiPairRotationKey::generate();auto &intent=operations["entries"][0]["continuation"];
    intent["stage"]="awaiting_commit";intent["private_key"]=nullptr;
    intent["prepared"]={{"contract","Wsprry-Pair/1"},{"schema","rotate-prepared"},{"operation_id",id('d')},
        {"next_epoch","2"},{"pB",peer.public_point()},{"nonce",std::string(43,'A')}};
    // Artificial key bytes qualify atomic storage only; rotation ECDH/HKDF is
    // tested independently once its exact transcript amendment is approved.
    catalog["active"]["pending"]=epoch("2");
}
void put(const std::string &path,const std::string &bytes){std::ofstream file(path);file<<bytes;file.close();check(!chmod(path.c_str(),0600));}
}
int main(int argc,char **argv){try{
    if(argc==4&&std::string(argv[1])=="--crash"){
        const auto point=std::stoi(argv[3]);
        PiPairCatalogStore store(argv[2],id('a'),[&](auto current){if(static_cast<int>(current)==point)_exit(73);});
        auto next=catalog();next["authorization_generation"]="2";next["active"]["authorization_generation"]="2";
        store.publish(store.snapshot(),next);return 2;
    }
    if(argc==4&&std::string(argv[1])=="--rotation-crash"){
        const auto point=std::stoi(argv[3]);
        PiPairCatalogStore store(argv[2],id('a'),[&](auto current){if(static_cast<int>(current)==point)_exit(73);});
        const auto before=store.snapshot();auto next=parse_pi_pair_catalog(before.catalog.bytes,id('a'));
        auto operations=parse_pi_pair_operations(before.operations.bytes,id('a'));prepared_projection(next,operations);
        store.publish(before,next,operations);return 2;
    }
    const auto original=catalog();check(valid_pi_pair_catalog(original,id('a')));
    check(parse_pi_pair_catalog(original.dump(),id('a'))==original);
    check(!valid_pi_pair_catalog(original,id('b')));
    for(const auto &change:{Json{{"schema","pi-management-pair/2"}},Json{{"self_id",id('b')}},Json{{"peer_id",id('a')}},
        Json{{"state","revoked"}},Json{{"authorization_generation","2"}},Json{{"retirement_verifier",nullptr}},
        Json{{"retirement_verifier",std::string(42,'A')+"B"}},Json{{"role","admin"}},Json{{"code","2345-ABCD"}},
        Json{{"receipt",Json{{"operation_id",id('d')},{"request_digest",std::string(64,'e')},{"result_digest",nullptr}}}}}){
        auto value=original;value["active"].update(change);check(!valid_pi_pair_catalog(value,id('a')));
    }
    for(const auto field:{"current","pending","previous","receipt"}){
        auto value=original;value["active"].erase(field);check(!valid_pi_pair_catalog(value,id('a')));
    }
    for(const auto number:{"00","-1","18446744073709551616"}){
        auto value=original;value["active"]["current"]["connections"]=number;check(!valid_pi_pair_catalog(value,id('a')));
    }
    auto value=original;value["active"]["current"]["connections"]="18446744073709551615";check(valid_pi_pair_catalog(value,id('a')));
    value=original;value["active"]["pending"]=epoch("2");check(valid_pi_pair_catalog(value,id('a')));
    value["active"]["pending"]=epoch("3");check(!valid_pi_pair_catalog(value,id('a')));
    value=original;value["active"]["current"]=epoch("2");value["active"]["previous"]=epoch("1");check(valid_pi_pair_catalog(value,id('a')));
    value["active"]["pending"]=epoch("3");check(!valid_pi_pair_catalog(value,id('a')));
    value=original;value["active"]["state"]="pending";value["active"]["current"]=nullptr;value["active"]["pending"]=epoch("1");check(valid_pi_pair_catalog(value,id('a')));
    value["active"]["pending"]=epoch("2");check(!valid_pi_pair_catalog(value,id('a')));
    auto retired=record();retired["state"]="revoked";retired["current"]=nullptr;
    value=original;value["active"]=nullptr;value["retired"].push_back(retired);check(valid_pi_pair_catalog(value,id('a')));
    value["active"]=record();check(!valid_pi_pair_catalog(value,id('a')));
    value["active"]=nullptr;value["retired"][0]["current"]=epoch("1");check(!valid_pi_pair_catalog(value,id('a')));
    value=original;value["active"]=nullptr;value["authorization_generation"]="0";check(valid_pi_pair_catalog(value,id('a')));
    value["retired"].push_back(retired);check(!valid_pi_pair_catalog(value,id('a')));
    value=original;value["active"]=nullptr;value["authorization_generation"]="33";
    for(unsigned index=0;index<32;++index){
        auto row=retired;std::string identity(30,'d');identity+="0123456789abcdef"[index>>4];identity+="0123456789abcdef"[index&15];
        row["pair_id"]=identity;row["authorization_generation"]=std::to_string(index+1);value["retired"].push_back(row);
    }
    check(valid_pi_pair_catalog(value,id('a')));
    auto extra=retired;extra["pair_id"]=id('f');value["retired"].push_back(extra);check(!valid_pi_pair_catalog(value,id('a')));
    std::string duplicate=original.dump();duplicate.pop_back();duplicate+=",\"active\":null}";
    refuses([&]{(void)parse_pi_pair_catalog(duplicate,id('a'));});
    refuses([&]{(void)parse_pi_pair_catalog(std::string(1048577,' '),id('a'));});
    // Private pointer publication is conditional on the exact prior observed
    // inode, not merely equal bytes. Reads never initialize missing history.
    char pattern[]="/tmp/pi-pair-records-XXXXXX";const auto tmp=mkdtemp(pattern);check(tmp);
    const std::string root=tmp,path=root+"/selection.json";
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} cleanup{root};
    testing::set_wtp_pi_private_state_path(root+"/device");
    // This dedicated fixture process never starts the endpoint. Its path seam
    // deliberately cannot be reset to a production location or an empty path.
    check(private_device_state_directory(PrivateDeviceDomain::Pairing,id('a'))==root+"/device.pairing-"+id('a'));
    check(private_device_state_directory(PrivateDeviceDomain::Recurrence,id('a'))==root+"/device.recurrence-"+id('a'));
    check(private_device_state_directory(PrivateDeviceDomain::ControllerRetirements,id('a'))==root+"/device.controller-retirements-"+id('a'));
    refuses([&]{(void)private_device_state_directory(static_cast<PrivateDeviceDomain>(99),id('a'));});
    refuses([&]{(void)private_device_state_directory(PrivateDeviceDomain::Pairing,"browser-path");});
    check(!PiManagementJournal::read_private_record(path));check(std::filesystem::is_empty(root));
    const auto first=PiManagementJournal::publish_private_record(path,{},"first");
    check(first.bytes=="first");struct stat info{};check(!lstat(path.c_str(),&info)&&(info.st_mode&0777)==0600);
    refuses([&]{(void)PiManagementJournal::publish_private_record(path,{},"overwrite");});
    put(path+".replacement","first");check(!rename((path+".replacement").c_str(),path.c_str()));
    refuses([&]{(void)PiManagementJournal::publish_private_record(path,first,"stale");});
    const auto observed=PiManagementJournal::read_private_record(path);
    const auto next=PiManagementJournal::publish_private_record(path,observed,"next");check(next.bytes=="next");
    check(!unlink(path.c_str())&&!symlink("outside",path.c_str()));
    refuses([&]{(void)PiManagementJournal::read_private_record(path);});
    refuses([&]{(void)PiManagementJournal::publish_private_record(path,{},"unsafe");});
    check(!std::filesystem::exists(root+"/outside"));check(!unlink(path.c_str()));
    put(path,"private");check(!chmod(path.c_str(),0644));
    refuses([&]{(void)PiManagementJournal::read_private_record(path);});
    check(!chmod(path.c_str(),0600));check(!link(path.c_str(),(root+"/hardlink").c_str()));
    refuses([&]{(void)PiManagementJournal::read_private_record(path);});
    check(!unlink((root+"/hardlink").c_str()));
    refuses([&]{(void)PiManagementJournal::publish_private_record(path,PiManagementJournal::read_private_record(path),std::string(1048577,'x'));});
    check(PiManagementJournal::read_private_record(path)->bytes=="private");
    check(!unlink(path.c_str())&&!mkfifo(path.c_str(),0600));
    refuses([&]{(void)PiManagementJournal::read_private_record(path);});
    check(!unlink(path.c_str())&&!mkdir(path.c_str(),0700));
    refuses([&]{(void)PiManagementJournal::read_private_record(path);});
    check(!rmdir(path.c_str())&&!chmod(root.c_str(),0777));
    refuses([&]{(void)PiManagementJournal::publish_private_record(path,{},"unsafe parent");});
    check(!chmod(root.c_str(),0700)&&!PiManagementJournal::read_private_record(path));
    const auto store_root=root+"/catalog";
    refuses([&]{PiPairCatalogStore store(store_root,id('a'));});check(!std::filesystem::exists(store_root));
    check(!mkdir(store_root.c_str(),0700));
    refuses([&]{PiPairCatalogStore store(store_root,id('a'));});check(std::filesystem::is_empty(store_root));
    put(store_root+"/authority.json",original.dump());
    put(store_root+"/operations.json",pi_pair_operations(id('a'),Json::array()).document.dump());
    {
        PiPairCatalogStore store(store_root,id('a'));
        refuses([&]{PiPairCatalogStore duplicate(store_root,id('a'));});
        const auto before=store.snapshot();auto next=original;next["active"]["current"]["connections"]="1";
        store.publish(before,next);check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,id('a'))==next);
        refuses([&]{store.publish(before,original);});check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,id('a'))==next);
        auto invalid=next;invalid["active"]["code"]="2345ABCD";
        refuses([&]{store.publish(store.snapshot(),invalid);});check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,id('a'))==next);
        put(store_root+"/replacement",next.dump());check(!rename((store_root+"/replacement").c_str(),(store_root+"/authority.json").c_str()));
        refuses([&]{(void)store.snapshot();});refuses([&]{store.publish(before,original);});
        check(!store.recover_own_failure());refuses([&]{(void)store.snapshot();});
    }
    check(!unlink((store_root+"/authority.json").c_str()));
    refuses([&]{PiPairCatalogStore store(store_root,id('a'));});check(!std::filesystem::exists(store_root+"/authority.json"));
    put(store_root+"/authority.json","{broken");refuses([&]{PiPairCatalogStore store(store_root,id('a'));});
    for(const auto point:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const auto storage=root+"/live-failure-"+std::to_string(static_cast<int>(point));check(!mkdir(storage.c_str(),0700));
        put(storage+"/authority.json",original.dump());put(storage+"/operations.json",pi_pair_operations(id('a'),Json::array()).document.dump());
        PiPairCatalogStore store(storage,id('a'),[&](auto current){if(current==point)throw std::runtime_error("fixture local failure");});
        const auto before=store.snapshot();auto next=original;next["authorization_generation"]="2";next["active"]["authorization_generation"]="2";
        refuses([&]{store.publish(before,next);});check(store.recover_own_failure());
        const auto after=store.snapshot();check(parse_pi_pair_catalog(after.catalog.bytes,id('a'))==(point==PiPairCatalogWritePoint::Prepared?original:next));
        check(!std::filesystem::exists(storage+"/transactions/transaction.json"));
        check(!store.recover_own_failure()&&store.snapshot().catalog.signature==after.catalog.signature);
    }
    {
        const auto storage=root+"/unrelated-live-decision";check(!mkdir(storage.c_str(),0700));
        put(storage+"/authority.json",original.dump());put(storage+"/operations.json",pi_pair_operations(id('a'),Json::array()).document.dump());
        PiPairCatalogStore store(storage,id('a'),[](auto current){if(current==PiPairCatalogWritePoint::Prepared)throw std::runtime_error("fixture prepared");});
        auto next=original;next["authorization_generation"]="2";next["active"]["authorization_generation"]="2";
        refuses([&]{store.publish(store.snapshot(),next);});const auto path=storage+"/transactions/transaction.json";
        auto decision=Json::parse(PiManagementJournal::read_private_record(path)->bytes);
        decision["transaction_id"]=decision["transaction_id"]==id('e')?id('f'):id('e');put(path,decision.dump());
        check(!store.recover_own_failure()&&PiManagementJournal::read_private_record(storage+"/authority.json")->bytes==original.dump());
        refuses([&]{(void)store.snapshot();});check(std::filesystem::exists(path));
    }
    for(const auto point:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const auto storage=root+"/crash-"+std::to_string(static_cast<int>(point));check(!mkdir(storage.c_str(),0700));
        put(storage+"/authority.json",original.dump());
        put(storage+"/operations.json",pi_pair_operations(id('a'),Json::array()).document.dump());
        const auto child=fork();check(child>=0);
        if(!child){const auto index=std::to_string(static_cast<int>(point));execl(argv[0],argv[0],"--crash",storage.c_str(),index.c_str(),nullptr);_exit(127);}
        int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
        PiPairCatalogStore recovered(storage,id('a'));const auto saved=parse_pi_pair_catalog(recovered.snapshot().catalog.bytes,id('a'));
        check(saved["authorization_generation"]==(point==PiPairCatalogWritePoint::Prepared?"1":"2"));
        check(saved["active"]["authorization_generation"]==saved["authorization_generation"]);
        check(std::filesystem::is_empty(storage+"/transactions"));
    }
    // Checksums alone do not establish a valid PairRecord. A semantically
    // invalid but digest-consistent staged image must not reach a final path.
    const auto damaged=root+"/damaged-decision";check(!mkdir(damaged.c_str(),0700));
    put(damaged+"/authority.json",original.dump());
    put(damaged+"/operations.json",pi_pair_operations(id('a'),Json::array()).document.dump());
    const auto child=fork();check(child>=0);
    if(!child){execl(argv[0],argv[0],"--crash",damaged.c_str(),"1",nullptr);_exit(127);}
    int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
    const auto wal_path=damaged+"/transactions/transaction.json";
    auto wal=Json::parse(PiManagementJournal::read_private_record(wal_path)->bytes);
    const auto staged=damaged+"/transactions/"+wal["records"][0]["staged_name"].get<std::string>()+".new";
    auto malformed=original;malformed["active"]["schema"]="pi-management-pair/2";
    put(staged,malformed.dump());wal["records"][0]["new_digest"]=PiManagementJournal::digest(malformed.dump());put(wal_path,wal.dump());
    refuses([&]{PiPairCatalogStore store(damaged,id('a'));});
    check(PiManagementJournal::read_private_record(damaged+"/authority.json")->bytes==original.dump());
    check(std::filesystem::exists(wal_path));
    for(const auto point:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const auto storage=root+"/rotation-crash-"+std::to_string(static_cast<int>(point));check(!mkdir(storage.c_str(),0700));
        const auto awaiting=rotation_book();put(storage+"/authority.json",original.dump());put(storage+"/operations.json",awaiting.dump());
        const auto child=fork();check(child>=0);
        if(!child){const auto index=std::to_string(static_cast<int>(point));execl(argv[0],argv[0],"--rotation-crash",storage.c_str(),index.c_str(),nullptr);_exit(127);}
        int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
        PiPairCatalogStore recovered(storage,id('a'));const auto snapshot=recovered.snapshot();
        const auto pair=parse_pi_pair_catalog(snapshot.catalog.bytes,id('a')),operations=parse_pi_pair_operations(snapshot.operations.bytes,id('a'));
        const auto before_commit=point==PiPairCatalogWritePoint::Prepared;
        check(pair["active"]["pending"].is_null()==before_commit);
        check(operations["entries"][0]["continuation"]["private_key"].is_string()==before_commit);
        check(operations["entries"][0]["continuation"]["stage"]==(before_commit?"awaiting_prepare":"awaiting_commit"));
        check(std::filesystem::is_empty(storage+"/transactions"));
        const auto retained=operations["entries"][0]["continuation"]["private_key"];
        auto inconsistent=pair;inconsistent["active"]["pending"]=before_commit?Json(epoch("2")):Json(nullptr);
        refuses([&]{recovered.publish(snapshot,inconsistent,operations);});
        check(recovered.snapshot().catalog.bytes==snapshot.catalog.bytes&&recovered.snapshot().operations.bytes==snapshot.operations.bytes);
        check(parse_pi_pair_operations(recovered.snapshot().operations.bytes,id('a'))["entries"][0]["continuation"]["private_key"]==retained);
    }
    std::cout<<"PASS private pair catalog and conditional private publication: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
