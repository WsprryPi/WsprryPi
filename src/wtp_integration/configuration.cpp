// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "configuration.hpp"
#include "consumer_radio.hpp"
#include "browser_api.hpp"
#include <algorithm>
#include <set>
#include <regex>
namespace wsprrypi {
namespace {
using Json = nlohmann::json;
struct Rejected { unsigned status; const char *code; };
void require(bool condition, const char *code = "invalid_remote_configuration", unsigned status = 502) {
  if (!condition) throw Rejected{status, code};
}
bool fields(const Json &j, std::initializer_list<const char *> required,
    std::initializer_list<const char *> optional = {}) {
  if (!j.is_object()) return false;
  std::set<std::string> names;
  for (const auto name : required) { if (!j.contains(name)) return false; names.insert(name); }
  for (const auto name : optional) names.insert(name);
  for (const auto &[name, value] : j.items()) { (void)value; if (!names.contains(name)) return false; }
  return true;
}
bool identity(const std::string &s) {
  return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool etag(const std::string &s) {
  return s.size() >= 2 && s.size() <= 128 && s.front() == '"' && s.back() == '"' &&
      std::all_of(s.begin()+1, s.end()-1, [](unsigned char c) { return c >= 33 && c < 127 && c != '"'; });
}
bool shape(const Json &j, bool partial, bool redacted) {
  if (!partial && j.dump().size() > 1800) return false;
  if (!(partial ? fields(j, {}, {"version", "enabled", "station", "wifi", "schedules", "pins",
        "expires_utc_s", "allow_unqualified_frequency", "allow_non_amateur_frequency"}) && !j.empty()
      : fields(j, {"version", "enabled", "station", "wifi", "schedules"}, {"pins",
        "expires_utc_s", "allow_unqualified_frequency", "allow_non_amateur_frequency"}))) return false;
  if (j.contains("version") && (!j["version"].is_number_integer() || j["version"] != 1)) return false;
  for (const auto key : {"enabled", "allow_unqualified_frequency", "allow_non_amateur_frequency"})
    if (j.contains(key) && !j[key].is_boolean()) return false;
  if (j.contains("expires_utc_s") && (!j["expires_utc_s"].is_number_integer() ||
      j["expires_utc_s"] < 0 || j["expires_utc_s"] > 2147483647)) return false;
  if (j.contains("station")) {
    const auto &s = j["station"];
    if (!(partial ? fields(s, {}, {"callsign", "locator", "power_dbm"}) && !s.empty()
                  : fields(s, {"callsign", "locator", "power_dbm"}))) return false;
    for (const auto key : {"callsign", "locator"}) if (s.contains(key) && !s[key].is_string()) return false;
    if (s.contains("power_dbm") && !s["power_dbm"].is_number_integer()) return false;
  }
  if (j.contains("wifi")) {
    const auto &w = j["wifi"];
    if (!(partial ? fields(w, {}, {"ssid", "password", "ntp_ipv4"}) && !w.empty()
                  : fields(w, {"ssid", "password"}, {"ntp_ipv4"}))) return false;
    for (const auto key : {"ssid", "ntp_ipv4"}) if (w.contains(key) && !w[key].is_string()) return false;
    if (w.contains("password") && !(w["password"].is_null() || (!redacted && w["password"].is_string()))) return false;
  }
  if (j.contains("schedules")) {
    if (!j["schedules"].is_array() || j["schedules"].size() > 8) return false;
    for (const auto &s : j["schedules"])
      if (!fields(s, {"period_s", "phase_s"}) || !s["period_s"].is_number_integer() ||
          !s["phase_s"].is_number_integer()) return false;
  }
  if (j.contains("pins")) {
    const auto &p = j["pins"];
    if (!fields(p, {"engine", "rf_gp", "i2c_pair", "button_gp", "amplifier_gp", "lpf_gps", "indicator", "indicator_gp"}, {"indicator_active_high"}) ||
        !p["engine"].is_string() || !p["indicator"].is_string() || !p["lpf_gps"].is_array()) return false;
    if (p.contains("indicator_active_high") && !p["indicator_active_high"].is_boolean()) return false;
    for (const auto key : {"rf_gp", "i2c_pair", "button_gp", "amplifier_gp", "indicator_gp"})
      if (!p[key].is_null() && !p[key].is_number_integer()) return false;
    for (const auto &pin : p["lpf_gps"]) if (!pin.is_null() && !pin.is_number_integer()) return false;
  }
  return true;
}
bool decimal(const Json &j) {
  if (!j.is_string()) return false;
  const auto &s = j.get_ref<const std::string &>();
  return !s.empty() && s.size() <= 20 && (s.size() == 1 || s.front() != '0') &&
      std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
      (s.size() < 20 || s <= "18446744073709551615");
}
Json resource_descriptor(bool extended) {
  Json result{{"schema",extended ? "transmitter-application/2" : "transmitter-application/1"},
      {"read",extended ? "/api/v1/application/2" : "/api/v1/application"},
      {"station",extended ? "/api/v1/station/2" : "/api/v1/station"},
      {"hardware","/api/v1/hardware"},{"management_carrier","authenticated_https"}};
  if (extended) { result["config"]="/api/v1/config/2"; result["update"]="/api/v1/config/update/2"; }
  return result;
}
bool application_shape(const Json &j, const Json &target) {
  const bool extended = j.is_object() && j.value("schema", "") == "transmitter-application/2";
  if (extended) {
    if (!j.contains("wspr_plan")) return false;
    const auto &p=j["wspr_plan"];
    if (!p.is_null()) {
      if (!fields(p,{"schema","supported","frame_types","frames","slot_separation_s","rationale","type2_ambiguous","alternate_callsign"}) ||
          p["schema"] != "wspr-plan/1" || !p["supported"].is_boolean() || !p["type2_ambiguous"].is_boolean() ||
          (p["type2_ambiguous"] == false ? !p["alternate_callsign"].is_null() :
           (!p["alternate_callsign"].is_string() || p["alternate_callsign"].get_ref<const std::string &>().size()<3 || p["alternate_callsign"].get_ref<const std::string &>().size()>12)) || !p["frames"].is_number_integer() ||
          !p["frame_types"].is_array() || p["frames"] != p["frame_types"].size() || !p["slot_separation_s"].is_number_integer() || p["slot_separation_s"] != 120 ||
          !p["rationale"].is_string() || p["rationale"].get_ref<const std::string &>().size() > 512) return false;
      const auto &types=p["frame_types"];
      if (p["supported"] == false ? !types.empty() :
          !(types == Json::array({"type1"}) || types == Json::array({"type2"}) ||
            types == Json::array({"type3"}) || types == Json::array({"type2","type3"}))) return false;
      if (p["type2_ambiguous"] == true && (types.empty() || types[0] != "type2" ||
          !std::regex_match(p["alternate_callsign"].get<std::string>(),std::regex("[A-Z0-9]+/[A-Z0-9]+")))) return false;
    }
  }
  if (!fields(j, {"schema", "target", "station", "hardware", "recurrence", "capabilities", "reboot_required"}, extended ? std::initializer_list<const char *>{"wspr_plan"} : std::initializer_list<const char *>{}) ||
      j["schema"] != (extended ? "transmitter-application/2" : "transmitter-application/1") || j["target"] != target || !j["reboot_required"].is_boolean()) return false;
  const auto &s = j["station"], &h = j["hardware"], &r = j["recurrence"], &c = j["capabilities"];
  if (extended && s.is_object() && s.contains("station") &&
      j["wspr_plan"].is_null() != s["station"].is_null()) return false;
  if (!fields(s, {"schema", "target", "station", "saved_generation", "storage_healthy"}) ||
      s["schema"] != (extended ? "transmitter-station/2" : "transmitter-station/1") || s["target"] != target || !s["storage_healthy"].is_boolean() ||
      !(s["saved_generation"].is_null() || decimal(s["saved_generation"])) ||
      !(s["station"].is_null() || (fields(s["station"], {"callsign", "locator", "power_dbm"}) &&
          shape(Json{{"station", s["station"]}}, true, true)))) return false;
  if (!fields(h, {"schema", "target", "saved", "saved_generation", "active", "active_revision", "pending_restart",
          "execution_engine", "storage_healthy", "application_error"}) ||
      h["schema"] != "transmitter-hardware/1" || h["target"] != target || !h["pending_restart"].is_boolean() ||
      !h["storage_healthy"].is_boolean() || !h["execution_engine"].is_string() || h["execution_engine"].empty() ||
      h["execution_engine"].get_ref<const std::string &>().size() > 128 ||
      !(h["saved_generation"].is_null() || decimal(h["saved_generation"])) ||
      !(h["application_error"].is_null() || h["application_error"] == "output_disable_failed")) return false;
  for (const auto key : {"saved", "active"})
    if (!h[key].is_null() && !shape(Json{{"pins", h[key]}}, true, true)) return false;
  if (!h["active_revision"].is_null()) {
    if (!h["active_revision"].is_string()) return false;
    const auto &revision = h["active_revision"].get_ref<const std::string &>();
    if (revision.size() != 64 || !std::all_of(revision.begin(), revision.end(), [](char value) {
          return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); })) return false;
  }
  if (!fields(r, {"authority", "enabled", "suspended", "expires_utc_s", "schedules", "schedule_base_frequency_nhz"}) ||
      r["authority"] != "member" || !r["enabled"].is_boolean() || !r["suspended"].is_boolean() ||
      !decimal(r["expires_utc_s"]) || !decimal(r["schedule_base_frequency_nhz"]) ||
      !shape(Json{{"schedules", r["schedules"]}}, true, true)) return false;
  if (!fields(c, {"management_carrier", "application_read", "station_write", "hardware_write", "max_update_bytes",
          "max_config_bytes", "hardware_activation", "station_encoding", "station_power_meaning", "recurrence_authorities",
          "autonomous_modes", "unavailable"}) || c["management_carrier"] != "authenticated_https" ||
      c["application_read"] != true || !c["station_write"].is_boolean() || !c["hardware_write"].is_boolean() ||
      c["hardware_activation"] != "restart" || c["station_encoding"] != (extended ? "wspr_type123_auto" : "wspr_type1") ||
      c["station_power_meaning"] != "reported_wspr_dbm") return false;
  for (const auto key : {"max_update_bytes", "max_config_bytes"})
    if (!c[key].is_number_integer() || c[key] <= 0) return false;
  for (const auto key : {"recurrence_authorities", "autonomous_modes", "unavailable"}) {
    if (!c[key].is_array()) return false;
    for (const auto &value : c[key]) if (!value.is_string()) return false;
  }
  return true;
}
Json parsed(const PicoHttpResponse &r) {
  require(r.body.size() <= 16384, "invalid_remote_response");
  try { return strict_browser_json(r.body); }
  catch (...) { throw Rejected{502, "invalid_remote_response"}; }
}
void success(const PicoHttpResponse &r) {
  if (r.status == 200) return;
  // Never reflect arbitrary remote strings or a response containing submitted credentials.
  const auto j = parsed(r);
  const auto e = j.find("error");
  std::string code;
  if (e != j.end() && e->is_object() && e->contains("code") && (*e)["code"].is_string()) code = (*e)["code"];
  for (const auto known : {"revision_conflict", "target_mismatch", "consumer_setup_identity_required",
       "busy", "not_configured", "invalid_config", "storage_fault", "output_disable_failed",
       "resource_exhausted", "management_outcome_unknown", "network_unavailable", "network_deadline",
       "tls_authentication_or_connection_failed", "host_busy_or_output_unresolved",
       "consumer_login_required", "consumer_session_unconfirmed", "setup_pending",
       "unsupported_station_encoding", "unsupported_pin_adapter", "invalid_pin_plan",
       "network_change_pending", "unsupported_resource_schema", "resource_body_too_large"})
    if (code == known) throw Rejected{r.status, known};
  throw Rejected{r.status >= 400 && r.status <= 599 ? r.status : 502U, "remote_configuration_rejected"};
}
Json merge(Json current, const Json &patch) {
  for (const auto &[key, value] : patch.items()) {
    if (key == "station" || key == "wifi") {
      for (const auto &[leaf, replacement] : value.items()) current[key][leaf] = replacement;
    } else current[key] = value;
  }
  return current;
}
} // namespace
PicoHttpResponse pico_configuration_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &method,
    const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  bool attempted = false;
  try {
    require(identity(device) && identity(boot), "member_identity_unavailable", 409);
    require(method == "GET" || method == "POST", "not_found", 404);
    require(body.size() <= 2048, "body_too_large", 413);
    const Json target{{"scope", "member"}, {"device_id", device}, {"boot_id", boot}};
    Json input;
    if (method == "POST") {
      require(etag(revision), "revision_required", 428);
      try { input = strict_browser_json(body); } catch (...) { throw Rejected{400, "invalid_json"}; }
      require(fields(input, {"schema", "target", "operation", "config"}) &&
          input["schema"] == "wsprrypi-pico-settings/1" &&
          (input["operation"] == "save" || input["operation"] == "schedules") &&
          shape(input["config"], true, false), "invalid_configuration_edit", 400);
      require(input["target"] == target, "target_mismatch", 409);
      if (input["operation"] == "schedules")
        require(fields(input["config"], {"schedules"}), "invalid_configuration_edit", 400);
    }
    const auto read = [&](const std::string &resource) {
      const auto r = exchange(resource, "GET", {}, {}); success(r); return r;
    };
    const auto caps = parsed(read("capabilities"));
    require(caps.is_object() && caps.contains("api_version") && caps["api_version"].is_number_integer() &&
        caps["api_version"] == 1, "unsupported_remote_api", 409);
    require(!caps.contains("application_resources_v2") || caps.contains("application_resources"),
        "unsupported_configuration_context",409);
    if (caps.contains("management_carriers")) {
      require(fields(caps["management_carriers"], {"schema", "path"}) &&
          (caps["management_carriers"]["schema"] == "transmitter-management/1" ||
           caps["management_carriers"]["schema"] == "transmitter-management/2") &&
          caps["management_carriers"]["path"] == "/api/v1/management", "unsupported_management_contract", 409);
      const auto management = parsed(read("management"));
      require(valid_management_contract(management, carrier, device, boot, generation) &&
          management["schema"] == caps["management_carriers"]["schema"],
          "unsupported_management_contract", 409);
    } else require(carrier == "engineering_mtls", "unsupported_management_contract", 409);
    auto original = read("config");
    auto configuration = parsed(original);
    require(fields(configuration, {"config"}) && (configuration["config"].is_null() ||
        shape(configuration["config"], false, true)) && etag(original.etag));
    const auto status = parsed(read("status"));
    require(status.contains("job") && status["job"].is_object(), "invalid_remote_response");
    const auto &job = status["job"];
    require(job.value("boot_id", "") == boot, "target_mismatch", 409);
    require(job.contains("output_active") && job["output_active"] == false &&
        job.contains("owner_id") && job["owner_id"].is_null() && job.contains("state") &&
        (job["state"] == "empty" || job["state"] == "complete" || job["state"] == "aborted" || job["state"] == "missed"),
        "remote_busy_or_output_unresolved", 409);
    Json application = nullptr, update = nullptr;
    bool extended = false;
    const bool has_update = caps.contains("configuration_updates");
    std::size_t update_limit = 2048, config_limit = 1800;
    if (has_update) {
      update = caps["configuration_updates"];
      require(update.is_object() && update.value("schema", "") == "transmitter-config-update/1" &&
          update.value("path", "") == "/api/v1/config/update" && update.value("method", "") == "POST" &&
          update.contains("modes") && update["modes"].is_array() &&
          std::all_of(update["modes"].begin(), update["modes"].end(), [](const Json &mode) { return mode.is_string(); }) &&
          std::find(update["modes"].begin(), update["modes"].end(), "merge") != update["modes"].end(),
          "unsupported_configuration_updates", 409);
      for (const auto key : {"max_update_bytes", "max_config_bytes"})
        require(update.contains(key) && update[key].is_number_integer() && update[key] > 0 &&
            update[key] <= 32768, "unsupported_configuration_updates", 409);
      update_limit = std::min<std::size_t>(2048, update["max_update_bytes"].get<std::size_t>());
      config_limit = std::min<std::size_t>(1800, update["max_config_bytes"].get<std::size_t>());
    }
    if (caps.contains("application_resources")) {
      extended = caps.contains("application_resources_v2");
      const auto &resources = caps[extended ? "application_resources_v2" : "application_resources"];
      require(extended ? resources == resource_descriptor(true) :
          (resources.is_object() && resources.value("schema", "") == "transmitter-application/1" && resources.value("read", "") == "/api/v1/application"), "unsupported_configuration_context",409);
      const auto r = read(extended ? "application/2" : "application"); application = parsed(r);
      require(application_shape(application, target) && application["schema"] == (extended ? "transmitter-application/2" : "transmitter-application/1"), "invalid_remote_response");
      require(r.etag == original.etag, "configuration_context_changed", 409);
      require(application["station"]["storage_healthy"] == true && application["hardware"]["storage_healthy"] == true &&
          application["hardware"]["application_error"].is_null(), "remote_busy_or_output_unresolved", 409);
    } else require(!has_update, "unsupported_configuration_context", 409);
    const auto check = read("config");
    require(check.etag == original.etag && parsed(check) == configuration, "revision_conflict", 412);
    require(method != "POST" || revision == original.etag, "revision_conflict", 412);
    auto config = configuration["config"];
    std::string strategy = config.is_null() ? "initial" : has_update ? "merge" : "legacy";
    Json reboot = application.is_object() && application.contains("reboot_required") &&
        application["reboot_required"].is_boolean() ? application["reboot_required"] : Json(nullptr);
    if (method == "POST") {
      const auto &patch = input["config"];
      std::string resource = extended ? "config/2" : "config", verb = "PUT";
      Json candidate;
      if (input["operation"] == "schedules") {
        require(!config.is_null(), "not_configured", 409);
        resource = "schedules"; candidate = patch;
      } else if (strategy == "merge") {
        resource = extended ? "config/update/2" : "config/update"; verb = "POST";
        candidate = {{"schema", "transmitter-config-update/1"}, {"target", target}, {"mode", "merge"}, {"config", patch}};
      } else if (strategy == "initial") {
        require(shape(patch, false, false) && patch["wifi"]["password"].is_string(), "initial_configuration_required", 400);
        if (!has_update)
          for (const auto key : {"pins", "allow_unqualified_frequency", "allow_non_amateur_frequency"})
            require(!patch.contains(key), "unsupported_legacy_field", 409);
        candidate = patch;
      } else {
        for (const auto key : {"pins", "allow_unqualified_frequency", "allow_non_amateur_frequency"})
          require(!patch.contains(key) || config.contains(key), "unsupported_legacy_field", 409);
        candidate = merge(config, patch);
      }
      const auto payload = candidate.dump();
      require(payload.size() <= (verb == "POST" ? update_limit : config_limit), "configuration_too_large", 413);
      attempted = true;
      auto r = exchange(resource, verb, payload, revision); success(r);
      auto result = parsed(r);
      require(result.contains("config") && shape(result["config"], false, true) && etag(r.etag), "management_outcome_unknown");
      config = result["config"]; original.etag = r.etag;
      reboot = result.contains("reboot_required") && result["reboot_required"].is_boolean()
          ? result["reboot_required"] : Json(nullptr);
      application = nullptr; // Pre-write active/saved observations are no longer current.
    }
    return {200, Json{{"schema", "wsprrypi-pico-settings/1"}, {"target", target}, {"config", config},
        {"update_strategy", strategy}, {"configuration_updates", update}, {"application", application},
        {"reboot_required", reboot}}.dump(), original.etag};
  } catch (const Rejected &e) {
    return {e.status, Json{{"error", {{"code", e.code}}}, {"mutation_attempted", attempted}}.dump(), {}};
  } catch (...) {
    return {502, Json{{"error", {{"code", attempted ? "management_outcome_unknown" : "invalid_remote_response"}}},
        {"mutation_attempted", attempted}}.dump(), {}};
  }
}
namespace {
const Json eligible = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,26,27,28};
const unsigned pairs[12][3] = {{0,0,1},{0,4,5},{0,8,9},{0,12,13},{0,16,17},{0,20,21},
    {1,2,3},{1,6,7},{1,10,11},{1,14,15},{1,18,19},{1,26,27}};
bool whole(const Json &j, std::uint64_t maximum) {
  return j.is_number_integer() && j >= 0 && j <= maximum;
}
bool station_valid(const Json &j, bool extended = false) {
  static const std::regex call("[A-Z0-9]+(/[A-Z0-9]+)*"), extended_call("([A-Z0-9]+(/[A-Z0-9]+)*|<[A-Z0-9]+(/[A-Z0-9]+)*>)"), grid("[A-R]{2}[0-9]{2}([A-X]{2})?");
  const Json powers = {0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60};
  return fields(j, {"callsign", "locator", "power_dbm"}) && j["callsign"].is_string() &&
      j["callsign"].get_ref<const std::string &>().size() >= 3 && j["callsign"].get_ref<const std::string &>().size() <= 12 &&
      std::regex_match(j["callsign"].get_ref<const std::string &>(), extended ? extended_call : call) && j["locator"].is_string() &&
      j["callsign"].get_ref<const std::string &>().find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos &&
      j["callsign"].get_ref<const std::string &>().find_first_of("0123456789") != std::string::npos &&
      std::regex_match(j["locator"].get_ref<const std::string &>(), grid) && j["power_dbm"].is_number_integer() &&
      std::find(powers.begin(), powers.end(), j["power_dbm"]) != powers.end();
}
bool plan_shape(const Json &p) {
  if (!fields(p, {"engine","rf_gp","i2c_pair","button_gp","amplifier_gp","lpf_gps","indicator","indicator_gp"},
      {"indicator_active_high"}) || (p["engine"] != "direct" && p["engine"] != "i2c") ||
      (p["indicator"] != "onboard_led" && p["indicator"] != "external" && p["indicator"] != "disabled") ||
      !p["lpf_gps"].is_array() || p["lpf_gps"].size() > 26 ||
      (p.contains("indicator_active_high") && !p["indicator_active_high"].is_boolean())) return false;
  for (const auto key : {"rf_gp","i2c_pair","button_gp","amplifier_gp","indicator_gp"})
    if (!p[key].is_null() && !whole(p[key], UINT32_MAX)) return false;
  for (const auto &gp : p["lpf_gps"]) if (!whole(gp, UINT32_MAX)) return false;
  return true;
}
// Independent interpretation of the frozen pin-allocation contract. Also checks
// the whole preview response, including partial ownership on a conflict.
Json review_plan(const Json &p) {
  Json result{{"valid",true},{"error",""},{"role",""},{"owner",""},{"gp",nullptr},
      {"owners",Json::object()},{"i2c_available",Json::array()},{"operational_supported",false},{"applied",false}};
  auto fail = [&](const char *error, const std::string &role) { result["valid"] = false; result["error"] = error; result["role"] = role; };
  auto claim = [&](const Json &gp, const std::string &role) {
    if (std::find(eligible.begin(),eligible.end(),gp) == eligible.end()) {
      fail("invalid_gp",role); result["gp"] = gp; return false;
    }
    const auto key = "GP" + std::to_string(gp.get<unsigned>());
    if (result["owners"].contains(key)) { fail("pin_conflict",role); result["gp"] = gp; result["owner"] = result["owners"][key]; return false; }
    result["owners"][key] = role; return true;
  };
  const bool direct = p["engine"] == "direct";
  if (direct ? p["rf_gp"].is_null() || !p["i2c_pair"].is_null() : !p["rf_gp"].is_null() || p["i2c_pair"].is_null())
    fail("engine_allocation","rf");
  if (result["valid"] == true && !p["rf_gp"].is_null()) (void)claim(p["rf_gp"],"rf");
  if (result["valid"] == true && !p["i2c_pair"].is_null()) {
    if (!whole(p["i2c_pair"],11)) fail("invalid_pair","i2c");
    else { const auto id = p["i2c_pair"].get<unsigned>(); if (claim(pairs[id][1],"i2c_sda")) (void)claim(pairs[id][2],"i2c_scl"); }
  }
  for (const auto &[key,role] : {std::pair{"button_gp","button"}, {"amplifier_gp","amplifier"}})
    if (result["valid"] == true && !p[key].is_null()) (void)claim(p[key],role);
  for (std::size_t i = 0; i < p["lpf_gps"].size() && result["valid"] == true; ++i)
    (void)claim(p["lpf_gps"][i], "lpf_" + std::to_string(i));
  if (result["valid"] == true) {
    if ((p["indicator"] == "external") == p["indicator_gp"].is_null()) fail("indicator_allocation","indicator");
    else if (!p["indicator_gp"].is_null()) (void)claim(p["indicator_gp"],"indicator");
  }
  for (const auto &pair : pairs) result["i2c_available"].push_back(
      !result["owners"].contains("GP" + std::to_string(pair[1])) && !result["owners"].contains("GP" + std::to_string(pair[2])));
  result["operational_supported"] = result["valid"] == true && direct && p["amplifier_gp"].is_null() && p["lpf_gps"].empty();
  return result;
}
bool plan_valid(const Json &p) { return plan_shape(p) && review_plan(p)["operational_supported"] == true; }
void shared_application(const Json &j, const Json &target) {
  require(j.dump().size() <= 4096 && application_shape(j,target), "invalid_remote_resource");
  const auto &s = j["station"], &h = j["hardware"], &c = j["capabilities"], &r = j["recurrence"];
  require((s["station"].is_null() || station_valid(s["station"], j["schema"] == "transmitter-application/2")) &&
      s["station"].is_null() == s["saved_generation"].is_null() &&
      (s["storage_healthy"] == true || s["station"].is_null()) &&
      h["saved"].is_null() == h["saved_generation"].is_null() &&
      (h["storage_healthy"] == true || h["saved"].is_null()) &&
      h["active"].is_null() == h["active_revision"].is_null(), "invalid_remote_resource");
  for (const auto key : {"saved","active"}) require(h[key].is_null() || plan_valid(h[key]), "invalid_remote_resource");
  require(c["station_write"] == true && c["hardware_write"] == true && c["max_update_bytes"] == 1024 &&
      c["max_config_bytes"] == 1800 && c["recurrence_authorities"] == Json::array({"member"}) &&
      c["autonomous_modes"] == Json::array({"wspr"}) && c["unavailable"] == Json::array({"recurrence_transfer","si5351",
        "amplifier","lpf","drive","reference_calibration","waveform_preferences","usb_json_management","plain_lan_json_management","ble_json_management"}),
      "unsupported_resource_capabilities",409);
  for (const auto &schedule : r["schedules"]) {
    require(whole(schedule["period_s"],86400) && schedule["period_s"] >= 120 && whole(schedule["phase_s"],86280), "invalid_remote_resource");
    const auto period = schedule["period_s"].get<unsigned>(), phase = schedule["phase_s"].get<unsigned>();
    require(86400 % period == 0 && period % 120 == 0 && phase < period && phase % 120 == 0, "invalid_remote_resource");
  }
  require(h["application_error"].is_null() || (h["pending_restart"] == true && j["reboot_required"] == true), "invalid_remote_resource");
  require(h["pending_restart"] != true || j["reboot_required"] == true, "invalid_remote_resource");
  require(s["storage_healthy"] == h["storage_healthy"] &&
      (s["saved_generation"].is_null() || h["saved_generation"].is_null() || s["saved_generation"] == h["saved_generation"]), "invalid_remote_resource");
  require(h["saved"].is_null() || h["active"].is_null() || h["saved"] == h["active"] || h["pending_restart"] == true, "invalid_remote_resource");
  require(r["enabled"] == false || !r["schedules"].empty(), "invalid_remote_resource");
  for (std::size_t i=0;i<r["schedules"].size();++i) for (std::size_t k=0;k<i;++k) {
    const auto &a=r["schedules"][i], &b=r["schedules"][k];
    const auto pa=a["period_s"].get<unsigned>(), pb=b["period_s"].get<unsigned>();
    const auto fa=a["phase_s"].get<unsigned>(), fb=b["phase_s"].get<unsigned>();
    for (unsigned slot=fa;slot<86400;slot+=pa) require(slot < fb || (slot-fb)%pb != 0,"invalid_remote_resource");
  }
}
void pin_choices(const Json &j) {
  require(fields(j,{"choices","active"}) && plan_valid(j["active"]), "invalid_remote_pins");
  const auto &c = j["choices"];
  require(fields(c,{"version","numbering","eligible_gps","i2c_pairs","indicator_choices","activation_supported","adapter_support"}) &&
      c["version"].is_number_integer() && c["version"] == 1 && c["numbering"] == "pico_gp" && c["eligible_gps"] == eligible &&
      c["indicator_choices"] == Json::array({"onboard_led","external","disabled"}) && c["activation_supported"] == true &&
      c["adapter_support"] == Json{{"rf","configurable_pio_gpio"},{"button","configurable_active_low_when_enabled"},
        {"indicator","configurable_shared_cues_and_tx"},{"amplifier",false},{"lpf",false},{"i2c_rf",false}} &&
      c["i2c_pairs"].is_array() && c["i2c_pairs"].size() == 12, "unsupported_pin_capabilities",409);
  for (unsigned i=0;i<12;++i) {
    const auto &p = c["i2c_pairs"][i];
    require(fields(p,{"id","controller","sda_gp","scl_gp"}), "invalid_remote_pins");
    for (const auto key : {"id","controller","sda_gp","scl_gp"}) require(p[key].is_number_integer(), "invalid_remote_pins");
    require(p == Json{{"id",i},{"controller",pairs[i][0]},{"sda_gp",pairs[i][1]},{"scl_gp",pairs[i][2]}}, "unsupported_pin_capabilities",409);
  }
  for (const auto &gp : c["eligible_gps"]) require(gp.is_number_integer(),"invalid_remote_pins");
}
}
PicoHttpResponse pico_resource_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &resource,
    const std::string &method, const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  bool attempted = false;
  try {
    const bool preview = resource == "pins/validate" && method == "POST";
    const bool write = (resource == "station" || resource == "hardware") && method == "PUT";
    require(preview || write || (method == "GET" && (resource == "application" || resource == "station" || resource == "hardware" || resource == "pins")),"not_found",404);
    require(identity(device) && identity(boot),"member_identity_unavailable",409);
    require(body.size() <= (preview ? 2048U : 1024U),"body_too_large",413);
    const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
    Json input;
    if (preview || write) {
      require(etag(revision),"revision_required",428);
      try { input = strict_browser_json(body); } catch (...) { throw Rejected{400,"invalid_json"}; }
      if (preview) require(plan_shape(input),"invalid_pin_plan",400);
      else {
        const bool station = resource == "station";
        require(fields(input,{"schema","target",station ? "station" : "pins"}) &&
            (station ? (input["schema"] == "transmitter-station/1" || input["schema"] == "transmitter-station/2") : input["schema"] == "transmitter-hardware/1"),"invalid_resource",400);
        require(input["target"] == target,"target_mismatch",409);
        require(station ? station_valid(input["station"], input["schema"] == "transmitter-station/2") : plan_valid(input["pins"]), station ? "invalid_station" : "unsupported_pin_adapter",400);
      }
    }
    const auto read = [&](const std::string &path, std::size_t limit = 4096) {
      auto r = exchange(path,"GET",{},{}); success(r);
      require(r.body.size() <= limit,"invalid_remote_resource"); (void)parsed(r); return r;
    };
    auto caps = parsed(read("capabilities",16384));
    require(caps.is_object() && caps.contains("api_version") && caps["api_version"].is_number_integer() && caps["api_version"] == 1,"unsupported_remote_api",409);
    if (caps.contains("management_carriers")) {
      require((caps["management_carriers"] == Json{{"schema","transmitter-management/1"},{"path","/api/v1/management"}} ||
               caps["management_carriers"] == Json{{"schema","transmitter-management/2"},{"path","/api/v1/management"}}),"unsupported_management_contract",409);
      const auto management = parsed(read("management",16384));
      require(valid_management_contract(management,carrier,device,boot,generation) &&
          management["schema"] == caps["management_carriers"]["schema"],"unsupported_management_contract",409);
    } else require(carrier == "engineering_mtls","unsupported_management_contract",409);
    require(caps.contains("application_resources"),"shared_resources_unavailable",409);
    const bool extended = caps.contains("application_resources_v2");
    require(caps[extended ? "application_resources_v2" : "application_resources"] == resource_descriptor(extended),"unsupported_resource_capabilities",409);
    const std::string application_path = extended ? "application/2" : "application";
    const auto resource_path = [&](const std::string &path) { return extended && path == "station" ? std::string("station/2") : path; };
    if (write && resource == "station") require(input["schema"] == (extended ? "transmitter-station/2" : "transmitter-station/1"),"unsupported_resource_schema",409);
    require(caps.contains("features") && caps["features"].is_object() && caps["features"].contains("consumer_setup_identity") &&
        caps["features"]["consumer_setup_identity"].is_boolean(),"invalid_remote_resource");
    for (const auto feature : {"application","station","hardware"})
      require(caps["features"].contains(feature) && caps["features"][feature] == true,"unsupported_resource_capabilities",409);
    const bool profile_owned = caps["features"]["consumer_setup_identity"] == true;
    if (caps.contains("management_carriers")) require(profile_owned == (carrier == "consumer_station" || carrier == "consumer_ap"),"invalid_remote_resource");
    const auto first = read(application_path); auto application = parsed(first); shared_application(application,target);
    require(application["schema"] == (extended ? "transmitter-application/2" : "transmitter-application/1"),"invalid_remote_resource");
    require(etag(first.etag),"invalid_remote_resource");
    for (const auto path : {"station","hardware"}) {
      const auto r = read(resource_path(path));
      require(r.etag == first.etag && parsed(r).dump() == application[path].dump(),"configuration_context_changed",409);
    }
    const auto pins = parsed(read("pins")); pin_choices(pins);
    const auto last = read(application_path); const auto after = parsed(last); shared_application(after,target);
    // Configuration consistency is distinct from mutable live recurrence/status.
    require(last.etag == first.etag && after["station"].dump() == application["station"].dump() &&
        after["hardware"]["saved"].dump() == application["hardware"]["saved"].dump() &&
        after["hardware"]["saved_generation"] == application["hardware"]["saved_generation"],"configuration_context_changed",409);
    application = after;
    if (method == "GET") return {200,Json{{"schema","wsprrypi-member-resources/1"},{"target",target},{"carrier",carrier},
        {"profile_generation",generation.empty() ? Json(nullptr) : Json(generation)}, {"profile_owned_station",profile_owned},
        {"application",application},{"pin_choices",pins["choices"]}}.dump(),last.etag};
    require(revision == last.etag,"revision_conflict",412);
    const auto status = parsed(read("status",16384));
    require(status.contains("job") && status["job"].is_object(),"invalid_remote_response");
    const auto &job = status["job"];
    require(job.contains("boot_id") && job["boot_id"] == boot,"target_mismatch",409);
    require(job.contains("output_active") && job["output_active"] == false && job.contains("owner_id") && job["owner_id"].is_null() &&
        job.contains("state") && (job["state"] == "empty" || job["state"] == "complete" || job["state"] == "aborted" || job["state"] == "missed"),"remote_busy_or_output_unresolved",409);
    if (write) {
      require(application["station"]["storage_healthy"] == true && application["hardware"]["storage_healthy"] == true,"storage_fault",503);
      require(application["hardware"]["application_error"].is_null(),"output_disable_failed",503);
      require(!application["hardware"]["saved"].is_null(),"not_configured",409);
      require(resource != "station" || !profile_owned,"consumer_setup_identity_required",409);
    }
    attempted = write;
    auto result = exchange(resource_path(resource),method,body,preview ? "" : revision); success(result);
    require(result.body.size() <= 4096,preview ? "invalid_pin_validation" : "management_outcome_unknown");
    const auto value = parsed(result);
    if (preview) {
      require(value.contains("gp") && (value["gp"].is_null() || value["gp"].is_number_integer()) && value.dump() == review_plan(input).dump(),"invalid_pin_validation");
      const auto confirmed = read(application_path);
      const auto observed = parsed(confirmed); shared_application(observed,target);
      require(confirmed.etag == revision && observed["station"].dump() == application["station"].dump() &&
          observed["hardware"].dump() == application["hardware"].dump(),"configuration_context_changed",409);
      return {200,value.dump(),{}};
    }
    require(etag(result.etag) && result.etag != revision && result.body.size() <= 4096,"management_outcome_unknown");
    const auto confirmed = read(application_path); const auto updated = parsed(confirmed);
    shared_application(updated,target);
    require(confirmed.etag == result.etag && updated[resource].dump() == value.dump(),"management_outcome_unknown");
    if (resource == "hardware" && !input["pins"].contains("indicator_active_high")) input["pins"]["indicator_active_high"] = true;
    require(resource == "station" ? value["station"].dump() == input["station"].dump() : value["saved"].dump() == input["pins"].dump(),"management_outcome_unknown");
    return result;
  } catch (const Rejected &e) {
    return {e.status,Json{{"error",{{"code",e.code}}},{"mutation_attempted",attempted}}.dump(),{}};
  } catch (...) {
    return {502,Json{{"error",{{"code",attempted ? "management_outcome_unknown" : "invalid_remote_resource"}}},{"mutation_attempted",attempted}}.dump(),{}};
  }
}
} // namespace wsprrypi
