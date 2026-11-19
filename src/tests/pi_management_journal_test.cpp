// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/pi_managed_records.hpp"
#include "wtp_integration/catalog.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::string &path){std::ifstream input(path,std::ios::binary);return {(std::istreambuf_iterator<char>(input)),{}};}
void put(const std::string &path,const std::string &data){std::ofstream output(path,std::ios::binary);output<<data;output.close();check(!chmod(path.c_str(),0600),"Fixture permissions");}
template<class F>void refuses(F action){bool held=false;try{action();}catch(...){held=true;}check(held,"Expected storage hold");}
std::map<std::string,std::string> paths(const std::string &root){return {{"ini",root+"/settings.ini"},{"settings",root+"/saved.json"},{"operations",root+"/operations.json"},{"authority",root+"/catalog.json"}};}
}
int main(int argc,char **argv){
 try{
  const std::string self(32,'a');
  if(argc==3&&std::string(argv[1])=="--recover"){
   const auto destinations=paths(argv[2]);PiManagementJournal journal(std::string(argv[2])+"/transaction",self,destinations);check(journal.recover(),"Cold recovery decision");
   if(std::filesystem::exists(destinations.at("settings"))){
    (void)parse_pi_managed_settings_record(bytes(destinations.at("settings")),self,PiManagementJournal::digest(bytes(destinations.at("ini"))));
    (void)parse_pi_operations_record(bytes(destinations.at("operations")),self);
    WtpCatalog catalog(destinations.at("authority"));check(catalog.pi_management_anchor(self,{}).read()==std::optional<std::string>(std::string(32,'b')),"Cold settings and history marker share one commit decision");
   }
   return 0;
  }
  if(argc!=2)return 2;
  std::ifstream vectors_file(argv[1]);nlohmann::json vectors,request;vectors_file>>vectors;
  for(const auto &entry:vectors["cases"])if(entry["name"]=="Pi closed structural save")request=entry["value"];
  check(request.is_object(),"Authoritative complete-candidate fixture");
  char pattern[]="/tmp/pi-management-journal-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Temporary directory");const std::string root=temporary;
  struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
  const auto destinations=paths(root);const auto ini=destinations.at("ini"),settings=destinations.at("settings"),operations=destinations.at("operations");
  PiManagementJournal journal(root+"/transaction",self,destinations);
  auto registered=destinations;registered["progress"]=root+"/progress.json";registered["recurrence"]=root+"/recurrence.json";registered["legacy"]=root+"/legacy.json";
  PiManagementJournal bounded(root+"/bounded-transaction",self,registered);
  std::vector<PiManagementJournal::Image> too_many;for(const auto &[kind,path]:registered){(void)path;too_many.push_back({kind,std::nullopt,"unpublished"});}
  refuses([&]{bounded.prepare(too_many);});check(!bounded.decision(),"Seven registered destinations never permit seven transaction images");
  iniFile.set_file_writer([&](const std::string &path,const std::string &image){check(path==ini,"Fixed journal INI destination");journal.write_committed_ini(image);});
  struct ClearWriter{~ClearWriter(){iniFile.set_file_writer({});}} clear_writer;
  check(!journal.decision()&&!std::filesystem::exists(root+"/transaction"),"Read does not create authority");
  init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
  config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
  config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";resolve_backend_specific_config(config);config_to_json();
  put(ini,std::string("; retained ")+static_cast<char>(0xff)+"\n[Security]\nPrivileged Network Safety = enforced\n");
  iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});
  // Initial fixture setup precedes the managed publication policy.
  iniFile.set_file_writer({});json_to_ini();
  iniFile.set_file_writer([&](const std::string &path,const std::string &image){check(path==ini,"Fixed journal INI destination");journal.write_committed_ini(image);});
  for(const auto &value:{std::string("line\ninjected"),std::string("line;comment"),std::string("line#comment"),std::string(" leading"),std::string("trailing "),std::string("nul\0byte",8)}){
   auto invalid=jConfig;invalid["CW"]["Message"]=value;refuses([&]{(void)serialize_config_ini_projection_for_management(invalid);});
  }
  const auto before=bytes(ini);const auto before_hash=PiManagementJournal::digest(before);
  const auto observation=journal.inspect("ini");
  check(observation&&observation->bytes==before&&!journal.inspect("settings")&&
      !std::filesystem::exists(root+"/transaction"),"Inspection is bounded and does not initialize authority");
  const auto replacement=ini+".replacement";put(replacement,before);
  check(!rename(replacement.c_str(),ini.c_str()),"External replacement fixture");
  check(journal.inspect("ini")->bytes==before&&journal.inspect("ini")->signature!=observation->signature,
      "Equal-byte external replacement changes the owner observation");
  refuses([&]{journal.prepare({{"ini",before_hash,"forbidden stale observation",observation->signature}});});
  check(!journal.decision()&&bytes(ini)==before,"Caller observation is checked before staging, not just captured after an ABA replacement");
  refuses([&]{(void)journal.inspect("unapproved");});
  refuses([&]{journal.prepare({{"ini",before_hash,std::string(1048577,'x')}});});
  check(!std::filesystem::exists(root+"/transaction"),"Capacity refusal precedes staging");
  journal.prepare({{"ini",before_hash,"next"},{"settings",std::nullopt,"new settings"}});
  refuses([&]{journal.write_committed_ini("next");});
  check(bytes(ini)==before&&!std::filesystem::exists(settings)&&journal.decision()->at("state")=="prepared","Prepared changes no final path");
  const auto cold=[&]{const auto child=fork();check(child>=0,"fork");if(!child){execl(argv[0],argv[0],"--recover",root.c_str(),nullptr);_exit(127);}int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Cold process recovery");};
  put(ini,"next");put(settings,"new settings");
  cold();check(bytes(ini)==before&&!std::filesystem::exists(settings)&&!journal.decision(),"Prepared recovery restores authoritative absence");
  const auto snapshot=get_public_config_snapshot();
  std::string frozen_settings,frozen_operations;
  WtpCatalog catalog(destinations.at("authority"));auto anchor=catalog.pi_management_anchor(self,{});
  patch_config_with_persistence_coordinator(nlohmann::json{{"CW",{{"Message","SAVED"}}}},snapshot.second,[&](const auto &internal,const auto &persist){
   const auto after=serialize_config_ini_projection_for_management(internal);
   const nlohmann::json candidate{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(internal)},{"scheduling",request["candidate"]["scheduling"]}};
   request["candidate"]=candidate;
   const auto saved_record=pi_managed_settings_record(self,"1",candidate,PiManagementJournal::digest(after));
   const auto route="/api/control/v1/targets/"+self+"/settings";
   const auto digest=pi_managed_request_digest(self,self,"1",route,request);
   const nlohmann::json receipt{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",request["binding"]},{"operation_id",request["operation_id"]},{"request_digest",digest},{"state","committed"},{"revision",saved_record["revision"]},{"error",nullptr}};
   const nlohmann::json entry{{"principal_id",self},{"route",route},{"authorization_generation","1"},{"request_digest",digest},{"receipt",receipt}};
   frozen_settings=saved_record.dump();frozen_operations=pi_operations_record(self,nlohmann::json::array({entry})).document.dump();
   anchor.coordinate(std::string(32,'b'),[&](const auto &authority_before,const auto &authority_after){
    journal.prepare({{"ini",before_hash,after},{"settings",std::nullopt,frozen_settings},{"operations",std::nullopt,frozen_operations},
      {"authority",PiManagementJournal::digest(authority_before),authority_after}});
    journal.commit();
    refuses([&]{journal.write_committed_ini(after+"wrong");});
    {PiManagementJournal cold_writer(root+"/transaction",self,destinations);refuses([&]{cold_writer.write_committed_ini(after);});}
    const auto settings_image=root+"/transaction/"+journal.decision()->at("records")[1].at("staged_name").get<std::string>()+".new";
    put(settings_image,"corrupt later record");refuses([&]{journal.write_committed_ini(after);});
    check(bytes(ini)==before,"Corrupt companion staging refuses before the first final INI publication");
    put(settings_image,frozen_settings);
    check(bytes(ini)==before&&!std::filesystem::exists(settings)&&bytes(destinations.at("authority"))==authority_before,"Commit decision precedes all final paths including authority");
    // Model loss after partial application. Recovery must apply every image.
    persist();check(bytes(ini)==after&&journal.decision()->at("state")=="committed","Owned INI writer runs while the durable decision is retained");
    refuses([&]{journal.write_committed_ini(after);});
    put(settings,frozen_settings);cold();
    check(bytes(ini)==after&&bytes(operations)==frozen_operations&&bytes(destinations.at("authority"))==authority_after,"Committed recovery applies the complete frozen settings/receipt/authority projection");
   });
  });
  check(!journal.recover()&&!journal.decision()&&std::filesystem::is_empty(root+"/transaction")&&!config.transmit,"Retired transaction and no output");
  refuses([&]{journal.write_committed_ini(bytes(ini));});
  const auto saved=bytes(ini),saved_hash=PiManagementJournal::digest(saved);
  journal.prepare({{"ini",saved_hash,"later"},{"settings",PiManagementJournal::digest(bytes(settings)),"later settings"}});journal.commit();
  put(ini,"external edit");refuses([&]{(void)journal.recover();});check(bytes(settings)==frozen_settings&&bytes(ini)=="external edit","Conflict refuses before any record application");
  put(ini,saved);
  const auto decision=*journal.decision();const auto image=root+"/transaction/"+decision["records"][1]["staged_name"].get<std::string>()+".new";
  put(image,"corrupt");refuses([&]{(void)journal.recover();});check(bytes(ini)==saved,"Corrupt later image refuses before INI application");
  put(image,"later settings");check(journal.recover()&&bytes(ini)=="later","Coherent images recover after repair");
  journal.prepare({{"ini",PiManagementJournal::digest(bytes(ini)),"last"}});
  const auto staged=*journal.decision();const auto stage=root+"/transaction/"+staged["records"][0]["staged_name"].get<std::string>()+".new";
  const auto wal=root+"/transaction/transaction.json";
  for(const auto &field:{std::string("path"),std::string("schema"),std::string("device_id")}){
   auto malformed=staged;
   if(field=="path")malformed["records"][0]["path"]="/unapproved/destination";
   else malformed[field]=field=="schema"?"pi-management-transaction/0":std::string(32,'b');
   put(wal,malformed.dump());refuses([&]{(void)journal.recover();});check(bytes(ini)=="later","Closed WAL validation precedes mutation");
  }
  put(wal,"{\"schema\":\"duplicate\","+staged.dump().substr(1));refuses([&]{(void)journal.decision();});
  put(wal,staged.dump());check(!unlink(stage.c_str()),"Missing image fixture");
  refuses([&]{(void)journal.recover();});check(bytes(ini)=="later","Missing staging refuses before mutation");
  put(stage,"broken");refuses([&]{journal.commit();});check(journal.decision()->at("state")=="prepared"&&bytes(ini)=="later","Corrupt staging refuses before linearization");
  put(stage,"last");check(journal.recover()&&bytes(ini)=="later","Prepared decision restores old image");
  check(!unlink(ini.c_str())&&!symlink(settings.c_str(),ini.c_str()),"Symlink fixture");
  refuses([&]{journal.prepare({{"ini",PiManagementJournal::digest(bytes(settings)),"forbidden"}});});
  refuses([&]{(void)journal.inspect("ini");});
  check(!journal.decision()&&bytes(settings)=="later settings","Symlink target remains untouched");
  check(!unlink(ini.c_str()),"Remove fixture link");put(ini,"later");
  check(!chmod(ini.c_str(),0666),"Writable fixture");
  refuses([&]{journal.prepare({{"ini",PiManagementJournal::digest("later"),"forbidden"}});});
  refuses([&]{(void)journal.inspect("ini");});
  check(bytes(ini)=="later"&&!journal.decision(),"Untrusted write permissions refuse before reservation");
  check(!chmod(ini.c_str(),0600),"Restore fixture permissions");
  journal.prepare({{"ini",PiManagementJournal::digest("later"),"uncommitted first"}});
  const auto initial=*journal.decision();
  check(journal.recover(true)&&journal.decision()==initial&&bytes(ini)=="later","Retained rollback keeps its exact prepared proof");
  const auto orphan=root+"/transaction/"+std::string(32,'f')+".new";put(orphan,"interrupted replacement staging");
  check(journal.recover(true)&&!std::filesystem::exists(orphan)&&journal.decision()==initial,
      "Interrupted pre-replacement staging can retire without losing the retained proof");
  auto stale_proof=initial;stale_proof["transaction_id"]=std::string(32,'e');
  refuses([&]{journal.prepare({{"ini",PiManagementJournal::digest("later"),"new first"}},stale_proof);});
  check(journal.decision()==initial&&bytes(ini)=="later","Wrong rollback proof refuses without replacing authority");
  journal.prepare({{"ini",PiManagementJournal::digest("later"),"new first"}},initial);
  const auto newer=*journal.decision();check(newer["transaction_id"]!=initial["transaction_id"]&&bytes(ini)=="later",
      "A replacement prepared proof becomes durable before any final write");
  check(journal.recover(true)&&journal.decision()==newer&&bytes(ini)=="later","Interrupted replacement retains its new verifiable rollback proof");
  journal.prepare({{"ini",PiManagementJournal::digest("later"),"first committed"}},newer);journal.commit();journal.recover();
  check(bytes(ini)=="first committed"&&!journal.decision(),"First committed transaction safely retires all prior rollback staging");
  journal.prepare({{"ini",PiManagementJournal::digest("first committed"),"must refuse"}});
  put(ini+".replacement","first committed");check(!rename((ini+".replacement").c_str(),ini.c_str()),"Prepared equal-byte external replacement");
  refuses([&]{journal.commit();});check(bytes(ini)=="first committed"&&journal.decision()->at("state")=="prepared",
      "Equal-byte replacement after preparation refuses before the durable commit decision");
  journal.recover();
  journal.prepare({{"ini",PiManagementJournal::digest(bytes(ini)),"committed replacement"}});journal.commit();
  put(ini+".replacement",bytes(ini));check(!rename((ini+".replacement").c_str(),ini.c_str()),"Committed equal-byte external replacement");
  refuses([&]{journal.write_committed_ini("committed replacement");});
  check(bytes(ini)=="first committed","Live callback refuses an equal-byte replaced destination after commit");
  journal.recover();
  refuses([&]{PiManagementJournal unsafe(root+"/bad",self,{{"ini",root+"/bad/nested/settings"}});});
  std::cout<<"PASS: approved prepared/committed multi-record WAL, actual INI preview/writer parity, cold recovery, partial apply, prior absence, conflict/corruption refusal, and no output; service/admission integration remains unqualified\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
