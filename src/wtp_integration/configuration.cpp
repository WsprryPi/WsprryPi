// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "configuration.hpp"
#include "browser_api.hpp"
#include <algorithm>
#include <set>
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
      std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 32 && c < 127; });
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
bool application_shape(const Json &j, const Json &target) {
  if (!fields(j, {"schema", "target", "station", "hardware", "recurrence", "capabilities", "reboot_required"}) ||
      j["schema"] != "transmitter-application/1" || j["target"] != target || !j["reboot_required"].is_boolean()) return false;
  const auto &s = j["station"], &h = j["hardware"], &r = j["recurrence"], &c = j["capabilities"];
  if (!fields(s, {"schema", "target", "station", "saved_generation", "storage_healthy"}) ||
      s["schema"] != "transmitter-station/1" || s["target"] != target || !s["storage_healthy"].is_boolean() ||
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
      c["hardware_activation"] != "restart" || c["station_encoding"] != "wspr_type1" ||
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
       "tls_authentication_or_connection_failed", "host_busy_or_output_unresolved"})
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
    const std::string &body, const std::string &revision) {
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
      const auto &resources = caps["application_resources"];
      require(resources.is_object() && resources.value("schema", "") == "transmitter-application/1" &&
          resources.value("read", "") == "/api/v1/application", "unsupported_configuration_context", 409);
      const auto r = read("application"); application = parsed(r);
      require(application_shape(application, target), "invalid_remote_response");
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
      std::string resource = "config", verb = "PUT";
      Json candidate;
      if (input["operation"] == "schedules") {
        require(!config.is_null(), "not_configured", 409);
        resource = "schedules"; candidate = patch;
      } else if (strategy == "merge") {
        resource = "config/update"; verb = "POST";
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
} // namespace wsprrypi
