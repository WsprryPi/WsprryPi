// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "browser_api.hpp"
#include <set>
#include <atomic>
#include <random>
namespace wsprrypi {
namespace {
bool identity(const std::string &s) {
  return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
PicoHttpResponse error(unsigned status, const char *code) {
  return {status, nlohmann::json{{"error", {{"code", code}}}}.dump(), {}};
}
}
nlohmann::json strict_browser_json(const std::string &body) {
  if (body.size() > 32768) throw std::runtime_error("body_too_large");
  std::vector<std::set<std::string>> objects;
  return nlohmann::json::parse(body, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &value) {
    if (depth > 32) throw std::runtime_error("nesting_limit");
    if (event == nlohmann::json::parse_event_t::object_start) objects.emplace_back();
    if (event == nlohmann::json::parse_event_t::object_end) objects.pop_back();
    if (event == nlohmann::json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)
      throw std::runtime_error("duplicate_key");
    return true;
  });
}
PicoHttpResponse WtpBrowserApi::handle(const BrowserRequest &r) {
  if (r.body.size() > 32768) return error(413, "body_too_large");
  if (r.method != "GET" && !r.mutation_context) return error(403, "origin_or_content_type");
  if (r.method == "GET" && (r.path == "/api/v1/status" || r.path == "/api/v1/jobs")) {
    const auto host = status_();
    return {200, nlohmann::json{{"job", host.value("remote", nlohmann::json(nullptr))},
        {"standalone", nullptr}, {"network", host.value("network", nlohmann::json(nullptr))},
        {"host", host}, {"scope", "wsprrypi-host/1"}}.dump(), {}};
  }
  if (r.method == "GET" && r.path == "/api/v1/capabilities") {
    const auto host = status_();

    return {200, nlohmann::json{{"api_version", 1}, {"scope", "wsprrypi-host/1"},
        {"host_logging", {{"schema","host-usb-logging/1"},{"path","/api/v1/host/logging"},{"maximum_sources",8}}},
        {"wtp", host.value("capabilities", nlohmann::json(nullptr))},
        {"features", {{"config", true}, {"schedules", true}, {"network", true},
                      {"jobs", true}, {"member_resources",true}, {"softap", false}, {"ble", false}}},
        {"member_resources", {{"scope","pico-member"},{"application","/api/v1/member/application"},
          {"station","/api/v1/member/station"},{"hardware","/api/v1/member/hardware"},
          {"pins","/api/v1/member/pins"},{"preview","/api/v1/member/pins/validate"},
          {"remote_support","authenticated_resource_discovery_required"}}},
        {"job_operations", {"ABORT"}}, {"job_submission", "host-application"},
        {"management_scope", "pico-standalone"},
        {"management_carriers", {{"scope", "wsprrypi-host/1"}, {"built_in", {"engineering_mtls", "consumer_station", "consumer_ap", "usb_physical"}},
          {"selection", "/api/v1/member/management"}, {"remote_capabilities", "authenticated_read_required"}}}, {"active_job_connections", false},
        {"max_body_bytes", 32768}}.dump(), {}};
  }
  if (r.path == "/api/v1/member/management" && (r.method == "GET" || r.method == "POST")) {
    std::lock_guard lock(management_mutex_);
    if (r.body.size() > 512) return error(413, "body_too_large");
    if (r.method == "POST") { usb_resource_reads_.clear(); member_context_.clear(); member_revision_.clear(); legacy_read_required_ = true; alias_reads_required_ = {"config","schedules","network"}; }
    return management_("member/management", r.method, r.body, {}, {});
  }
  for (const std::string resource : {"wifi","hostname","logging","udp-logging","udp-logging/capabilities","udp-logging/test","udp-logging/test/capabilities","restart","ap","progress","progress/update","progress/apply","gpio-drive","gpio-drive-progress"}) {
    if (r.path != "/api/v1/member/" + resource) continue;
    const bool read = r.method == "GET" && (resource == "wifi" || resource == "hostname" || resource == "logging" || resource == "udp-logging" || resource == "udp-logging/capabilities" || resource == "udp-logging/test/capabilities" || resource == "progress" || resource == "gpio-drive" || resource == "gpio-drive-progress");
    const bool write = (r.method == "PUT" && (resource == "wifi" || resource == "hostname" || resource == "logging" || resource == "udp-logging" || resource == "gpio-drive" || resource == "gpio-drive-progress")) ||
        (r.method == "DELETE" && resource == "wifi") ||
        (r.method == "POST" && (resource == "udp-logging/test" || resource == "gpio-drive-progress" || resource == "restart" || resource == "ap" || resource == "progress/update" || resource == "progress/apply"));
    if (!read && !write) return error(404,"not_found");
    std::lock_guard lock(management_mutex_);
    if (r.body.size() > 2048) return error(413,"body_too_large");
    std::string selection;
    try {
      const auto state=management_("member/management","GET",{},{},{});
      if (state.status!=200) return error(409,"member_read_required");
      const auto value=strict_browser_json(state.body);
      if (!resource.starts_with("udp-logging") && resource != "gpio-drive" && resource != "gpio-drive-progress" && !resource.starts_with("progress") && value.value("carrier","")!="usb_physical") return error(409,"usb_management_required");
      selection=value.at("selection_id").get<std::string>();
    } catch (...) { return error(409,"member_read_required"); }
    const auto key=resource=="udp-logging/test"?"udp-logging":resource.starts_with("progress/")?"progress":(resource=="restart" || resource=="ap")?"hostname":resource;
    if (write) {
      const auto found=usb_resource_reads_.find(key);
      if (r.revision.empty()) return error(428,"revision_required");
      if (found==usb_resource_reads_.end() || found->second!=std::pair{selection,r.revision}) return error(409,"member_read_required");
      try { if (!strict_browser_json(r.body).is_object()) return error(400,"invalid_json"); }
      catch (...) { return error(400,"invalid_json"); }
      // A mutation can change the global revision. Every editor must read again.
      usb_resource_reads_.clear(); member_context_.clear(); member_revision_.clear();
      legacy_read_required_=true; alias_reads_required_={"config","schedules","network"};
    }
    auto result=management_("member/"+resource,r.method,r.body,r.revision,selection);
    if (read && result.status==200 && !result.etag.empty()) usb_resource_reads_[resource]={selection,result.etag};
    return result;
  }
  for (const std::string resource : {"application", "station", "hardware", "pins", "pins/validate"}) {
    if (r.path != "/api/v1/member/" + resource) continue;
    const bool read = r.method == "GET" && resource != "pins/validate";
    const bool preview = r.method == "POST" && resource == "pins/validate";
    const bool write = r.method == "PUT" && (resource == "station" || resource == "hardware");
    if (!read && !preview && !write) return error(404,"not_found");
    std::lock_guard lock(management_mutex_);
    if (r.body.size() > (preview ? 2048U : 1024U)) return error(413,"body_too_large");
    std::string selection;
    try {
      const auto state = management_("member/management","GET",{},{},{});
      if (state.status != 200) return error(409,"member_read_required");
      const auto value = strict_browser_json(state.body);
      if (!value.contains("selection_id") || !value["selection_id"].is_string()) return error(409,"member_read_required");
      selection = value["selection_id"];
    } catch (...) { return error(409,"member_read_required"); }
    if (!read) {
      if (r.revision.empty()) return error(428,"revision_required");
      if (member_context_.empty() || r.member_context != member_context_ || r.revision != member_revision_ || selection != member_selection_)
        return error(409,"member_read_required");
    }
    if (!preview) { member_context_.clear(); member_revision_.clear(); }
    if (write) { legacy_read_required_ = true; alias_reads_required_ = {"config","schedules","network"}; }
    auto result = management_("member/" + resource,r.method,r.body,r.revision,selection);
    if (read && result.status == 200) {
      try {
        auto value = strict_browser_json(result.body);
        if (!value.is_object() || result.etag.empty()) return error(502,"invalid_remote_resource");
        static std::atomic<std::uint64_t> sequence{};
        static const std::string process_context=[] {
          std::random_device entropy; std::string value;
          for (unsigned i=0;i<32;++i) value += "0123456789abcdef"[entropy() & 15];
          return value;
        }();
        member_context_ = process_context + ":" + std::to_string(++sequence);
        member_revision_ = result.etag;
        member_selection_ = selection;
        value["member_context"] = member_context_;
        result.body = value.dump();
      } catch (...) { return error(502,"invalid_remote_resource"); }
    }
    return result;
  }
  const auto current_selection = [&]() -> std::string {
    try {
      const auto state = management_("member/management","GET",{},{},{});
      if (state.status != 200) return {};
      const auto value = strict_browser_json(state.body);
      return value.contains("selection_id") && value["selection_id"].is_string() ? value["selection_id"].get<std::string>() : "";
    } catch (...) { return {}; }
  };
  const auto invalidate_management = [&] {
    member_context_.clear(); member_revision_.clear(); legacy_read_required_=true;
    alias_reads_required_={"config","schedules","network"};
  };
  if (r.path == "/api/v1/member/configuration" && (r.method == "GET" || r.method == "POST")) {
    std::lock_guard lock(management_mutex_);
    if (r.body.size() > 2048) return error(413, "body_too_large");
    if (r.method == "POST") {
      if (r.revision.empty()) return error(428, "revision_required");
      try { if (!strict_browser_json(r.body).is_object()) return error(400, "invalid_request"); }
      catch (...) { return error(400, "invalid_json"); }
    }
    const auto selection = r.method == "GET" ? current_selection() : legacy_selection_;
    if (selection.empty() || (r.method == "POST" && legacy_read_required_)) return error(409,"member_read_required");
    if (r.method == "POST") invalidate_management();
    auto result = management_("member/configuration", r.method, r.body, r.revision,selection);
    if (result.status == 200) {
      legacy_selection_=selection; legacy_read_required_=false;
      if (r.method == "GET") for (const auto alias : {"config","schedules"}) { alias_reads_required_.erase(alias); alias_selections_[alias]=selection; }
    }
    return result;
  }
  for (const std::string resource : {"config", "schedules", "network"}) {
    if (r.path == "/api/v1/" + resource && (r.method == "GET" || r.method == "PUT")) {
      std::lock_guard lock(management_mutex_);
      if (r.method == "PUT") {
        if (r.revision.empty()) return error(428, "revision_required");
        try { if (!strict_browser_json(r.body).is_object()) return error(400, "invalid_request"); }
        catch (...) { return error(400, "invalid_json"); }
      }
      const auto selection = r.method == "GET" ? current_selection() : alias_selections_[resource];
      if (selection.empty() || (r.method == "PUT" && alias_reads_required_.contains(resource))) return error(409,"member_read_required");
      if (r.method == "PUT") invalidate_management();
      auto result = management_(resource, r.method, r.body, r.revision,selection);
      if (result.status == 200) {
        alias_reads_required_.erase(resource); alias_selections_[resource]=selection;
        if (r.method == "GET" && resource == "config") { alias_reads_required_.erase("schedules"); alias_selections_["schedules"]=selection; }
      }
      return result;
    }
  }
  if (r.method != "POST" || (r.path != "/api/v1/jobs" && !r.path.starts_with("/api/v1/jobs/")))
    return error(404, "not_found");
  nlohmann::json j;
  try { j = strict_browser_json(r.body); }
  catch (...) { return error(400, "invalid_json"); }
  if (!j.is_object() || j.size() != 4 || !j.contains("session_id") || !j["session_id"].is_string() ||
      !j.contains("request_id") || !j["request_id"].is_string() ||
      !j.contains("operation") || j["operation"] != "ABORT" ||
      !j.contains("body") || !j["body"].is_object() || j["body"].size() != 1 ||
      !j["body"].contains("job_id") || !j["body"]["job_id"].is_string())
    return error(400, "unsupported_or_invalid_job_request");
  const std::string session = j["session_id"], request = j["request_id"], job = j["body"]["job_id"];
  if (!identity(session) || !identity(request) || !identity(job)) return error(400, "invalid_identity");
  if (r.path != "/api/v1/jobs" && r.path != "/api/v1/jobs/" + job + "/abort") return error(400, "job_id_mismatch");
  std::lock_guard lock(mutex_);
  const auto key = session + request;
  const auto payload = j.dump();
  if (const auto found = replays_.find(key); found != replays_.end())
    return found->second.body == payload ? found->second.response : error(409, "request_id_reuse");
  if (replays_.size() >= 1024) return error(503, "browser_request_capacity");
  // Reserve before invoking authority: an exception must never leave a retry
  // looking like a new cancellation. The selected host job is checked atomically
  // by the runtime, not authorized by a browser-supplied owner label.
  auto &entry = replays_[key]; entry.body = payload;
  entry.response = error(503, "cancellation_outcome_unknown");
  try { entry.response = cancel_(job); } catch (...) {}
  auto result = nlohmann::json::parse(entry.response.body);
  const bool ok = entry.response.status == 200;
  entry.response.body = nlohmann::json{{"ok", ok}, {"request_id", request},
      {ok ? "result" : "error", ok ? result : result.value("error", nlohmann::json{{"code", "cancellation_failed"}})}}.dump();
  return entry.response;
}
} // namespace wsprrypi
