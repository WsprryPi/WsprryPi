// SPDX-License-Identifier: MIT
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/consumer_radio.hpp"
#include "wtp_integration/control_contract.hpp"
#include <iostream>
#include <openssl/sha.h>
using namespace wsprrypi;
using Json = nlohmann::json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
namespace {
const std::string device(32, 'a'), boot(32, '1');
const Json target{{"scope", "member"}, {"device_id", device}, {"boot_id", boot}};
Json canonical_caps() {
  Json result{{"wtp",{{"profiles",{"rf-events/1"}},{"modes",{"wspr","tone"}},{"engine","test"},
    {"frequency_ranges",Json::array({{{"minimum_nhz","100000000000000"},{"maximum_nhz","30000000000000000"}}})},
    {"max_payload_bytes",65536},{"max_events",512},{"max_job_duration_ns","3600000000000"},
    {"minimum_arm_lead_ns","1000000"},{"maximum_arm_ahead_ns","300000000000"},{"maximum_arm_uncertainty_ns","500000000"},
    {"maximum_holdover_age_ns","3600000000000"},{"output_disable_timeout_ns","50000000"},{"minimum_lease_ms",5000},{"maximum_lease_ms",60000},
    {"response_cache_entries",8},{"response_cache_ttl_seconds",300},{"terminal_record_entries",8},{"terminal_record_ttl_seconds",3600}}},
    {"features",Json::object()},{"active_job_connections",false},{"max_network_connections",2},{"max_wtp_connections",1},
    {"max_pending_connections",1},{"max_handshakes",1},{"max_body_bytes",32768}};
  for(const auto name:{"config","schedules","jobs","network","application","station","hardware","consumer_setup_identity","softap","ble","restart"})result["features"][name]=std::string(name)!="consumer_setup_identity";
  for(const auto name:{"application_resources","configuration_updates","management_carriers","saved_progress","gpio_drive","amplifier_timing"})result[name]=Json::parse(control_descriptor(name));
  return result;
}
Json management() {
  Json carriers=Json::array();for(const auto name:{"engineering_mtls","engineering_field","consumer_station","consumer_ap","usb_physical"}) carriers.push_back({{"carrier",name},{"built_in",true},{"available",true}});
  return {{"contract","Wsprry-Control/1"},{"schema","management"},{"target",target},{"profile","engineering"},{"preclock",true},{"profile_generation",nullptr},{"selected","engineering_mtls"},{"carriers",carriers},{"unsupported",{"plain_wtp_json","tls_wtp_json","ble_json"}},
    {"operations",{{"GET",{"capabilities","status","config","schedules","network","application","management"}},{"PUT",{"schedules","network"}},{"POST",{"config/update"}}}},
    {"limits",{{"http_body",32768},{"consumer_plaintext",32724},{"config",1800},{"update",2048},{"login_ms",30000},{"idle_ms",1800000},{"absolute_ms",28800000},{"consumer_sessions",3},{"consumer_pending_logins",1}}}};
}
std::string document(Json value) { if(!value.contains("contract"))value["contract"]="Wsprry-Control/1";return value.dump(); }
struct Member {
  Json config = Json::parse(R"({"version":1,"enabled":false,"station":{"callsign":"AA0NT","locator":"EM18","power_dbm":20},"wifi":{"ssid":"test-network","password":null,"ntp_ipv4":"192.0.2.1"},"schedules":[{"period_s":120,"phase_s":0}],"allow_unqualified_frequency":true,"allow_non_amateur_frequency":true,"expires_utc_s":1900000000})");
  Json caps=canonical_caps();
  Json job{{"boot_id", boot}, {"output_active", false}, {"owner_id", nullptr}, {"state", "empty"}};
  std::string revision = "\"r1\"", malformed;
  unsigned mutations = 0, reads = 0;
  bool race = false, drop = false, bad_reply = false;
  std::function<void(Json &)> corrupt_application;
  Json emitted;
  std::string resource, method;
  Member() {
    config["pins"] = {{"engine","direct"},{"rf_gp",28},{"i2c_pair",nullptr},{"button_gp",14},
        {"amplifier_gp",nullptr},{"lpf_gps",Json::array()},{"indicator","onboard_led"},
        {"indicator_gp",nullptr},{"indicator_active_high",true},{"amplifier_active_high",true}};
    config.erase("version");config["gpio_drive_ma"]=4;modern();
    caps["management_carriers"]={{"schema","management"},{"path","/api/control/v1/management"}};
  }
  void modern() {
    caps["configuration_updates"] = {{"schema", "config-update"}, {"path", "/api/control/v1/config/update"},
        {"method", "POST"}, {"modes", {"merge"}}, {"max_update_bytes", 2048}, {"max_config_bytes", 1800}};
    caps["application_resources"] = {{"schema","application"},{"read","/api/control/v1/application"},
        {"station","/api/control/v1/station"},{"hardware","/api/control/v1/hardware"},
        {"config","/api/control/v1/config"},{"update","/api/control/v1/config/update"}};
  }
  void type23() {
    modern();config["station"]={{"callsign","<AA0NT>"},{"locator","EM18IG"},{"power_dbm",20}};
  }
  PicoHttpResponse exchange(const std::string &path, const std::string &verb, const std::string &body, const std::string &etag) {
    if (verb != "GET") {
      ++mutations; resource = path; method = verb; emitted = strict_browser_json(body);
      CHECK(etag == revision);
      if(config.is_null()) return {400,R"({"error":{"code":"invalid_configuration"}})",{}};
      if (drop) return {503, R"({"error":{"code":"management_outcome_unknown"}})", {}};
      return {200, bad_reply ? "{}" : document(Json{{"config", config}, {"reboot_required", false}}), "\"r2\""};
    }
    ++reads;
    if (path == "management") return {200,management().dump(),{}};
    if (path == "capabilities") return {200, document(caps), {}};
    if (path == "status") return {200, document(Json{{"job", job}}), {}};
    if (path == "application") {
      const auto station=config.is_null()?Json(nullptr):config["station"];
      const auto pins=config.is_null()?Json(nullptr):config["pins"];
      const auto schedules=config.is_null()?Json::array():config["schedules"];
      Json application{{"schema", "application"}, {"target", target}, {"reboot_required", false},
          {"station", {{"schema", "station"}, {"target", target}, {"station", station},
              {"saved_generation", "1"}, {"storage_healthy", true}}},
          {"hardware", {{"schema", "hardware"}, {"target", target}, {"saved", pins},
              {"saved_generation", "1"}, {"active", pins}, {"active_revision", std::string(64, 'a')},
              {"pending_restart", false}, {"execution_engine", "test"}, {"storage_healthy", true}, {"application_error", nullptr},{"gpio_drive",{{"schema","gpio-drive"},{"target",target},{"saved",4},{"boot_active",4},{"pad_readback",nullptr},{"pending_restart",false},{"application_error",nullptr},{"rf_gp",28},{"execution_engine","test"},{"storage_healthy",true}}}}},
          {"recurrence", {{"authority", "member"}, {"enabled", false}, {"suspended", false}, {"expires_utc_s", "0"},
              {"schedules", schedules}, {"schedule_dial_frequency_nhz", "3568600000000000"}}},
          {"capabilities", {{"application_read", true},
              {"station_write", true}, {"hardware_write", true}, {"max_update_bytes", 1024}, {"max_config_bytes", 1800},
              {"hardware_activation", "restart"}, {"station_encoding", "wspr_type1"}, {"station_power_meaning", "reported_wspr_dbm"},
              {"recurrence_authorities", {"member"}}, {"autonomous_modes", {"wspr"}}, {"unavailable", Json::array()}}}};
      if (path == "application") {
        application["schema"]="application";
        application["station"]["schema"]="station";
        application["capabilities"]["station_encoding"]="wspr_type123_auto";
        application["wspr_plan"]={{"schema","wspr-plan"},{"supported",true},{"frames",1},
            {"frame_types",{"type3"}},{"type2_ambiguous",false},{"alternate_callsign",nullptr},{"slot_separation_s",120},{"rationale","Hash context is needed."}};
      }
      if(config.is_null()){application["station"]["saved_generation"]=nullptr;application["hardware"]["saved_generation"]=nullptr;application["wspr_plan"]=nullptr;}
      if (corrupt_application) corrupt_application(application);
      return {200, document(application), revision};
    }
    CHECK(path == "config");
    if (race && reads > 3) revision = "\"r2\"";
    return {200, malformed.empty() ? document(Json{{"config", config}}) : malformed, revision};
  }
  PicoHttpResponse call(Json patch = Json{{"station", {{"callsign", "K1ABC"}}}}, std::string etag = "\"r1\"") {
    return raw(Json{{"contract","Wsprry-Control/1"},{"schema", "pico-settings"}, {"target", target}, {"operation", "save"}, {"config", patch}}.dump(), etag);
  }
  PicoHttpResponse raw(const std::string &body, const std::string &etag = "\"r1\"") {
    return pico_configuration_request([&](auto p, auto m, auto b, auto e) { return exchange(p,m,b,e); }, device, boot, "POST", body, etag);
  }
};
void checks() {
  Member old;old.caps.erase("configuration_updates");
  CHECK(old.call().status!=200 && old.mutations==0);
  Member modern; modern.modern();
  const auto modern_result=modern.call();if(modern_result.status!=200)std::cerr<<modern_result.status<<" "<<modern_result.body<<" reads "<<modern.reads<<" mutations "<<modern.mutations<<"\n";
  CHECK(modern_result.status == 200 && modern.resource == "config/update" && modern.method == "POST");
  CHECK(modern.emitted["target"] == target && modern.emitted["config"].size() == 1 && modern.emitted["mode"] == "merge");
  for (const auto flags : {false, true}) {
    Member m; m.modern();
    CHECK(m.call({{"allow_unqualified_frequency", flags}, {"expires_utc_s", 0}}).status == 200);
    CHECK(m.emitted["config"]["allow_unqualified_frequency"] == flags && m.emitted["config"]["expires_utc_s"] == 0);
  }
  for (unsigned variant = 0; variant < 12; ++variant) {
    Member m; m.modern();
    if (variant == 0) m.config["future"] = true;
    if (variant == 1) m.config["wifi"]["future"] = true;
    if (variant == 2) m.caps["configuration_updates"]["schema"] = "future/1";
    if (variant == 3) m.caps["configuration_updates"]["path"] = "/api/control/v1/config";
    if (variant == 4) m.caps["configuration_updates"]["modes"] = {"replace"};
    if (variant == 5) m.caps["configuration_updates"]["max_update_bytes"] = 1;
    if (variant == 6) m.job["boot_id"] = std::string(32, '2');
    if (variant == 7) m.job["owner_id"] = device;
    if (variant == 8) m.job["state"] = "loaded";
    if (variant == 9) m.job["output_active"] = true;
    if (variant == 10) m.malformed = R"({"config":null,"config":null})";
    if (variant == 11) m.race = true;
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto &version : {Json(1.1), Json(1.0), Json("1"), Json(true), Json(nullptr)}) {
    Member m; m.modern(); m.caps["contract"] = version;
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto &modes : {Json::array({"merge", 42}), Json::array({"merge", nullptr})}) {
    Member m; m.modern(); m.caps["configuration_updates"]["modes"] = modes;
    CHECK(m.call().status == 409 && m.mutations == 0);
  }
  Member future_mode; future_mode.modern();
  future_mode.caps["configuration_updates"]["modes"] = {"merge", "future"};
  CHECK(future_mode.call().status == 409 && future_mode.mutations == 0);
  for (const auto key : {"station", "hardware", "recurrence", "capabilities", "reboot_required"}) {
    Member m; m.modern(); m.corrupt_application = [key](Json &j) { j.erase(key); };
    CHECK(m.call().status == 502 && m.mutations == 0);
  }
  for (const auto pointer : {"/reboot_required", "/station/storage_healthy", "/station/station/callsign",
       "/hardware/saved", "/hardware/active", "/hardware/active_revision", "/hardware/pending_restart",
       "/hardware/application_error", "/recurrence/schedules", "/recurrence/expires_utc_s", "/capabilities/application_read"}) {
    Member m; m.modern(); m.corrupt_application = [pointer](Json &j) { j[Json::json_pointer(pointer)] = 42; };
    CHECK(m.call().status == 502 && m.mutations == 0);
  }
  for (const auto pointer : {"/station/target/boot_id", "/hardware/target/device_id"}) {
    Member m; m.modern(); m.corrupt_application = [pointer](Json &j) { j[Json::json_pointer(pointer)] = std::string(32, 'f'); };
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto state : {"loaded", "armed", "running", "faulted", "unconfirmed"}) {
    Member m; m.job["state"] = state;
    CHECK(m.call().status == 409 && m.mutations == 0);
  }
  Member old_frequency; old_frequency.modern();
  old_frequency.corrupt_application = [](Json &j) {
    auto &r = j["recurrence"];
    r["schedule_base_frequency_nhz"] = r["schedule_dial_frequency_nhz"];
    r.erase("schedule_dial_frequency_nhz");
  };
  CHECK(old_frequency.call().status == 502 && old_frequency.mutations == 0);
  Member both_frequencies; both_frequencies.modern();
  both_frequencies.corrupt_application = [](Json &j) {
    j["recurrence"]["schedule_base_frequency_nhz"] = "3568600000000000";
  };
  CHECK(both_frequencies.call().status == 502 && both_frequencies.mutations == 0);
  Member unhealthy; unhealthy.modern();
  unhealthy.corrupt_application = [](Json &j) { j["hardware"]["storage_healthy"] = false; };
  CHECK(unhealthy.call().status == 409 && unhealthy.mutations == 0);
  for (auto patch : {Json{{"future", 1}}, Json{{"wifi", {{"password", false}}}}, Json{{"station", Json::object()}}, Json{{"pins", nullptr}}}) {
    Member m; CHECK(m.call(patch).status == 400 && m.reads == 0 && m.mutations == 0);
  }
  Member stale; CHECK(stale.call({}, "\"old\"").status == 400);
  CHECK(stale.call({{"enabled", false}}, "\"old\"").status == 412 && stale.mutations == 0);
  CHECK(stale.call({{"enabled", false}}, "").status == 428 && stale.mutations == 0);
  CHECK(stale.raw(R"({"schema":1,"schema":2})").status == 400);
  CHECK(stale.raw(std::string(2049, ' ')).status == 413);
  for (const auto bad : {false, true}) {
    Member m; m.modern(); m.drop = !bad; m.bad_reply = bad;
    const auto response = m.call();
    CHECK(response.status != 200 && m.mutations == 1);
    CHECK(Json::parse(response.body)["mutation_attempted"] == true && response.etag.empty());
  }
  Member initial; initial.config = nullptr;
  const auto initial_result=initial.call();if(initial_result.status!=400)std::cerr<<initial_result.status<<" "<<initial_result.body<<"\n";
  CHECK(initial_result.status == 400 && initial.mutations == 1 && initial.config.is_null());
  for (const auto placeholder : {"N0CALL", "<N0CALL>"}) {
    Member member;
    CHECK(member.call(Json{{"station", {{"callsign", placeholder}}}}).status == 400);
    CHECK(member.mutations == 0);
  }
  {
    Member member;
    CHECK(member.call(Json{{"station", {{"locator", "ZZ99"}}}}).status == 400);
    CHECK(member.mutations == 0);
  }
  Member type23; type23.type23();
  CHECK(type23.call(Json{{"station",{{"callsign","<K1ABC>"},{"locator","FN42AA"}}}}).status==200);
  CHECK(type23.resource=="config/update" && type23.emitted["config"]["station"]["callsign"]=="<K1ABC>");
  for (unsigned fault=0;fault<9;++fault) {
    Member bad; bad.type23();
    if(fault==0) bad.caps["application_resources"]["schema"]="application9";
    else if(fault==8) { bad.caps.erase("application_resources");bad.caps.erase("configuration_updates"); }
    else bad.corrupt_application=[fault](Json &a) {
      if(fault==1) a["wspr_plan"]["frame_types"]={"type99"};
      if(fault==2) a["capabilities"]["station_encoding"]="wspr_type1";
      if(fault==3) a["station"]["schema"]="unknown-station";
      if(fault==4) { a["wspr_plan"]["type2_ambiguous"]=true; a["wspr_plan"]["alternate_callsign"]="AA0NT/09"; }
      if(fault==5) a["wspr_plan"]=nullptr;
      if(fault==6) { a["wspr_plan"]["frame_types"]={"type2"}; a["wspr_plan"]["type2_ambiguous"]=true; a["wspr_plan"]["alternate_callsign"]="<invalid>"; }
      if(fault==7) a["wspr_plan"]["slot_separation_s"]=120.0;
    };
    CHECK(bad.call().status>=400 && bad.mutations==0);
  }
  std::cout << "Configuration negotiation, single current contract, closed shapes, revisions, identity and ambiguous-write checks passed\n";
}
struct ProgressPeer {
  Json value{{"schema","progress"},{"target",target},{"draft_revision","0"},
    {"config_revision","0"},{"profile_revision","0"},{"fields",Json::object()},
    {"last_receipt",nullptr},{"last_outcomes",Json::object()},{"applied_station",nullptr},
    {"station",{{"callsign",nullptr},{"locator",nullptr},{"power_dbm",nullptr}}},
    {"station_complete",false},{"complete",false},{"apply_domains",{{"station",false},{"network",false},{"radio",false},{"all",false},{"drive",false}}},
    {"wspr_plan",nullptr},{"schedule_enabled",false},{"schedule_suspended",true},{"matches_applied",false}};
  Json caps=canonical_caps();
  unsigned writes=0, sequence=0;
  bool busy=false, storage_fault=false, drop=false;
  Json applied_pins=Member{}.config["pins"];
  bool unconfigured=false;std::string config_revision;
  std::function<void(Json &)> corrupt_read, corrupt_ack;
  std::string revision() const { return "\"p"+std::to_string(sequence)+"\""; }
  PicoHttpResponse exchange(std::string path,std::string method,std::string body,std::string etag) {
    if (path=="management") return {200,management().dump(),{}};
    if (path=="capabilities") return {200,document(caps),{}};
    if (path=="status") return {200,document(Json{{"job",{{"boot_id",boot},{"output_active",false},{"owner_id",nullptr},{"state",busy?"armed":"empty"}}}}),{}};
    if (method=="GET") {
      if(path=="config"){Member configured;configured.config["pins"]=applied_pins;return {200,document(Json{{"config",unconfigured?Json(nullptr):configured.config}}),config_revision.empty()?revision():config_revision};}
      CHECK(path=="progress");
      if (storage_fault) return {503,R"({"error":{"code":"storage_fault"}})",{}};
      auto observed=value; if (corrupt_read) corrupt_read(observed);
      return {200,document(observed),revision()};
    }
    ++writes; CHECK(method=="POST" && etag==revision());
    const auto input=strict_browser_json(body);
    if (path=="progress/apply") {
      value["applied_station"]=value["station"]; value["config_revision"]="1"; ++sequence;
      Member complete; complete.config["station"]=value["station"];
      return {200,document(Json{{"config",complete.config}}),revision()};
    }
    CHECK(path=="progress/update");
    Json outcomes=Json::object(); bool accepted=false;
    for (const auto &[key,field] : input["fields"].items()) {
      if (field=="invalid") outcomes[key]="invalid";
      else {
        accepted=true; outcomes[key]=field.is_null()?"cleared":"saved";
        auto normalized=field;
        if(key=="pins" && !field.is_null()) {
          if(!normalized.contains("amplifier_active_high")) normalized["amplifier_active_high"]=value["fields"].contains("pins") && !value["fields"]["pins"].is_null()
            ? value["fields"]["pins"]["amplifier_active_high"].get<bool>() : applied_pins.value("amplifier_active_high",true);
          if(!normalized.contains("indicator_active_high"))normalized["indicator_active_high"]=true;
        }
        value["fields"][key]=normalized;
        if(key=="callsign" || key=="locator" || key=="power_dbm")value["station"][key]=normalized;
      }
    }
    if (accepted) {
      unsigned char bytes[SHA256_DIGEST_LENGTH]; SHA256(reinterpret_cast<const unsigned char *>(body.data()),body.size(),bytes);
      const char *hex="0123456789abcdef"; std::string receipt;
      for (auto byte : bytes) { receipt+=hex[byte>>4];receipt+=hex[byte&15]; }
      ++sequence; value["draft_revision"]=std::to_string(sequence); value["last_receipt"]=receipt;value["last_outcomes"]=outcomes;
    }
    value["station_complete"]=!value["station"]["callsign"].is_null() && !value["station"]["locator"].is_null() && !value["station"]["power_dbm"].is_null();
    value["apply_domains"]["station"]=value["station_complete"];
    if (drop) return {503,R"({"error":{"code":"management_outcome_unknown"}})",{}};
    auto ack=value;ack["outcomes"]=outcomes;ack["request_id"]=input["request_id"];
    if (corrupt_ack) corrupt_ack(ack);
    return {200,document(ack),revision()};
  }
  PicoHttpResponse call(const Json &fields) {
    return pico_progress_request([&](auto p,auto m,auto b,auto e){return exchange(p,m,b,e);},device,boot,
      "progress/update","POST",Json{{"contract","Wsprry-Control/1"},{"schema","progress"},{"target",target},{"request_id",std::string(32,'a')},{"fields",fields}}.dump(),revision());
  }
};
void advertised_policy_checks() {
  Member config_peer;
  config_peer.caps["gpio_drive"]["values"]={2,4};config_peer.caps["gpio_drive"]["default"]=4;
  CHECK(config_peer.call({{"gpio_drive_ma",8}}).status==400);
  CHECK(config_peer.mutations==0);
  config_peer.config["gpio_drive_ma"]=8;
  CHECK(config_peer.call({{"enabled",false}}).status==502);
  CHECK(config_peer.mutations==0);
  Member inconsistent_app;
  inconsistent_app.caps["gpio_drive"]["values"]={2,4};inconsistent_app.caps["gpio_drive"]["default"]=4;
  inconsistent_app.corrupt_application=[](Json &app){app["hardware"]["gpio_drive"]["boot_active"]=8;};
  CHECK(inconsistent_app.call({{"enabled",false}}).status==502);CHECK(inconsistent_app.mutations==0);
  auto caps=canonical_caps();caps["gpio_drive"]["values"]={2,4};caps["gpio_drive"]["default"]=4;
  caps["amplifier_timing"]["minimum"]=100;caps["amplifier_timing"]["maximum"]=300;
  unsigned writes=0; Json saved{{"lead_ms",200},{"tail_ms",200}};
  const PicoManagementExchange exchange=[&](auto path,auto method,auto,auto) {
    if(method!="GET") {++writes;return PicoHttpResponse{503,"{}",{}};}
    if(path=="capabilities")return PicoHttpResponse{200,document(caps),{}};
    if(path=="management")return PicoHttpResponse{200,management().dump(),{}};
    CHECK(path=="amplifier-timing");
    return PicoHttpResponse{200,document(Json{{"schema","amplifier-timing"},{"target",target},
      {"saved",saved},{"boot_active",saved},{"pending_restart",false},{"phase","inactive"},
      {"output_active",false},{"rf_active",false},{"storage_healthy",true}}),"\"r1\""};
  };
  const auto call=[&](const std::string &resource,const std::string &method,Json value) {
    return pico_resource_request(exchange,device,boot,resource,method,method=="GET"?"":document(value),"\"r1\"");
  };
  CHECK(call("gpio-drive","PUT",{{"schema","gpio-drive"},{"target",target},{"gpio_drive_ma",8}}).status==400);
  CHECK(call("amplifier-timing","GET",{}).status==200);
  for(const auto invalid:{99,301}) CHECK(call("amplifier-timing","PUT",{{"schema","amplifier-timing"},{"target",target},
    {"timing",{{"lead_ms",200},{"tail_ms",invalid}}}}).status==400);
  saved["tail_ms"]=301;
  CHECK(call("amplifier-timing","GET",{}).status==502);
  CHECK(writes==0);
  ProgressPeer progress;
  progress.caps["gpio_drive"]["values"]={2,4};progress.caps["gpio_drive"]["default"]=4;
  CHECK(progress.call({{"gpio_drive_ma",8}}).status==400);CHECK(progress.writes==0);
  progress.value["fields"]["gpio_drive_ma"]=8;
  progress.value["draft_revision"]="1";progress.value["apply_domains"]["drive"]=true;
  const auto apply=pico_progress_request([&](auto p,auto m,auto b,auto e){return progress.exchange(p,m,b,e);},device,boot,
    "progress/apply","POST",document(Json{{"schema","progress"},{"target",target},{"application",{{"draft_revision","1"},{"domain","drive"}}}}),progress.revision());
  CHECK(apply.status==502);CHECK(progress.writes==0);
  std::cout<<"Advertised GPIO/timing policy rejects requests, stale readbacks and draft apply before writes passed\n";
}
void config_pin_omission_checks() {
  for(const auto polarity:{false,true}) {
    Member peer;peer.config["pins"]["amplifier_active_high"]=polarity;
    auto pins=peer.config["pins"];pins.erase("amplifier_active_high");
    CHECK(peer.call({{"pins",pins}}).status==200);CHECK(peer.mutations==1);
    CHECK(!peer.emitted["config"]["pins"].contains("amplifier_active_high"));
    Member explicit_peer;explicit_peer.config["pins"]["amplifier_active_high"]=polarity;
    CHECK(explicit_peer.call({{"pins",explicit_peer.config["pins"]}}).status==200);CHECK(explicit_peer.mutations==1);
  }
  for(unsigned fault=0;fault<3;++fault) {
    Member peer;auto pins=peer.config["pins"];
    if(fault==0)pins["amplifier_active_high"]="false";
    if(fault==1)pins["unknown"]=true;
    if(fault==2)pins["amplifier_gp"]=pins["rf_gp"];
    CHECK(peer.call({{"pins",pins}}).status==400);CHECK(peer.mutations==0);
  }
  std::cout<<"Config pin omission preserves saved polarity and rejects malformed plans before writes passed\n";
}
void sibling_document_checks() {
  for(unsigned fault=0;fault<5;++fault) {
    Member peer;
    const PicoManagementExchange exchange=[&](auto path,auto method,auto body,auto revision) {
      if(method=="GET" && (path=="station" || path=="hardware")) {
        const auto application=strict_browser_json(peer.exchange("application","GET",{},{}).body);
        auto sibling=application[path];
        if(path=="station" && fault==0)sibling["station"]["power_dbm"]=20.0;
        if(path=="hardware" && fault==1)sibling["saved"]["rf_gp"]=28.0;
        if(path=="station" && fault==2)sibling["extra"]=true;
        if(path=="station" && fault==3)sibling.erase("storage_healthy");
        if(path=="station" && fault==4)sibling["target"]["boot_id"]=std::string(32,'f');
        return PicoHttpResponse{200,document(sibling),peer.revision};
      }
      return peer.exchange(path,method,body,revision);
    };
    const auto response=pico_resource_request(exchange,device,boot,"application","GET",{},{});
    CHECK(response.status==502);CHECK(peer.mutations==0);
  }
  std::cout<<"Independent sibling resource typed validation rejects numeric coercion and malformed documents before writes passed\n";
}
void progress_polarity_checks() {
  auto pins=Member{}.config["pins"];pins.erase("amplifier_active_high");pins.erase("indicator_active_high");
  ProgressPeer applied;applied.applied_pins["amplifier_active_high"]=false;
  CHECK(applied.call({{"pins",pins}}).status==200);CHECK(applied.writes==1);
  CHECK(applied.value["fields"]["pins"]["amplifier_active_high"]==false);
  CHECK(applied.value["fields"]["pins"]["indicator_active_high"]==true);
  applied.applied_pins["amplifier_active_high"]=true;
  CHECK(applied.call({{"pins",pins}}).status==200);CHECK(applied.writes==2);
  CHECK(applied.value["fields"]["pins"]["amplifier_active_high"]==false);
  ProgressPeer defaulted;defaulted.unconfigured=true;CHECK(defaulted.call({{"pins",pins}}).status==200);
  CHECK(defaulted.value["fields"]["pins"]["amplifier_active_high"]==true);
  ProgressPeer raced;raced.config_revision="\"other\"";CHECK(raced.call({{"pins",pins}}).status==409);CHECK(raced.writes==0);
  for(const auto malformed:{0,1,2}) {
    ProgressPeer invalid;auto candidate=pins;
    if(malformed==0)candidate["amplifier_active_high"]="false";
    if(malformed==1)candidate["unknown"]=true;
    if(malformed==2)candidate["amplifier_gp"]=candidate["rf_gp"];
    CHECK(invalid.call({{"pins",candidate},{"enabled",false}}).status==400);CHECK(invalid.writes==0);
  }
  std::cout<<"Progress omitted polarity preserves draft/applied/default and malformed plans refuse before writes passed\n";
}
void progress_checks() {
  ProgressPeer peer;
  const auto first_progress=peer.call({{"callsign","AA0NT"},{"locator","invalid"},{"power_dbm",0}});if(first_progress.status!=200)std::cerr<<first_progress.status<<" "<<first_progress.body<<" writes "<<peer.writes<<"\n";CHECK(first_progress.status==200);
  CHECK(peer.value["fields"]["callsign"]=="AA0NT" && peer.value["fields"]["power_dbm"]==0 && peer.value["station_complete"]==false);
  CHECK(peer.value["applied_station"].is_null() && peer.value["schedule_suspended"]==true);
  CHECK(peer.call({{"power_dbm",nullptr}}).status==200 && peer.value["station"]["power_dbm"].is_null());
  const auto old=peer.value;CHECK(peer.call({{"locator","invalid"}}).status==200 && peer.value==old);
  CHECK(peer.call({{"locator","EM18"},{"power_dbm",0}}).status==200);
  CHECK(pico_progress_request([&](auto p,auto m,auto b,auto e){return peer.exchange(p,m,b,e);},device,boot,
      "progress/apply","POST",Json{{"contract","Wsprry-Control/1"},{"schema","progress"},{"target",target},
        {"application",{{"draft_revision",peer.value["draft_revision"]},{"domain","station"}}}}.dump(),peer.revision()).status==200);
  CHECK(peer.value["applied_station"]==peer.value["station"]);
  for (unsigned fault=0;fault<12;++fault) {
    ProgressPeer bad;
    if (fault==0) bad.caps.erase("saved_progress");
    if (fault==1) bad.caps["saved_progress"]["schema"]="progress9";
    if (fault==2) bad.busy=true;
    if (fault==3) bad.storage_fault=true;
    if (fault>=4) bad.corrupt_read=[fault](Json &j) {
      if(fault==4)j["target"]["boot_id"]=std::string(32,'f');
      if(fault==5)j["draft_revision"]="18446744073709551616";
      if(fault==6)j["draft_revision"]=1;
      if(fault==7)j["fields"]={{"future",true}};
      if(fault==8)j["fields"]={{"wifi",{{"ssid","synthetic"},{"password","secret-must-not-be-forwarded"}}}};
      if(fault==9)j["station"]["power_dbm"]=false;
      if(fault==10)j["apply_domains"]["station"]=1;
      if(fault==11)j["extra"]=true;
    };
    CHECK(bad.call({{"callsign","AA0NT"}}).status>=400 && bad.writes==0);
  }
  for (unsigned fault=0;fault<5;++fault) {
    ProgressPeer bad;
    if (fault==4) bad.drop=true;
    else bad.corrupt_ack=[fault](Json &j){
      if(fault==0)j["request_id"]=std::string(32,'b');
      if(fault==1)j["last_receipt"]=std::string(64,'b');
      if(fault==2)j["outcomes"]["callsign"]="invalid";
      if(fault==3)j["outcomes"]["locator"]="saved";
    };
    const auto result=bad.call({{"callsign","AA0NT"}});
    CHECK(result.status>=400 && bad.writes==1 && strict_browser_json(result.body)["mutation_attempted"]==true);
  }
  ProgressPeer changed_applied;
  changed_applied.corrupt_read=[&](Json &j){if(changed_applied.writes)j["config_revision"]="1";};
  const auto changed=changed_applied.call({{"callsign","AA0NT"}});
  CHECK(changed.status>=400 && changed_applied.writes==1);
  ProgressPeer regressed;
  regressed.sequence=10; regressed.value["draft_revision"]="10";
  regressed.corrupt_read=[&](Json &j){if(regressed.writes)j["draft_revision"]="9";};
  regressed.corrupt_ack=[](Json &j){j["draft_revision"]="9";};
  const auto rollback=regressed.call({{"callsign","AA0NT"}});
  CHECK(rollback.status>=400 && regressed.writes==1 && strict_browser_json(rollback.body)["mutation_attempted"]==true);
  std::cout<<"Saved-progress discovery, incomplete/null/zero, receipt attribution, invalid siblings, explicit apply and fail-closed readbacks passed\n";
}
struct HttpInput : wtp::ByteStream {
  std::string input;
  explicit HttpInput(std::string raw) : input(std::move(raw)) {}
  wtp::IoResult write(std::span<const std::uint8_t> b) override { return {wtp::IoState::Progress,b.size()}; }
  wtp::IoResult read(std::span<std::uint8_t> b) override {
    const auto count=std::min(b.size(),input.size());
    std::copy_n(input.begin(),count,b.begin()); input.erase(0,count);
    return {count?wtp::IoState::Progress:wtp::IoState::Closed,count};
  }
  void close() noexcept override {}
};
void usb_management_contract() {
  Json contract{{"contract","Wsprry-Control/1"},{"schema","management"},{"target",target},{"profile","blank"},{"preclock",true},
    {"profile_generation",nullptr},{"selected","usb_physical"},{"carriers",Json::array()},
    {"unsupported",{"plain_wtp_json","tls_wtp_json","ble_json"}},
    {"operations",{{"GET",{"capabilities","status","config","schedules","network","application","management"}},
      {"PUT",{"schedules","network"}},{"POST",{"config/update"}}}},
    {"limits",{{"http_body",32768},{"consumer_plaintext",32724},{"config",1800},{"update",2048},
      {"login_ms",30000},{"idle_ms",300000},{"absolute_ms",900000},{"consumer_sessions",3},{"consumer_pending_logins",1}}}};
  for(const auto carrier:{"engineering_mtls","engineering_field","consumer_station","consumer_ap","usb_physical"})
    contract["carriers"].push_back({{"carrier",carrier},{"built_in",true},{"available",std::string(carrier)=="usb_physical"}});
  const auto valid=[&](const Json &j){return valid_management_contract(j,"usb_physical",device,boot,"");};
  CHECK(valid(contract));
  auto reordered=contract;std::reverse(reordered["carriers"].begin(),reordered["carriers"].end());CHECK(valid(reordered));
  auto duplicate_carrier=contract;duplicate_carrier["carriers"][0]=duplicate_carrier["carriers"][1];CHECK(!valid(duplicate_carrier));
  auto invalid_pending=contract;invalid_pending["limits"]["consumer_sessions"]=1;invalid_pending["limits"]["consumer_pending_logins"]=2;CHECK(!valid(invalid_pending));
  auto changed=contract;changed["schema"]="unknown-management";CHECK(!valid(changed));
  changed=contract;changed["carriers"][4]["available"]=false;CHECK(!valid(changed));
  changed=contract;changed["target"]["boot_id"]=std::string(32,'f');CHECK(!valid(changed));
  changed=contract;changed["limits"]["update"]=4096;CHECK(!valid(changed));
  changed=contract;changed["operations"]["DELETE"]={"wifi"};CHECK(!valid(changed));
  changed=contract;changed["future"]=true;CHECK(!valid(changed));
  CHECK(!valid_management_contract(contract,"engineering_mtls",device,boot,""));
  auto current = contract;
  current["schema"] = "management";
  current["limits"]["idle_ms"] = 1800000;
  current["limits"]["absolute_ms"] = 28800000;
  CHECK(valid(current));
  CHECK(valid_management_descriptor(
      {{"schema", "management"}, {"path", "/api/control/v1/management"}},
      "usb_physical"));
  CHECK(valid_management_descriptor(
      {{"schema", "management"}, {"path", "/api/control/v1/management"}},
      "consumer_station"));
  for(const auto idle:{300000,1800000,3600000}) {
    changed=current;changed["limits"]["idle_ms"]=idle;CHECK(valid(changed));
  }
  for (const Json &invalid : {Json(true), Json(1800000.0), Json("1800000"),
                              Json(-1), Json(nullptr)}) {
    changed = current;
    changed["limits"]["idle_ms"] = invalid;
    CHECK(!valid(changed));
  }
  changed = current;
  changed["schema"] = "unknown-management";
  CHECK(!valid(changed));

  // Every management entry point must refuse before reaching resources or
  // dispatching a write when discovery is unsupported or inconsistent.
  for (unsigned fault = 0; fault < 13; ++fault) {
    Json descriptor{{"schema", "management"}, {"path", "/api/control/v1/management"}};
    auto advertised = current;
    if (fault == 0) descriptor["path"] = "/api/control/v1/other";
    if (fault == 1) descriptor["schema"] = "unknown-management";
    if (fault == 2) descriptor["extra"] = true;
    if (fault == 3) descriptor["schema"] = "transmitter-management/3";
    if (fault == 4) advertised["contract"] = "Wsprry-Control/2";
    if (fault == 5) advertised["limits"]["idle_ms"] = 0;
    if (fault == 6) advertised["extra"] = true;
    if (fault == 7) advertised["schema"] = 5;
    if (fault == 8) advertised["target"]["boot_id"] = std::string(32, 'f');
    if (fault == 9) advertised.erase("schema");
    if (fault == 10) advertised = Json::array();
    if (fault == 11) advertised = nullptr;
    if (fault == 12) { advertised["limits"]["consumer_sessions"]=1;advertised["limits"]["consumer_pending_logins"]=2; }
    Json caps=Member{}.caps;caps["management_carriers"]=descriptor;
    caps["saved_progress"]={{"schema","progress"},{"read","/api/control/v1/progress"},{"save","/api/control/v1/progress/update"},{"apply","/api/control/v1/progress/apply"},{"max_bytes",2048}};
    unsigned writes = 0, resource_reads = 0, management_reads = 0;
    const PicoManagementExchange exchange = [&](auto resource, auto method, auto, auto) {
      if (method != "GET") ++writes;
      if (resource == "capabilities") return PicoHttpResponse{200, document(caps), {}};
      if (resource == "management") {
        ++management_reads;
        return PicoHttpResponse{200, advertised.dump(), {}};
      }
      ++resource_reads;
      return PicoHttpResponse{503, "{}", {}};
    };
    for (const auto &response : {
      pico_configuration_request(exchange, device, boot, "GET", {}, {}, "usb_physical"),
      pico_progress_request(exchange, device, boot, "progress", "GET", {}, {}, "usb_physical"),
      pico_resource_request(exchange, device, boot, "application", "GET", {}, {}, "usb_physical"),
      pico_configuration_request(exchange, device, boot, "POST",
        Json{{"contract","Wsprry-Control/1"},{"schema", "pico-settings"}, {"target", target},
          {"operation", "save"}, {"config", {{"enabled", false}}}}.dump(), "\"r1\"", "usb_physical"),
      pico_progress_request(exchange, device, boot, "progress/update", "POST",
        Json{{"contract","Wsprry-Control/1"},{"schema", "progress"}, {"target", target},
          {"request_id", std::string(32, 'a')}, {"fields", {{"callsign", "AA0NT"}}}}.dump(), "\"r1\"", "usb_physical"),
      pico_resource_request(exchange, device, boot, "station", "PUT",
        Json{{"contract","Wsprry-Control/1"},{"schema", "station"}, {"target", target},
          {"station", {{"callsign", "K1ABC"}, {"locator", "EM18"}, {"power_dbm", 20}}}}.dump(), "\"r1\"", "usb_physical")}) {
      CHECK(response.status == 409 || response.status == 502);
      const auto refusal = strict_browser_json(response.body);
      CHECK(refusal["error"]["code"] == "unsupported_management_contract" || refusal["error"]["code"] == "unsupported_remote_contract" || refusal["error"]["code"] == "invalid_remote_response");
      CHECK(refusal["mutation_attempted"] == false);
    }
    CHECK(writes == 0 && resource_reads == 0);
    if (fault < 4) CHECK(management_reads == 0);
  }
}
void management_descriptor_matrix() {
  for (const auto carrier : {"engineering_mtls", "engineering_field", "consumer_station",
                             "consumer_ap", "usb_physical", "unknown"}) {
    for (unsigned version = 0; version <= 6; ++version) {
      const Json descriptor{{"schema", "transmitter-management/" + std::to_string(version)},
                            {"path", "/api/control/v1/management"}};
      const bool expected = false;
      CHECK(valid_management_descriptor(descriptor, carrier) == expected);
      auto invalid = descriptor;
      invalid["path"] = "/api/control/v1/other";
      CHECK(!valid_management_descriptor(invalid, carrier));
      invalid = descriptor;
      invalid["extra"] = true;
      CHECK(!valid_management_descriptor(invalid, carrier));
      for (const Json &bad_schema : {Json(nullptr), Json(true), Json(version),
                                     Json::array(), Json::object()}) {
        invalid = descriptor;
        invalid["schema"] = bad_schema;
        CHECK(!valid_management_descriptor(invalid, carrier));
      }
    }
  }
  for(const auto carrier:{"engineering_mtls","engineering_field","consumer_station","consumer_ap","usb_physical"})
    CHECK(valid_management_descriptor({{"schema","management"},{"path","/api/control/v1/management"}},carrier));
  CHECK(!valid_management_descriptor(nullptr, "usb_physical"));
  CHECK(!valid_management_descriptor(Json::array(), "consumer_station"));
  std::cout << "Closed management versions, carriers, descriptors and pre-mutation refusal matrix passed\n";
}
void http_boundaries() {
  for (const auto raw : {"HTTP/1.1 \r\n\r\n", "HTTP/1.1 20x OK\r\n\r\n", "HTTP/1.1 200X\r\n\r\n",
       "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\ncontent-length: 2\r\n\r\n{}",
       "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 32769\r\n\r\n"}) {
    HttpInput stream(raw); std::uint64_t tick=0;
    CHECK(wtp_json_http_exchange(stream,[&]{return ++tick;},"",100,true).status==502);
  }
  HttpInput valid("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
  std::uint64_t tick=0;
  CHECK(wtp_json_http_exchange(valid,[&]{return ++tick;},"",100,true).status==200);
}
} // namespace
int main() { try { checks(); advertised_policy_checks(); config_pin_omission_checks(); sibling_document_checks(); progress_polarity_checks(); progress_checks(); usb_management_contract(); management_descriptor_matrix(); http_boundaries(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
