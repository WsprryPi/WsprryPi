// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "browser_api.hpp"
#include <set>
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
        {"wtp", host.value("capabilities", nlohmann::json(nullptr))},
        {"features", {{"config", true}, {"schedules", true}, {"network", true},
                      {"jobs", true}, {"softap", false}, {"ble", false}}},
        {"job_operations", {"ABORT"}}, {"job_submission", "host-application"},
        {"management_scope", "pico-standalone"},
        {"management_carriers", {{"scope", "wsprrypi-host/1"}, {"built_in", {"engineering_mtls", "consumer_station", "consumer_ap"}},
          {"selection", "/api/v1/member/management"}, {"remote_capabilities", "authenticated_read_required"}}}, {"active_job_connections", false},
        {"max_body_bytes", 32768}}.dump(), {}};
  }
  if (r.path == "/api/v1/member/management" && (r.method == "GET" || r.method == "POST")) {
    if (r.body.size() > 512) return error(413, "body_too_large");
    return management_("member/management", r.method, r.body, {});
  }
  if (r.path == "/api/v1/member/configuration" && (r.method == "GET" || r.method == "POST")) {
    if (r.body.size() > 2048) return error(413, "body_too_large");
    if (r.method == "POST") {
      if (r.revision.empty()) return error(428, "revision_required");
      try { if (!strict_browser_json(r.body).is_object()) return error(400, "invalid_request"); }
      catch (...) { return error(400, "invalid_json"); }
    }
    return management_("member/configuration", r.method, r.body, r.revision);
  }
  for (const std::string resource : {"config", "schedules", "network"}) {
    if (r.path == "/api/v1/" + resource && (r.method == "GET" || r.method == "PUT")) {
      if (r.method == "PUT") {
        if (r.revision.empty()) return error(428, "revision_required");
        try { if (!strict_browser_json(r.body).is_object()) return error(400, "invalid_request"); }
        catch (...) { return error(400, "invalid_json"); }
      }
      return management_(resource, r.method, r.body, r.revision);
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
