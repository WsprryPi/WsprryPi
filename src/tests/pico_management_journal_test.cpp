// SPDX-License-Identifier: MIT
#include "wtp_integration/pico_management_journal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
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
        // Native Pi replies carry operation state. A 202 transport success
        // must not turn pending into committed or release its write barrier.
        {
            const auto native_route="/api/control/v1/targets/"+device+"/settings";
            const auto native_directory=temporary+"/native";
            PicoManagementJournal native(native_directory,self,device,PicoManagementHistoryAuthority::isolated());
            CHECK(!native.reserve(self,native_route,"PUT",binding,"1",operation,digest));
            Json body{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",binding},{"operation_id",operation},
                {"request_digest",digest},{"state","pending"},{"revision",nullptr},{"error",nullptr}};
            CHECK(!native.finish(operation,{202,body.dump(),{}}));
            CHECK(native.snapshot()["receipts"][0]["receipt"]["state"]=="pending"&&native.snapshot()["receipts"][0]["response"].is_null());
            refuses([&]{native.reserve(self,native_route,"PUT",binding,"1",next,digest);});
            body["state"]="committed";body["revision"]=std::string(64,'f');
            CHECK(!native.finish(operation,{200,body.dump(),"\""+std::string(64,'f')+"\""})&&native.receipt(operation)==body);
            CHECK(!native.reserve(self,native_route,"PUT",binding,"1",next,digest));
            body["operation_id"]=next;body["binding"]["connection_generation"]="3";
            CHECK(!native.finish(next,{200,body.dump(),{}})&&native.receipt(next)["state"]=="unknown");
        }
        {
            const auto route="/api/control/v1/targets/"+device+"/settings";
            const auto directory=temporary+"/native-independent-stop";
            PicoManagementJournal journal(directory,self,device,PicoManagementHistoryAuthority::isolated());
            CHECK(!journal.reserve(self,route,"PUT",binding,"1",operation,digest));
            CHECK(!journal.finish(operation,{503,R"({"error":{"code":"transport_unavailable"}})",{}}));
            const auto unknown=journal.receipt(operation);CHECK(unknown["state"]=="unknown");
            refuses([&]{journal.reserve(self,route,"PUT",binding,"1",next,digest);});
            const auto stop_id=std::string(32,'e');CHECK(!journal.reserve_native_stop(self,binding,"1",stop_id,digest));
            CHECK(journal.receipt(operation)==unknown&&journal.snapshot()["receipts"].back()["receipt"]["state"]=="pending");
            Json stopped{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",binding},{"operation_id",stop_id},
                {"request_digest",digest},{"state","committed"},{"revision",std::string(64,'f')},{"error",nullptr}};
            CHECK(!journal.finish(stop_id,{200,stopped.dump(),"\""+std::string(64,'f')+"\""}));
            PicoManagementJournal cold(directory,self,device,PicoManagementHistoryAuthority::isolated());
            CHECK(cold.receipt(operation)==unknown&&cold.receipt(stop_id)==stopped);
            refuses([&]{cold.reserve(self,route,"PUT",binding,"1",next,digest);});
        }
        for(unsigned scenario=0;scenario<4;++scenario){
            PicoManagementJournal native(temporary+"/native-malformed-"+std::to_string(scenario),self,device,PicoManagementHistoryAuthority::isolated());
            const auto native_route="/api/control/v1/targets/"+device+"/settings";
            CHECK(!native.reserve(self,native_route,"PUT",binding,"1",operation,digest));
            Json body{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",binding},{"operation_id",operation},
                {"request_digest",digest},{"state","committed"},{"revision",std::string(64,'f')},{"error",nullptr}};
            if(scenario==0)body["revision"]=nullptr;
            if(scenario==1)body["request_digest"]=std::string(64,'e');
            if(scenario==2)body["tls_psk"]="artificial-secret";
            const auto etag=scenario==3?"\""+std::string(64,'e')+"\"":std::string{};
            CHECK(!native.finish(operation,{200,body.dump(),etag})&&native.receipt(operation)["state"]=="unknown");
            CHECK(native.snapshot()["receipts"][0]["response"].is_null());
        }
        // A lost native reply survives a new journal owner. Only an exact
        // terminal receipt authenticated to the original pair can resolve it;
        // even a valid 404 or a different manager's success proves nothing.
        {
            const auto native_route="/api/control/v1/targets/"+device+"/settings";
            const auto native_directory=temporary+"/native-readback";
            PicoManagementJournal original(native_directory,self,device,PicoManagementHistoryAuthority::isolated());
            CHECK(!original.reserve(self,native_route,"PUT",binding,"1",operation,digest));
            original.finish(operation,{503,"{}",{}});
            PicoManagementJournal cold(native_directory,self,device,PicoManagementHistoryAuthority::isolated());
            Json body{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",binding},{"operation_id",operation},
                {"request_digest",digest},{"state","committed"},{"revision",std::string(64,'f')},{"error",nullptr}};
            const PicoHttpResponse terminal{200,body.dump(),"\""+std::string(64,'f')+"\""};
            CHECK(!cold.confirm_native(operation,next,"1",terminal));
            CHECK(!cold.confirm_native(operation,self,"2",terminal));
            CHECK(!cold.confirm_native(operation,self,"1",{404,R"({"error":{"code":"operation_unknown"}})",{}}));
            for(unsigned scenario=0;scenario<6;++scenario){
                auto altered=body;
                if(scenario==0)altered["request_digest"]=std::string(64,'e');
                if(scenario==1)altered["binding"]["boot_id"]=std::string(32,'e');
                if(scenario==2)altered["operation_id"]=next;
                if(scenario==3){altered["state"]="pending";altered["revision"]=nullptr;}
                if(scenario==4){altered["state"]="unknown";altered["revision"]=nullptr;}
                if(scenario==5)altered["tls_psk"]="artificial-secret";
                CHECK(!cold.confirm_native(operation,self,"1",{200,altered.dump(),{}}));
            }
            CHECK(cold.receipt(operation)["state"]=="unknown");
            refuses([&]{cold.reserve(self,native_route,"PUT",binding,"1",next,digest);});
            CHECK(cold.confirm_native(operation,self,"1",terminal)&&cold.receipt(operation)==body);
            CHECK(cold.confirm_native(operation,self,"1",terminal));
            CHECK(!cold.reserve(self,native_route,"PUT",binding,"1",next,digest));
            cold.finish(next,{503,"{}",{}});cold.reconcile(next,binding,std::string(64,'a'));
            body["operation_id"]=next;
            CHECK(!cold.confirm_native(next,self,"1",{200,body.dump(),{}}));
            CHECK(cold.receipt(next)["state"]=="unknown");
        }
        const auto directory=temporary+"/management";
        PicoManagementJournal journal(directory,self,device);
        CHECK(journal.snapshot()["receipts"].empty()&&!std::filesystem::exists(directory));
        // Distinct source owners of the same target share admission and epoch;
        // another target remains independent. A nested owner on this thread is
        // required by the coordinator/context/journal call chain.
        {
            auto held=journal.target_admission();CHECK(held);
            PicoManagementJournal same(directory,self,device);
            auto nested=same.target_admission();CHECK(nested&&nested->epoch()==held->epoch());
            bool blocked=false,independent=false;
            std::thread concurrent([&]{
                blocked=!same.target_admission();
                independent=bool(PicoManagementJournal::target_admission(directory,next));
            });concurrent.join();CHECK(blocked&&independent);
            const auto epoch=same.target_epoch();held->retire();
            CHECK(same.target_epoch()==epoch+1&&journal.target_epoch()==epoch+1);
        }

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
