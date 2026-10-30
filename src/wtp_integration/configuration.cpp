// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "configuration.hpp"
#include "consumer_radio.hpp"
#include "browser_api.hpp"
#include "control_contract.hpp"
#include "../WTP-Client/include/wtp/codec.hpp"
#include <algorithm>
#include <set>
#include <regex>
#include <openssl/sha.h>
namespace wsprrypi {
namespace {
using Json = nlohmann::json;
bool plan_shape(const Json &p);
bool plan_valid(const Json &p);
Json normalized_plan(Json plan, bool amplifier = true) {
  if (plan.is_object()) {
    if (!plan.contains("amplifier_active_high")) plan["amplifier_active_high"]=amplifier;
    if (!plan.contains("indicator_active_high")) plan["indicator_active_high"]=true;
  }
  return plan;
}
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
  if (!(partial ? fields(j, {}, {"enabled", "station", "wifi", "schedules", "pins",
        "expires_utc_s", "allow_unqualified_frequency", "allow_non_amateur_frequency", "gpio_drive_ma"}) && !j.empty()
      : fields(j, {"enabled", "station", "wifi", "schedules"}, {"pins",
        "expires_utc_s", "allow_unqualified_frequency", "allow_non_amateur_frequency", "gpio_drive_ma"}))) return false;
  if (j.contains("gpio_drive_ma") && (!j["gpio_drive_ma"].is_number_integer() || !(j["gpio_drive_ma"] == 2 || j["gpio_drive_ma"] == 4 || j["gpio_drive_ma"] == 8 || j["gpio_drive_ma"] == 12))) return false;
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
    if (!fields(p, {"engine", "rf_gp", "i2c_pair", "button_gp", "amplifier_gp", "lpf_gps", "indicator", "indicator_gp"}, {"indicator_active_high", "amplifier_active_high"}) ||
        !p["engine"].is_string() || !p["indicator"].is_string() || !p["lpf_gps"].is_array()) return false;
    if ((!partial && !p.contains("amplifier_active_high")) ||
        (p.contains("amplifier_active_high") && !p["amplifier_active_high"].is_boolean())) return false;
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
bool supported_descriptor(const Json &value, std::string_view name) {
  const auto expected=Json::parse(control_descriptor(name));
  const auto policies=Json::parse(control_descriptor_policy(name));
  const auto runtime=Json::parse(control_descriptor_runtime(name));
  if (expected.empty() || !value.is_object() || value.size()!=expected.size()+runtime.size()) return false;
  for (const auto &[key,reference] : expected.items()) {
    if (!value.contains(key)) return false;
    const auto &actual=value[key];
    if (!policies.contains(key)) { if (actual!=reference) return false; continue; }
    const auto &policy=policies[key];
    if (policy["type"]=="boolean") { if (!actual.is_boolean()) return false; }
    else if (policy["type"]=="integer") {
      if (!actual.is_number_integer()) return false;
      if (policy.contains("minimum") && actual<policy["minimum"]) return false;
      if (policy.contains("maximum") && actual>policy["maximum"]) return false;
      for (const auto &[bound,minimum] : {std::pair{"minimumFromValue",true},{"maximumFromValue",false}}) {
        if (!policy.contains(bound)) continue;
        const auto &other=value.at(policy[bound].get<std::string>());
        if (!other.is_number_integer() || (minimum ? actual<other : actual>other)) return false;
      }
      if (policy.contains("allowedValuesFromValue")) {
        const auto &allowed=value.at(policy["allowedValuesFromValue"].get<std::string>());
        if (!allowed.is_array() || std::find(allowed.begin(),allowed.end(),actual)==allowed.end()) return false;
      }
    } else if (policy["type"]=="array") {
      if (!actual.is_array() || actual.size()<policy["minimumItems"].get<unsigned>() || actual.size()>policy["maximumItems"].get<unsigned>()) return false;
      std::set<std::string> unique;
      const auto &allowed=expected.at(policy["allowedValuesFromDescriptor"].get<std::string>());
      for (const auto &entry:actual)
        if (!entry.is_number_integer() || std::find(allowed.begin(),allowed.end(),entry)==allowed.end() || !unique.insert(entry.dump()).second) return false;
    } else return false;
  }
  for (const auto &[key,policy] : runtime.items()) {
    if (!value.contains(key)) return false;
    if (policy.contains("enum")) {
      if (std::find(policy["enum"].begin(),policy["enum"].end(),value[key])==policy["enum"].end()) return false;
    } else if (policy.value("type","")=="string") {
      if (!value[key].is_string() || value[key].get_ref<const std::string&>().size()>policy.value("maxLength",128U) ||
          !std::regex_match(value[key].get_ref<const std::string&>(),std::regex(policy.at("pattern").get<std::string>()))) return false;
    } else return false;
  }
  return true;
}
bool drive_shape(const Json &v, const Json &target) {
  if (!fields(v,{"schema","target","saved","boot_active","pad_readback","pending_restart",
        "application_error","rf_gp","execution_engine","storage_healthy"}) ||
      v["schema"] != "gpio-drive" || v["target"] != target ||
      !v["pending_restart"].is_boolean() || !v["storage_healthy"].is_boolean() ||
      !v["execution_engine"].is_string() || v["execution_engine"].get_ref<const std::string&>().size()>128)
    return false;
  for (const auto key : {"saved","boot_active","pad_readback"})
    if (!(v[key].is_null() || (v[key].is_number_integer() && (v[key]==2 || v[key]==4 || v[key]==8 || v[key]==12)))) return false;
  if (!(v["rf_gp"].is_null() || (v["rf_gp"].is_number_integer() &&
        ((v["rf_gp"]>=0 && v["rf_gp"]<=22) || (v["rf_gp"]>=26 && v["rf_gp"]<=28))))) return false;
  return v["application_error"].is_null() || v["application_error"]=="pad_application_failed" ||
      v["application_error"]=="output_disable_failed";
}
bool drive_policy_value(const Json &value, const Json &caps) {
  if (!caps.contains("gpio_drive") || !supported_descriptor(caps["gpio_drive"],"gpio_drive")) return false;
  const auto &values=caps["gpio_drive"]["values"];
  return value.is_number_integer() && std::find(values.begin(),values.end(),value)!=values.end();
}
bool drive_policy_fields(const Json &value, const Json &caps, bool nullable = false) {
  return !value.contains("gpio_drive_ma") || (nullable && value["gpio_drive_ma"].is_null()) ||
      drive_policy_value(value["gpio_drive_ma"],caps);
}
bool drive_policy_observation(const Json &value, const Json &caps) {
  for (const auto key : {"saved","boot_active","pad_readback"})
    if (!value.at(key).is_null() && !drive_policy_value(value.at(key),caps)) return false;
  return true;
}
bool application_shape(const Json &j, const Json &target, bool = false) {
  const bool extended = true;
  if (extended) {
    if (!j.contains("wspr_plan")) return false;
    const auto &p=j["wspr_plan"];
    if (!p.is_null()) {
      if (!fields(p,{"schema","supported","frame_types","frames","slot_separation_s","rationale","type2_ambiguous","alternate_callsign"}) ||
          p["schema"] != "wspr-plan" || !p["supported"].is_boolean() || !p["type2_ambiguous"].is_boolean() ||
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
      j["schema"] != "application" || j["target"] != target || !j["reboot_required"].is_boolean()) return false;
  const auto &s = j["station"], &h = j["hardware"], &r = j["recurrence"], &c = j["capabilities"];
  if (extended && s.is_object() && s.contains("station") &&
      j["wspr_plan"].is_null() != s["station"].is_null()) return false;
  if (!fields(s, {"schema", "target", "station", "saved_generation", "storage_healthy"}) ||
      s["schema"] != "station" || s["target"] != target || !s["storage_healthy"].is_boolean() ||
      !(s["saved_generation"].is_null() || decimal(s["saved_generation"])) ||
      !(s["station"].is_null() || (fields(s["station"], {"callsign", "locator", "power_dbm"}) &&
          shape(Json{{"station", s["station"]}}, true, true)))) return false;
  if (!fields(h, {"schema", "target", "saved", "saved_generation", "active", "active_revision", "pending_restart",
          "execution_engine", "storage_healthy", "application_error", "gpio_drive"}) ||
      h["schema"] != "hardware" || h["target"] != target || !h["pending_restart"].is_boolean() ||
      !h["storage_healthy"].is_boolean() || !h["execution_engine"].is_string() || h["execution_engine"].empty() ||
      h["execution_engine"].get_ref<const std::string &>().size() > 128 ||
      !(h["saved_generation"].is_null() || decimal(h["saved_generation"])) ||
      !(h["application_error"].is_null() || h["application_error"] == "output_disable_failed")) return false;
  if (!drive_shape(h["gpio_drive"],target)) return false;
  for (const auto key : {"saved", "active"})
    if (!h[key].is_null() && !shape(Json{{"pins", h[key]}}, true, true)) return false;
  if (!h["active_revision"].is_null()) {
    if (!h["active_revision"].is_string()) return false;
    const auto &revision = h["active_revision"].get_ref<const std::string &>();
    if (revision.size() != 64 || !std::all_of(revision.begin(), revision.end(), [](char value) {
          return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); })) return false;
  }
  if (!fields(r, {"authority", "enabled", "suspended", "expires_utc_s", "schedules", "schedule_dial_frequency_nhz"}) ||
      r["authority"] != "member" || !r["enabled"].is_boolean() || !r["suspended"].is_boolean() ||
      !decimal(r["expires_utc_s"]) || !decimal(r["schedule_dial_frequency_nhz"]) ||
      !shape(Json{{"schedules", r["schedules"]}}, true, true)) return false;
  if (!fields(c, {"application_read", "station_write", "hardware_write", "max_update_bytes",
          "max_config_bytes", "hardware_activation", "station_encoding", "station_power_meaning", "recurrence_authorities",
          "autonomous_modes", "unavailable"}) ||
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
Json resource_value(Json value) {
  // Contract binds the logical root; embedded application resources omit it.
  value.erase("contract");
  return value;
}
Json control_document(const std::string &body) {
  auto value = strict_browser_json(body);
  require(value.is_object() && value.value("contract", "") == "Wsprry-Control/1", "unsupported_control_contract", 409);
  value.erase("contract");
  return value;
}
bool capabilities_shape(const Json &caps) {
  const auto shape=Json::parse(control_capabilities_shape());
  if (!caps.is_object() || caps.size()>shape["properties"].size()) return false;
  for (const auto &field:shape["required"]) {
    const auto &name=field.get_ref<const std::string&>();
    if (name!="contract" && !caps.contains(name)) return false;
  }
  for (const auto &[name,value]:caps.items()) {
    if (!shape["properties"].contains(name)) return false;
    const auto &rule=shape["properties"][name];
    const auto type=rule.value("type","");
    if ((type=="object" && !value.is_object()) || (type=="boolean" && !value.is_boolean()) ||
        (type=="integer" && !value.is_number_integer())) return false;
    if (type=="integer" && ((rule.contains("minimum") && value<rule["minimum"]) ||
        (rule.contains("maximum") && value>rule["maximum"]))) return false;
  }
  const Json wtp_response={{"type","response"},{"protocol","WTP/1"},
    {"session_id",std::string(32,'1')},{"request_id",std::string(32,'2')},
    {"op","CAPS"},{"ok",true},{"body",caps.at("wtp")}};
  if (!wtp::decode(wtp_response.dump())) return false;
  const auto &features=caps.at("features");
  if (features.size()>shape["properties"]["features"]["maxProperties"].get<unsigned>()) return false;
  for (const auto &field:shape["properties"]["features"]["required"])
    if (!features.contains(field.get<std::string>())) return false;
  for (const auto &[name,value]:features.items()) {
    if (!std::regex_match(name,std::regex(shape["properties"]["features"]["propertyNames"]["pattern"].get<std::string>())) ||
        !value.is_boolean()) return false;
  }
  return true;
}
Json parsed(const PicoHttpResponse &r, bool capabilities = false) {
  require(r.body.size() <= 16384, "invalid_remote_response");
  try { auto value = strict_browser_json(r.body); require(value.is_object() && (value.contains("error") || (value.contains("contract") && value["contract"] == "Wsprry-Control/1")), "unsupported_remote_contract", 409); value.erase("contract"); if (capabilities) require(capabilities_shape(value),"invalid_remote_capabilities"); return value; }
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
      try { input = control_document(body); } catch (...) { throw Rejected{400, "invalid_json"}; }
      require(fields(input, {"schema", "target", "operation", "config"}) &&
          input["schema"] == "pico-settings" &&
          (input["operation"] == "save" || input["operation"] == "schedules") &&
          shape(input["config"], true, false), "invalid_configuration_edit", 400);
      require(input["target"] == target, "target_mismatch", 409);
      if (input["operation"] == "schedules")
        require(fields(input["config"], {"schedules"}), "invalid_configuration_edit", 400);
    }
    const auto read = [&](const std::string &resource) {
      const auto r = exchange(resource, "GET", {}, {}); success(r); return r;
    };
    const auto caps = parsed(read("capabilities"),true);
    require(caps.is_object(),"unsupported_remote_contract",409);
    require(caps.contains("application_resources"),
        "unsupported_configuration_context",409);
    if (caps.contains("management_carriers")) {
      require(valid_management_descriptor(caps["management_carriers"], carrier),
              "unsupported_management_contract", 409);
      const auto management = strict_browser_json(read("management").body);
      require(valid_management_contract(management, carrier, device, boot, generation) &&
          management["schema"] == caps["management_carriers"]["schema"],
          "unsupported_management_contract", 409);
    } else throw Rejected{409,"unsupported_management_contract"};
    if (method == "POST") require(drive_policy_fields(input["config"],caps),"invalid_gpio_drive",400);
    auto original = read("config");
    auto configuration = parsed(original);
    require(fields(configuration, {"config"}) && (configuration["config"].is_null() ||
        shape(configuration["config"], false, true)) && etag(original.etag));
    if (!configuration["config"].is_null()) require(drive_policy_fields(configuration["config"],caps),"invalid_remote_resource");
    const auto status = parsed(read("status"));
    require(status.contains("job") && status["job"].is_object(), "invalid_remote_response");
    const auto &job = status["job"];
    require(job.value("boot_id", "") == boot, "target_mismatch", 409);
    require(job.contains("output_active") && job["output_active"] == false &&
        job.contains("owner_id") && job["owner_id"].is_null() && job.contains("state") &&
        (job["state"] == "empty" || job["state"] == "complete" || job["state"] == "aborted" || job["state"] == "missed"),
        "remote_busy_or_output_unresolved", 409);
    Json application = nullptr, update = nullptr;
    require(caps.contains("configuration_updates"), "unsupported_configuration_updates", 409);
    const bool has_update = true;
    std::size_t update_limit = 2048, config_limit = 1800;
    if (has_update) {
      update = caps["configuration_updates"];
      require(supported_descriptor(update,"configuration_updates"), "unsupported_configuration_updates", 409);
      update_limit = std::min<std::size_t>(2048, update["max_update_bytes"].get<std::size_t>());
      config_limit = std::min<std::size_t>(1800, update["max_config_bytes"].get<std::size_t>());
    }
    if (caps.contains("application_resources")) {
      const auto &resources = caps["application_resources"];
      require(supported_descriptor(resources,"application_resources"), "unsupported_configuration_context",409);
      const auto r = read("application"); application = parsed(r);
      require(application_shape(application, target, carrier == "usb_physical") && application["schema"] == "application" &&
          drive_policy_observation(application["hardware"]["gpio_drive"],caps), "invalid_remote_response");
      require(r.etag == original.etag, "configuration_context_changed", 409);
      require(application["station"]["storage_healthy"] == true && application["hardware"]["storage_healthy"] == true &&
          application["hardware"]["application_error"].is_null(), "remote_busy_or_output_unresolved", 409);
    } else require(!has_update, "unsupported_configuration_context", 409);
    const auto check = read("config");
    require(check.etag == original.etag && parsed(check) == configuration, "revision_conflict", 412);
    require(method != "POST" || revision == original.etag, "revision_conflict", 412);
    auto config = configuration["config"];
    const std::string strategy = "merge";
    Json reboot = application.at("reboot_required");
    Json expected_config_pins;
    if (method == "POST" && input["config"].contains("pins")) {
      expected_config_pins=normalized_plan(input["config"]["pins"],config.is_null()?true:config.at("pins").value("amplifier_active_high",true));
      require(plan_valid(expected_config_pins),"invalid_pin_plan",400);
    }
    if (method == "POST") {
      const auto &patch = input["config"];
      const std::string resource = "config/update", verb = "POST";
      Json candidate{{"contract","Wsprry-Control/1"},{"schema","config-update"},
          {"target",target},{"mode","merge"},{"config",patch}};
      const auto payload = candidate.dump();
      require(payload.size() <= (verb == "POST" ? update_limit : config_limit), "configuration_too_large", 413);
      attempted = true;
      auto r = exchange(resource, verb, payload, revision); success(r);
      auto result = parsed(r);
      require(result.contains("config") && shape(result["config"], false, true) && etag(r.etag), "management_outcome_unknown");
      require(drive_policy_fields(result["config"],caps),"management_outcome_unknown");
      if (!expected_config_pins.is_null()) require(result["config"].at("pins").dump()==expected_config_pins.dump(),"management_outcome_unknown");
      config = result["config"]; original.etag = r.etag;
      reboot = result.contains("reboot_required") && result["reboot_required"].is_boolean()
          ? result["reboot_required"] : Json(nullptr);
      application = nullptr; // Pre-write active/saved observations are no longer current.
    }
    return {200, Json{{"contract","Wsprry-Control/1"},{"schema", "pico-settings"}, {"target", target}, {"config", config},
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
      {"indicator_active_high", "amplifier_active_high"}) || (p["engine"] != "direct" && p["engine"] != "i2c") ||
      (p["indicator"] != "onboard_led" && p["indicator"] != "external" && p["indicator"] != "disabled") ||
      !p["lpf_gps"].is_array() || p["lpf_gps"].size() > 26 ||
      (p.contains("indicator_active_high") && !p["indicator_active_high"].is_boolean()) ||
      !p.contains("amplifier_active_high") || !p["amplifier_active_high"].is_boolean()) return false;
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
  result["operational_supported"] = result["valid"] == true && direct && p["lpf_gps"].empty();
  return result;
}
bool plan_valid(const Json &p) { return plan_shape(p) && review_plan(p)["operational_supported"] == true; }
void shared_application(const Json &j, const Json &target, bool usb = false) {
  require(j.dump().size() <= 4096 && application_shape(j,target,usb), "invalid_remote_resource");
  const auto &s = j["station"], &h = j["hardware"], &c = j["capabilities"], &r = j["recurrence"];
  require((s["station"].is_null() || station_valid(s["station"], j["schema"] == "application")) &&
      s["station"].is_null() == s["saved_generation"].is_null() &&
      (s["storage_healthy"] == true || s["station"].is_null()) &&
      h["saved"].is_null() == h["saved_generation"].is_null() &&
      (h["storage_healthy"] == true || h["saved"].is_null()) &&
      h["active"].is_null() == h["active_revision"].is_null(), "invalid_remote_resource");
  for (const auto key : {"saved","active"}) require(h[key].is_null() || plan_valid(h[key]), "invalid_remote_resource");
  require(c["unavailable"].size() <= 64,"invalid_resource_capabilities");
  std::set<std::string> unavailable;
  for (const auto &entry : c["unavailable"]) {
    const auto &name=entry.get_ref<const std::string&>();
    require(!name.empty() && name.size()<=80 && unavailable.insert(name).second,"invalid_resource_capabilities");
  }
  require(!unavailable.contains("amplifier") && !unavailable.contains("drive"),"unsupported_resource_capabilities",409);
  require(c["station_write"] == true && c["hardware_write"] == true && c["max_update_bytes"] > 0 && c["max_update_bytes"] <= 1024 &&
      c["max_config_bytes"] > 0 && c["max_config_bytes"] <= 1800 && c["recurrence_authorities"] == Json::array({"member"}) &&
      c["autonomous_modes"] == Json::array({"wspr"}),
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
  require(fields(c,{"schema","numbering","eligible_gps","i2c_pairs","indicator_choices","activation_supported","adapter_support"}) &&
      c["schema"] == "pins" && c["numbering"] == "pico_gp" && c["eligible_gps"] == eligible &&
      c["indicator_choices"] == Json::array({"onboard_led","external","disabled"}) && c["activation_supported"] == true &&
      c["adapter_support"] == Json{{"rf","configurable_pio_gpio"},{"button","configurable_active_low_when_enabled"},
        {"indicator","configurable_shared_cues_and_tx"},{"amplifier",true},{"lpf",false},{"i2c_rf",false}} &&
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
namespace {
bool progress_fields(const Json &j) {
  return fields(j, {}, {"callsign","locator","power_dbm","enabled","schedules",
      "expires_utc_s","pins","frequency_policy","wifi","ntp_ipv4","gpio_drive_ma"});
}
bool progress_outcomes(const Json &j) {
  if (!progress_fields(j)) return false;
  for (const auto &v : j) if (v != "saved" && v != "cleared" && v != "invalid") return false;
  return true;
}
bool progress_station(const Json &j) {
  if (!fields(j,{"callsign","locator","power_dbm"})) return false;
  for (const auto &[key,value] : j.items()) {
    if (value.is_null()) continue;
    Json candidate{{"callsign","AA0NT"},{"locator","EM18"},{"power_dbm",0}};
    candidate[key]=value;
    if (!station_valid(candidate,true)) return false;
  }
  return true;
}
std::string progress_receipt(const std::string &body) {
  unsigned char bytes[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(body.data()),body.size(),bytes);
  static constexpr char hex[]="0123456789abcdef";
  std::string result;
  for (const auto byte : bytes) { result+=hex[byte>>4]; result+=hex[byte&15]; }
  return result;
}
Json progress_readback(const PicoHttpResponse &response, const Json &target, bool acknowledgement = false) {
  success(response);
  require(response.body.size()<=4096 && etag(response.etag),"invalid_remote_progress");
  const auto v=parsed(response);
  require(fields(v,{"schema","target","draft_revision","config_revision","profile_revision","fields",
      "last_receipt","last_outcomes","applied_station","station","station_complete","complete",
      "apply_domains","wspr_plan","schedule_enabled","schedule_suspended","matches_applied"},
      acknowledgement ? std::initializer_list<const char *>{"outcomes","request_id"} : std::initializer_list<const char *>{}) &&
      v["schema"]=="progress" && v["target"]==target &&
      decimal(v["draft_revision"]) && decimal(v["config_revision"]) && decimal(v["profile_revision"]) &&
      progress_fields(v["fields"]) && v["fields"].dump().size()<=1400 && progress_outcomes(v["last_outcomes"]) &&
      progress_station(v["station"]) && (v["applied_station"].is_null() || station_valid(v["applied_station"],true)) &&
      fields(v["apply_domains"],{"station","network","radio","all","drive"}),"invalid_remote_progress");
  for (const auto key : {"station_complete","complete","schedule_enabled","schedule_suspended","matches_applied"})
    require(v[key].is_boolean(),"invalid_remote_progress");
  for (const auto &value : v["apply_domains"]) require(value.is_boolean(),"invalid_remote_progress");
  require(v["last_receipt"].is_null() || (v["last_receipt"].is_string() &&
      std::regex_match(v["last_receipt"].get<std::string>(),std::regex("[0-9a-f]{64}"))),"invalid_remote_progress");
  require(v["station_complete"]==station_valid(v["station"],true),"invalid_remote_progress");
  for (const auto &[key,value] : v["fields"].items()) {
    if (key=="callsign" || key=="locator" || key=="power_dbm") {
      require(value==v["station"][key],"invalid_remote_progress");
    }
    if (value.is_null()) continue;
    if (key=="callsign" || key=="locator" || key=="power_dbm") {
      auto station=v["station"]; station[key]=value;
      require(progress_station(station) && value==v["station"][key],"invalid_remote_progress");
    } else if (key=="enabled") require(value.is_boolean(),"invalid_remote_progress");
    else if (key=="wifi") require(fields(value,{"ssid","password"}) && value["ssid"].is_string() &&
        value["ssid"].get_ref<const std::string &>().size()<=32 && value["password"].is_null(),"invalid_remote_progress");
    else if (key=="ntp_ipv4") require(value.is_string() && value.get_ref<const std::string &>().size()<=253,"invalid_remote_progress");
    else if (key=="expires_utc_s") require(whole(value,2147483647),"invalid_remote_progress");
    else if (key=="gpio_drive_ma") require(value.is_number_integer() && (value==2 || value==4 || value==8 || value==12),"invalid_remote_progress");
    else if (key=="pins") require(plan_valid(value),"invalid_remote_progress");
    else if (key=="frequency_policy") require(fields(value,{"allow_unqualified_frequency","allow_non_amateur_frequency"}) &&
        value["allow_unqualified_frequency"].is_boolean() && value["allow_non_amateur_frequency"].is_boolean() &&
        (value["allow_non_amateur_frequency"]==false || value["allow_unqualified_frequency"]==true),"invalid_remote_progress");
    else {
      require(value.is_array() && value.size()<=8,"invalid_remote_progress");
      for (const auto &row : value) require(fields(row,{"period_s","phase_s"}) &&
          whole(row["period_s"],86400) && row["period_s"]>=120 && whole(row["phase_s"],86399),"invalid_remote_progress");
    }
  }
  require(v["wspr_plan"].is_null() || (fields(v["wspr_plan"],{"schema","supported","frame_types","frames",
      "slot_separation_s","rationale","type2_ambiguous","alternate_callsign"}) &&
      v["wspr_plan"]["schema"]=="wspr-plan" && v["wspr_plan"]["supported"].is_boolean() &&
      v["wspr_plan"]["frame_types"].is_array() && whole(v["wspr_plan"]["frames"],2) &&
      v["wspr_plan"]["frames"]==v["wspr_plan"]["frame_types"].size() &&
      v["wspr_plan"]["slot_separation_s"].is_number_integer() && v["wspr_plan"]["slot_separation_s"]==120 &&
      v["wspr_plan"]["rationale"].is_string() && v["wspr_plan"]["rationale"].get_ref<const std::string &>().size()<=512 &&
      v["wspr_plan"]["type2_ambiguous"].is_boolean() &&
      (v["wspr_plan"]["alternate_callsign"].is_null() || v["wspr_plan"]["alternate_callsign"].is_string())),"invalid_remote_progress");
  if (acknowledgement) require(v.contains("outcomes") && v.contains("request_id") && progress_outcomes(v["outcomes"]) &&
      v["request_id"].is_string() && identity(v["request_id"].get<std::string>()),"invalid_remote_progress");
  return v;
}
} // namespace
PicoHttpResponse pico_progress_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &resource,
    const std::string &method, const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  bool attempted=false;
  try {
    const bool update=resource=="progress/update" && method=="POST";
    const bool apply=resource=="progress/apply" && method=="POST";
    require(update || apply || (resource=="progress" && method=="GET"),"not_found",404);
    require(identity(device) && identity(boot),"member_identity_unavailable",409);
    require(body.size()<=(apply?512U:2048U),"body_too_large",413);
    const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
    Json input;
    if (method=="POST") {
      require(etag(revision),"revision_required",428);
      try { input=control_document(body); } catch (...) { throw Rejected{400,"invalid_json"}; }
      require(input.is_object() && input.contains("schema") && input["schema"]=="progress" &&
          input.contains("target") && input["target"]==target,"invalid_progress",400);
      if (update) require(fields(input,{"schema","target","request_id","fields"}) &&
          input["request_id"].is_string() && identity(input["request_id"].get<std::string>()) &&
          progress_fields(input["fields"]) && !input["fields"].empty(),"invalid_progress",400);
      else require(fields(input,{"schema","target","application"}) &&
          fields(input["application"],{"draft_revision","domain"}) && decimal(input["application"]["draft_revision"]) &&
          input["application"]["draft_revision"]!="0" && (input["application"]["domain"]=="station" ||
          input["application"]["domain"]=="network" || input["application"]["domain"]=="radio" ||
          input["application"]["domain"]=="all" || input["application"]["domain"]=="drive"),"invalid_progress_application",400);
    }
    const auto read=[&](const std::string &path) { auto r=exchange(path,"GET",{},{}); success(r); return r; };
    const auto caps=parsed(read("capabilities"),true);
    require(caps.is_object(),"unsupported_remote_contract",409);
    require(caps.contains("saved_progress"),"saved_progress_unavailable",409);
    require(supported_descriptor(caps["saved_progress"],"saved_progress"),"unsupported_saved_progress",409);
    require(method=="GET" || body.size()<=caps["saved_progress"]["max_bytes"].get<std::size_t>(),"body_too_large",413);
    if (caps.contains("management_carriers")) {
      require(valid_management_descriptor(caps["management_carriers"],carrier),
          "unsupported_management_contract",409);
      const auto management=strict_browser_json(read("management").body);
      require(valid_management_contract(management,carrier,device,boot,generation) &&
          management["schema"]==caps["management_carriers"]["schema"],"unsupported_management_contract",409);
    } else require(carrier=="engineering_mtls","unsupported_management_contract",409);
    const auto before=read("progress"); const auto old=progress_readback(before,target);
    require(drive_policy_fields(old["fields"],caps,true),"invalid_remote_progress");
    if (update) require(drive_policy_fields(input["fields"],caps,true),"invalid_gpio_drive",400);
    if (method=="GET") return before;
    require(before.etag==revision,"revision_conflict",412);
    Json expected_pins;
    if (update && input["fields"].contains("pins") && !input["fields"]["pins"].is_null()) {
      expected_pins=input["fields"]["pins"];
      require(expected_pins.is_object(),"invalid_pin_plan",400);
      if (!expected_pins.contains("amplifier_active_high")) {
        bool polarity=true;
        if (old["fields"].contains("pins") && !old["fields"]["pins"].is_null())
          polarity=old["fields"]["pins"].at("amplifier_active_high").get<bool>();
        else {
          const auto saved_response=read("config");const auto saved=parsed(saved_response);
          require(fields(saved,{"config"}) && (saved["config"].is_null() || shape(saved["config"],false,true)),"invalid_remote_resource");
          require(saved_response.etag==before.etag,"configuration_context_changed",409);
          if (!saved["config"].is_null()) polarity=saved["config"].at("pins").value("amplifier_active_high",true);
        }
        expected_pins["amplifier_active_high"]=polarity;
      }
      if (!expected_pins.contains("indicator_active_high")) expected_pins["indicator_active_high"]=true;
      require(plan_valid(expected_pins),"invalid_pin_plan",400);
    }
    if (apply) {
      const auto domain=input["application"]["domain"].get<std::string>();
      require(input["application"]["draft_revision"]==old["draft_revision"] && old["apply_domains"][domain]==true,
          "invalid_progress_application",409);
      if (carrier!="usb_physical" && (domain=="station" || domain=="network" || domain=="all"))
        require(caps.contains("features") && caps["features"].contains("consumer_setup_identity") &&
            caps["features"]["consumer_setup_identity"].is_boolean() && caps["features"]["consumer_setup_identity"]==false,
            "consumer_setup_identity_required",409);
    }
    const auto status=parsed(read("status")); const auto &job=status.at("job");
    require(job.at("boot_id")==boot && job.at("output_active")==false && job.at("owner_id").is_null() &&
        (job.at("state")=="empty" || job.at("state")=="complete" || job.at("state")=="aborted" || job.at("state")=="missed"),
        "remote_busy_or_output_unresolved",409);
    // Consumer-Radio embeds parsed JSON inside an encrypted envelope. Freeze
    // identical canonical bytes for every carrier before hashing the receipt.
    input["contract"] = control_contract;
    const auto wire_body=input.dump();
    require(wire_body.size()<=(apply?512U:2048U),"body_too_large",413);
    attempted=true; const auto result=exchange(resource,"POST",wire_body,revision); success(result);
    const auto after_response=read("progress"); const auto after=progress_readback(after_response,target);
    require(drive_policy_fields(after["fields"],caps,true),"management_outcome_unknown");
    require(result.etag==after_response.etag,"management_outcome_unknown");
    if (update) {
      const auto ack=progress_readback(result,target,true);
      require(ack["request_id"]==input["request_id"] && ack["outcomes"].size()==input["fields"].size(),"management_outcome_unknown");
      for (const auto key : {"applied_station","config_revision","profile_revision","schedule_enabled","schedule_suspended"})
        require(after[key]==old[key],"management_outcome_unknown");
      for (const auto &[key,value] : old["fields"].items()) if (!input["fields"].contains(key))
        require(after["fields"].contains(key) && after["fields"][key]==value,"management_outcome_unknown");
      for (const auto &[key,value] : after["fields"].items()) {
        (void)value;
        require(input["fields"].contains(key) || old["fields"].contains(key),"management_outcome_unknown");
      }
      bool accepted=false;
      for (const auto &[key,value] : input["fields"].items()) {
        require(ack["outcomes"].contains(key),"management_outcome_unknown");
        const auto &outcome=ack["outcomes"][key];
        if (outcome=="invalid") {
          require(after["fields"].contains(key)==old["fields"].contains(key) &&
              (!old["fields"].contains(key) || after["fields"][key]==old["fields"][key]),"management_outcome_unknown");
          continue;
        }
        accepted=true;
        require(outcome==(value.is_null()?"cleared":"saved") && after["fields"].contains(key),"management_outcome_unknown");
        if (key=="wifi" && !value.is_null()) require(after["fields"][key]==Json{{"ssid",value.at("ssid")},{"password",nullptr}},"management_outcome_unknown");
        else if (key=="pins" && !value.is_null()) {
          require(after["fields"][key]==expected_pins,"management_outcome_unknown");
        } else require(after["fields"][key]==value,"management_outcome_unknown");
      }
      auto projection=ack; projection.erase("outcomes"); projection.erase("request_id");
      require(projection==after,"management_outcome_unknown");
      if (accepted) require(after["last_receipt"]==progress_receipt(wire_body) && after["last_outcomes"]==ack["outcomes"] &&
          std::stoull(after["draft_revision"].get<std::string>())>std::stoull(old["draft_revision"].get<std::string>()) &&
          after_response.etag!=revision,"management_outcome_unknown");
      else require(after==old && after_response.etag==revision,"management_outcome_unknown");
      return result;
    }
    const auto applied=parsed(result);
    require(fields(applied,{"config"},{"reboot_required"}) && shape(applied["config"],false,true) &&
        (!applied.contains("reboot_required") || applied["reboot_required"].is_boolean()),"management_outcome_unknown");
    require(drive_policy_fields(applied["config"],caps),"management_outcome_unknown");
    if (input["application"]["domain"]=="station") require(applied["config"].contains("station") &&
        applied["config"]["station"]==old["station"] && after["applied_station"]==old["station"],"management_outcome_unknown");
    // Preserve the existing USB apply API's complete-configuration result.
    // The validated progress readback above confirms the effect, but is not a
    // replacement wire schema for clients of this established route.
    return result;
  } catch (const Rejected &e) {
    return {e.status,Json{{"error",{{"code",e.code}}},{"mutation_attempted",attempted}}.dump(),{}};
  } catch (...) {
    return {502,Json{{"error",{{"code",attempted?"management_outcome_unknown":"invalid_remote_progress"}}},{"mutation_attempted",attempted}}.dump(),{}};
  }
}
namespace {
PicoHttpResponse gpio_drive_progress_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &method,
    const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  if (method != "GET" && method != "PUT" && method != "POST")
    return {404, R"({"error":{"code":"not_found"}})", {}};
  const auto result = pico_progress_request(exchange,device,boot,
      method == "GET" ? "progress" : method == "PUT" ? "progress/update" : "progress/apply",
      method == "GET" ? "GET" : "POST",body,revision,carrier,generation);
  if (method != "POST" || result.status != 200) return result;
  return pico_progress_request(exchange,device,boot,"progress","GET",{}, {},carrier,generation);
}

bool valid_filter_settings(const Json &v) {
  if (!fields(v,{"policy","release_ms","settle_ms","selectors","ranges"}) ||
      !(v["policy"] == "fixed_manual" || v["policy"] == "required") ||
      !v["selectors"].is_array() || v["selectors"].size()>5 || !v["ranges"].is_array() || v["ranges"].size()>10 || v.dump().size()>1536) return false;
  const auto number=[](const Json &n,unsigned low,unsigned high) { return n.is_number_integer() && n>=low && n<=high; };
  const auto gp=[&](const Json &n) { return number(n,0,22) || number(n,26,28); };
  const auto name=[](const Json &n) {return n.is_string() && std::regex_match(n.get<std::string>(),std::regex("[A-Za-z0-9_/-]([A-Za-z0-9 _/-]{0,14}[A-Za-z0-9_/-])?"));};
  if (!number(v["release_ms"],0,250) || !number(v["settle_ms"],0,250)) return false;
  std::set<unsigned> ids,pins; std::set<std::string> names;
  for (const auto &o:v["selectors"]) {
    if (!fields(o,{"id","name","gp","active_high"}) || !number(o["id"],1,5) || !name(o["name"]) || !gp(o["gp"]) || !o["active_high"].is_boolean()) return false;
    if (!ids.insert(o["id"].get<unsigned>()).second || !pins.insert(o["gp"].get<unsigned>()).second || !names.insert(o["name"].get<std::string>()).second) return false;
  }
  names.clear();
  for (const auto &r:v["ranges"]) {
    if (!fields(r,{"name","selector","low_hz","high_hz"}) || !name(r["name"]) || !number(r["selector"],1,5) || !ids.contains(r["selector"].get<unsigned>()) ||
        !number(r["low_hz"],1,1000000000) || !number(r["high_hz"],1,1000000000) || r["low_hz"]>r["high_hz"] || !names.insert(r["name"].get<std::string>()).second) return false;
    for (const auto &p:v["ranges"])
      if (fields(p,{"name","selector","low_hz","high_hz"}) && p["selector"]!=r["selector"] && r["low_hz"]<=p["high_hz"] && p["low_hz"]<=r["high_hz"]) return false;
  }
  return true;
}
PicoHttpResponse filter_selector_request(const PicoManagementExchange &exchange,
    const std::string &device,const std::string &boot,const std::string &method,
    const std::string &body,const std::string &revision,const std::string &carrier,const std::string &generation) {
  bool attempted=false;
  try {
    require(identity(device) && identity(boot),"member_identity_unavailable",409);
    require(method=="GET" || method=="PUT","not_found",404);
    require(body.size()<=1792,"body_too_large",413);
    const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
    Json input;
    if (method=="PUT") {
      require(etag(revision),"revision_required",428);
      try {input=control_document(body);} catch (...) {throw Rejected{400,"invalid_json"};}
      require(fields(input,{"schema","target","settings"}) && input["schema"]=="filter-selector" && input["target"]==target && valid_filter_settings(input["settings"]),"invalid_filter_settings",400);
    }
    const auto caps_response=exchange("capabilities","GET",{},{});success(caps_response);const auto caps=parsed(caps_response,true);
    require(caps.is_object() && caps.contains("filter_selector") && supported_descriptor(caps["filter_selector"],"filter_selector"),"filter_selector_unavailable",409);
    require(caps.contains("management_carriers") && valid_management_descriptor(caps["management_carriers"],carrier),"unsupported_management_contract",409);
    const auto management_response=exchange("management","GET",{},{});success(management_response);const auto management=strict_browser_json(management_response.body);
    require(valid_management_contract(management,carrier,device,boot,generation) && management["schema"]==caps["management_carriers"]["schema"],"unsupported_management_contract",409);
    const auto read=[&] {
      auto r=exchange("filter-selector","GET",{},{});success(r);const auto v=parsed(r);
      require(fields(v,{"schema","target","saved","boot_active","pending_restart","selected_id","selection_known","admission_error","output_active","rf_active","storage_healthy","reserved"}) && v["schema"]=="filter-selector" && v["target"]==target && etag(r.etag),"invalid_remote_resource");
      for (const auto key:{"pending_restart","selection_known","output_active","rf_active","storage_healthy"})require(v[key].is_boolean(),"invalid_remote_resource");
      require((v["saved"].is_null() || valid_filter_settings(v["saved"])) && (v["boot_active"].is_null() || valid_filter_settings(v["boot_active"])) && v["storage_healthy"]==(!v["saved"].is_null() && !v["boot_active"].is_null()),"invalid_remote_resource");
      require(v["admission_error"].is_null() || v["admission_error"]=="filter_coverage_required","invalid_remote_resource");
      require(v["reserved"].is_array() && v["reserved"].size()<=26,"invalid_remote_resource");std::set<unsigned> reserved;
      for(const auto &p:v["reserved"]) {
        require(fields(p,{"gp","role"}) && p["gp"].is_number_integer() && ((p["gp"]>=0 && p["gp"]<=22)||(p["gp"]>=26 && p["gp"]<=28)) &&
            (p["role"]=="rf" || p["role"]=="button" || p["role"]=="amplifier" || p["role"]=="indicator" || p["role"]=="i2c_sda" || p["role"]=="i2c_scl"),"invalid_remote_resource");
        require(reserved.insert(p["gp"].get<unsigned>()).second,"invalid_remote_resource");
      }
      if(!v["saved"].is_null()) for(const auto &o:v["saved"]["selectors"])require(!reserved.contains(o["gp"].get<unsigned>()),"invalid_remote_resource");
      require(v["saved"].is_null() || v["boot_active"].is_null() || v["saved"]==v["boot_active"] || v["pending_restart"]==true,"invalid_remote_resource");
      require(v["rf_active"]==false || v["output_active"]==true,"invalid_remote_resource");
      require(v["selected_id"].is_null() || (v["selected_id"].is_number_integer() && v["selection_known"]==true && v["output_active"]==true && !v["boot_active"].is_null() && v["boot_active"]["policy"]=="required" &&
          std::any_of(v["boot_active"]["selectors"].begin(),v["boot_active"]["selectors"].end(),[&](const auto &o){return o["id"]==v["selected_id"];})),"invalid_remote_resource");
      return r;
    };
    const auto before=read();if(method=="GET")return before;
    require(before.etag==revision,"revision_conflict",412);const auto state=parsed(before);
    require(state["storage_healthy"]==true && state["selection_known"]==true && state["output_active"]==false,"remote_busy_or_output_unresolved",409);
    for(const auto &o:input["settings"]["selectors"])for(const auto &p:state["reserved"])require(o["gp"]!=p["gp"],"filter_pin_conflict",400);
    const auto status_response=exchange("status","GET",{},{});success(status_response);const auto status=parsed(status_response);
    require(status.is_object() && status.contains("job") && status["job"].is_object(),"invalid_remote_resource");const auto &job=status["job"];
    require(job.contains("boot_id") && job["boot_id"]==boot && job.contains("output_active") && job["output_active"]==false && job.contains("owner_id") && job["owner_id"].is_null() && job.contains("state") &&
        (job["state"]=="empty" || job["state"]=="complete" || job["state"]=="aborted" || job["state"]=="missed"),"remote_busy_or_output_unresolved",409);
    attempted=true;const auto result=exchange("filter-selector","PUT",body,revision);success(result);const auto confirmed=read();
    require(result.etag!=revision && result.etag==confirmed.etag && parsed(result)==parsed(confirmed) && parsed(confirmed)["saved"]==input["settings"],"management_outcome_unknown");return result;
  } catch (const Rejected &e) {return {e.status,Json{{"error",{{"code",e.code}}},{"mutation_attempted",attempted}}.dump(),{}};}
    catch (...) {return {502,Json{{"error",{{"code",attempted?"management_outcome_unknown":"invalid_remote_resource"}}},{"mutation_attempted",attempted}}.dump(),{}};}
}

PicoHttpResponse amplifier_timing_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &method,
    const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  bool attempted = false;
  try {
    require(identity(device) && identity(boot),"member_identity_unavailable",409);
    require(method == "GET" || method == "PUT","not_found",404);
    require(body.size() <= 1024,"body_too_large",413);
    const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
    Json timing_minimum=0, timing_maximum=1000;
    const auto timing = [&](const Json &v) {
      if (!fields(v,{"lead_ms","tail_ms"})) return false;
      for (const auto key : {"lead_ms","tail_ms"})
        if (!v[key].is_number_integer() || v[key] < timing_minimum || v[key] > timing_maximum) return false;
      return true;
    };
    Json input;
    if (method == "PUT") {
      require(etag(revision),"revision_required",428);
      try { input = control_document(body); } catch (...) { throw Rejected{400,"invalid_json"}; }
      require(fields(input,{"schema","target","timing"}) && input["schema"] == "amplifier-timing" &&
          input["target"] == target && timing(input["timing"]),"invalid_amplifier_timing",400);
    }
    const auto caps_response=exchange("capabilities","GET",{},{}); success(caps_response);
    const auto caps=parsed(caps_response,true);
    require(caps.is_object() && caps.contains("amplifier_timing") && supported_descriptor(caps["amplifier_timing"],"amplifier_timing"),
        "amplifier_timing_unavailable",409);
    timing_minimum=caps["amplifier_timing"]["minimum"];
    timing_maximum=caps["amplifier_timing"]["maximum"];
    if (method == "PUT") require(timing(input["timing"]),"invalid_amplifier_timing",400);
    require(caps.contains("management_carriers") && valid_management_descriptor(caps["management_carriers"],carrier),
        "unsupported_management_contract",409);
    const auto management_response=exchange("management","GET",{},{}); success(management_response);
    const auto management=strict_browser_json(management_response.body);
    require(valid_management_contract(management,carrier,device,boot,generation) &&
        management["schema"] == caps["management_carriers"]["schema"],"unsupported_management_contract",409);
    const auto read = [&] {
      auto r=exchange("amplifier-timing","GET",{},{}); success(r);
      const auto v=parsed(r);
      require(fields(v,{"schema","target","saved","boot_active","pending_restart","phase","output_active","rf_active","storage_healthy"}) &&
          v["schema"] == "amplifier-timing" && v["target"] == target && etag(r.etag),"invalid_remote_resource");
      require((v["saved"].is_null() || timing(v["saved"])) && (v["boot_active"].is_null() || timing(v["boot_active"])),"invalid_remote_resource");
      for (const auto key : {"pending_restart","output_active","rf_active","storage_healthy"}) require(v[key].is_boolean(),"invalid_remote_resource");
      require(v["phase"] == "inactive" || v["phase"] == "waiting" || v["phase"] == "lead" || v["phase"] == "rf" ||
          v["phase"] == "tail" || v["phase"] == "fault","invalid_remote_resource");
      require(v["rf_active"] == (v["phase"] == "rf") &&
          (!(v["phase"] == "lead" || v["phase"] == "rf" || v["phase"] == "tail") || v["output_active"] == true),"invalid_remote_resource");
      require(v["saved"].is_null() || v["boot_active"].is_null() || v["saved"] == v["boot_active"] || v["pending_restart"] == true,"invalid_remote_resource");
      return r;
    };
    const auto before=read();
    if (method == "GET") return before;
    require(before.etag == revision,"revision_conflict",412);
    const auto v=parsed(before);
    require(v["storage_healthy"] == true && !v["saved"].is_null() && v["output_active"] == false && v["phase"] == "inactive",
        "remote_busy_or_output_unresolved",409);
    const auto status_response=exchange("status","GET",{},{}); success(status_response);
    const auto status=parsed(status_response);
    require(status.is_object() && status.contains("job") && status["job"].is_object(),"invalid_remote_resource");
    const auto &job=status["job"];
    require(job.contains("boot_id") && job["boot_id"] == boot && job.contains("output_active") && job["output_active"] == false &&
        job.contains("owner_id") && job["owner_id"].is_null() && job.contains("state") &&
        (job["state"] == "empty" || job["state"] == "complete" || job["state"] == "aborted" || job["state"] == "missed"),"remote_busy_or_output_unresolved",409);
    attempted=true;
    const auto result=exchange("amplifier-timing","PUT",body,revision); success(result);
    const auto confirmed=read();
    require(result.etag != revision && result.etag == confirmed.etag && parsed(result) == parsed(confirmed) &&
        parsed(confirmed)["saved"] == input["timing"],"management_outcome_unknown");
    return result;
  } catch (const Rejected &e) {
    return {e.status,Json{{"error",{{"code",e.code}}},{"mutation_attempted",attempted}}.dump(),{}};
  } catch (...) {
    return {502,Json{{"error",{{"code",attempted ? "management_outcome_unknown" : "invalid_remote_resource"}}},{"mutation_attempted",attempted}}.dump(),{}};
  }
}
PicoHttpResponse gpio_drive_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &method,
    const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  bool attempted = false;
  try {
    require(method == "GET" || method == "PUT", "not_found", 404);
    require(body.size() <= 1024, "body_too_large", 413);
    const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
    Json input;
    if (method == "PUT") {
      try { input = control_document(body); } catch (...) { throw Rejected{400,"invalid_json"}; }
      require(fields(input,{"schema","target","gpio_drive_ma"}) &&
          input["schema"] == "gpio-drive" && input["target"] == target &&
          input["gpio_drive_ma"].is_number_integer() &&
          (input["gpio_drive_ma"] == 2 || input["gpio_drive_ma"] == 4 ||
           input["gpio_drive_ma"] == 8 || input["gpio_drive_ma"] == 12), "invalid_gpio_drive",400);
    }
    const auto caps_response = exchange("capabilities","GET",{},{}); success(caps_response);
    const auto caps = parsed(caps_response,true);
    require(caps.contains("gpio_drive") && supported_descriptor(caps["gpio_drive"],"gpio_drive"),"gpio_drive_unavailable",409);
    if (method == "PUT") require(drive_policy_value(input["gpio_drive_ma"],caps),"invalid_gpio_drive",400);
    // Reuse existing carrier validation, boot identity, saved/active consistency
    // and selected application authority; do not weaken any closed legacy shape.
    const auto context = pico_resource_request(exchange,device,boot,"application","GET",{}, {},carrier,generation);
    success(context);
    const auto read = [&] {
      auto r = exchange("gpio-drive","GET",{},{}); success(r);
      const auto v = parsed(r);
      require(fields(v,{"schema","target","saved","boot_active","pad_readback","pending_restart",
          "application_error","rf_gp","execution_engine","storage_healthy"}) &&
          v["schema"] == "gpio-drive" && v["target"] == target &&
          v["pending_restart"].is_boolean() && v["storage_healthy"].is_boolean() &&
          v["execution_engine"].is_string() && etag(r.etag),"invalid_remote_resource");
      for (const auto key : {"saved","boot_active","pad_readback"})
        require(v[key].is_null() || drive_policy_value(v[key],caps),"invalid_remote_resource");
      require(v["rf_gp"].is_null() || (v["rf_gp"].is_number_integer() && v["rf_gp"] >= 0 && v["rf_gp"] <= 28),"invalid_remote_resource");
      require(v["application_error"].is_null() || v["application_error"] == "pad_application_failed" ||
          v["application_error"] == "output_disable_failed","invalid_remote_resource");
      return r;
    };
    const auto before = read();
    require(before.etag == context.etag,"configuration_context_changed",409);
    if (method == "GET") return before;
    require(revision == before.etag,"revision_conflict",412);
    const auto observed = parsed(before);
    require(observed["storage_healthy"] == true && observed["application_error"].is_null() && !observed["saved"].is_null(),"remote_busy_or_output_unresolved",409);
    const auto status_response = exchange("status","GET",{},{}); success(status_response);
    const auto state = parsed(status_response).at("job");
    require(state.at("boot_id") == boot && state.at("output_active") == false && state.at("owner_id").is_null() &&
        (state.at("state") == "empty" || state.at("state") == "complete" || state.at("state") == "aborted" || state.at("state") == "missed"),"remote_busy_or_output_unresolved",409);
    attempted = true;
    const auto result = exchange("gpio-drive","PUT",body,revision); success(result);
    const auto confirmed = read();
    require(result.etag != revision && result.etag == confirmed.etag && parsed(result) == parsed(confirmed) &&
        parsed(confirmed)["saved"] == input["gpio_drive_ma"],"management_outcome_unknown");
    return result;
  } catch (const Rejected &e) {
    return {e.status,Json{{"error",{{"code",e.code}}},{"mutation_attempted",attempted}}.dump(),{}};
  } catch (...) {
    return {502,Json{{"error",{{"code",attempted ? "management_outcome_unknown" : "invalid_remote_resource"}}},{"mutation_attempted",attempted}}.dump(),{}};
  }
}
} // namespace
PicoHttpResponse pico_resource_request(const PicoManagementExchange &exchange,
    const std::string &device, const std::string &boot, const std::string &resource,
    const std::string &method, const std::string &body, const std::string &revision,
    const std::string &carrier, const std::string &generation) {
  if (resource == "filter-selector") return filter_selector_request(exchange,device,boot,method,body,revision,carrier,generation);
  if (resource == "amplifier-timing") return amplifier_timing_request(exchange,device,boot,method,body,revision,carrier,generation);
  if (resource == "gpio-drive-progress") return gpio_drive_progress_request(exchange,device,boot,method,body,revision,carrier,generation);
  if (resource == "gpio-drive") return gpio_drive_request(exchange,device,boot,method,body,revision,carrier,generation);
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
      try { input = control_document(body); } catch (...) { throw Rejected{400,"invalid_json"}; }
      if (preview) require(plan_shape(normalized_plan(input)),"invalid_pin_plan",400);
      else {
        const bool station = resource == "station";
        require(fields(input,{"schema","target",station ? "station" : "pins"}) &&
            (station ? (input["schema"] == "station" || input["schema"] == "station") : input["schema"] == "hardware"),"invalid_resource",400);
        require(input["target"] == target,"target_mismatch",409);
        require(station ? station_valid(input["station"], input["schema"] == "station") : plan_valid(normalized_plan(input["pins"])), station ? "invalid_station" : "unsupported_pin_adapter",400);
      }
    }
    const auto read = [&](const std::string &path, std::size_t limit = 4096) {
      auto r = exchange(path,"GET",{},{}); success(r);
      require(r.body.size() <= limit,"invalid_remote_resource"); (void)parsed(r); return r;
    };
    auto caps = parsed(read("capabilities",16384),true);
    require(caps.is_object(),"unsupported_remote_contract",409);
    if (caps.contains("management_carriers")) {
      require(valid_management_descriptor(caps["management_carriers"], carrier),
              "unsupported_management_contract", 409);
      const auto management = strict_browser_json(read("management",16384).body);
      require(valid_management_contract(management,carrier,device,boot,generation) &&
          management["schema"] == caps["management_carriers"]["schema"],"unsupported_management_contract",409);
    } else throw Rejected{409,"unsupported_management_contract"};
    require(caps.contains("application_resources"),"shared_resources_unavailable",409);
    require(supported_descriptor(caps["application_resources"],"application_resources"),"unsupported_resource_capabilities",409);
    const std::string application_path = "application";
    const auto resource_path = [](const std::string &path) { return path; };
    if (write && resource == "station") require(input["schema"] == "station","unsupported_resource_schema",409);
    require(caps.contains("features") && caps["features"].is_object() && caps["features"].contains("consumer_setup_identity") &&
        caps["features"]["consumer_setup_identity"].is_boolean(),"invalid_remote_resource");
    for (const auto feature : {"application","station","hardware"})
      require(caps["features"].contains(feature) && caps["features"][feature] == true,"unsupported_resource_capabilities",409);
    const bool profile_owned = carrier != "usb_physical" && caps["features"]["consumer_setup_identity"] == true;
    if (carrier != "usb_physical" && caps.contains("management_carriers")) require(profile_owned == (carrier == "consumer_station" || carrier == "consumer_ap"),"invalid_remote_resource");
    const auto first = read(application_path); auto application = parsed(first); shared_application(application,target,carrier == "usb_physical");
    require(application["schema"] == "application","invalid_remote_resource");
    require(etag(first.etag),"invalid_remote_resource");
    for (const auto path : {"station","hardware"}) {
      const auto r = read(resource_path(path));
      const auto sibling=resource_value(parsed(r));
      auto independently_checked=application;
      independently_checked[path]=sibling;
      shared_application(independently_checked,target,carrier == "usb_physical");
      require(r.etag == first.etag && sibling.dump() == resource_value(application[path]).dump(),"configuration_context_changed",409);
    }
    const auto pins = parsed(read("pins")); pin_choices(pins);
    const auto last = read(application_path); const auto after = parsed(last); shared_application(after,target,carrier == "usb_physical");
    // Configuration consistency is distinct from mutable live recurrence/status.
    require(last.etag == first.etag && after["station"].dump() == application["station"].dump() &&
        after["hardware"]["saved"].dump() == application["hardware"]["saved"].dump() &&
        after["hardware"]["saved_generation"] == application["hardware"]["saved_generation"],"configuration_context_changed",409);
    require(drive_policy_observation(application["hardware"]["gpio_drive"],caps) &&
        drive_policy_observation(after["hardware"]["gpio_drive"],caps),"invalid_remote_resource");
    application = after;
    Json control_capabilities=Json::object();
    for (const auto feature : {"gpio_drive","amplifier_enable","amplifier_timing","filter_selector"}) {
      if (!caps.contains(feature)) continue;
      require(supported_descriptor(caps[feature],feature),"unsupported_resource_capabilities",409);
      control_capabilities[feature]=caps[feature];
    }
    if (method == "GET") return {200,Json{{"contract","Wsprry-Control/1"},{"schema","member-resources"},{"target",target},{"carrier",carrier},
        {"profile_generation",generation.empty() ? Json(nullptr) : Json(generation)}, {"profile_owned_station",profile_owned},
        {"application",application},{"pin_choices",pins["choices"]},{"control_capabilities",control_capabilities}}.dump(),last.etag};
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
    Json expected_hardware_pins;
    if (write && resource=="hardware")
      expected_hardware_pins=normalized_plan(input["pins"],application["hardware"]["saved"].value("amplifier_active_high",true));
    attempted = write;
    auto result = exchange(resource_path(resource),method,body,preview ? "" : revision); success(result);
    require(result.body.size() <= 4096,preview ? "invalid_pin_validation" : "management_outcome_unknown");
    const auto value = parsed(result);
    if (preview) {
      require(value.contains("gp") && (value["gp"].is_null() || value["gp"].is_number_integer()) && resource_value(value).dump() == resource_value(review_plan(normalized_plan(input))).dump(),"invalid_pin_validation");
      const auto confirmed = read(application_path);
      const auto observed = parsed(confirmed); shared_application(observed,target,carrier == "usb_physical");
      require(confirmed.etag == revision && observed["station"].dump() == application["station"].dump() &&
          observed["hardware"].dump() == application["hardware"].dump(),"configuration_context_changed",409);
      return {200,value.dump(),{}};
    }
    require(etag(result.etag) && result.etag != revision && result.body.size() <= 4096,"management_outcome_unknown");
    const auto confirmed = read(application_path); const auto updated = parsed(confirmed);
    shared_application(updated,target,carrier == "usb_physical");
    require(drive_policy_observation(updated["hardware"]["gpio_drive"],caps),"management_outcome_unknown");
    auto independently_checked=updated;
    independently_checked[resource]=resource_value(value);
    try { shared_application(independently_checked,target,carrier == "usb_physical"); }
    catch (...) { throw Rejected{502,"management_outcome_unknown"}; }
    require(confirmed.etag == result.etag && resource_value(updated[resource]).dump() == resource_value(value).dump(),"management_outcome_unknown");
    require(resource == "station" ? value["station"].dump() == input["station"].dump() : value["saved"].dump() == expected_hardware_pins.dump(),"management_outcome_unknown");
    return result;
  } catch (const Rejected &e) {
    return {e.status,Json{{"error",{{"code",e.code}}},{"mutation_attempted",attempted}}.dump(),{}};
  } catch (...) {
    return {502,Json{{"error",{{"code",attempted ? "management_outcome_unknown" : "invalid_remote_resource"}}},{"mutation_attempted",attempted}}.dump(),{}};
  }
}
} // namespace wsprrypi
