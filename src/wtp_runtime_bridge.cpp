// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_runtime_bridge.hpp"
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "json.hpp"
#include "wtp_integration/browser_api.hpp"
#include <openssl/crypto.h>
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
  const bool management_requires_selection = runtime && !runtime->management_carrier.empty() &&
      runtime->management_carrier != "engineering_mtls";
  runtime = settings ? std::make_shared<NativeRuntime>(*settings, std::function<bool()>{}, std::move(lease)) : nullptr;
  if (runtime && management_requires_selection) runtime->management_carrier = "disconnected";
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
  if (r && r->consumer_management) {
    if (!r->consumer_management->active()) r->consumer_management->clear();
    // This selected management path owns its authenticated context and needs
    // no background WTP reconnect. Starting a job still negotiates WTP normally.
    return;
  }
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
  if (!r) return {409, R"({"error":{"code":"member_not_selected"}})", {}};
  wsprrypi::PicoHttpResponse response;
  try {
    const auto failure = [](unsigned status, const char* code) {
      return wsprrypi::PicoHttpResponse{status, nlohmann::json{{"error", {{"code", code}}}}.dump(), {}};
    };
    if (resource == "member/management") {
      if (method == "GET") {
        const bool connected = r->consumer_management && r->consumer_management->active();
        return {200, nlohmann::json{{"carrier", r->management_carrier.empty()
            ? (r->settings.transport == "network" ? "engineering_mtls" : "unsupported") : r->management_carrier},
            {"connected", connected}, {"host", r->management_host}}.dump(), {}};
      }
      if (method != "POST" || body.size() > 512) return failure(400, "invalid_management_selection");
      auto input = wsprrypi::strict_browser_json(body);
      auto* credential = input.is_object() && input.contains("password") && input["password"].is_string()
          ? &input["password"].get_ref<std::string&>() : nullptr;
      struct CredentialWipe { std::string* s; ~CredentialWipe() { if (s) OPENSSL_cleanse(s->data(), s->size()); } } credential_wipe{credential};
      if (!input.is_object() || !input.contains("action") || !input["action"].is_string())
        return failure(400, "invalid_management_selection");
      const auto action = input["action"].get<std::string>();
      if (action == "connect") {
        if (input.size() != 4 || !input.contains("carrier") || !input["carrier"].is_string() ||
            !input.contains("host") || !input["host"].is_string() ||
            !input.contains("password") || !input["password"].is_string()) return failure(400, "invalid_management_selection");
        const auto carrier = input["carrier"].get<std::string>(), host = input["host"].get<std::string>();
        const auto canonical = wsprrypi::canonical_network_identity(host);
        if ((carrier != "consumer_station" && carrier != "consumer_ap") ||
            !canonical || *canonical != host || host.find(':') != std::string::npos ||
            (carrier == "consumer_ap" && host != "192.168.4.1") ||
            (carrier == "consumer_station" && host == "192.168.4.1") || r->settings.device_id.empty())
          return failure(400, "invalid_consumer_endpoint");
        if (r->consumer_management && r->consumer_management->active())
          return failure(409, "management_session_already_selected");
        // Erase this parse's credential storage on every exit, including exceptions.
        auto& password = input["password"].get_ref<std::string&>();

        if (!r->app->idle_management([&] {
          r->management_carrier = carrier; r->management_host = host;
          r->consumer_management = std::make_unique<wsprrypi::ConsumerRadioClient>(
              [owner = r.get(), host](const auto& path, const auto& verb, const auto& payload, const auto& media) {
                return wsprrypi::consumer_http_request(owner->management_tcp, [owner] { return owner->clock.now_ms(); },
                    host, path, verb, payload, media);
              }, [owner = r.get()] { return owner->clock.now_ms(); });
          response = r->consumer_management->login(r->settings.device_id, carrier, password);
        }, true)) return failure(409, "host_busy_or_output_unresolved");
        return response;
      }
      if ((action != "disconnect" && action != "engineering_mtls") || input.size() != 1)
        return failure(400, "invalid_management_selection");
      if (action == "engineering_mtls" && r->settings.transport != "network")
        return failure(409, "network_transport_required");
      if (!r->app->idle_management([&] {
        if (r->consumer_management) r->consumer_management->logout();
        r->consumer_management.reset(); r->management_host.clear();
        r->management_carrier = action == "engineering_mtls" ? "engineering_mtls" : "disconnected";
      }, true)) return failure(409, "host_busy_or_output_unresolved");
      return {200, R"({"ok":true})", {}};
    }
    const bool consumer = r->management_carrier == "consumer_station" || r->management_carrier == "consumer_ap";
    if (consumer) {
      if (!r->consumer_management || !r->consumer_management->active()) return failure(401, "consumer_login_required");
      if (!r->app->idle_management([&] {
        const auto exchange = [&](const auto& path, const auto& verb, const auto& payload, const auto& etag) {
          return r->consumer_management->request(path, verb, payload, etag);
        };
        if (resource == "member/configuration")
          response = wsprrypi::pico_configuration_request(exchange, r->consumer_management->device(),
              r->consumer_management->boot(), method, body, revision, r->management_carrier,
              r->consumer_management->generation());
        else response = exchange(resource, method, body, revision);
      }, true)) return failure(409, "host_busy_or_output_unresolved");
      return response;
    }
    if ((!r->management_carrier.empty() && r->management_carrier != "engineering_mtls") || r->settings.transport != "network")
      return failure(409, "management_carrier_required");
    if (!r->credentials_current())
      return {409, R"({"error":{"code":"credentials_changed_reload_required"}})", {}};
    if (!r->app->idle_management([&] {
      const auto exchange = [&](const std::string &path, const std::string &verb,
          const std::string &payload, const std::string &etag) {
        return wsprrypi::pico_http_request(r->tls, r->network_selection(), r->credentials,
            [&] { return r->clock.now_ms(); }, path, verb, payload, etag);
      };
      if (resource == "member/configuration") {
        const auto identity = r->app->status().identity;
        response = wsprrypi::pico_configuration_request(exchange,
            identity ? identity->device_id : "", identity ? identity->boot_id : "",
            method, body, revision);
      } else response = exchange(resource, method, body, revision);
    })) return {409, R"({"error":{"code":"host_busy_or_output_unresolved"}})", {}};
  } catch (...) { return {503, R"({"error":{"code":"network_unavailable"}})", {}}; }
  return response;
}
