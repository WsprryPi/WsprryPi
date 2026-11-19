// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_history.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "wtp_integration/browser_http.hpp"
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
const std::string self(32,'a');
void check(bool value){++checks;if(!value)throw std::runtime_error("pair recovery check "+std::to_string(checks));}
template<class F>void refuses(F action){bool held=false;try{action();}catch(...){held=true;}check(held);}
void put(const std::string &path,const std::string &bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();check(!chmod(path.c_str(),0600));}
std::string bytes(const std::string &path){return PiManagementJournal::read_private_record(path)->bytes;}
Json selector(const std::string &root){return Json::parse(bytes(root+"/authority.json"));}
std::string space(const std::string &root){return root+".ns-"+selector(root)["namespace_id"].get<std::string>();}
struct Approval {
    ManagementBrowserSessions sessions;
    httplib::Request http;
    std::optional<LocalBrowserPrincipal> principal;
    BrowserRequest request;
    Approval(std::string generation="0",char operation='b',std::string action="recover"){
        http.method="POST";http.path="/api/control/v1/host/pairing/window";http.target=http.path;http.remote_addr="192.168.1.20";
        http.headers={{"Host","controller.local"},{"Origin","http://controller.local"},{"Content-Type","application/json"},
            {"X-WsprryPico-Request","1"},{"Sec-Fetch-Site","same-origin"}};
        http.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",std::string(32,operation)},
            {"action",action},{"expected_authorization_generation",generation}}.dump();
        auto bootstrap=http;bootstrap.path="/api/control/v1/host/management-session";bootstrap.target=bootstrap.path;
        httplib::Response reply;check(sessions.establish(bootstrap,reply));
        const auto cookie=reply.get_header_value("Set-Cookie");http.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));
        http.headers.emplace("X-WsprryPi-CSRF",reply.get_header_value("X-WsprryPi-CSRF"));
        principal=sessions.authorize_local_mutation(http,{true,"controller.local",{},{{"192.168.1.10","255.255.255.0"}}});
        check(principal.has_value());request=*wtp_browser_http_request(http);
    }
    void recover(PiPairHistory &owner,const std::function<void(PiPairCatalogWritePoint)> &checkpoint={}){
        owner.recover_access(request,*principal,sessions,checkpoint);
    }
};
}
int main(int argc,char **argv){try{
    if(argc==5&&std::string(argv[1])=="--crash"){
        PiPairHistory owner(argv[2],self);Approval approval(argv[4],'c');const int point=std::stoi(argv[3]);
        approval.recover(owner,[&](auto stage){if(static_cast<int>(stage)==point)_exit(73);});return 3;
    }
    char pattern[]="/tmp/pi-pair-recovery-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    const auto path=root+"/pairing";
    {
        PiPairHistory owner(path,self);check(!owner.available()&&!std::filesystem::exists(path));
        check(owner.recovery_generation()=="0"&&!std::filesystem::exists(path));
        Approval wrong("0",'c',"pair");refuses([&]{wrong.recover(owner);});check(!std::filesystem::exists(path));
        Approval recover;ManagementBrowserSessions other;
        refuses([&]{owner.recover_access(recover.request,*recover.principal,other);});check(recover.principal->matches(recover.request));
        auto changed=recover.request;changed.body+=" ";refuses([&]{owner.recover_access(changed,*recover.principal,recover.sessions);});
        recover.recover(owner);check(owner.available()&&!recover.principal->matches(recover.request));
        const auto state=owner.snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self);
        const auto operations=parse_pi_pair_operations(state.operations.bytes,self);
        check(catalog["authorization_generation"]=="0"&&catalog["active"].is_null()&&operations["entries"].size()==1);
        check(selector(path)["initialization"]["previous_namespace"].is_null()&&selector(path)["initialization"]["previous_selector_digest"].is_null());
        const auto exact=state.authority->bytes;
        refuses([&]{recover.recover(owner);});Approval fresh;
        refuses([&]{fresh.recover(owner);});check(owner.snapshot().authority->bytes==exact);
        // A sound history is never reset by a different protected local action.
        Approval new_operation("0",'d');refuses([&]{new_operation.recover(owner);});check(owner.snapshot().authority->bytes==exact);
    }
    const auto previous=selector(path);const auto previous_space=space(path);const auto previous_catalog=bytes(previous_space+"/authority.json");
    const auto previous_operations=bytes(previous_space+"/operations.json");
    put(previous_space+"/authority.json","{}");
    {
        PiPairHistory owner(path,self);check(!owner.available()&&owner.recovery_generation()=="0");Approval stale("1");refuses([&]{stale.recover(owner);});
        Approval recover("0",'c');recover.recover(owner);check(owner.available());const auto now=selector(path);
        check(now["namespace_id"]!=previous["namespace_id"]&&now["authorization_generation"]=="1"&&now["initialization"]["previous_namespace"]==previous["namespace_id"]);
        check(owner.recovery_generation()=="1");
        check(bytes(previous_space+"/authority.json")=="{}"&&bytes(previous_space+"/operations.json")==previous_operations);
        check(Json::parse(bytes(space(path)+"/recovery-selector.json"))==previous);
        // Restoring old files cannot select them or restore their authority.
        put(previous_space+"/authority.json",previous_catalog);check(owner.snapshot().authority->bytes==now.dump());
    }
    {
        PiPairHistory owner(path,self);check(owner.available());const auto evidence=space(path)+"/recovery-selector.json";
        const auto old=bytes(evidence);put(evidence,"{}");check(!owner.available());put(evidence,old);
    }
    {PiPairHistory owner(path,self);check(owner.available());}
    // A well-owned but corrupt/empty selector is retained byte-for-byte as
    // evidence; its unverified namespace/generation fields grant no authority.
    char recovery_operation='d';
    for(const auto &corrupt:{std::string("{}"),std::string()}){
        put(path+"/authority.json",corrupt);PiPairHistory owner(path,self);check(!owner.available()&&owner.recovery_generation()=="0");Approval recover("0",recovery_operation++);
        recover.recover(owner);check(owner.available()&&selector(path)["authorization_generation"]=="0");
        check(bytes(space(path)+"/recovery-selector.json")==corrupt&&selector(path)["initialization"]["previous_namespace"].is_null());
    }
    // Exact generation overflow is a hold, never wraparound to a reusable pair.
    {
        PiPairHistory owner(path,self);const auto before=owner.snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self);
        catalog["authorization_generation"]="18446744073709551615";owner.publish(before,catalog);
    }
    put(space(path)+"/authority.json","{}");
    {PiPairHistory owner(path,self);Approval recover("18446744073709551615");const auto before=bytes(path+"/authority.json");
        check(owner.recovery_generation()=="18446744073709551615");
        refuses([&]{recover.recover(owner);});check(bytes(path+"/authority.json")==before);}
    {
        const auto test=root+"/competing-absent";PiPairHistory first(test,self),second(test,self);
        Approval one,two;one.recover(first);const auto exact=first.snapshot().authority->bytes;
        refuses([&]{two.recover(second);});check(first.snapshot().authority->bytes==exact&&!second.available());
        refuses([&]{PiPairHistory duplicate(test,self);});
    }
    {
        const auto test=root+"/changed-selection";
        {PiPairHistory owner(test,self);Approval initialize;initialize.recover(owner);}
        put(space(test)+"/authority.json","{}");PiPairHistory owner(test,self);Approval recover("0",'c');
        refuses([&]{recover.recover(owner,[&](auto stage){if(stage==PiPairCatalogWritePoint::Prepared)put(test+"/authority.json","{}");});});
        check(bytes(test+"/authority.json")=="{}"&&!owner.available()&&!recover.principal->matches(recover.request));
    }
    {
        const auto test=root+"/retired-before-publication";PiPairHistory owner(test,self);Approval recover;
        refuses([&]{recover.recover(owner,[&](auto stage){if(stage==PiPairCatalogWritePoint::Prepared)recover.sessions.retire();});});
        check(!std::filesystem::exists(test)&&!owner.available());
    }
    {
        const auto test=root+"/capacity";PiPairHistory owner(test,self);Approval recover;
        for(unsigned i=0;i<32;++i)check(!mkdir((test+".ns-"+std::to_string(i)).c_str(),0700));
        refuses([&]{recover.recover(owner);});check(!std::filesystem::exists(test)&&!owner.available());
    }
    {
        const auto test=root+"/unsafe-selector",victim=root+"/unrelated";put(victim,"private unrelated fixture");
        {PiPairHistory owner(test,self);Approval initialize;initialize.recover(owner);}
        check(!unlink((test+"/authority.json").c_str())&&!symlink(victim.c_str(),(test+"/authority.json").c_str()));
        PiPairHistory owner(test,self);Approval recover("0",'c');refuses([&]{recover.recover(owner);});
        check(bytes(victim)=="private unrelated fixture"&&!owner.available()&&!owner.recovery_generation());
    }
    // Fresh process death before and after the single selector linearization
    // retains either held old state or the complete, unpaired new namespace.
    for(bool missing:{false,true})for(auto stage:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const auto test=root+"/crash-"+std::to_string(missing)+"-"+std::to_string(static_cast<int>(stage));std::string expected="0",old;
        if(!missing){
            {PiPairHistory owner(test,self);Approval initialize;initialize.recover(owner);}
            old=bytes(test+"/authority.json");put(space(test)+"/authority.json","{}");
        }
        const auto child=fork();check(child>=0);
        if(!child){const auto point=std::to_string(static_cast<int>(stage));execl(argv[0],argv[0],"--crash",test.c_str(),point.c_str(),expected.c_str(),nullptr);_exit(127);}
        int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
        PiPairHistory recovered(test,self);
        if(stage==PiPairCatalogWritePoint::Prepared){
            check(!recovered.available());if(!missing)check(bytes(test+"/authority.json")==old);
            Approval retry("0",'d');retry.recover(recovered);
        }
        check(recovered.available());const auto state=recovered.snapshot();const auto catalog=parse_pi_pair_catalog(state.catalog.bytes,self);
        check(catalog["active"].is_null()&&catalog["authorization_generation"]==(missing?"0":"1"));
        check(parse_pi_pair_operations(state.operations.bytes,self)["entries"].size()==1);
    }
    std::cout<<"PASS protected local pairing namespace recovery and fresh-process publication: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<"After recovery fixture check "<<checks<<": "<<e.what()<<'\n';return 1;}}
