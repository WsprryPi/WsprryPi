// SPDX-License-Identifier: MIT
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pico_management_journal.hpp"
#include "wtp_settings_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
#define CHECK(x) do{if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(false)
std::string bytes(const std::string &path){std::ifstream in(path);return {(std::istreambuf_iterator<char>(in)),{}};}
WtpSettings settings(const std::string &device){WtpSettings s;s.transport="usb";s.path="/dev/fixture-wtp";s.usb_serial=device;s.vendor_id=0xcafe;s.product_id=0x4012;s.device_id=device;return s;}
std::pair<std::string,std::string> add(WtpCatalog &catalog,const std::string &device){
 const auto [before,revision]=catalog.snapshot({});(void)before;
 const auto [after,next]=catalog.mutate({{"operation","add"},{"name","Test Pico"},{"method","manual"},{"discovery_id",""},{"settings",wtp_settings_json(settings(device))},{"consent_plain",false}},revision,{});
 CHECK(after["version"]==1&&after.size()==2&&!after["profiles"].back().contains("generation"));
 const auto id=after["profiles"].back()["id"].get<std::string>();return {id,catalog.profile(id,next,{}).generation};
}
Json binding(const std::string &device,const std::string &generation){return {{"device_id",device},{"boot_id",std::string(32,'b')},{"profile_generation",generation},{"connection_generation","1"}};}
int main(int argc,char **argv){
 if(argc==6&&std::string(argv[1])=="--missing-catalog"){
  try{WtpCatalog catalog(argv[2]);PicoManagementJournal journal(argv[3],argv[4],argv[5],catalog.management_history_authority({}));
   try{(void)journal.snapshot();return 3;}catch(...){}
   return !std::filesystem::exists(argv[2])&&std::filesystem::exists(std::string(argv[3])+"/"+argv[5]+".json")?0:4;
  }catch(...){return 5;}
 }
 if(argc==6&&std::string(argv[1])=="--retirement-recovery"){
  try{WtpCatalog catalog(argv[2]);auto authority=catalog.management_history_authority({});PicoManagementJournal journal(argv[3],argv[4],argv[5],authority);
   (void)journal.snapshot();journal.prune_retired();const auto view=authority.read();
   return !view.epochs.contains(argv[5])&&!view.retired_epochs.contains(argv[5])&&!std::filesystem::exists(std::string(argv[3])+"/"+argv[5]+".json")?0:3;
  }catch(...){return 5;}
 }
 if(argc==6&&std::string(argv[1])=="--missing-history"){
  try{WtpCatalog catalog(argv[2]);auto authority=catalog.management_history_authority({});if(!authority.read().epochs.contains(argv[5]))return 3;
   PicoManagementJournal journal(argv[3],argv[4],argv[5],authority);try{(void)journal.snapshot();return 4;}catch(...){return 0;}
  }catch(...){return 5;}
 }
 std::string temporary;
 try{
  char pattern[]="/tmp/pico-history-XXXXXX";const auto directory=mkdtemp(pattern);CHECK(directory);temporary=directory;
  const std::string self(32,'c'),device(32,'a'),op(32,'d');const auto digest=PicoManagementJournal::digest("request");
  const auto path=temporary+"/catalog.json",history=temporary+"/history";
  WtpCatalog catalog(path);const auto [id,generation]=add(catalog,device);
  CHECK(catalog.snapshot({}).first["version"]==1);
  PicoManagementJournal journal(history,self,device,catalog.management_history_authority({}));
  CHECK(journal.snapshot()["receipts"].empty()&&!std::filesystem::exists(history));
  CHECK(!journal.reserve(self,"/api/control/v1/targets/"+device+"/pico/station","PUT",binding(device,generation),"1",op,digest));
  journal.finish(op,{200,R"({"contract":"Wsprry-Control/1","saved":true})",{}});
  const auto record=journal.snapshot();CHECK(catalog.management_history_authority({}).read().epochs.at(device)==record["history_id"].get<std::string>());
  const auto original_catalog=bytes(path);
  const auto cold_refuses=[&]{
   const auto child=fork();CHECK(child>=0);if(!child){execl(argv[0],argv[0],"--missing-history",path.c_str(),history.c_str(),self.c_str(),device.c_str(),nullptr);_exit(127);}
   int status{};CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
   CHECK(bytes(path)==original_catalog);
  };
  std::filesystem::remove(history+"/"+device+".json");cold_refuses();CHECK(!std::filesystem::exists(history+"/"+device+".json"));
  std::filesystem::remove(history);cold_refuses();CHECK(!std::filesystem::exists(history));
  // Legacy catalog storage is read without a rewrite. Public control stays at
  // one catalog view; the next explicit mutation publishes current private data.
  const auto legacy_path=temporary+"/legacy.json";Json legacy{{"version",1},{"profiles",catalog.snapshot({}).first["profiles"]}};
  {std::ofstream out(legacy_path);out<<legacy.dump();}CHECK(!chmod(legacy_path.c_str(),0600));const auto old_bytes=bytes(legacy_path);
  WtpCatalog migrated(legacy_path);const auto [old,old_revision]=migrated.snapshot({});CHECK(bytes(legacy_path)==old_bytes&&old["profiles"]==legacy["profiles"]);
  const auto [renamed,renamed_revision]=migrated.mutate({{"operation","rename"},{"id",id},{"name","Temporary"}},old_revision,{});(void)renamed;
  const auto [restored,restored_revision]=migrated.mutate({{"operation","rename"},{"id",id},{"name","Test Pico"}},renamed_revision,{});
  CHECK(restored["profiles"]==legacy["profiles"]&&restored_revision!=old_revision&&migrated.profile(id,restored_revision,{}).generation!="1");
  CHECK(Json::parse(bytes(legacy_path))["version"]==2);
  // P62-13's private index never appears as a WTP profile. The previous
  // current seven-field catalog remains byte-for-byte unchanged on reads.
  const auto seal=[](Json value){value["checksum"]=wtp_catalog_revision(value).substr(1,64);return value;};
  const auto write_private=[&](const std::string &file,const Json &value){std::ofstream out(file);out<<seal(value).dump();out.close();CHECK(!chmod(file.c_str(),0600));};
  auto private_catalog=Json::parse(bytes(legacy_path));private_catalog["profiles"]=Json::array();
  private_catalog["remote_management"]=Json::object();
  const Json remote{{"parent_id",device},{"profile_id",std::string(32,'1')},{"profile_generation","2"},
      {"pair_id",std::string(32,'2')},{"authorization_generation","3"},{"product","WsprryPi"}};
  const auto previous_current=temporary+"/previous-current.json";auto previous=private_catalog;previous.erase("remote_management");write_private(previous_current,previous);
  WtpCatalog previous_owner(previous_current);const auto previous_bytes=bytes(previous_current);
  CHECK(previous_owner.snapshot({}).first["profiles"].empty()&&previous_owner.remote_management({}).empty()&&bytes(previous_current)==previous_bytes);
  for(unsigned scenario=0;scenario<8;++scenario){
   auto candidate=private_catalog;candidate["remote_management"][device]=remote;auto &row=candidate["remote_management"][device];
   if(scenario==0)row.erase("pair_id");
   if(scenario==1)row["private_key"]="forbidden";
   if(scenario==2)row["parent_id"]=self;
   if(scenario==3)row["profile_generation"]="0";
   if(scenario==4)row["authorization_generation"]=3;
   if(scenario==5)row["product"]="unknown";
   if(scenario==6)row["pair_id"]=std::string(32,'X');
   if(scenario==7)row["product"]="WsprryPico"; // A Pico cannot be its parent.
   const auto file=temporary+"/bad-remote-"+std::to_string(scenario)+".json";write_private(file,candidate);WtpCatalog bad(file);
   bool refused=false;try{(void)bad.snapshot({});}catch(...){refused=true;}CHECK(refused);
  }
  {
   const auto remote_ini=temporary+"/remote.ini",remote_path=remote_ini+".wtp-devices.json",remote_history=remote_ini+".management";
   auto candidate=private_catalog;candidate["remote_management"][device]=remote;write_private(remote_path,candidate);
   WtpCatalog owner(remote_path);const auto before=bytes(remote_path);
   CHECK(owner.snapshot({}).first["profiles"].empty()&&!owner.snapshot({}).first.contains("remote_management")&&bytes(remote_path)==before);
   CHECK(owner.management_history_authority({}).read().devices.contains(device));
   PicoManagementJournal entry(remote_history,self,device,owner.management_history_authority({}));
   CHECK(!entry.reserve(std::string(32,'2'),"/api/control/v1/targets/"+device+"/settings","PUT",binding(device,"2"),"3",op,digest));
   const auto record=entry.snapshot(),anchored=Json::parse(bytes(remote_path));
   auto missing_index=anchored;missing_index.erase("remote_management");write_private(remote_path,missing_index);
   CHECK(entry.snapshot()["receipts"]==record["receipts"]&&entry.receipt(op)["state"]=="unknown");
   entry.prune_retired();CHECK(std::filesystem::exists(remote_history+"/"+device+".json")&&owner.management_history_authority({}).read().epochs.contains(device));
   bool refused=false;try{(void)entry.reserve(std::string(32,'2'),"/api/control/v1/targets/"+device+"/settings","PUT",binding(device,"2"),"3",std::string(32,'e'),digest);}catch(...){refused=true;}CHECK(refused);
   write_private(remote_path,anchored);CHECK(entry.snapshot()["receipts"]==record["receipts"]);
   CHECK(std::filesystem::remove(remote_path));
   const auto child=fork();CHECK(child>=0);if(!child){execl(argv[0],argv[0],"--missing-catalog",remote_path.c_str(),remote_history.c_str(),self.c_str(),device.c_str(),nullptr);_exit(127);}
   int status{};CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  }
  // Explicit removal retains unresolved authority; reviewed/terminal orphan
  // history is retired and cannot permanently exhaust the directory bound.
  const auto gc_path=temporary+"/gc.json",gc_directory=temporary+"/gc";WtpCatalog gc(gc_path);
  const auto [pending_id,pending_generation]=add(gc,device);PicoManagementJournal pending(gc_directory,self,device,gc.management_history_authority({}));
  CHECK(!pending.reserve(self,"/api/control/v1/targets/"+device+"/pico/station","PUT",binding(device,pending_generation),"1",op,digest));
  gc.mutate({{"operation","remove"},{"id",pending_id}},gc.snapshot({}).second,{});pending.prune_retired();CHECK(std::filesystem::exists(gc_directory+"/"+device+".json"));
  pending.reconcile(op,binding(device,pending_generation),PicoManagementJournal::digest("explicit readback review"));pending.prune_retired();CHECK(!std::filesystem::exists(gc_directory+"/"+device+".json")&&gc.management_history_authority({}).read().epochs.empty());
  for(unsigned i=0;i<100;++i){const auto d=PicoManagementJournal::digest("target-"+std::to_string(i)).substr(0,32);const auto [profile,g]=add(gc,d);PicoManagementJournal entry(gc_directory,self,d,gc.management_history_authority({}));
   CHECK(!entry.reserve(self,"/api/control/v1/targets/"+d+"/pico/station","PUT",binding(d,g),"1",op,digest));entry.finish(op,{200,R"({"contract":"Wsprry-Control/1","saved":true})",{}});
   gc.mutate({{"operation","remove"},{"id",profile}},gc.snapshot({}).second,{});entry.prune_retired();const auto view=gc.management_history_authority({}).read();CHECK(view.epochs.empty()&&view.retired_epochs.empty());
  }
  CHECK(std::filesystem::is_empty(gc_directory));
  const auto cold=[&](const char *mode,const std::string &catalog_path,const std::string &journal_directory){
   const auto child=fork();CHECK(child>=0);if(!child){execl(argv[0],argv[0],mode,catalog_path.c_str(),journal_directory.c_str(),self.c_str(),device.c_str(),nullptr);_exit(127);}
   int status{};CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  };
  for(unsigned cut=0;cut<3;++cut){
   const auto ini=temporary+"/retirement-"+std::to_string(cut)+".ini",catalog_path=ini+".wtp-devices.json",journal_directory=ini+".management";
   WtpCatalog owner(catalog_path);const auto [profile,g]=add(owner,device);auto authority=owner.management_history_authority({});
   PicoManagementJournal entry(journal_directory,self,device,authority);
   CHECK(!entry.reserve(self,"/api/control/v1/targets/"+device+"/pico/station","PUT",binding(device,g),"1",op,digest));entry.finish(op,{200,R"({"contract":"Wsprry-Control/1","saved":true})",{}});
   const auto record_path=journal_directory+"/"+device+".json",record_bytes=bytes(record_path);
   if(cut==2){
    CHECK(std::filesystem::remove(catalog_path));cold("--missing-catalog",catalog_path,journal_directory);
    bool held=false;try{(void)owner.snapshot({});}catch(...){held=true;}
    CHECK(held&&!std::filesystem::exists(catalog_path)&&bytes(record_path)==record_bytes);
   }else{
    const auto epoch=authority.read().epochs.at(device);owner.mutate({{"operation","remove"},{"id",profile}},owner.snapshot({}).second,{});
    authority.retire(device,epoch);CHECK(authority.read().retired_epochs.at(device)==epoch);
    if(cut==1)CHECK(std::filesystem::remove(record_path));
    cold("--retirement-recovery",catalog_path,journal_directory);
    CHECK(!std::filesystem::exists(record_path)&&authority.read().retired_epochs.empty());
   }
  }
  std::filesystem::remove_all(temporary);
  std::cout<<"PASS: private catalog migration/public parity, durable generations, fresh-process missing-file/directory refusal, unresolved retention and 100 retired histories without capacity leakage\n";
 }catch(const std::exception &e){if(!temporary.empty())std::filesystem::remove_all(temporary);std::cerr<<e.what()<<'\n';return 1;}
}
