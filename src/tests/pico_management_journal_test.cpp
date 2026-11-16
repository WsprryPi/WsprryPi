// SPDX-License-Identifier: MIT
#include "wtp_integration/pico_management_journal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
#define CHECK(x) do{if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(false)
template<class F> void refuses(F operation){bool rejected=false;try{operation();}catch(...){rejected=true;}CHECK(rejected);}
int main() {
    std::string temporary;
    try {
        char pattern[]="/tmp/pico-management-journal-XXXXXX";const auto dir=mkdtemp(pattern);CHECK(dir);temporary=dir;
        const std::string self(32,'1'),device(32,'2'),operation(32,'3'),next(32,'4');
        const std::string route="/api/control/v1/targets/"+device+"/pico/station";
        Json binding{{"device_id",device},{"boot_id",std::string(32,'5')},{"profile_generation","1"},{"connection_generation","2"}};
        const auto digest=PicoManagementJournal::digest("request");
        const auto directory=temporary+"/management";
        PicoManagementJournal journal(directory,self,device);
        CHECK(journal.snapshot()["receipts"].empty()&&!std::filesystem::exists(directory));
        CHECK(!journal.reserve(self,route,"PUT",binding,"1",operation,digest));
        CHECK(journal.receipt(operation)["state"]=="unknown");
        PicoManagementJournal reopened(directory,self,device);
        CHECK(reopened.receipt(operation)["state"]=="unknown");
        CHECK(reopened.reserve(self,route,"PUT",binding,"1",operation,digest).has_value());
        refuses([&]{reopened.reserve(self,route,"PUT",binding,"1",operation,PicoManagementJournal::digest("different"));});
        refuses([&]{reopened.reserve(self,route,"PUT",binding,"1",next,digest);});
        journal.finish(operation,{200,R"({"contract":"Wsprry-Control/1","saved":true})","\"r1\""});
        CHECK(reopened.receipt(operation)["state"]=="committed");
        const auto replay=reopened.reserve(self,route,"PUT",binding,"1",operation,digest);
        CHECK(replay&&(*replay)["response"]["body"]["saved"]==true);
        CHECK(!reopened.reserve(self,route,"PUT",binding,"1",next,digest));
        reopened.finish(next,{503,R"({"error":{"code":"management_outcome_unknown"}})",{}});
        CHECK(journal.receipt(next)["state"]=="unknown");
        const auto fresh=PicoManagementJournal::digest("authoritative readback");
        reopened.reconcile(next,binding,fresh);
        CHECK(journal.receipt(next)["state"]=="unknown");
        const auto third=std::string(32,'6');
        CHECK(!journal.reserve(self,route,"PUT",binding,"1",third,digest));
        journal.finish(third,{200,R"({"password":"must-not-enter-a-replay-record"})",{}});
        CHECK(journal.receipt(third)["state"]=="unknown");
        const auto path=directory+"/"+device+".json";
        std::ifstream input(path);const std::string bytes((std::istreambuf_iterator<char>(input)),{});
        CHECK(bytes.find("must-not-enter-a-replay-record")==std::string::npos);
        // Valid JSON and closed shapes cannot hide changed durable authority.
        // A new directory has no cached file stamp, so checksum validation itself
        // must detect this change rather than relying on an in-process cache.
        const auto damaged=temporary+"/damaged";
        std::filesystem::create_directory(damaged);CHECK(!chmod(damaged.c_str(),0700));
        auto altered=Json::parse(bytes);
        altered["receipts"][0]["receipt"]["request_digest"]=PicoManagementJournal::digest("bit-flipped request");
        {std::ofstream output(damaged+"/"+device+".json");output<<altered.dump();}
        CHECK(!chmod((damaged+"/"+device+".json").c_str(),0600));
        PicoManagementJournal corrupt(damaged,self,device);refuses([&]{corrupt.snapshot();});
        // Even an identical out-of-band rewrite invalidates the cached file
        // authority; changing its metadata cannot authorize later mutations.
        const auto external=temporary+"/external";
        PicoManagementJournal observed(external,self,device);
        CHECK(!observed.reserve(self,route,"PUT",binding,"1",operation,digest));
        const auto external_path=external+"/"+device+".json";
        CHECK(!chmod(external_path.c_str(),0644));refuses([&]{observed.snapshot();});
        auto changed=binding;changed["boot_id"]=std::string(32,'7');
        refuses([&]{journal.reserve(self,route,"PUT",changed,"1",operation,digest);});
        CHECK(!chmod(path.c_str(),0644));refuses([&]{journal.snapshot();});CHECK(!chmod(path.c_str(),0600));
        {std::ofstream output(path);output<<"{\"schema\":null,\"schema\":null}";}
        refuses([&]{journal.snapshot();});
        std::filesystem::remove(path);std::filesystem::create_symlink(temporary+"/missing",path);
        refuses([&]{journal.snapshot();});
        PicoManagementJournal retained(temporary+"/retained",self,device);
        std::string first,last;
        for(unsigned i=0;i<258;++i) {
            const auto id=PicoManagementJournal::digest(std::to_string(i)).substr(0,32);if(!i)first=id;last=id;
            CHECK(!retained.reserve(self,route,"PUT",binding,"1",id,digest));
            retained.finish(id,{200,R"({"contract":"Wsprry-Control/1","saved":true})",{}});
        }
        CHECK(retained.snapshot()["receipts"].size()==256&&retained.receipt(first).is_null()&&retained.receipt(last)["state"]=="committed");
        const auto oldest_retained=retained.snapshot()["receipts"][0]["receipt"]["operation_id"].get<std::string>();
        const auto pending_retention=std::string(32,'9');
        CHECK(!retained.reserve(self,route,"PUT",binding,"1",pending_retention,digest));
        CHECK(retained.snapshot()["receipts"].size()==257&&!retained.receipt(oldest_retained).is_null());
        CHECK(!retained.finish(pending_retention,{503,R"({"error":{"code":"management_outcome_unknown"}})",{}}));
        CHECK(retained.snapshot()["receipts"].size()==257&&!retained.receipt(oldest_retained).is_null());
        CHECK(retained.reconcile(pending_retention,binding,fresh));
        CHECK(retained.snapshot()["receipts"].size()==256&&retained.receipt(oldest_retained).is_null());
        // The 32-unresolved ceiling is controller-wide, including targets
        // without any live editor connection. Reopening cannot reset it.
        const auto shared=temporary+"/global";std::vector<std::string> devices;
        for(unsigned i=0;i<33;++i)devices.push_back(PicoManagementJournal::digest("device-"+std::to_string(i)).substr(0,32));
        for(unsigned i=0;i<32;++i) {
            PicoManagementJournal entry(shared,self,devices[i]);auto bound=binding;bound["device_id"]=devices[i];
            CHECK(!entry.reserve(self,"/api/control/v1/targets/"+devices[i]+"/pico/station","PUT",bound,"1",operation,digest));
        }
        PicoManagementJournal overflow(shared,self,devices[32]);auto bound=binding;bound["device_id"]=devices[32];
        refuses([&]{overflow.reserve(self,"/api/control/v1/targets/"+devices[32]+"/pico/station","PUT",bound,"1",operation,digest);});
        CHECK(!std::filesystem::exists(shared+"/"+devices[32]+".json"));
        PicoManagementJournal oldest(shared,self,devices[0]);auto old_binding=binding;old_binding["device_id"]=devices[0];
        oldest.reconcile(operation,old_binding,fresh);
        CHECK(!overflow.reserve(self,"/api/control/v1/targets/"+devices[32]+"/pico/station","PUT",bound,"1",operation,digest));
        std::filesystem::remove_all(temporary);
        std::cout<<"PASS: durable reservation/replay, restart uncertainty, request-ID/binding refusal, explicit readback review, secret refusal, checksum corruption, file authority and global unresolved bounds\n";
    } catch(const std::exception &e) {if(!temporary.empty())std::filesystem::remove_all(temporary);std::cerr<<e.what()<<'\n';return 1;}
}
