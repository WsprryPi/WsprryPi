// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "application_control/web_bootstrap.hpp"
#include "wtp_integration/pico_management_journal.hpp"
#include <cassert>
#include <iostream>
#undef assert
#define assert(condition) do{if(!(condition))throw std::runtime_error(std::string("Native journal check failed at line ")+std::to_string(__LINE__)+": "+#condition+" (Windows error "+std::to_string(::GetLastError())+")");}while(false)
using wsprrypi::PiManagementJournal;
void test_native_windows_pair_catalog(const std::filesystem::path &);
void test_native_windows_logging_policy(const std::filesystem::path &);
namespace {
template<class F>void refuses(F action){bool held=false;try{action();}catch(const std::exception &){held=true;}assert(held);}
void target_journal(const std::filesystem::path &root){
    using namespace wsprrypi;using Json=nlohmann::json;namespace wf=windows_file;
    const std::string self(32,'1'),device(32,'2'),operation(32,'3'),next(32,'4');
    const auto text=[](const auto &path){return NativeWindowsFixture::utf8(path);};
    const auto directory=text(root/L"target-journal");
    const Json binding{{"device_id",device},{"boot_id",std::string(32,'5')},{"profile_generation","1"},{"connection_generation","2"}};
    const auto digest=PicoManagementJournal::digest("request"),route="/api/control/v1/targets/"+device+"/settings";
    PicoManagementJournal native(directory,self,device,PicoManagementHistoryAuthority::isolated());
    assert(!native.reserve(self,route,"PUT",binding,"1",operation,digest));
    Json body{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",binding},{"operation_id",operation},
        {"request_digest",digest},{"state","pending"},{"revision",nullptr},{"error",nullptr}};
    assert(!native.finish(operation,{202,body.dump(),{}})&&native.snapshot()["receipts"][0]["receipt"]["state"]=="pending");
    refuses([&]{native.reserve(self,route,"PUT",binding,"1",next,digest);});
    body["state"]="committed";body["revision"]=std::string(64,'f');
    assert(!native.finish(operation,{200,body.dump(),"\""+std::string(64,'f')+"\""})&&native.receipt(operation)==body);
    PicoManagementJournal cold(directory,self,device,PicoManagementHistoryAuthority::isolated());assert(cold.receipt(operation)==body);
    assert(!cold.reserve(self,route,"PUT",binding,"1",next,digest));
    cold.finish(next,{503,"{}",{}});assert(cold.receipt(next)["state"]=="unknown");
    const auto held=cold.receipt(next);refuses([&]{cold.reserve(self,route,"PUT",binding,"1",std::string(32,'6'),digest);});assert(cold.receipt(next)==held);
    // Build a valid large isolated history independently of the writer. The
    // production validator and subsequent native writes must retain >1 MiB.
    const auto large_root=root/L"large-target-journal";wf::PrivateSecurity security(wf::current_sid());
    assert(::CreateDirectoryW(large_root.c_str(),security.attributes()));
    auto data=native.snapshot();auto entry=data["receipts"][0];entry["route"]="/api/control/v1/targets/"+device+"/pico/station";
    entry["response"]["body"]={{"contract","Wsprry-Control/1"},{"saved",true},{"padding",std::string(32000,'x')}};
    data["receipts"]=Json::array();
    for(unsigned index=0;index<34;++index){auto copy=entry;copy["receipt"]["operation_id"]=PicoManagementJournal::digest("large-"+std::to_string(index)).substr(0,32);data["receipts"].push_back(copy);}
    data.erase("checksum");data["checksum"]=PicoManagementJournal::digest(data.dump());const auto bytes=data.dump();assert(bytes.size()>1048576);
    const auto file=large_root/(device+".json");
    {wf::Handle output(::CreateFileW(file.c_str(),GENERIC_WRITE|READ_CONTROL,0,security.attributes(),CREATE_NEW,FILE_FLAG_WRITE_THROUGH,nullptr));assert(output.get()!=INVALID_HANDLE_VALUE);
        DWORD count{};assert(::WriteFile(output.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&::FlushFileBuffers(output.get()));}
    PicoManagementJournal large(text(large_root),self,device,PicoManagementHistoryAuthority::isolated());assert(large.snapshot()["receipts"].size()==34);
    const auto large_route="/api/control/v1/targets/"+device+"/pico/station";
    assert(!large.reserve(self,large_route,"PUT",binding,"1",operation,digest));large.finish(operation,{200,R"({"contract":"Wsprry-Control/1","saved":true})",{}});
    PicoManagementJournal reopened(text(large_root),self,device,PicoManagementHistoryAuthority::isolated());assert(reopened.snapshot()["receipts"].size()==35);
    const auto alias=large_root/L"alias";assert(::CreateHardLinkW(alias.c_str(),file.c_str(),nullptr));refuses([&]{reopened.snapshot();});assert(::DeleteFileW(alias.c_str()));
    std::cout<<"PASS native target journal: durable pending/terminal decisions, cold replay, uncertainty fence, >1 MiB history, and hard-link refusal; isolated history authority only\n";
}

}
int main(){
    try{
        NativeWindowsFixture fixture;
        test_native_windows_pair_catalog(fixture.root);
        test_native_windows_logging_policy(fixture.root);
        target_journal(fixture.root);
        const auto root=fixture.root/L"journal-\u03a9";
        wsprrypi::windows_file::PrivateSecurity security(wsprrypi::windows_file::current_sid());
        assert(::CreateDirectoryW(root.c_str(),security.attributes()));
        const auto text=[](const auto &path){return NativeWindowsFixture::utf8(path);};
        const auto cache=text(root/L"listeners.json");
        const nlohmann::json bootstrap{{"schema","web-service-bootstrap/1"},{"device_id",std::string(32,'a')},{"role","controller"},
            {"boot",std::string(32,'b')},{"web_port",20001},{"socket_port",20002},{"http_enabled",true},{"websocket_enabled",true},
            {"http_loopback_only",true},{"socket_loopback_only",true},{"socket_loopback_family","auto"}};
        assert(wsprrypi::valid_web_bootstrap(bootstrap));
        refuses([&]{wsprrypi::read_web_bootstrap_cache(cache);});
        wsprrypi::write_web_bootstrap_cache(cache,bootstrap);
        assert(wsprrypi::read_web_bootstrap_cache(cache)==bootstrap);
        const auto stamp=PiManagementJournal::read_private_record(cache);
        wsprrypi::write_web_bootstrap_cache(cache,bootstrap);
        assert(PiManagementJournal::read_private_record(cache)->signature==stamp->signature);
        auto invalid=bootstrap;invalid["web_port"]=invalid["socket_port"];
        refuses([&]{wsprrypi::write_web_bootstrap_cache(cache,invalid);});
        assert(PiManagementJournal::read_private_record(cache)->signature==stamp->signature);
        (void)PiManagementJournal::publish_private_record(cache,stamp,"{\"schema\":1,\"schema\":2}");
        refuses([&]{wsprrypi::read_web_bootstrap_cache(cache);});
        wsprrypi::write_web_bootstrap_cache(cache,bootstrap);
        assert(wsprrypi::read_web_bootstrap_cache(cache)==bootstrap);
        const auto alias_cache=root/L"cache-alias";
        assert(::CreateHardLinkW(alias_cache.c_str(),wsprrypi::windows_file::path_from_utf8(cache).c_str(),nullptr));
        refuses([&]{wsprrypi::read_web_bootstrap_cache(cache);});
        refuses([&]{wsprrypi::write_web_bootstrap_cache(cache,bootstrap);});
        assert(::DeleteFileW(alias_cache.c_str()));
        const auto first=text(root/L"settings.json"),second=text(root/L"operations.json"),wal=text(root/L"transaction");
        const std::string self(32,'a');
        const std::map<std::string,std::string> paths{{"settings",first},{"operations",second}};
        PiManagementJournal journal(wal,self,paths);
        assert(!journal.decision()&&!std::filesystem::exists(root/L"transaction"));
        const auto initial=PiManagementJournal::publish_private_record(first,{},"old");
        refuses([&]{PiManagementJournal::publish_private_record(first,{},"forbidden");});
        auto upper=root/L"SETTINGS.JSON";
        refuses([&]{PiManagementJournal duplicate(wal,self,{{"settings",first},{"operations",text(upper)}});});
        refuses([&]{journal.prepare({{"settings",PiManagementJournal::digest("old"),std::string(1048577,'x')}});});
        journal.prepare({{"settings",PiManagementJournal::digest("old"),"new",initial.signature},{"operations",{},"history"}});
        assert(journal.inspect("settings")->bytes=="old"&&!journal.inspect("operations"));
        {PiManagementJournal cold(wal,self,paths);refuses([&]{cold.commit();});assert(cold.recover());}
        assert(journal.inspect("settings")->bytes=="old"&&!journal.inspect("operations")&&!journal.decision());
        const auto old=journal.inspect("settings");
        journal.prepare({{"settings",PiManagementJournal::digest("old"),"new",old->signature},{"operations",{},"history"}});
        journal.commit();
        {PiManagementJournal cold(wal,self,paths);assert(cold.recover());}
        assert(journal.inspect("settings")->bytes=="new"&&journal.inspect("operations")->bytes=="history"&&!journal.decision());
        const auto before=journal.inspect("settings");
        PiManagementJournal::publish_private_record(first,before,"new");
        assert(journal.inspect("settings")->signature!=before->signature);
        refuses([&]{journal.prepare({{"settings",PiManagementJournal::digest("new"),"forbidden",before->signature}});});
        journal.prepare({{"settings",PiManagementJournal::digest("new"),"final"}});
        const auto decision=*journal.decision();
        const auto image=root/L"transaction"/(decision.at("records")[0].at("staged_name").get<std::string>()+".new");
        const auto image_text=text(image);
        PiManagementJournal::publish_private_record(image_text,PiManagementJournal::read_private_record(image_text),"corrupt");
        refuses([&]{journal.commit();});refuses([&]{journal.recover();});
        assert(journal.inspect("settings")->bytes=="new"&&journal.decision()==std::optional(decision));
        std::cout<<"Native journal rollback, committed replay, live CAS, bounds, Unicode, alias, and retained-corruption checks passed\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
