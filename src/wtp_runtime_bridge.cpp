// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_runtime_bridge.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "json.hpp"
#include "wtp_integration/usb_cdc.hpp"
#include "wtp_integration/tls.hpp"
#include "wtp_integration/plain_tcp.hpp"
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace {
using NativeRuntime = wsprrypi::WtpTargetRuntime;
std::mutex operation_mutex, runtime_mutex;
std::shared_ptr<NativeRuntime> runtime;
std::shared_ptr<NativeRuntime> get() {
  std::lock_guard lock(runtime_mutex);
  return runtime;
}
std::shared_ptr<NativeRuntime> require() {
  auto r = get();
  if (!r)
    throw std::runtime_error("Pico backend is not selected");
  return r;
}
} // namespace
void set_wtp_runtime_for_test(WtpSettings settings,
                              wsprrypi::WtpScheduleClock &clock,
                              wsprrypi::wtp::ByteStream &stream,
                              wsprrypi::wtp::SessionOptions options,
                              std::function<bool()> reopen) {
  std::lock_guard operation(operation_mutex);
  std::lock_guard lock(runtime_mutex);
  if (runtime)
    throw std::logic_error("Test runtime must start unselected");
  runtime =
      std::make_shared<NativeRuntime>(std::move(settings), clock, stream,
                                      std::move(options), std::move(reopen));
}
std::string
wtp_runtime_selection_error(const std::optional<WtpSettings> &settings) {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  try {
    if (settings) validate_wtp_settings(*settings, true);
    if (settings && !settings->device_id.empty() &&
        (!r || r->settings.device_id != settings->device_id) &&
        wsprrypi::wtp_output_identity_in_use(settings->device_id))
      return "This WTP output already has an independent fleet assignment";
    bool changed = r && (!settings || r->settings != *settings);
    if (settings && settings->transport == "network") {
      wsprrypi::TlsCredentials candidate({settings->hostname, settings->tls_identity,
          settings->tls_ca, settings->tls_certificate, settings->tls_key,
          static_cast<unsigned>(settings->tcp_port)});
      changed = changed || (r && (!r->credentials || !r->credentials->matches(candidate)));
    }
    if (r && changed && !r->app->replaceable())
      return "Resolve Pico ownership and output before changing transport, endpoint or credentials";
  } catch (const std::exception &error) { return error.what(); }
  return {};
}
nlohmann::json wtp_runtime_safe_probe(const std::function<nlohmann::json()> &probe) {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  if (r && !r->app->replaceable())
    throw std::runtime_error("Resolve Pico ownership and output before identifying another device");
  return probe();
}
bool wtp_runtime_switch_allowed() noexcept {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  return !r || r->app->replaceable();
}
void wtp_runtime_prepare_skip() {
  std::lock_guard operation(operation_mutex);
  require()->app->prepare_skip();
}
void select_wtp_runtime(const std::optional<WtpSettings> &settings) {
  std::lock_guard operation(operation_mutex);
  std::lock_guard lock(runtime_mutex);
  if (runtime && settings && runtime->settings == *settings &&
      runtime->credentials_current())
    return;
  if (runtime && !runtime->app->replaceable())
    throw std::runtime_error("Resolve Pico ownership and output before "
                             "changing the backend or endpoint");
  auto lease = runtime && settings && runtime->settings.device_id == settings->device_id
      ? runtime->output_lease : std::shared_ptr<void>{};
  runtime = settings ? std::make_shared<NativeRuntime>(*settings, std::function<bool()>{}, std::move(lease)) : nullptr;
}
bool wtp_runtime_selected() noexcept { return static_cast<bool>(get()); }
bool wtp_runtime_ready() noexcept {
  auto r = get();
  return r && r->app->ready();
}
bool wtp_runtime_invalidate_for_reload() {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  if (!r)
    return false;
  r->app
      ->invalidate(); // CAS decides whether waiting work or committed work won.
  const auto phase = r->app->phase();
  if (phase == wsprrypi::WtpSchedulePhase::Preparing ||
      phase == wsprrypi::WtpSchedulePhase::Executing)
    return true;
  (void)r->app->stop();
  return false;
}
bool wtp_runtime_defers_reload() noexcept {
  auto r = get();
  if (!r)
    return false;
  auto phase = r->app->status().phase;
  return phase == wsprrypi::WtpSchedulePhase::Preparing ||
         phase == wsprrypi::WtpSchedulePhase::Executing;
}
WsprTransmitState wtp_runtime_state() noexcept {
  auto r = get();
  if (!r)
    return WsprTransmitState::DISABLED;
  auto s = r->app->status();
  if (s.recovery_required || s.safety_fault || !r->app->ready())
    return WsprTransmitState::FAILED;
  if (s.job && s.job->state == wsprrypi::wtp::State::Running)
    return WsprTransmitState::TRANSMITTING;
  if (r->app->active() || r->app->skipping() ||
      s.phase != wsprrypi::WtpSchedulePhase::Idle)
    return WsprTransmitState::ENABLED;
  if (s.last_report) {
    if (s.last_report->outcome == wsprrypi::WtpScheduleOutcome::Complete)
      return WsprTransmitState::COMPLETE;
    if (s.last_report->outcome == wsprrypi::WtpScheduleOutcome::Cancelled)
      return WsprTransmitState::CANCELLED;
    if (s.last_report->outcome == wsprrypi::WtpScheduleOutcome::Failed)
      return WsprTransmitState::FAILED;
  }
  return WsprTransmitState::DISABLED;
}
wsprrypi::TransmissionMode wtp_runtime_mode() noexcept {
  auto r = get();
  return r ? r->app->mode() : wsprrypi::TransmissionMode::WSPR;
}
void wtp_runtime_prepare(wsprrypi::TransmissionRequest r) {
  std::lock_guard operation(operation_mutex);
  require()->app->prepare(std::move(r));
}
std::chrono::nanoseconds wtp_runtime_preparation_lead() {
  return std::chrono::nanoseconds(require()->app->preparation_lead_ns());
}
void wtp_runtime_start() {
  std::lock_guard operation(operation_mutex);
  require()->app->start();
}
wsprrypi::CleanupResult wtp_runtime_stop() {
  auto r = get();
  if (r && r->tls.opening()) r->tls.cancel();
  std::lock_guard operation(operation_mutex);
  r = get();
  return r ? r->app->stop() : wsprrypi::CleanupResult{true, {}};
}
wsprrypi::StartupQuiesceResult wtp_runtime_inspect() {
  std::lock_guard operation(operation_mutex);
  return require()->app->inspect();
}
void wtp_runtime_poll_idle() {
  std::unique_lock operation(operation_mutex, std::try_to_lock);
  if (!operation) return;
  auto r = get();
  if (r) (void)r->app->poll_idle();
}
wsprrypi::CleanupResult wtp_runtime_recover() {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  return r ? r->app->recover()
           : wsprrypi::CleanupResult{false, "Pico is not selected"};
}
std::string wtp_runtime_json() {
  auto r = get();
  if (!r)
    return R"({"selected":false})";
  auto j = nlohmann::json::parse(
      wsprrypi::wtp_runtime_status_json(r->app->status()));
  j["now_ms"] = std::to_string(r->clock.now_ms());
  j["selected"] = true;
  j["transport"] = r->settings.transport;
  if (r->settings.transport == "network" || r->settings.transport == "network_plain") {
    const auto n = r->settings.transport == "network" ? r->tls.observation()
                                                       : r->plain.observation();
    j["network"] = {{"hostname", r->settings.hostname}, {"port", r->settings.tcp_port},
        {"expected_identity", r->settings.transport == "network"
            ? wsprrypi::canonical_network_identity(r->settings.tls_identity.empty() ? r->settings.hostname : r->settings.tls_identity).value_or("") : ""},
        {"resolved_address", n.address}, {"authenticated_identity", n.authenticated_identity},
        {"security", r->settings.transport == "network" ? "tls" : "plain_lan"},
        {"state", n.state}, {"diagnostic", n.diagnostic}, {"observed_ms", std::to_string(n.observed_ms)}};
  }
  j["worker_active"] = r->app->active();
  j["host_skip_waiting"] = r->app->skipping();
  j["ready"] = r->app->ready();
  j["host_utc_valid"] = wsprrypi::wtp_host_utc_valid();
  return j.dump();
}
std::optional<wsprrypi::WtpScheduleReport> wtp_runtime_completion() {
  auto r = get();
  return r ? r->app->take_completion() : std::nullopt;
}

wsprrypi::CleanupResult wtp_runtime_cancel_job(const std::string &job_id) {
  std::lock_guard operation(operation_mutex);
  auto r = get();
  if (!r) return {false, "Pico is not selected"};
  const auto status = r->app->status();
  if (job_id.empty() || status.job_id != job_id ||
      (!status.owns && status.phase != wsprrypi::WtpSchedulePhase::Waiting))
    return {false, "Only the current host-owned job may be cancelled"};
  return r->app->stop();
}
wsprrypi::PicoHttpResponse wtp_runtime_management(const std::string &resource,
    const std::string &method, const std::string &body, const std::string &revision) {
  std::unique_lock operation(operation_mutex, std::try_to_lock);
  if (!operation.owns_lock()) return {409, R"({"error":{"code":"busy"}})", {}};
  auto r = get();
  if (!r || r->settings.transport != "network")
    return {409, R"({"error":{"code":"network_transport_required"}})", {}};
  wsprrypi::PicoHttpResponse response;
  try {
    if (!r->credentials_current())
      return {409, R"({"error":{"code":"credentials_changed_reload_required"}})", {}};
    if (!r->app->idle_management([&] {
      response = wsprrypi::pico_http_request(r->tls, r->network_selection(), r->credentials,
          [&] { return r->clock.now_ms(); }, resource, method, body, revision);
    })) return {409, R"({"error":{"code":"host_busy_or_output_unresolved"}})", {}};
  } catch (...) { return {503, R"({"error":{"code":"network_unavailable"}})", {}}; }
  return response;
}
