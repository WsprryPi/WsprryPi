// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include "version.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_managed_store.hpp"
#include "wtp_integration/pi_managed_records.hpp"
#include "wtp_integration/controller_contract.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool held=false;try{action();}catch(...){held=true;}check(held,"Expected refusal");}
std::string bytes(const std::string &path){std::ifstream file(path,std::ios::binary);return {(std::istreambuf_iterator<char>(file)),{}};}
void put(const std::string &path,const std::string &value){std::ofstream file(path,std::ios::binary);file<<value;file.close();check(!chmod(path.c_str(),0600),"Private fixture file");}
}
int main(int argc,char **argv){
 try{
  if(argc==3&&std::string(argv[1])=="--recover-initial"){
      const std::string ini=argv[2],self(32,'a');WtpCatalog catalog(ini+".wtp-devices.json");
      PiManagedStore store(ini,self,catalog.pi_management_anchor(self,{}));store.recover();
      check(!store.initialized()&&!store.recovery_revision().empty(),"Cold initial rollback remains verifiable and uninitialized");return 0;
  }
  if(argc!=2)return 2;
  char pattern[]="/tmp/pi-managed-store-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Temporary fixture");const std::string root=temporary;
  struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
  const auto ini=root+"/settings.ini";const std::string self(32,'a');WtpCatalog catalog(ini+".wtp-devices.json");
  init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
  config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
  config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";resolve_backend_specific_config(config);config_to_json();
  put(ini,"; preserved layout\n[Security]\nPrivileged Network Safety = enforced\n");
  iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
  patch_all_from_web({{"WSPR",{{"Grid Square","EM18"},{"Call Sign","AA0NT"}}}});
  PiManagedStore store(ini,self,catalog.pi_management_anchor(self,{}));
  refuses([&]{store.initialize(jConfig);});refuses([&]{store.recovery_revision();});store.recover();
  check(!store.initialized()&&!std::filesystem::exists(ini+".pi-management"),"Recovery alone cannot initialize authority");
  const auto original_ini=bytes(ini);store.initialize(jConfig);const auto original=store.settings();
  check(original["configuration_generation"]=="1"&&original["scheduling"]["enabled"]==true&&original["scheduling"]["solar"]["enabled"]==false&&
      original["scheduling"]["solar"]["location"]["locator"]=="EM18"&&bytes(ini)==original_ini,"Explicit startup migrates settings without changing INI or output");
  set_config_persistence_owner([&](const auto &internal,const auto &persist){store.persist(internal,persist);},
      [&](const auto &internal){store.validate_snapshot(internal);},
      [&](const auto &internal,const auto &bytes){store.persist_stock(internal,bytes);});
  struct ClearOwner{~ClearOwner(){set_config_persistence_owner({},{},{});}} clear_owner;
  // A legacy writer joins the same record/revision owner with its real semantic
  // validator and INI writer. No candidate or receipt can get ahead of disk.
  patch_all_from_web_revision({{"CW",{{"Message","LEGACY"}}}},get_public_config_snapshot().second);
  const auto legacy=store.settings();check(legacy["configuration_generation"]=="2"&&legacy["revision"]!=original["revision"]&&
      legacy["configuration"]["CW"]["Message"]=="LEGACY","Legacy configuration advances complete managed revision");
  std::ifstream vectors_file(argv[1]);nlohmann::json vectors,request;vectors_file>>vectors;
  for(const auto &entry:vectors["cases"])if(entry["name"]=="Pi closed structural save")request=entry["value"];
  request["binding"]["device_id"]=self;request["expected_revision"]=legacy["revision"];
  request["candidate"]={{"configuration",legacy["configuration"]},{"scheduling",legacy["scheduling"]}};
  request["candidate"]["configuration"]["CW"]["Message"]="MANAGED";
  request["candidate"]["scheduling"]["solar"]["morning"]["lower_mdeg"]=-7101;
  const PiManagedStore::Operation operation{self,"1","/api/control/v1/targets/"+self+"/settings",request};
  check(!store.replay(operation),"New operation has no receipt");
  patch_config_with_persistence_coordinator({{"CW",{{"Message","MANAGED"}}}},get_public_config_snapshot().second,
      [&](const auto &internal,const auto &persist){store.persist(internal,persist,operation);});
  const auto managed=store.settings();const auto receipt=store.replay(operation);
  check(receipt&&(*receipt)["state"]=="committed"&&(*receipt)["revision"]==managed["revision"]&&managed["configuration_generation"]=="3",
      "INI, settings and exact committed receipt share one decision");
  auto changed=operation;changed.request["candidate"]["configuration"]["CW"]["Message"]="REUSED";
  refuses([&]{store.replay(changed);});
  changed=operation;changed.principal=std::string(32,'c');refuses([&]{store.replay(changed);});
  check(!store.receipt(self,"2",request["operation_id"]),"Changed authorization cannot retrieve old receipt");
  const auto unchanged=bytes(ini);unsigned attempts=0;
  auto stale=operation;stale.request["operation_id"]=std::string(32,'d');
  refuses([&]{store.persist(jConfig,[&]{++attempts;},stale);});
  check(attempts==0&&bytes(ini)==unchanged&&store.settings()==managed,"Stale request refuses before writer or journal mutation");
  // A complete-candidate mismatch cannot be silently normalized into another
  // save. The existing validator remains responsible for value semantics.
  auto mismatch=stale;mismatch.request["expected_revision"]=managed["revision"];
  mismatch.request["candidate"]["configuration"]["CW"]["Message"]="DIFFERENT";
  refuses([&]{store.persist(jConfig,[&]{++attempts;},mismatch);});
  check(attempts==0&&bytes(ini)==unchanged&&store.settings()==managed,"Complete candidate mismatch is a pre-write refusal");
  for(const auto &message:{std::string("LEGACY"),std::string("MANAGED")})
   patch_all_from_web_revision({{"CW",{{"Message",message}}}},get_public_config_snapshot().second);
  const auto aba=store.settings();
  check(aba["configuration"]==managed["configuration"]&&aba["candidate_digest"]==managed["candidate_digest"]&&
      aba["revision"]!=managed["revision"]&&aba["configuration_generation"]=="5","A-to-B-to-A cannot reuse a revision");
  std::string reload_error;
  for(unsigned i=0;i<3;++i)check(load_json(ini,&reload_error,nullptr),"Our own committed file remains reloadable");
  check(store.settings()==aba,"Unchanged file-monitor reloads cannot write, advance revisions, or create an autosave loop");
  auto stock_json=jConfig;stock_json["CW"]["Message"]="STOCK";
  const auto stock_bytes=serialize_config_ini_projection_for_management(stock_json)+"; version %SEMANTIC_VERSION%\n";
  put(ini+".stock",stock_bytes);
  const auto before_stock_runtime=get_public_config_snapshot();
  const auto repair_preview=iniFile.serialize_from_stock(get_raw_version_string(),true);
  persist_stock_configuration(true);
  check(bytes(ini)==repair_preview&&store.settings()["configuration_generation"]=="6"&&
      store.settings()["configuration"]["CW"]["Message"]=="MANAGED"&&
      store.settings()["scheduling"]==aba["scheduling"]&&get_public_config_snapshot()==before_stock_runtime,
      "Repair joins exact INI/settings transaction without publishing runtime or changing scheduling");
  const auto reset_preview=iniFile.serialize_from_stock(get_raw_version_string(),false);
  persist_stock_configuration(false);
  check(bytes(ini)==reset_preview&&store.settings()["configuration_generation"]=="7"&&
      store.settings()["configuration"]["CW"]["Message"]=="STOCK"&&get_public_config_snapshot()==before_stock_runtime,
      "Reset commits its exact stock after-image before runtime publication");
  check(load_json(ini,&reload_error,nullptr),"Existing reload applies the committed stock configuration");
  const auto post_stock=store.settings();
  const auto memory_before_rejection=iniFile.getData();
  auto invalid_stock=stock_json;invalid_stock["CW"]["Fade Shape"]="invalid";
  put(ini+".stock",serialize_config_ini_projection_for_management(invalid_stock));
  refuses([&]{persist_stock_configuration(false);});
  check(bytes(ini)==reset_preview&&store.settings()==post_stock&&iniFile.getData()==memory_before_rejection&&
      !iniFile.hasPendingChanges(),"Invalid stock refuses before final writes and restores the live parser");
  put(ini+".stock",std::string(1048577,'x'));
  refuses([&]{persist_stock_configuration(false);});
  check(bytes(ini)==reset_preview&&store.settings()==post_stock,"Oversized stock refuses before mutation");
  put(ini+".stock",stock_bytes);
  iniFile.set_string_value("CW","Message","UNSAVED COMPONENT EDIT");
  refuses([&]{persist_stock_configuration(false);});
  check(iniFile.hasPendingChanges()&&iniFile.get_value("CW","Message")=="UNSAVED COMPONENT EDIT"&&
      bytes(ini)==reset_preview&&store.settings()==post_stock,"Stock preparation cannot discard pending component work");
  iniFile.load();
  check(!chmod((ini+".stock").c_str(),0666),"Untrusted stock fixture");
  refuses([&]{persist_stock_configuration(false);});
  check(bytes(ini)==reset_preview&&store.settings()==post_stock,"Writable stock refuses before persistence");
  check(!chmod((ini+".stock").c_str(),0600),"Restore private stock fixture");
  for(const auto &entry:std::filesystem::directory_iterator(root))
      check(entry.path().filename().string().find(".preview.")==std::string::npos,"Private stock preparation must be removed");
  PiManagedStore restarted(ini,self,catalog.pi_management_anchor(self,{}));restarted.recover();restarted.initialize(jConfig);
  check(restarted.settings()==post_stock&&restarted.replay(operation)==receipt,"Restart retains stock candidate and exact historical replay");
  const auto saved_ini=bytes(ini),saved_settings=bytes(ini+".managed-settings.json"),saved_operations=bytes(ini+".management-operations.json");
  const auto public_before=get_public_config_snapshot();
  refuses([&]{patch_config_with_persistence_coordinator({{"CW",{{"Fade Shape","invalid"}}}},public_before.second,
      [&](const auto &internal,const auto &persist){store.persist(internal,persist);});});
  check(bytes(ini)==saved_ini&&store.settings()==post_stock,"Owning semantic refusal precedes store reservation");
  // A writer failure after the commit decision leaves admission held. A new
  // owner recovers all after-images, never treating the old runtime as saved.
  refuses([&]{patch_config_with_persistence_coordinator({{"CW",{{"Message","RECOVERED"}}}},public_before.second,
      [&](const auto &internal,const auto &persist){store.persist(internal,[&]{persist();throw std::runtime_error("lost reply");});});});
  refuses([&]{store.settings();});check(get_public_config_snapshot()==public_before,"Uncertain commit does not publish runtime state");
  PiManagedStore recovered(ini,self,catalog.pi_management_anchor(self,{}));recovered.recover();
  check(recovered.settings()["configuration"]["CW"]["Message"]=="RECOVERED"&&recovered.settings()["configuration_generation"]=="8",
      "Committed partial application recovers before configuration parsing");
  // Equal-byte replacement is still external activity in this process.
  put(ini+".replacement",bytes(ini));check(!rename((ini+".replacement").c_str(),ini.c_str()),"External replacement");
  refuses([&]{recovered.settings();});
  check(bytes(ini+".managed-settings.json")!=saved_settings&&bytes(ini+".management-operations.json")==saved_operations,
      "External activity causes a hold without synthesizing receipts");
  check(!unlink((ini+".managed-settings.json").c_str()),"Deletion fixture");
  PiManagedStore missing(ini,self,catalog.pi_management_anchor(self,{}));refuses([&]{missing.recover();});
  // Deleting all sidecars and the catalog cannot turn an initialized private
  // transaction directory into a new-install authority.
  check(!unlink((ini+".management-operations.json").c_str())&&!unlink((ini+".wtp-devices.json").c_str()),"History deletion fixture");
  PiManagedStore erased(ini,self,catalog.pi_management_anchor(self,{}));refuses([&]{erased.recover();});
  const auto first_ini=root+"/first.ini";WtpCatalog first_catalog(first_ini+".wtp-devices.json");
  PiManagedStore first_write(first_ini,self,first_catalog.pi_management_anchor(self,{}));first_write.recover();
  auto first_internal=jConfig;first_internal["Meta"]["INI Filename"]=first_ini;first_internal["CW"]["Message"]="FIRST RECOVERED";
  refuses([&]{first_write.initialize_persisted(first_internal,[]{throw std::runtime_error("initial commit interrupted before INI write");});});
  check(!std::filesystem::exists(first_ini),"Initial committed decision can precede creation of the first INI");
  PiManagedStore first_recovered(first_ini,self,first_catalog.pi_management_anchor(self,{}));first_recovered.recover();
  check(first_recovered.settings()["configuration_generation"]=="1"&&first_recovered.settings()["configuration"]["CW"]["Message"]=="FIRST RECOVERED"&&
      bytes(first_ini).find("FIRST RECOVERED")!=std::string::npos,"Initial recovery publishes one complete INI/settings/history decision from prior absence");
  for(const auto *kind:{"fifo","symlink"}){
      const auto unsafe=root+"/"+kind+".ini";
      if(std::string(kind)=="fifo")check(!mkfifo(unsafe.c_str(),0600),"Owned software FIFO fixture");
      else check(!symlink(first_ini.c_str(),unsafe.c_str()),"Owned symlink fixture");
      WtpCatalog unsafe_catalog(unsafe+".wtp-devices.json");PiManagedStore unsafe_store(unsafe,self,unsafe_catalog.pi_management_anchor(self,{}));
      refuses([&]{unsafe_store.recover();});
      check(!std::filesystem::exists(unsafe+".wtp-devices.json")&&!std::filesystem::exists(unsafe+".managed-settings.json"),
          "Unsafe existing INI refuses before parsing, catalog initialization, or writes");
  }
  // Recovery uses its own closed command and strong fixed-file precondition;
  // the first save must retain its receipt even if the initial reply is lost.
  const auto recovery_ini=root+"/recovery.ini";WtpCatalog recovery_catalog(recovery_ini+".wtp-devices.json");
  PiManagedStore recovery_store(recovery_ini,self,recovery_catalog.pi_management_anchor(self,{}));recovery_store.recover();
  nlohmann::json recovery_request;
  for(const auto &entry:vectors["cases"])if(entry["name"]=="P209 complete recovery save")recovery_request=entry["value"];
  check(recovery_request.is_object(),"Current authoritative recovery command required");
  auto recovery_internal=jConfig;recovery_internal["Meta"]["INI Filename"]=recovery_ini;
  recovery_internal["CW"]["Message"]="FIRST RECOVERY RECEIPT";
  recovery_request["binding"]["device_id"]=self;
  recovery_request["configuration"]=config_handler_serialization::saved_configuration_from_internal_json(recovery_internal);
  for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})recovery_request["configuration"]["WTP"][key]="";
  recovery_request["expected_revision"]=recovery_store.recovery_revision();
  PiManagedStore::Operation recovery_operation{self,"1","/api/control/v1/host/configuration-recovery",recovery_request};
  unsigned recovery_attempts=0;
  auto wrong_candidate=recovery_internal;wrong_candidate["CW"]["Message"]="DIFFERENT";
  refuses([&]{recovery_store.persist_recovery(wrong_candidate,[&]{++recovery_attempts;},recovery_operation);});
  check(recovery_attempts==0&&!recovery_store.initialized()&&!std::filesystem::exists(recovery_ini+".pi-management"),
      "Recovery candidate mismatch refuses before initial storage reservation");
  refuses([&]{recovery_store.persist_recovery(recovery_internal,[]{throw std::runtime_error("first recovery lost reply");},recovery_operation);});
  PiManagedStore recovery_restarted(recovery_ini,self,recovery_catalog.pi_management_anchor(self,{}));recovery_restarted.recover();
  const auto recovery_receipt=recovery_restarted.replay(recovery_operation);
  check(recovery_receipt&&(*recovery_receipt)["state"]=="committed"&&
      recovery_restarted.settings()["configuration_generation"]=="1"&&
      (*recovery_receipt)["revision"]==recovery_restarted.settings()["revision"]&&
      bytes(recovery_ini).find("FIRST RECOVERY RECEIPT")!=std::string::npos,
      "First recovery save, INI, managed generation and exact receipt recover as one decision");
  const auto recovery_before=bytes(recovery_ini);const auto recovery_settings=recovery_restarted.settings();
  auto reused_recovery=recovery_operation;reused_recovery.request["configuration"]["CW"]["Message"]="REUSED";
  refuses([&]{recovery_restarted.replay(reused_recovery);});
  auto stale_recovery=recovery_operation;stale_recovery.request["operation_id"]=std::string(32,'e');
  refuses([&]{recovery_restarted.persist_recovery(recovery_internal,[&]{++recovery_attempts;},stale_recovery);});
  check(recovery_attempts==0&&bytes(recovery_ini)==recovery_before&&recovery_restarted.settings()==recovery_settings,
      "Stale recovery source cannot invoke writer or advance managed history");
  nlohmann::json repair;
  for(const auto &entry:vectors["cases"])if(entry["name"]=="P209 explicit recovery repair")repair=entry["value"];
  repair["binding"]["device_id"]=self;repair["expected_revision"]=recovery_restarted.recovery_revision();
  repair["operation_id"]=std::string(32,'d');
  const PiManagedStore::Operation repair_operation{self,"1","/api/control/v1/host/configuration-recovery",repair};
  auto repaired_internal=recovery_internal;repaired_internal["CW"]["Message"]="REPAIRED";
  recovery_restarted.persist_recovery(repaired_internal,[]{},repair_operation,serialize_config_ini_projection_for_management(repaired_internal));
  check(recovery_restarted.settings()["configuration_generation"]=="2"&&recovery_restarted.replay(repair_operation)&&
      recovery_restarted.settings()["scheduling"]==recovery_settings["scheduling"],
      "Explicit stock recovery action retains scheduling and commits its receipt atomically");
  const auto replace_ini=root+"/replace.ini";put(replace_ini,recovery_before);
  WtpCatalog replace_catalog(replace_ini+".wtp-devices.json");PiManagedStore replace_store(replace_ini,self,replace_catalog.pi_management_anchor(self,{}));replace_store.recover();
  auto replace_operation=recovery_operation;replace_operation.request["expected_revision"]=replace_store.recovery_revision();
  put(replace_ini+".replacement",recovery_before);check(!rename((replace_ini+".replacement").c_str(),replace_ini.c_str()),"Initial equal-byte replacement");
  check(replace_store.recovery_revision()!=replace_operation.request["expected_revision"].get<std::string>(),"Recovery precondition detects equal-byte replacement before first history");
  refuses([&]{replace_store.persist_recovery(recovery_internal,[&]{++recovery_attempts;},replace_operation);});
  check(recovery_attempts==0&&!replace_store.initialized()&&!std::filesystem::exists(replace_ini+".pi-management"),
      "Initial replacement refuses before receipt, settings, catalog, or INI writes");
  for(const auto *fault:{"none","commit-interrupted","missing-proof","corrupt-proof","missing-image","replaced-input","replaced-catalog"}){
      const auto initial=root+"/initial-"+fault+".ini";put(initial,recovery_before);
      WtpCatalog initial_catalog(initial+".wtp-devices.json");(void)initial_catalog.snapshot({});
      const auto authority_before=bytes(initial+".wtp-devices.json");auto authority_after=nlohmann::json::parse(authority_before);
      authority_after["pi_management_history"]={{"device_id",self},{"history_id",std::string(32,'b')}};
      PiManagementJournal prepared(initial+".pi-management",self,{{"ini",initial},{"settings",initial+".managed-settings.json"},
          {"operations",initial+".management-operations.json"},{"authority",initial+".wtp-devices.json"}});
      prepared.prepare({{"ini",PiManagementJournal::digest(recovery_before),"uncommitted first INI"},{"settings",{},"uncommitted first settings"},
          {"operations",{},"uncommitted first operations"},{"authority",PiManagementJournal::digest(authority_before),authority_after.dump()}});
      const auto original_proof=*prepared.decision();
      for(unsigned restart=0;restart<3;++restart){
          const auto child=fork();check(child>=0,"Fork cold first-initialization recovery");
          if(!child){execl(argv[0],argv[0],"--recover-initial",initial.c_str(),nullptr);_exit(127);}
          int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Repeated cold recovery preserves initial proof");
          check(prepared.decision()==original_proof&&bytes(initial)==recovery_before&&bytes(initial+".wtp-devices.json")==authority_before,
              "Every uncommitted first rollback preserves original input, catalog, and durable proof");
      }
      PiManagedStore retry(initial,self,initial_catalog.pi_management_anchor(self,{}));retry.recover();
      auto retry_internal=recovery_internal;retry_internal["Meta"]["INI Filename"]=initial;retry_internal["CW"]["Message"]="RETRY FIRST COMMIT";
      const std::string mode=fault;
      if(mode=="missing-proof"||mode=="corrupt-proof"||mode=="missing-image"){
          const auto wal=initial+".pi-management/transaction.json";
          if(mode=="missing-proof")check(!unlink(wal.c_str()),"Remove retained proof fixture");
          else if(mode=="corrupt-proof")put(wal,"{corrupt proof");
          else check(!unlink((initial+".pi-management/"+original_proof["records"][1]["staged_name"].get<std::string>()+".new").c_str()),"Remove retained proof image fixture");
          refuses([&]{retry.recovery_revision();});
          PiManagedStore cold(initial,self,initial_catalog.pi_management_anchor(self,{}));refuses([&]{cold.recover();});
          check(bytes(initial)==recovery_before&&!std::filesystem::exists(initial+".managed-settings.json"),"Missing/corrupt proof cannot initialize or rewrite input");
      }else if(mode=="replaced-input"||mode=="replaced-catalog"){
          const auto file=mode=="replaced-input"?initial:initial+".wtp-devices.json";
          put(file+".replacement",bytes(file));check(!rename((file+".replacement").c_str(),file.c_str()),"Replace retained-proof source");
          refuses([&]{retry.recovery_revision();});refuses([&]{retry.initialize_stock(retry_internal,recovery_before);});
          check(prepared.decision()==original_proof&&!std::filesystem::exists(initial+".managed-settings.json"),"Source replacement refuses before proof replacement or writes");
      }else{
          if(mode=="commit-interrupted"){
              refuses([&]{retry.initialize_persisted(retry_internal,[]{throw std::runtime_error("lost first commit reply");});});
              PiManagedStore cold(initial,self,initial_catalog.pi_management_anchor(self,{}));cold.recover();
              check(cold.settings()["configuration_generation"]=="1"&&cold.settings()["configuration"]["CW"]["Message"]=="RETRY FIRST COMMIT",
                  "Interrupted first commit replaces retained proof with recoverable complete authority");
          }else retry.initialize_persisted(retry_internal,[]{});
          PiManagedStore cold(initial,self,initial_catalog.pi_management_anchor(self,{}));cold.recover();
          check(cold.initialized()&&!prepared.decision()&&cold.settings()["configuration_generation"]=="1",
              "Successful first initialization retires retained rollback proof exactly once");
      }
  }
  // Protected-file recovery also rejects a structurally current future policy.
  const auto future=root+"/future.ini";put(future,original_ini);
  WtpCatalog future_catalog(future+".wtp-devices.json");
  PiManagedStore future_store(future,self,future_catalog.pi_management_anchor(self,{}));future_store.recover();future_store.initialize(jConfig);
  auto future_candidate=future_store.settings();future_candidate["scheduling"]["solar"]["enabled"]=true;
  const auto future_record=pi_managed_settings_record(self,"2",{{"configuration",future_candidate["configuration"]},
      {"scheduling",future_candidate["scheduling"]}},PiManagementJournal::digest(bytes(future)));
  put(future+".managed-settings.json",canonical_pi_management_json(future_record));
  PiManagedStore cold_future(future,self,future_catalog.pi_management_anchor(self,{}));refuses([&]{cold_future.recover();});
  check(!cold_future.initialized()&&bytes(future)==original_ini,"File activation refuses before parser, repair or output admission");
  // A first installation records its boundary before a protected setup save.
  // Test both a missing INI and the untouched template installed by the normal
  // installer. Neither is a claim about the hardware's earlier lifetime.
  for(const auto *kind:{"absent","stock"}){
      const auto initial=root+"/installation-"+kind+".ini";
      auto internal=jConfig;internal["Meta"]["INI Filename"]=initial;
      internal["Operation"]["Transmit"]=false;internal["Operation"]["Enable on Boot"]="Never";
      if(std::string(kind)=="stock"){
          const auto stock=serialize_config_ini_projection_for_management(internal);
          put(initial,stock);put(initial+".stock",stock);
      }
      WtpCatalog catalog(initial+".wtp-devices.json");PiManagedStore first(initial,self,catalog.pi_management_anchor(self,{}));
      first.recover();first.prepare_installation();check(first.installation_pending(),"Explicit startup establishes pending installation before use");
      refuses([&]{first.settings();});refuses([&]{first.initialize(internal);});
      const auto record=bytes(initial+".recurrence-authority.json");
      PiManagedStore cold(initial,self,catalog.pi_management_anchor(self,{}));cold.recover();cold.prepare_installation();
      check(cold.installation_pending()&&bytes(initial+".recurrence-authority.json")==record,"Restart retains original installation identity without enabling output");
      auto setup=recovery_operation;setup.request["expected_revision"]=cold.recovery_revision();setup.request["operation_id"]=std::string(32,'7');
      setup.request["configuration"]=config_handler_serialization::saved_configuration_from_internal_json(internal);
      for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})setup.request["configuration"]["WTP"][key]="";
      auto enabled=internal;enabled["Operation"]["Transmit"]=true;
      refuses([&]{cold.persist_recovery(enabled,[]{},setup);});
      check(cold.installation_pending()&&bytes(initial+".recurrence-authority.json")==record,"An initial enable refuses before any final mutation");
      cold.persist_recovery(internal,[]{},setup);
      check(!cold.installation_pending()&&cold.settings()["scheduling"]["enabled"]==false&&
          cold.recurrence_authority()["state"]=="staged"&&cold.recurrence_authority()["staged_revision"]==cold.settings()["revision"]&&
          cold.replay(setup).has_value(),"Setup atomically stages disabled settings, authority and receipt");
      PiManagedStore restarted(initial,self,catalog.pi_management_anchor(self,{}));restarted.recover();
      check(restarted.replay(setup)==cold.replay(setup)&&restarted.recurrence_authority()==cold.recurrence_authority(),"Cold readback retains exact setup receipt and authority");
      check(!unlink((initial+".recurrence-authority.json").c_str()),"Delete authority fixture");
      PiManagedStore missing(initial,self,catalog.pi_management_anchor(self,{}));refuses([&]{missing.recover();});
  }
  check(!config.transmit,"No output admitted");
  std::cout<<"PASS: explicit Pi store initialization, legacy/native transaction parity, revision ABA, exact replay, authorization isolation, held uncertainty, restart recovery, and external replacement/deletion refusal without output\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
