// SPDX-License-Identifier: MIT
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/consumer_radio.hpp"
#include <iostream>
#include <openssl/sha.h>
using namespace wsprrypi;
using Json = nlohmann::json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
namespace {
const std::string device(32, 'a'), boot(32, '1');
const Json target{{"scope", "member"}, {"device_id", device}, {"boot_id", boot}};
struct Member {
  Json config = Json::parse(R"({"version":1,"enabled":false,"station":{"callsign":"AA0NT","locator":"EM18","power_dbm":20},"wifi":{"ssid":"test-network","password":null,"ntp_ipv4":"192.0.2.1"},"schedules":[{"period_s":120,"phase_s":0}],"allow_unqualified_frequency":true,"allow_non_amateur_frequency":true,"expires_utc_s":1900000000})");
  Json caps{{"api_version", 1}};
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
        {"indicator_gp",nullptr},{"indicator_active_high",true}};
  }
  void modern() {
    caps["configuration_updates"] = {{"schema", "transmitter-config-update/1"}, {"path", "/api/v1/config/update"},
        {"method", "POST"}, {"modes", {"merge", "replace"}}, {"max_update_bytes", 2048}, {"max_config_bytes", 1800}};
    caps["application_resources"] = {{"schema", "transmitter-application/1"}, {"read", "/api/v1/application"}};
  }
  void type23() {
    modern();
    caps["application_resources_v2"]={{"schema","transmitter-application/2"},{"read","/api/v1/application/2"},
        {"station","/api/v1/station/2"},{"hardware","/api/v1/hardware"},{"management_carrier","authenticated_https"},
        {"config","/api/v1/config/2"},{"update","/api/v1/config/update/2"}};
    config["station"]={{"callsign","<AA0NT>"},{"locator","EM18IG"},{"power_dbm",20}};
  }
  PicoHttpResponse exchange(const std::string &path, const std::string &verb, const std::string &body, const std::string &etag) {
    if (verb != "GET") {
      ++mutations; resource = path; method = verb; emitted = strict_browser_json(body);
      CHECK(etag == revision);
      if (drop) return {503, R"({"error":{"code":"management_outcome_unknown"}})", {}};
      return {200, bad_reply ? "{}" : Json{{"config", config}, {"reboot_required", false}}.dump(), "\"r2\""};
    }
    ++reads;
    if (path == "capabilities") return {200, caps.dump(), {}};
    if (path == "status") return {200, Json{{"job", job}}.dump(), {}};
    if (path == "application" || path == "application/2") {
      Json application{{"schema", "transmitter-application/1"}, {"target", target}, {"reboot_required", false},
          {"station", {{"schema", "transmitter-station/1"}, {"target", target}, {"station", config["station"]},
              {"saved_generation", "1"}, {"storage_healthy", true}}},
          {"hardware", {{"schema", "transmitter-hardware/1"}, {"target", target}, {"saved", config["pins"]},
              {"saved_generation", "1"}, {"active", config["pins"]}, {"active_revision", std::string(64, 'a')},
              {"pending_restart", false}, {"execution_engine", "test"}, {"storage_healthy", true}, {"application_error", nullptr}}},
          {"recurrence", {{"authority", "member"}, {"enabled", false}, {"suspended", false}, {"expires_utc_s", "0"},
              {"schedules", config["schedules"]}, {"schedule_dial_frequency_nhz", "3568600000000000"}}},
          {"capabilities", {{"management_carrier", "authenticated_https"}, {"application_read", true},
              {"station_write", true}, {"hardware_write", true}, {"max_update_bytes", 1024}, {"max_config_bytes", 1800},
              {"hardware_activation", "restart"}, {"station_encoding", "wspr_type1"}, {"station_power_meaning", "reported_wspr_dbm"},
              {"recurrence_authorities", {"member"}}, {"autonomous_modes", {"wspr"}}, {"unavailable", Json::array()}}}};
      if (path == "application/2") {
        application["schema"]="transmitter-application/2";
        application["station"]["schema"]="transmitter-station/2";
        application["capabilities"]["station_encoding"]="wspr_type123_auto";
        application["wspr_plan"]={{"schema","wspr-plan/1"},{"supported",true},{"frames",1},
            {"frame_types",{"type3"}},{"type2_ambiguous",false},{"alternate_callsign",nullptr},{"slot_separation_s",120},{"rationale","Hash context is needed."}};
      }
      if (corrupt_application) corrupt_application(application);
      return {200, application.dump(), revision};
    }
    CHECK(path == "config");
    if (race && reads > 3) revision = "\"r2\"";
    return {200, malformed.empty() ? Json{{"config", config}}.dump() : malformed, revision};
  }
  PicoHttpResponse call(Json patch = Json{{"station", {{"callsign", "K1ABC"}}}}, std::string etag = "\"r1\"") {
    return raw(Json{{"schema", "wsprrypi-pico-settings/1"}, {"target", target}, {"operation", "save"}, {"config", patch}}.dump(), etag);
  }
  PicoHttpResponse raw(const std::string &body, const std::string &etag = "\"r1\"") {
    return pico_configuration_request([&](auto p, auto m, auto b, auto e) { return exchange(p,m,b,e); }, device, boot, "POST", body, etag);
  }
};
void checks() {
  Member legacy;
  CHECK(legacy.call().status == 200 && legacy.mutations == 1);
  CHECK(legacy.resource == "config" && legacy.method == "PUT");
  CHECK(legacy.emitted["station"]["callsign"] == "K1ABC");
  CHECK(legacy.emitted["allow_unqualified_frequency"] == true && legacy.emitted["allow_non_amateur_frequency"] == true);
  CHECK(legacy.emitted["expires_utc_s"] == 1900000000 && legacy.emitted["wifi"]["password"].is_null());
  CHECK(legacy.emitted["pins"]["indicator_active_high"] == true);
  Member strict;
  strict.config.erase("allow_unqualified_frequency"); strict.config.erase("allow_non_amateur_frequency");
  strict.config.erase("pins");
  CHECK(strict.call().status == 200 && !strict.emitted.contains("allow_unqualified_frequency"));
  CHECK(strict.call({{"allow_unqualified_frequency", false}}).status == 409 && strict.mutations == 1);
  Member modern; modern.modern();
  CHECK(modern.call().status == 200 && modern.resource == "config/update" && modern.method == "POST");
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
    if (variant == 3) m.caps["configuration_updates"]["path"] = "/api/v1/config";
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
    Member m; m.modern(); m.caps["api_version"] = version;
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto &modes : {Json::array({"merge", 42}), Json::array({"merge", nullptr})}) {
    Member m; m.modern(); m.caps["configuration_updates"]["modes"] = modes;
    CHECK(m.call().status == 409 && m.mutations == 0);
  }
  Member future_mode; future_mode.modern();
  future_mode.caps["configuration_updates"]["modes"] = {"merge", "future"};
  CHECK(future_mode.call().status == 200 && future_mode.mutations == 1);
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
  CHECK(initial.call().status == 400 && initial.mutations == 0);
  Member type23; type23.type23();
  CHECK(type23.call(Json{{"station",{{"callsign","<K1ABC>"},{"locator","FN42AA"}}}}).status==200);
  CHECK(type23.resource=="config/update/2" && type23.emitted["config"]["station"]["callsign"]=="<K1ABC>");
  for (unsigned fault=0;fault<9;++fault) {
    Member bad; bad.type23();
    if(fault==0) bad.caps["application_resources_v2"]["schema"]="transmitter-application/99";
    else if(fault==8) { bad.caps.erase("application_resources");bad.caps.erase("configuration_updates"); }
    else bad.corrupt_application=[fault](Json &a) {
      if(fault==1) a["wspr_plan"]["frame_types"]={"type99"};
      if(fault==2) a["capabilities"]["station_encoding"]="wspr_type1";
      if(fault==3) a["station"]["schema"]="transmitter-station/1";
      if(fault==4) { a["wspr_plan"]["type2_ambiguous"]=true; a["wspr_plan"]["alternate_callsign"]="AA0NT/09"; }
      if(fault==5) a["wspr_plan"]=nullptr;
      if(fault==6) { a["wspr_plan"]["frame_types"]={"type2"}; a["wspr_plan"]["type2_ambiguous"]=true; a["wspr_plan"]["alternate_callsign"]="<invalid>"; }
      if(fault==7) a["wspr_plan"]["slot_separation_s"]=120.0;
    };
    CHECK(bad.call().status>=400 && bad.mutations==0);
  }
  std::cout << "Configuration negotiation, strict fallback, closed shapes, revisions, identity and ambiguous-write checks passed\n";
}
struct ProgressPeer {
  Json value{{"schema","transmitter-progress/1"},{"target",target},{"draft_revision","0"},
    {"config_revision","0"},{"profile_revision","0"},{"fields",Json::object()},
    {"last_receipt",nullptr},{"last_outcomes",Json::object()},{"applied_station",nullptr},
    {"station",{{"callsign",nullptr},{"locator",nullptr},{"power_dbm",nullptr}}},
    {"station_complete",false},{"complete",false},{"apply_domains",{{"station",false},{"network",false},{"radio",false},{"all",false}}},
    {"wspr_plan",nullptr},{"schedule_enabled",false},{"schedule_suspended",true},{"matches_applied",false}};
  Json caps{{"api_version",1},{"features",{{"consumer_setup_identity",false}}},
    {"saved_progress",{{"schema","transmitter-progress/1"},{"read","/api/v1/progress"},
      {"save","/api/v1/progress/update"},{"apply","/api/v1/progress/apply"},{"max_bytes",2048}}}};
  unsigned writes=0, sequence=0;
  bool busy=false, storage_fault=false, drop=false;
  std::function<void(Json &)> corrupt_read, corrupt_ack;
  std::string revision() const { return "\"p"+std::to_string(sequence)+"\""; }
  PicoHttpResponse exchange(std::string path,std::string method,std::string body,std::string etag) {
    if (path=="capabilities") return {200,caps.dump(),{}};
    if (path=="status") return {200,Json{{"job",{{"boot_id",boot},{"output_active",false},{"owner_id",nullptr},{"state",busy?"armed":"empty"}}}}.dump(),{}};
    if (method=="GET") {
      CHECK(path=="progress");
      if (storage_fault) return {503,R"({"error":{"code":"storage_fault"}})",{}};
      auto observed=value; if (corrupt_read) corrupt_read(observed);
      return {200,observed.dump(),revision()};
    }
    ++writes; CHECK(method=="POST" && etag==revision());
    const auto input=strict_browser_json(body);
    if (path=="progress/apply") {
      value["applied_station"]=value["station"]; value["config_revision"]="1"; ++sequence;
      Member complete; complete.config["station"]=value["station"];
      return {200,Json{{"config",complete.config}}.dump(),revision()};
    }
    CHECK(path=="progress/update");
    Json outcomes=Json::object(); bool accepted=false;
    for (const auto &[key,field] : input["fields"].items()) {
      if (field=="invalid") outcomes[key]="invalid";
      else { accepted=true; outcomes[key]=field.is_null()?"cleared":"saved"; value["fields"][key]=field; value["station"][key]=field; }
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
    return {200,ack.dump(),revision()};
  }
  PicoHttpResponse call(const Json &fields) {
    return pico_progress_request([&](auto p,auto m,auto b,auto e){return exchange(p,m,b,e);},device,boot,
      "progress/update","POST",Json{{"schema","transmitter-progress/1"},{"target",target},{"request_id",std::string(32,'a')},{"fields",fields}}.dump(),revision());
  }
};
void progress_checks() {
  ProgressPeer peer;
  CHECK(peer.call({{"callsign","AA0NT"},{"locator","invalid"},{"power_dbm",0}}).status==200);
  CHECK(peer.value["fields"]["callsign"]=="AA0NT" && peer.value["fields"]["power_dbm"]==0 && peer.value["station_complete"]==false);
  CHECK(peer.value["applied_station"].is_null() && peer.value["schedule_suspended"]==true);
  CHECK(peer.call({{"power_dbm",nullptr}}).status==200 && peer.value["station"]["power_dbm"].is_null());
  const auto old=peer.value;CHECK(peer.call({{"locator","invalid"}}).status==200 && peer.value==old);
  CHECK(peer.call({{"locator","EM18"},{"power_dbm",0}}).status==200);
  CHECK(pico_progress_request([&](auto p,auto m,auto b,auto e){return peer.exchange(p,m,b,e);},device,boot,
      "progress/apply","POST",Json{{"schema","transmitter-progress/1"},{"target",target},
        {"application",{{"draft_revision",peer.value["draft_revision"]},{"domain","station"}}}}.dump(),peer.revision()).status==200);
  CHECK(peer.value["applied_station"]==peer.value["station"]);
  for (unsigned fault=0;fault<12;++fault) {
    ProgressPeer bad;
    if (fault==0) bad.caps.erase("saved_progress");
    if (fault==1) bad.caps["saved_progress"]["schema"]="transmitter-progress/99";
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
  Json contract{{"schema","transmitter-management/3"},{"target",target},{"profile","blank"},{"preclock",true},
    {"profile_generation",nullptr},{"selected","usb_physical"},{"carriers",Json::array()},
    {"unsupported",{"plain_wtp_json","tls_wtp_json","ble_json"}},
    {"operations",{{"GET",{"capabilities","status","config","schedules","network","application","management"}},
      {"PUT",{"config","schedules","network"}},{"POST",{"config/update"}}}},
    {"limits",{{"http_body",32768},{"consumer_plaintext",32724},{"config",1800},{"update",2048},
      {"login_ms",30000},{"idle_ms",300000},{"absolute_ms",900000},{"consumer_sessions",3},{"consumer_pending_logins",1}}}};
  for(const auto carrier:{"engineering_mtls","engineering_field","consumer_station","consumer_ap","usb_physical"})
    contract["carriers"].push_back({{"carrier",carrier},{"built_in",true},{"available",std::string(carrier)=="usb_physical"}});
  const auto valid=[&](const Json &j){return valid_management_contract(j,"usb_physical",device,boot,"");};
  CHECK(valid(contract));
  auto changed=contract;changed["schema"]="transmitter-management/2";CHECK(!valid(changed));
  changed=contract;changed["carriers"][4]["available"]=false;CHECK(!valid(changed));
  changed=contract;changed["target"]["boot_id"]=std::string(32,'f');CHECK(!valid(changed));
  changed=contract;changed["limits"]["update"]=4096;CHECK(!valid(changed));
  changed=contract;changed["operations"]["DELETE"]={"wifi"};CHECK(!valid(changed));
  changed=contract;changed["future"]=true;CHECK(!valid(changed));
  CHECK(!valid_management_contract(contract,"engineering_mtls",device,boot,""));
  auto current = contract;
  current["schema"] = "transmitter-management/5";
  current["limits"]["idle_ms"] = 1800000;
  current["limits"]["absolute_ms"] = 28800000;
  CHECK(valid(current));
  CHECK(valid_management_descriptor(
      {{"schema", "transmitter-management/5"}, {"path", "/api/v1/management"}},
      "usb_physical"));
  CHECK(!valid_management_descriptor(
      {{"schema", "transmitter-management/5"}, {"path", "/api/v1/management"}},
      "consumer_station"));
  for (const auto key : {"idle_ms", "absolute_ms"}) {
    changed = current;
    changed["limits"][key] = contract["limits"][key];
    CHECK(!valid(changed));
    changed = contract;
    changed["limits"][key] = current["limits"][key];
    CHECK(!valid(changed));
  }
  for (const Json &invalid : {Json(true), Json(1800000.0), Json("1800000"),
                              Json(-1), Json(nullptr)}) {
    changed = current;
    changed["limits"]["idle_ms"] = invalid;
    CHECK(!valid(changed));
  }
  changed = current;
  changed["schema"] = "transmitter-management/6";
  CHECK(!valid(changed));
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
int main() { try { checks(); progress_checks(); usb_management_contract(); http_boundaries(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
