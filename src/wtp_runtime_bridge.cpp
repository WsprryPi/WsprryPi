// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_runtime_bridge.hpp"
#include "json.hpp"
#include "wtp_integration/usb_cdc.hpp"
#include "wtp_integration/tls.hpp"
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace {
struct NativeRuntime {
  WtpSettings settings;
  wsprrypi::WtpSystemScheduleClock clock{wsprrypi::wtp_host_utc_valid};
  wsprrypi::PosixCdcSystem system;
  wsprrypi::UsbCdcStream stream{system};
  wsprrypi::TlsStream tls{[this] { return clock.now_ms(); }};
  std::shared_ptr<wsprrypi::TlsCredentials> credentials;
  wsprrypi::TlsSelection network_selection() const {
    return {settings.hostname, settings.tls_identity, settings.tls_ca,
            settings.tls_certificate, settings.tls_key,
            static_cast<unsigned>(settings.tcp_port)};
  }
  std::unique_ptr<wsprrypi::WtpApplication> app;
  bool credentials_current() const {
    return settings.transport != "network" ||
           (credentials && credentials->matches(wsprrypi::TlsCredentials(network_selection())));
  }
  bool reopen() {
    if (const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
        disabled && std::string(disabled) == "1") return false;
    if (settings.transport == "network") {
      try {
        if (!credentials_current() || !tls.begin_open(network_selection(), credentials)) return false;
        while (tls.opening()) { tls.poll_open(); if (tls.opening()) clock.wait_ms(5); }
        return tls.ready();
      } catch (...) { return false; }
    }
    stream.close();
    if (!stream.begin_open({settings.path, settings.usb_serial,
                            static_cast<std::uint16_t>(settings.vendor_id),
                            static_cast<std::uint16_t>(settings.product_id)}, clock.now_ms()))
      return false;
    while (stream.state() == wsprrypi::CdcState::Resetting) {
      stream.poll_open(clock.now_ms());
      if (stream.state() == wsprrypi::CdcState::Resetting) clock.wait_ms(10);
    }
    return stream.state() == wsprrypi::CdcState::Ready;
  }
  explicit NativeRuntime(WtpSettings s) : settings(std::move(s)) {
    validate_wtp_settings(settings, true);
    if (settings.transport == "network")
      credentials = std::make_shared<wsprrypi::TlsCredentials>(network_selection());
    wsprrypi::wtp::ByteStream &selected = settings.transport == "network"
        ? static_cast<wsprrypi::wtp::ByteStream &>(tls) : stream;
    app = std::make_unique<wsprrypi::WtpApplication>(
        clock, selected, settings,
        wsprrypi::wtp::SessionOptions{wsprrypi::wtp_random_identity(),
                                    wsprrypi::wtp_random_identity(), settings.device_id},
        [this] { return reopen(); });
  }
  NativeRuntime(WtpSettings s, wsprrypi::WtpScheduleClock &c,
                wsprrypi::wtp::ByteStream &b,
                wsprrypi::wtp::SessionOptions options,
                std::function<bool()> reopen)
      : settings(std::move(s)),
        app(std::make_unique<wsprrypi::WtpApplication>(
            c, b, settings, std::move(options), std::move(reopen))) {
    if (settings.transport == "network")
      credentials = std::make_shared<wsprrypi::TlsCredentials>(network_selection());
  }
};
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
    bool changed = r && (!settings || r->settings != *settings);
    if (settings && settings->transport == "network") {
      validate_wtp_settings(*settings, true);
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
  runtime = settings ? std::make_shared<NativeRuntime>(*settings) : nullptr;
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
  if (r->settings.transport == "network") {
    const auto n = r->tls.observation();
    j["network"] = {{"hostname", r->settings.hostname}, {"port", r->settings.tcp_port},
        {"expected_identity", wsprrypi::canonical_network_identity(r->settings.tls_identity.empty() ? r->settings.hostname : r->settings.tls_identity).value_or("")},
        {"resolved_address", n.address}, {"authenticated_identity", n.authenticated_identity},
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
