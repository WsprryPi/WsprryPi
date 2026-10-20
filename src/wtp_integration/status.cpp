// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "json.hpp"
#include "scheduler.hpp"
#include <array>

namespace wsprrypi {
namespace {
using Json = nlohmann::json;
template <typename E, std::size_t N>
const char *name(E value, const std::array<const char *, N> &names) {
  auto index = static_cast<std::size_t>(value);
  return index < N ? names[index] : "unknown";
}
const char *state(wtp::State v) {
  return name(v, std::array{"empty", "loaded", "armed", "running", "complete",
                            "aborted", "missed", "failed"});
}
template <typename T> Json nullable(const std::optional<T> &value) {
  return value ? Json(*value) : Json(nullptr);
}
Json cleanup(const CleanupResult &c) {
  return {{"ok", c.ok}, {"error", c.error}};
}
Json evidence(const std::optional<wtp::JobEvidence> &e) {
  if (!e)
    return nullptr;
  return {{"device_id", e->device_id},
          {"boot_id", e->boot_id},
          {"job_id", e->job_id},
          {"state", e->state ? Json(state(*e->state)) : Json(nullptr)},
          {"output_active", nullable(e->output_active)},
          {"device_output_active", nullable(e->device_output_active)},
          {"authoritative", e->authoritative}};
}
Json identity(const std::optional<wtp::HelloResponse> &i) {
  if (!i)
    return nullptr;
  return {{"device_id", i->device_id},
          {"boot_id", i->boot_id},
          {"product", i->product},
          {"firmware_version", i->firmware_version}};
}
Json adjustments(const std::vector<wtp::Adjustment> &values) {
  auto j = Json::array();
  for (const auto &a : values)
    j.push_back(
        {{"event_index", a.event_index},
         {"requested_frequency_nhz", std::to_string(a.requested_frequency_nhz)},
         {"realized_frequency_nhz", std::to_string(a.realized_frequency_nhz)}});
  return j;
}
Json report(const std::optional<WtpScheduleReport> &r) {
  if (!r)
    return nullptr;
  const auto &e = r->execution;
  return {
      {"outcome",
       name(r->outcome, std::array{"complete", "cancelled", "invalidated",
                                   "failed", "blocked"})},
      {"request_id", std::to_string(r->request_id.value)},
      {"job_id", r->job_id},
      {"error", r->error},
      {"start_utc_ns", std::to_string(r->start_utc_ns)},
      {"dispatch_utc_ns", std::to_string(r->dispatch_utc_ns)},
      {"arm_handed_off", r->arm_handed_off},
      {"skipped", r->skipped},
      {"reload_deferred", r->reload_deferred},
      {"execution",
       {{"ok", e.ok},
        {"stopped", e.stopped},
        {"faulted", e.faulted},
        {"error", e.error},
        {"cleanup_attempted", e.cleanup_attempted},
        {"cleanup", e.cleanup_attempted ? cleanup(e.cleanup) : Json(nullptr)}}},
      {"identity", identity(r->identity)},
      {"job", evidence(r->job)},
      {"adjustments", adjustments(r->adjustments)}};
}
Json capabilities(const std::optional<wtp::Capabilities> &c) {
  if (!c)
    return nullptr;
  Json j{{"profiles", c->profiles},
         {"engine", c->engine},
         {"modes", Json::array()},
         {"frequency_ranges", Json::array()}};
  for (auto m : c->modes)
    j["modes"].push_back(
        name(m, std::array{"wspr", "qrss", "fskcw", "dfcw", "cw", "tone"}));
  for (const auto &r : c->frequency_ranges)
    j["frequency_ranges"].push_back(
        {{"minimum_nhz", std::to_string(r.minimum_nhz)},
         {"maximum_nhz", std::to_string(r.maximum_nhz)}});
#define INTEGER(field) j[#field] = c->field
  INTEGER(max_payload_bytes);
  INTEGER(max_events);
  INTEGER(minimum_lease_ms);
  INTEGER(maximum_lease_ms);
  INTEGER(response_cache_entries);
  INTEGER(response_cache_ttl_seconds);
  INTEGER(terminal_record_entries);
  INTEGER(terminal_record_ttl_seconds);
#undef INTEGER
#define DECIMAL(field) j[#field] = std::to_string(c->field)
  DECIMAL(max_job_duration_ns);
  DECIMAL(minimum_arm_lead_ns);
  DECIMAL(maximum_arm_ahead_ns);
  DECIMAL(maximum_arm_uncertainty_ns);
  DECIMAL(maximum_holdover_age_ns);
  DECIMAL(output_disable_timeout_ns);
#undef DECIMAL
  return j;
}
} // namespace
std::string wtp_runtime_status_json(const WtpRuntimeStatus &s) {
  Json j{
      {"schema", "wsprrypi.wtp-runtime/1"},
      {"phase", name(s.phase, std::array{"idle", "waiting", "invalidated",
                                         "preparing", "executing", "blocked"})},
      {"session_phase",
       name(s.session_phase,
            std::array{"disconnected", "hello", "status", "caps", "ready",
                       "identity_changed", "fault"})},
      {"job_id", s.job_id},
      {"session_id", s.session_id},
      {"owner_id", s.owner_id},
      {"diagnostic", s.diagnostic},
      {"session_diagnostic", s.session_diagnostic},
      {"capabilities", capabilities(s.capabilities)},
      {"job", evidence(s.job)},
      {"adjustments", adjustments(s.adjustments)},
      {"last_report", report(s.last_report)},
      {"last_recovery", nullptr},
      {"identity", nullptr},
      {"remote", nullptr},
      {"status_observed_ms", s.status_observed_ms
                                 ? Json(std::to_string(*s.status_observed_ms))
                                 : Json(nullptr)},
      {"request_id", std::to_string(s.request_id.value)}};
#define DECIMAL(field) j[#field] = std::to_string(s.field)
  DECIMAL(revision);
  DECIMAL(observed_ms);
  DECIMAL(start_utc_ns);
  DECIMAL(dispatch_utc_ns);
#undef DECIMAL
#define BOOLEAN(field) j[#field] = s.field
  BOOLEAN(arm_handed_off);
  BOOLEAN(stop_requested);
  BOOLEAN(reload_requested);
  BOOLEAN(uncertain);
  BOOLEAN(safety_fault);
  BOOLEAN(owns);
  BOOLEAN(lease_valid);
  BOOLEAN(recovery_required);
#undef BOOLEAN
  j["identity"] = identity(s.identity);
  if (s.remote) {
    j["remote"] = {{"boot_id", s.remote->boot_id},
                   {"state", state(s.remote->state)},
                   {"output_active", s.remote->output_active},
                   {"owner_id", nullable(s.remote->owner_id)},
                   {"job_id", nullable(s.remote->job_id)}};
  }
  if (s.last_recovery)
    j["last_recovery"] = {
        {"observed_ms", std::to_string(s.last_recovery->observed_ms)},
        {"cleanup", cleanup(s.last_recovery->cleanup)},
        {"request_id", std::to_string(s.last_recovery->request_id.value)},
        {"job_id", s.last_recovery->job_id},
        {"identity", identity(s.last_recovery->identity)},
        {"job", evidence(s.last_recovery->job)}};
  return j.dump();
}
} // namespace wsprrypi
