// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "config_handler_deserialization.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "recurrence_admission.hpp"
#include "solar_configuration.hpp"
#include "version.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_management_service.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "WTP-Server/include/wtp_server/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <iomanip>
#include <sstream>
#include <thread>
#include <future>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
using namespace wsprrypico::wtp;
using Json=nlohmann::json;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::string &path){std::ifstream input(path,std::ios::binary);return {(std::istreambuf_iterator<char>(input)),{}};}
class TestClock final:public Clock {public:ClockSnapshot snapshot() const override{return {ClockState::Synchronized,1000000000000ULL,10000000000ULL,1000,0,LeapState::Normal,std::nullopt};}};
class TestIdentity final:public IdentitySource {public:std::string boot=std::string(32,'1');std::string new_boot_id() override{return boot;}};
Request request(const char *operation,RequestBody body,char id){
    const std::string text(1,id);const auto digest=sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(text.data()),text.size()));
    return {"WTP/1",std::string(32,'7'),std::string(32,id),"local-network",operation,digest,std::move(body)};
}

void check_drive_contract(const Json &settings, const std::string &ini)
{
    for (const auto &[model, expected] : {
             std::pair{"Raspberry Pi Model A Plus Rev 1.1", 1},
             {"Raspberry Pi Model B Rev 2.0", 1},
             {"Raspberry Pi Zero W Rev 1.1", 1},
             {"Raspberry Pi 2 Model B Rev 1.2", 2},
             {"Raspberry Pi 3 Model B Plus Rev 1.3", 3},
             {"Raspberry Pi Zero 2 W Rev 1.0", 3},
             {"Raspberry Pi 4 Model B Rev 1.5", 4},
             {"Raspberry Pi 400 Rev 1.0", 4},
             {"Raspberry Pi Compute Module 4 Rev 1.1", 4},
             {"Raspberry Pi 5 Model B Rev 1.0", 5},
             {"Raspberry Pi 500+ Rev 1.0", 5},
             {"Raspberry Pi Compute Module 5 Rev 1.0", 5},
             {"Other Raspberry Pi 4 Model B", -1},
             {"Raspberry Pi 4junk", -1},
             {"Raspberry Pi prototype Rev 5.0", -1},
             {"Raspberry Pi 999999999999999999999999", -1}}) {
        check(raspberry_pi_generation_from_identity(model, {}) == expected,
              "Model resolves an exact board family without reading revision digits");
    }
    for (const auto &[cpu, expected] : {
             std::pair{"BCM2835", 1}, {"BCM2836", 2}, {"BCM2837", 3},
             {"BCM2711", 4}, {"BCM2838", 4}, {"BCM2712", 5},
             {"prefix BCM2711", -1}, {"BCM2712x", -1}}) {
        check(raspberry_pi_generation_from_identity("", cpu) == expected,
              "Processor fallback requires a recognized whole identifier");
    }
    const auto roundtrip = [&](const char *section, const char *key,
                               std::initializer_list<int> legal,
                               int fallback, std::initializer_list<Json> illegal) {
        auto candidate = settings;
        for (int selected : legal) {
            candidate[section][key] = selected;
            ArgParserConfig parsed;
            config_handler_deserialization::deserialize_json_to_runtime_config(candidate, parsed);
            config_handler_serialization::serialize_runtime_config_to_json(parsed, candidate);
            check(candidate[section][key] == selected, "Drive value survives runtime round trip");
        }
        candidate[section].erase(key);
        ArgParserConfig parsed;
        config_handler_deserialization::deserialize_json_to_runtime_config(candidate, parsed);
        config_handler_serialization::serialize_runtime_config_to_json(parsed, candidate);
        check(candidate[section][key] == fallback, "Older saved settings retain their missing-field default");

        const auto before = bytes(ini);
        const auto state = jConfig;
        for (const auto &bad : illegal) {
            bool refused = false;
            try { patch_all_from_web({{section, {{key, bad}}}}); }
            catch (const std::exception &) { refused = true; }
            check(refused && bytes(ini) == before && jConfig == state,
                  "Malformed drive PATCH is refused before publication");
        }
    };
    roundtrip("GPIO", "Power Level", {0, 1, 2, 3, 4, 5, 6, 7}, 7,
              {-1, 8, 12, 4.5, "4", nullptr, 4294967300ULL});
    roundtrip("GPIO", "RP1 Drive mA", {2, 4, 8, 12}, 2,
              {0, 1, 3, 6, 10, 13, 4.5, "4", nullptr, 4294967300ULL});
    roundtrip("Si5351", "Power Level", {1, 2, 3, 4}, 1,
              {0, 5, 6, 8, 3.5, "3", nullptr, 4294967299ULL});
}
}
int main(){
 try{
  char pattern[]="/tmp/pi-management-service-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private fixture directory");const std::string root=temporary;
  struct Remove{std::string root;~Remove(){std::filesystem::remove_all(root);}} remove{root};
  const auto ini=root+"/settings.ini";const std::string self(32,'a');
  init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
  config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
  config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";resolve_backend_specific_config(config);config_to_json();
  {std::ofstream output(ini);output<<"; local Pi fixture\n[Security]\nPrivileged Network Safety = enforced\n";}
  check(!chmod(ini.c_str(),0600),"Private INI");iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
  patch_all_from_web({{"WSPR",{{"Grid Square","EM18"},{"Call Sign","AA0NT"}}}});
  check_drive_contract(jConfig, ini);
  WtpCatalog catalog(ini+".wtp-devices.json");PiManagedStore store(ini,self,catalog.pi_management_anchor(self,{}));store.recover();store.initialize(jConfig);
  set_config_persistence_owner([&](const auto &internal,const auto &persist){store.persist(internal,persist);},
      [&](const auto &internal){store.validate_snapshot(internal);},[&](const auto &internal,const auto &image){store.persist_stock(internal,image);});
  iniFile.set_file_writer([&](const std::string &path,const std::string &image){check(path==ini,"Fixed owning INI publication");store.write_committed_ini(image);});
  struct Clear{~Clear(){iniFile.set_file_writer({});set_config_persistence_owner({},{},{});}} clear;
  TestClock clock;TestIdentity identity;InhibitedRfEngine engine;JobService jobs(clock,engine,identity);WtpPiAuthority authority(jobs,false);
  bool available=true,race_claim=false,race_stop=false,fail_cancel=false;unsigned cancellations=0;std::string stopping_id,management_boot;
  bool wrong_identity=false;
  try{PiLocalManagementService wrong(store,std::string(32,'b'),"Other Pi",[]()->std::optional<PiManagementObservation>{return {};});}
  catch(const std::invalid_argument &){wrong_identity=true;}
  check(wrong_identity,"A service cannot advertise an identity different from its durable store");
  const auto claim=[&]{check(authority.handle(request("HELLO",HelloBody{{"WTP/1"}},'a')).ok,"Fixture inspection");
      check(authority.handle(request("CLAIM",ClaimBody{std::string(32,'2'),10000},'b')).ok,"Fixture remote claim");};
  PiLocalManagementService service(store,self,"This Pi",[&]()->std::optional<PiManagementObservation>{
      if(!available)return {};
      auto observed=authority.management_snapshot(self);
      if(!management_boot.empty())observed.first.remote.boot_id=management_boot;
      return PiManagementObservation{self,std::move(observed.first),std::move(observed.second),[&]{if(race_claim){race_claim=false;claim();}return authority.begin_configuration();},[&]{authority.end_configuration();},
          [&](const WtpPiAuthority::ObservedOutput &expected){
              if(race_stop){race_stop=false;check(authority.begin_test_tone(),"Racing output admission");}
              return authority.reserve_observed_stop(self,identity.boot,expected);
          },[&]{authority.release_observed_stop();},[&]{
              ++cancellations;
              check(store.receipt(self,"1",stopping_id).has_value(),"Cancellation dispatch follows durable acceptance");
              if(fail_cancel)throw std::runtime_error("Fixture cancellation dispatch failed");
              check(authority.local_stop(),"Existing Stop cancels fixture authority");authority.end_local_work(true);
              config.transmit=false;config_to_json();
          }};
  });
  const auto path="/api/control/v1/targets/"+self+"/";
  const auto call=[&](const std::string &resource,const std::string &method="GET",Json body=Json(),const std::string &revision=std::string{},bool authenticated=true,bool origin=true){
      return service.handle({method,path+resource,body.is_null()?std::string{}:body.dump(),revision,method!="GET"&&origin},authenticated);
  };
  const auto original=bytes(ini);const auto original_saved=store.settings();const auto output_before=authority.snapshot();
  const auto busy=[&](auto &mutex){
      std::promise<void> acquired;auto ready=acquired.get_future();
      std::thread holder([&]{std::lock_guard lock(mutex);acquired.set_value();std::this_thread::sleep_for(std::chrono::milliseconds(200));});
      ready.wait();const auto result=call("settings");holder.join();
      check(result.status==409&&Json::parse(result.body)["error"]["code"]=="capacity"&&
          bytes(ini)==original&&store.settings()==original_saved,
          "Busy lifecycle/configuration owner refuses without waiting to negotiate or mutating state");
  };
  busy(test_tone_lifecycle_mutex());busy(config_update_mutex());
  check(call("context","GET",nullptr,{},false).status==403,"Unauthenticated local gateway refusal");
  const auto context_reply=call("context");check(context_reply.status==200,"Current context");const auto context=Json::parse(context_reply.body);
  check(valid_controller_document(context,"target-context")&&context["target"]["device_id"]==self,"Closed self context");
  for(const auto &cap:context["capabilities"])check(valid_pi_resource_capability(cap),"Recognized policy-bound descriptor");
  const auto snapshot_reply=call("settings");check(snapshot_reply.status==200,"Current settings");const auto snapshot=Json::parse(snapshot_reply.body);
  check(valid_controller_document(snapshot,"target-settings")&&snapshot_reply.etag=="\""+snapshot["revision"].get<std::string>()+"\"","Exact settings ETag");
  const auto status=call("status");
  check(status.status==200&&Json::parse(status.body)["authority_state"]=="legacy"&&
      bytes(ini)==original&&store.settings()==original_saved&&authority.snapshot().remote==output_before.remote,
      "Reads preserve settings/output ownership and do not invent completed legacy-authority inventory");
  Json save{{"contract","Wsprry-Control/1"},{"schema","target-save"},{"binding",context["binding"]},{"capability_revision",context["capability_revision"]},
      {"operation_id",std::string(32,'c')},{"expected_revision",snapshot["revision"]},{"candidate",snapshot["saved"]}};
  save["candidate"]["configuration"]["CW"]["Message"]="SERVICE SAVE";
  const auto revision=snapshot_reply.etag;
  check(call("settings","PUT",save,revision,true,false).status==403,"Origin/intent refusal");
  check(call("settings","PUT",save).status==428,"Strong revision required");
  const auto negative=[&](Json value){check(call("settings","PUT",value,revision).status!=200,"Malformed/stale request refused");check(bytes(ini)==original&&store.settings()==original_saved,"Refusal before persistence");};
  auto invalid=save;invalid["extra"]=true;negative(invalid);
  invalid=save;invalid["binding"]["device_id"]=std::string(32,'b');negative(invalid);
  invalid=save;invalid["binding"]["boot_id"]=std::string(32,'b');negative(invalid);
  invalid=save;invalid["binding"]["connection_generation"]="0";negative(invalid);
  invalid=save;invalid["capability_revision"]=std::string(64,'b');negative(invalid);
  invalid=save;invalid["candidate"]["configuration"]["Operation"]["Web Port"]=31419;negative(invalid);
  invalid=save;invalid["candidate"]["configuration"]["WTP"]["TLS Client Key"]="/never-open-this-key";negative(invalid);
  invalid=save;invalid["candidate"]["configuration"]["CW"]["Fade Shape"]="invalid";negative(invalid);
  invalid=save;invalid["candidate"]["scheduling"]["enabled"]=false;negative(invalid);
  invalid=save;invalid["candidate"]["configuration"]["CW"]["Message"]="";
  invalid["candidate"]["configuration"]["CW"]["Message"]=std::string(32768-invalid.dump().size(),'A');
  check(invalid.dump().size()==32768,"Maximum-sized complete request fixture");
  negative(invalid);
  check(call("settings","PUT",invalid,revision).status==400,"An unreadable complete candidate is rejected before persistence");
  check(call("enable","POST",save,revision).status==400&&call("stop","POST",save,revision).status==400,"Malformed activation and Stop cannot gain authority");
  const auto committed=call("settings","PUT",save,revision);check(committed.status==200,"Validated service save");
  const auto receipt=Json::parse(committed.body);check(receipt["state"]=="committed"&&receipt["binding"]==context["binding"]&&store.settings()["configuration"]["CW"]["Message"]=="SERVICE SAVE","Durable receipt and saved configuration");
  const auto after=bytes(ini);check(call("settings","PUT",save,revision).body==committed.body&&bytes(ini)==after,"Exact replay does not repeat a write");
  invalid=save;invalid["candidate"]["configuration"]["CW"]["Message"]="REUSED";
  check(call("settings","PUT",invalid,revision).status==409&&bytes(ini)==after,"Request-ID reuse refusal");
  check(call("operations/"+save["operation_id"].get<std::string>()).body==committed.body,"Lost-reply lookup returns exact receipt");
  check(call("operations/"+std::string(32,'f')).status==404,"Absent receipt remains explicitly unknown");
  const auto fresh_context=Json::parse(call("context").body),fresh_settings=Json::parse(call("settings").body);
  save["operation_id"]=std::string(32,'d');save["expected_revision"]=fresh_settings["revision"];save["binding"]=fresh_context["binding"];
  save["capability_revision"]=fresh_context["capability_revision"];save["candidate"]=fresh_settings["saved"];
  save["candidate"]["configuration"]["CW"]["Message"]="SHOULD NOT SAVE";
  race_claim=true;
  check(call("settings","PUT",save,"\""+save["expected_revision"].get<std::string>()+"\"").status==409&&bytes(ini)==after&&authority.snapshot().remote.owner_id==std::string(32,'2'),"A racing claim wins without a hidden takeover or persistence");
  check(call("settings").status==200&&authority.snapshot().remote.owner_id==std::string(32,'2'),"Management read preserves another owner's claim");
  check(authority.local_stop(),"Retire fixture remote owner");identity.boot=std::string(32,'3');check(authority.recover_remote_output(),"New native boot fixture");
  check(call("settings","PUT",save,"\""+save["expected_revision"].get<std::string>()+"\"").status==409&&bytes(ini)==after,"New native boot retires earlier edit binding");
  auto stop_context=Json::parse(call("context").body),stop_settings=Json::parse(call("settings").body);
  const auto stop_request=[&](char id){return Json{{"contract","Wsprry-Control/1"},{"schema","target-stop"},
      {"binding",stop_context["binding"]},{"capability_revision",stop_context["capability_revision"]},
      {"operation_id",std::string(32,id)},{"expected_revision",stop_settings["revision"]},{"observed_output",stop_settings["observed_output"]}};};
  auto stop=stop_request('8');const auto stop_before=bytes(ini);const auto stop_record=store.settings();
  check(call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"",false).status==403&&
      call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"",true,false).status==403,
      "Bound Stop requires gateway authentication and CSRF before mutation");
  race_stop=true;
  check(call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"").status==409&&
      bytes(ini)==stop_before&&store.settings()==stop_record&&cancellations==0,
      "A racing replacement output refuses before persistence and cancellation");
  authority.end_local_work(true);
  check(call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"").status==409&&cancellations==0,
      "Output ABA requires a new observation even when idle again");
  check(authority.begin_test_tone(),"Fixture current output for bound cancellation");
  stop_settings=Json::parse(call("settings").body);stop=stop_request('9');stopping_id=stop["operation_id"];
  const auto stopped=call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"");
  check(stopped.status==200&&Json::parse(stopped.body)["state"]=="committed"&&cancellations==1&&
      store.settings()["scheduling"]==stop_record["scheduling"]&&store.settings()["configuration"]["Operation"]["Transmit"]==false,
      "Bound cancellation acceptance is durable and preserves scheduler enablement");
  check(authority.begin_test_tone(),"A later explicit output fixture");
  const auto later=authority.observed_output(self);const auto stopped_bytes=bytes(ini);
  check(call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"").body==stopped.body&&
      cancellations==1&&authority.observed_output(self)==later&&bytes(ini)==stopped_bytes,
      "Lost-reply replay cannot cancel a later unobserved output or save again");
  auto reused=stop;reused["observed_output"]=Json::parse(call("settings").body)["observed_output"];
  check(call("stop","POST",reused,"\""+stop["expected_revision"].get<std::string>()+"\"").status==409&&cancellations==1,
      "Stop operation IDs cannot be rebound to a later output");
  stop_settings=Json::parse(call("settings").body);stop=stop_request('0');stopping_id=stop["operation_id"];fail_cancel=true;
  check(call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"").status==200&&cancellations==2&&
      !authority.begin_scheduled_work()&&!authority.begin_test_tone()&&!authority.reserve_reconfiguration(),
      "Cancellation dispatch failure preserves its durable acceptance and native admission fence");
  const auto held_status=Json::parse(call("status").body),held_context=Json::parse(call("context").body);
  check(held_status["transmit_effective"]==false&&Json::parse(call("settings").body)["pending_application"]=="held",
      "A retained cancellation fence reports held application and no effective transmit permission");
  for(const auto &cap:held_context["capabilities"])check(cap["write"]==(cap["resource"]=="pi-scheduling"),"A retained cancellation fence permits only safe scheduling disable");
  authority.release_observed_stop();authority.end_local_work(true);
  fail_cancel=false;claim();
  stop_context=Json::parse(call("context").body);stop_settings=Json::parse(call("settings").body);
  stop=stop_request('6');stopping_id=stop["operation_id"];
  const auto remote_stop=call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"");
  check(remote_stop.status==200,"Remote-owner Stop acceptance");
  check(cancellations==3&&!authority.snapshot().remote.owner_id,"Remote-owner cancellation dispatch");
  check(Json::parse(call("context").body)["capability_revision"]!=stop_context["capability_revision"],"Remote-owner Stop changes writable capabilities");
  check(call("stop","POST",stop,"\""+stop["expected_revision"].get<std::string>()+"\"").body==remote_stop.body&&cancellations==3,
      "Stop's own remote-owner transition can change capabilities without losing its exact accepted replay");
  PiManagedStore cold(ini,self,catalog.pi_management_anchor(self,{}));cold.recover();
  check(cold.receipt(self,"1",stopping_id).has_value()&&cold.settings()==store.settings(),"Accepted Stop survives cold storage recovery");
  const auto final_bytes=bytes(ini);available=false;check(call("context").status==503&&bytes(ini)==final_bytes,"Unavailable output authority is never replaced with a fabricated idle target");
  check(call("operations/"+std::string(32,'c')).body==committed.body,"Authoritative receipt lookup survives an unavailable/replaced output");
  available=true;
  // Exercise actual bounded receipt publication, rather than only the pure
  // retention helper. An evicted ID must never become a fresh write under an
  // earlier browser context, even when its candidate happens to be unchanged.
  Json before_eviction,evicted_request;
  for(unsigned index=1;index<=257;++index){
      const auto current_context=Json::parse(call("context").body);
      const auto current_settings=Json::parse(call("settings").body);
      std::ostringstream id;id<<std::setfill('0')<<std::setw(32)<<std::hex<<index;
      Json retained{{"contract","Wsprry-Control/1"},{"schema","target-save"},
          {"binding",current_context["binding"]},{"capability_revision",current_context["capability_revision"]},
          {"operation_id",id.str()},{"expected_revision",current_settings["revision"]},{"candidate",current_settings["saved"]}};
      const auto result=call("settings","PUT",retained,"\""+current_settings["revision"].get<std::string>()+"\"");
      check(result.status==200,"Bounded history publication through the production transaction owner");
      if(index==1)evicted_request=retained;
      if(index==257)before_eviction=current_context;
  }
  const auto retired_context=Json::parse(call("context").body);
  check(store.context_epoch()==6&&retired_context["binding"]["connection_generation"]!=before_eviction["binding"]["connection_generation"],
      "Actual terminal eviction retires the previous edit context");
  const auto retained_ini=bytes(ini);
  const auto retained_saved=store.settings();
  check(call("operations/"+std::string(32,'c')).status==404,"Evicted receipt lookup remains unknown");
  check(call("operations/"+evicted_request["operation_id"].get<std::string>()).status==404,"Second terminal eviction removes its receipt");
  const auto retired_reply=call("settings","PUT",evicted_request,"\""+evicted_request["expected_revision"].get<std::string>()+"\"");
  check(retired_reply.status==409&&Json::parse(retired_reply.body)["error"]["code"]=="connection_changed"&&
      bytes(ini)==retained_ini&&store.settings()==retained_saved,"An earlier edit context cannot revive an evicted operation");
  check(call("operations/00000000000000000000000000000100").status==200,"Latest retained receipt remains recoverable after context retirement");
  // Stored-only drafts use the same actual authority and atomic transaction.
  const auto solar_context=Json::parse(call("context").body);
  bool stored_only=false;
  for(const auto &cap:solar_context["capabilities"])if(cap["resource"]=="pi-scheduling")
      stored_only=cap["write"]==true&&cap["activation"]=="next_occurrence";
  check(stored_only,"Solar capability advertises target-owned next-occurrence activation");
  auto draft=store.settings()["scheduling"];
  draft["solar"]["location"]["locator"]="EM18AA12";
  draft["solar"]["morning"]["enabled"]=true;
  draft["solar"]["morning"]["lower_mdeg"]=-7001;
  draft["solar"]["day"]["allow"]=false;
  draft["solar"]["night"]["cw_base_hz"]="14097000.000000001";
  draft["solar"]["evening"]["wspr_override"]={{"Frequency","20m 0 30m"},{"Frequency Profile","existing_common"},{"Band Preferences",Json::object()}};
  unsigned draft_id=4096;
  const auto save_draft=[&](const Json &candidate){
      const auto ctx=Json::parse(call("context").body),saved=Json::parse(call("settings").body);
      std::ostringstream id;id<<std::setfill('0')<<std::setw(32)<<std::hex<<++draft_id;
      Json command{{"contract","Wsprry-Control/1"},{"schema","target-save"},{"binding",ctx["binding"]},
          {"capability_revision",ctx["capability_revision"]},{"operation_id",id.str()},
          {"expected_revision",saved["revision"]},{"candidate",saved["saved"]}};
      command["candidate"]["scheduling"]=candidate;
      return call("settings","PUT",command,"\""+saved["revision"].get<std::string>()+"\"");
  };
  const auto draft_saved=save_draft(draft);
  if(draft_saved.status!=200)std::cerr<<draft_saved.status<<" "<<draft_saved.body<<"\n";
  check(draft_saved.status==200,"Valid disabled solar draft saved through production service");
  check(store.settings()["scheduling"]==draft&&!config.transmit,"Exact stored draft cannot activate output");
  auto retained_metadata=draft;retained_metadata["solar"]["evening"]["wspr_override"]["Frequency"]="20m@5H 0 30m";
  const auto typed=parse_solar_configuration(retained_metadata,config);
  check(typed.locator=="EM18AA12"&&!typed.solar_enabled&&!typed.day.allow&&
      typed.night.cw_base_nhz==14097000000000001ULL&&typed.evening.wspr_override->entries.size()==3&&
      typed.evening.wspr_override->entries.front().selector_gpio==5,
      "Typed copy preserves precision, skips, metadata and full period policy");
  const auto refuse_draft=[&](Json candidate){
      const auto disk=bytes(ini);const auto settings=store.settings();
      check(save_draft(candidate).status!=200&&bytes(ini)==disk&&store.settings()==settings,
          "Invalid disabled draft refused before any transaction or output");
  };
  auto bad_draft=draft;bad_draft["solar"]["enabled"]=true;refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["enabled"]=!draft["enabled"].get<bool>();refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["location"]["locator"]="EM18aa";refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["morning"]["upper_mdeg"]=-7001;refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["morning"]["enabled"]=1;refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["day"]["extra"]=false;refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["night"]["cw_base_hz"]="18446744074";refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["night"]["cw_base_hz"]="14097000.0";refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["evening"]["wspr_override"]["Frequency"]="20m INVALID 30m";refuse_draft(bad_draft);
  bad_draft=draft;bad_draft["solar"]["evening"]["wspr_override"]["Frequency"]="200MHz";refuse_draft(bad_draft);
  if(transmit_backend_is_compiled(TransmitBackendKind::GPIO)){
      auto gpio=config;gpio.transmit_backend=TransmitBackendKind::GPIO;gpio.ppm=0;
      gpio.allow_unqualified_frequency=gpio.allow_non_amateur_frequency=true;
      set_raspberry_pi_generation_override_for_test(1);
      check(get_raspberry_pi_generation()==1,"Typed processor fixture reaches the legacy planner without hardware");
      const std::array<std::uint64_t,1> boundary{99999995605468750ULL};
      const bool accepts=solar_backend_frequencies(gpio,ModeType::QRSS,boundary);
      clear_raspberry_pi_generation_override_for_test();
      check(!accepts,"BCM2835 intrinsic correction is composed with additional PPM exactly as production before divider validation");
  }
  if(transmit_backend_is_compiled(TransmitBackendKind::RP1_GPCLK)){
      auto rp1=config;rp1.transmit_backend=TransmitBackendKind::RP1_GPCLK;rp1.ppm=0;
      const std::array<std::uint64_t,2> cw_tones{14097000000000000ULL,14097002000000000ULL};
      check(solar_backend_frequencies(rp1,ModeType::FSKCW,cw_tones),"RP1 pure production planner validates the complete CW frequency table");
      SolarModeIntent wspr;wspr.wspr.entries={{"20m",14095600,-1,false,true}};
      check(evaluate_solar_policy({},wspr,{}, {},[&](auto mode,auto tones){return solar_backend_frequencies(rp1,mode,tones);}).outcome==SolarOutcome::Allowed,
          "RP1 WSPR frequencies use the production numerical planner and dither count");
      rp1.allow_unqualified_frequency=rp1.allow_non_amateur_frequency=true;
      const std::array<std::uint64_t,1> edge{100000000000000000ULL};
      check(!solar_backend_frequencies(rp1,ModeType::QRSS,edge),"RP1 refuses a base whose compatibility table exceeds the direct-output bound");
  }
  if(transmit_backend_is_compiled(TransmitBackendKind::SI5351)){
      auto si=config;si.transmit_backend=TransmitBackendKind::SI5351;
      const std::array<std::uint64_t,2> cw_tones{14097000000000000ULL,14097002000000000ULL};
      check(solar_backend_frequencies(si,ModeType::FSKCW,cw_tones),"Compiled Si5351 validates both tones with its real pure planner");
      si.allow_unqualified_frequency=si.allow_non_amateur_frequency=true;
      const std::array<std::uint64_t,1> impossible{201000000000000000ULL};
      check(!solar_backend_frequencies(si,ModeType::QRSS,impossible),"Si5351 envelope refuses an impossible new override");
  }
  auto different_peer=config;different_peer.transmit_backend=TransmitBackendKind::WTP;
  different_peer.wtp.hostname="unobserved.invalid";
  const std::array<std::uint64_t,1> proposed_tone{14097000000000000ULL};
  check(!solar_backend_frequencies(different_peer,ModeType::QRSS,proposed_tone),
      "Changed WTP endpoint cannot borrow a previous peer's capabilities or negotiate during validation");
  // Retention is lexical, not a new grant to use an unavailable frequency.
  auto retained_unsupported=draft;retained_unsupported["solar"]["night"]["cw_base_hz"]="200000000";
  check(parse_solar_configuration(retained_unsupported,config,&retained_unsupported,true).night.cw_base_nhz==200000000000000000ULL,
      "Unchanged well-formed unsupported override can be retained for repair");
  bool unsupported_refused=false;
  try{(void)parse_solar_configuration(retained_unsupported,config,&draft,true);}catch(...){unsupported_refused=true;}
  check(unsupported_refused,"The same unsupported value cannot be newly saved");
  // An ordinary INI save joins the same owner and must retain all draft fields.
  patch_all_from_web({{"CW",{{"Message","SOLAR RETAINED"}}}});
  check(store.settings()["scheduling"]==draft,"Legacy INI writer retains the complete solar document");
  PiManagedStore reloaded_solar(ini,self,catalog.pi_management_anchor(self,{}));reloaded_solar.recover();
  check(reloaded_solar.settings()["scheduling"]==draft,"Cold recovery retains exact disabled draft");
  // Retire the existing self-owned recurrence through the production API.
  // A manual output must finish before handover; retirement never stops it.
  auto migration_context=Json::parse(call("context").body),migration_settings=Json::parse(call("settings").body);
  const auto migration_state=Json::parse(call("migration").body);
  check(valid_controller_document(migration_state,"target-migration-read")&&migration_state["controllers"].size()==1&&
      migration_state["controllers"][0]["controller_id"]==self,"Target-owned local inventory is returned without browser-supplied proof");
  auto retired_readback=migration_state;retired_readback["wtp_binding"]=nullptr;
  check(!migration_state.contains("wtp_binding")&&!valid_controller_document(retired_readback,"target-migration-read"),
      "Migration readback omits and rejects a retired WTP binding");
  Json migrate{{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",migration_context["binding"]},
      {"capability_revision",migration_context["capability_revision"]},{"operation_id",std::string(32,'4')},
      {"expected_revision",migration_settings["revision"]},{"expected_authority_generation",migration_state["authority_generation"]},
      {"controllers",migration_state["controllers"]}};
  const auto migration_etag="\""+migrate["expected_revision"].get<std::string>()+"\"";
  const auto pre_migration=bytes(ini);
  const auto native_backend=config.transmit_backend;
  const auto prior_authority=store.recurrence_authority();
  config.transmit_backend=TransmitBackendKind::WTP;
  const auto remote_replacement=call("migration","POST",migrate,migration_etag);
  check(remote_replacement.status==409&&Json::parse(remote_replacement.body)["error"]["code"]=="unsupported_resource"&&
      bytes(ini)==pre_migration&&store.recurrence_authority()==prior_authority,
      "A remote WTP output cannot become device-owned recurrence or mutate migration state");
  config.transmit_backend=native_backend;
  auto omitted=migrate;omitted["controllers"]=Json::array();
  check(call("migration","POST",omitted,migration_etag).status==409&&bytes(ini)==pre_migration,"Caller omission cannot replace authoritative inventory");
  check(authority.begin_test_tone(),"Independent manual output fixture");
  check(call("migration","POST",migrate,migration_etag).status==409&&authority.snapshot().local_work_active&&bytes(ini)==pre_migration,
      "Active manual output blocks handover without cancellation or writes");
  authority.end_local_work(true);
  migration_context=Json::parse(call("context").body);migrate["capability_revision"]=migration_context["capability_revision"];
  const auto migrated=call("migration","POST",migrate,migration_etag);
  check(migrated.status==200&&store.settings()["scheduling"]["enabled"]==false&&store.recurrence_authority()["state"]=="staged"&&
      store.recurrence_authority()["retirements"].size()==1,"Local retirement, disabled settings, frozen rollback and receipt commit together");
  check(call("migration","POST",migrate,migration_etag).body==migrated.body,"Lost migration reply replays without another retirement");
  PiManagedStore migrated_cold(ini,self,catalog.pi_management_anchor(self,{}));migrated_cold.recover();
  check(migrated_cold.recurrence_authority()==store.recurrence_authority(),"Target reboot retains the local tombstone");
  const auto rollback_context=Json::parse(call("context").body),rollback_settings=Json::parse(call("settings").body),rollback_state=Json::parse(call("migration").body);
  Json rollback{{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",rollback_context["binding"]},
      {"capability_revision",rollback_context["capability_revision"]},{"operation_id",std::string(32,'5')},
      {"expected_revision",rollback_settings["revision"]},{"expected_authority_generation",rollback_state["authority_generation"]},
      {"resume_controller_id",self},{"resume_assignment_id",rollback_state["controllers"][0]["assignment_id"]}};
  const auto rolled=call("migration/rollback","POST",rollback,"\""+rollback["expected_revision"].get<std::string>()+"\"");
  check(rolled.status==200&&store.settings()["scheduling"]==draft&&store.recurrence_authority()["state"]=="rollback"&&
      store.recurrence_authority()["retirements"].size()==1,"Explicit rollback restores the selected prior assignment without erasing retirement evidence");
  // The lifecycle marker represents an already accepted WSPR plan, including
  // its inter-frame interval. No transmitter is constructed in this fixture.
  active_wspr_plan_in_progress=true;
  migration_context=Json::parse(call("context").body);migration_settings=Json::parse(call("settings").body);
  auto draining=migrate;draining["binding"]=migration_context["binding"];draining["capability_revision"]=migration_context["capability_revision"];
  draining["expected_revision"]=migration_settings["revision"];draining["expected_authority_generation"]=store.recurrence_authority()["generation"];
  draining["controllers"]=store.recurrence_authority()["controllers"];draining["operation_id"]="123456789abcdef0123456789abcdef0";
  const auto drain_ini=bytes(ini);const auto accepted=call("migration","POST",draining,"\""+draining["expected_revision"].get<std::string>()+"\"");
  check(accepted.status==202&&Json::parse(accepted.body)["state"]=="pending"&&store.recurrence_authority()["state"]=="retiring"&&
      store.recurrence_authority()["jobs"].size()==1&&bytes(ini)==drain_ini,"Accepted pair is durably recorded and fenced without rewriting INI or cancelling work");
  service.advance_migration();
  check(store.migration_pending()&&bytes(ini)==drain_ini,"Inter-frame idle cannot falsely complete the accepted pair");
  reset_active_wspr_plan_state();management_boot=std::string(32,'f');
  service.advance_migration();
  check(store.migration_pending(),"A different boot cannot automatically claim the earlier work completed");
  auto reconcile=draining;const auto reconciliation_context=Json::parse(call("context").body);
  reconcile["binding"]=reconciliation_context["binding"];reconcile["capability_revision"]=reconciliation_context["capability_revision"];
  reconcile["expected_revision"]=store.settings()["revision"];reconcile["expected_authority_generation"]=store.recurrence_authority()["generation"];
  reconcile["operation_id"]="123456789abcdef0123456789abcdef1";
  const auto reconciled=call("migration","POST",reconcile,"\""+reconcile["expected_revision"].get<std::string>()+"\"");
  check(reconciled.status==200&&!store.migration_pending()&&store.settings()["scheduling"]["enabled"]==false&&bytes(ini)==drain_ini&&
      store.receipt(self,"1",draining["operation_id"])->at("state")=="committed","Explicit current-boot reconciliation completes retained intent and its earlier receipt atomically");
  const auto native_context=Json::parse(call("context").body),native_saved=store.settings();
  Json enable{{"contract","Wsprry-Control/1"},{"schema","target-enable"},{"binding",native_context["binding"]},
      {"capability_revision",native_context["capability_revision"]},{"operation_id","123456789abcdef0123456789abcdef2"},
      {"expected_revision",native_saved["revision"]},{"candidate_digest",native_saved["candidate_digest"]}};
  const auto enable_etag="\""+enable["expected_revision"].get<std::string>()+"\"";
  auto wrong_enable=enable;wrong_enable["candidate_digest"]=std::string(64,'f');
  check(call("enable","POST",wrong_enable,enable_etag).status==412&&store.settings()==native_saved,
      "Enable binds the complete saved candidate before storage or activation");
  const auto enabled=call("enable","POST",enable,enable_etag);
  check(enabled.status==200&&store.recurrence_authority()["state"]=="native"&&store.settings()["scheduling"]["enabled"]==true&&
      store.settings()["configuration"]==native_saved["configuration"]&&bytes(ini)==drain_ini,
      "Explicit native enable preserves Transmit, boot, INI, and completed retirement evidence");
  check(store.recurrence_progress()->snapshot()["consumed_through_utc_ns"]=="0","First native enable creates bounded authoritative progress before admission");
  const auto progress_owner=store.recurrence_progress();
  RecurrenceListDigests digests;digests.fill(PiManagementJournal::digest(""));
  const std::array<std::uint32_t,5> sizes{2,0,0,0,0};
  progress_owner->reserve(std::string(32,'a'),100,220,std::string(64,'b'),1,1,0,digests,sizes,0);
  const auto disable_context=Json::parse(call("context").body),disable_saved=store.settings();
  Json disable{{"contract","Wsprry-Control/1"},{"schema","target-action"},{"binding",disable_context["binding"]},
      {"capability_revision",disable_context["capability_revision"]},{"operation_id","123456789abcdef0123456789abcdef3"},{"expected_revision",disable_saved["revision"]}};
  check(authority.begin_test_tone(),"Independent accepted-work fixture");
  check(call("disable","POST",disable,"\""+disable["expected_revision"].get<std::string>()+"\"").status==200&&
      !store.settings()["scheduling"]["enabled"].get<bool>()&&authority.snapshot().local_work_active&&bytes(ini)==drain_ini,
      "Disable preserves accepted work and output ownership without cancellation");
  check(call("enable","POST",enable,enable_etag).body==enabled.body&&!store.settings()["scheduling"]["enabled"].get<bool>(),
      "Historical enable replay cannot undo a later durable disable");
  check(store.recurrence_progress()==progress_owner,"Settings publication retains the one live progress coordinator");
  progress_owner->resolve(true,digests);
  check(store.recurrence_progress()->snapshot()["cursors"]["ordinary"]["index"]==1,"Terminal progress after a disable is visible to the same owner");
  authority.end_local_work(true);
  PiManagedStore native_cold(ini,self,catalog.pi_management_anchor(self,{}));native_cold.recover();
  check(native_cold.recurrence_authority()["state"]=="native"&&!native_cold.settings()["scheduling"]["enabled"].get<bool>(),"Restart retains independent native disable and progress");
  auto native_rollback=rollback;const auto native_rollback_context=Json::parse(call("context").body);
  native_rollback["binding"]=native_rollback_context["binding"];native_rollback["capability_revision"]=native_rollback_context["capability_revision"];
  native_rollback["expected_revision"]=store.settings()["revision"];native_rollback["expected_authority_generation"]=store.recurrence_authority()["generation"];
  native_rollback["operation_id"]="123456789abcdef0123456789abcdef4";
  const auto resumed=call("migration/rollback","POST",native_rollback,"\""+native_rollback["expected_revision"].get<std::string>()+"\"");
  check(resumed.status==200&&store.recurrence_authority()["state"]=="rollback","Disabled reconciled native recurrence can explicitly resume its selected old assignment");
  auto retire_again=migrate;const auto retire_context=Json::parse(call("context").body);
  retire_again["binding"]=retire_context["binding"];retire_again["capability_revision"]=retire_context["capability_revision"];
  retire_again["expected_revision"]=store.settings()["revision"];retire_again["expected_authority_generation"]=store.recurrence_authority()["generation"];
  retire_again["controllers"]=store.recurrence_authority()["controllers"];retire_again["operation_id"]="123456789abcdef0123456789abcdef5";
  check(call("migration","POST",retire_again,"\""+retire_again["expected_revision"].get<std::string>()+"\"").status==200&&
      store.recurrence_authority()["state"]=="staged","A subsequent handover stages disabled without erasing prior native progress");
  // A trusted legacy writer can already own a candidate near the storage
  // limit. The adapter must bound the complete readback envelope as well.
  auto large_internal=jConfig;large_internal["CW"]["Message"]="";
  const Json large_candidate{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(large_internal)},
      {"scheduling",store.settings()["scheduling"]}};
  large_internal["CW"]["Message"]=std::string(32768-canonical_pi_management_json(large_candidate).size(),'A');
  const auto large_ini=serialize_config_ini_projection_for_management(large_internal);
  store.persist(large_internal,[&]{std::ofstream output(ini,std::ios::binary);output<<large_ini;output.close();check(bool(output),"Legacy after-image write");});
  const auto bounded=call("settings");
  check(bounded.status==503&&bounded.body.size()<32768&&Json::parse(bounded.body)["error"]["code"]=="payload_too_large",
      "An oversized legacy readback becomes a bounded typed refusal, never an oversized success");
  check(!config.transmit,"No transmission requested");
  for(const auto *suffix:{".recurrence-progress.json",".recurrence-progress-initialization.json"}){
      const auto path=ini+suffix,backup=path+".held";check(!rename(path.c_str(),backup.c_str()),"Missing established progress fixture");
      bool refused=false;try{PiManagedStore missing(ini,self,catalog.pi_management_anchor(self,{}));missing.recover();}catch(...){refused=true;}
      check(refused,"Established progress cannot be reinitialized even after rollback and staging");
      check(!rename(backup.c_str(),path.c_str()),"Restore isolated progress evidence");
  }
  std::cout<<"PASS: Pi local context/settings/status, policy-bound capabilities, semantic/read-only/binding refusals, actual configuration/WAL writes, exact replay/lookup, and racing-claim preservation without output\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
