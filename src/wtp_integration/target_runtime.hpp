// SPDX-License-Identifier: MIT
#pragma once
#include "application.hpp"
#include "usb_cdc.hpp"
#include "tls.hpp"
#include "plain_tcp.hpp"
#include "consumer_radio.hpp"
#include "configuration.hpp"
#include "output_lease.hpp"
#include "usb_management.hpp"
#include "../pico_usb_installer_state.hpp"
#include <cstdlib>
#include <memory>
namespace wsprrypi {
struct WtpTargetRuntime {
  WtpSettings settings;
  std::shared_ptr<void> output_lease;
  mutable std::mutex output_lease_mutex;
  std::string output_lease_device;
  bool reserve_output(const std::string &device) {
    std::lock_guard lock(output_lease_mutex);
    if (device.empty()) return false;
    if (output_lease) return output_lease_device==device;
    auto lease=wtp_reserve_output_identity(device);
    output_lease_device=device; output_lease=std::move(lease);
    return true;
  }
  std::shared_ptr<void> output_lease_snapshot() const {
    std::lock_guard lock(output_lease_mutex); return output_lease;
  }
  wsprrypi::WtpSystemScheduleClock clock{wsprrypi::wtp_host_utc_valid};
  wsprrypi::PosixCdcSystem system;
  wsprrypi::UsbCdcStream stream{system};
  std::shared_ptr<void> updater_lease;
  std::atomic_bool verified_update_reconnect{false};
  wsprrypi::TlsStream tls{[this] { return clock.now_ms(); }};
  wsprrypi::PlainTcpStream plain{[this] { return clock.now_ms(); }};
  std::shared_ptr<wsprrypi::TlsCredentials> credentials;
  wsprrypi::TlsSelection network_selection() const {
    return {settings.hostname, settings.tls_identity, settings.tls_ca,
            settings.tls_certificate, settings.tls_key,
            static_cast<unsigned>(settings.tcp_port)};
  }
  std::unique_ptr<wsprrypi::WtpApplication> app;
  // Management selection is volatile and independent of the WTP transport.
  std::string management_carrier, management_host;
  std::string management_selection_id{wtp_random_identity()};
  std::unique_ptr<wsprrypi::ConsumerRadioClient> consumer_management;
  wsprrypi::PlainTcpStream management_tcp{[this] { return clock.now_ms(); }};
  UsbCdcStream management_usb{system};
  std::unique_ptr<UsbManagementClient> usb_management;
  // The scheduler admission worker and periodic/UI callers share one lifecycle.
  // Nested ensure/name helpers retain this authority through the full exchange.
  mutable std::recursive_mutex usb_connection_mutex;
  std::function<std::unique_ptr<UsbManagementClient>()> usb_management_for_test;
  std::function<HostUtcObservation()> usb_utc_observer{qualified_host_utc};
  std::optional<std::uint64_t> last_usb_time_ms;
  std::atomic_bool usb_cancelled{false};
  std::string learned_usb_device, learned_usb_boot;
  mutable std::mutex usb_observation_mutex;
  nlohmann::json usb_observation, usb_name;
  bool usb_name_confirmed{}, usb_binding_fault{};
  bool has_usb_binding_fault() const {
    std::lock_guard lock(usb_observation_mutex); return usb_binding_fault;
  }
  void reject_usb_binding() {
    std::lock_guard lock(usb_observation_mutex); usb_binding_fault=true;
  }
  bool usb_unavailable() {
    std::lock_guard lock(usb_observation_mutex);
    usb_observation={{"connected",false},{"diagnostic","USB management unavailable; verify the selected cable and supported firmware"}};
    usb_name_confirmed=false;
    return false;
  }
  // Management identity negotiation is independent of WTP readiness and UTC.
  // No network fallback and no unchecked tty probing.
  bool ensure_usb_management() {
    std::lock_guard connection(usb_connection_mutex);
    if (settings.transport != "usb") return usb_unavailable();
    if (pico_usb_update_blocked(settings.usb_serial) && !verified_update_reconnect) return usb_unavailable();
    if (!usb_management_for_test) {
      if (const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
          disabled && std::string(disabled) == "1") return usb_unavailable();
    }
    if (usb_management && usb_management->connected()) {
      const auto identity = app->status().identity;
      if (identity && (identity->device_id != usb_management->device() || identity->boot_id != usb_management->boot())) {
        reject_usb_binding(); usb_management->close();
        return usb_unavailable();
      }
      return true;
    }
    try {
      usb_unavailable();
      if (usb_management_for_test) usb_management=usb_management_for_test();
      else {
      management_usb.close();
      PicoCdcSelection selection{settings.path,settings.usb_serial,
        static_cast<std::uint16_t>(settings.vendor_id),static_cast<std::uint16_t>(settings.product_id)};
      auto paired = paired_linux_cdc(selection,PicoCdcRole::Management);
      if (!management_usb.begin_open(paired,clock.now_ms())) return usb_unavailable();
      while (management_usb.state() == CdcState::Resetting) {
        if (usb_cancelled) { management_usb.close(); return usb_unavailable(); }
        management_usb.poll_open(clock.now_ms());
        if (management_usb.state() == CdcState::Resetting) clock.wait_ms(5);
      }
      if (management_usb.state() != CdcState::Ready) return usb_unavailable();
      usb_management = std::make_unique<UsbManagementClient>(management_usb,
        [this]{return clock.now_ms();},[this](unsigned ms){clock.wait_ms(ms);},[this]{return usb_cancelled.load();});
      }
      if (!usb_management) return usb_unavailable();
      const auto identity = app->status().identity;
      std::string expected;
      { std::lock_guard lock(usb_observation_mutex);
        expected = !learned_usb_device.empty() ? learned_usb_device :
            identity ? identity->device_id : settings.device_id;
      }
      if (!usb_management->hello(expected)) {
        if (usb_management->observation().value("diagnostic","")=="usb_identity_mismatch") reject_usb_binding();
        return usb_unavailable();
      }
      if (identity && identity->boot_id != usb_management->boot()) { reject_usb_binding(); usb_management->close(); return usb_unavailable(); }
      const auto management_device=usb_management->device(), management_boot=usb_management->boot();
      { std::lock_guard lock(usb_observation_mutex); learned_usb_device=management_device; learned_usb_boot=management_boot; }
      if (!reserve_output(management_device)) { reject_usb_binding(); usb_management->close(); return usb_unavailable(); }
      auto name = usb_management->request("/api/v1/hostname","GET");
      if (name.status == 200) {
        auto parsed = nlohmann::json::parse(name.body);
        if (parsed.value("schema","") == "transmitter-hostname/1" && parsed.contains("target") &&
            parsed["target"].value("device_id","") == management_device &&
            parsed["target"].value("boot_id","") == usb_management->boot()) {
          std::lock_guard lock(usb_observation_mutex); usb_name=std::move(parsed); usb_name_confirmed=true;
        }
      }
      return true;
    } catch (...) { management_usb.close(); return usb_unavailable(); }
  }
  void refresh_usb_time(bool force = false) {
    std::lock_guard connection(usb_connection_mutex);
    if (settings.transport != "usb") return;
    const auto now = clock.now_ms();
    if (!force && last_usb_time_ms && now >= *last_usb_time_ms && now-*last_usb_time_ms < 5000) return;
    last_usb_time_ms=now; // Failures have the same bounded cadence; never spin reopen.
    for (unsigned attempt=0;attempt<2 && !usb_cancelled;++attempt) {
      if (!ensure_usb_management()) return;
      (void)usb_management->synchronize(usb_utc_observer);
      const bool connected=usb_management->connected();
      { std::lock_guard lock(usb_observation_mutex);
        usb_observation=usb_management->observation();
        if (!connected) usb_name_confirmed=false;
      }
      if (connected) return;
      // Pico may reclaim this optional management session to preserve full WTP
      // LOAD capacity. Reopen once, rebind the same device/boot, read the name,
      // and take a fresh one-use time sample. Never replay a configuration write.
    }
  }
  // Only an explicit recovery path may clear a known contradiction.
  // Periodic reconnects never turn an old published WTP identity into proof.
  bool reconcile_usb_binding() {
    std::lock_guard connection(usb_connection_mutex);
    if (settings.transport!="usb" || !ensure_usb_management()) return false;
    const auto identity=app->status().identity;
    if (!identity || identity->device_id!=usb_management->device() || identity->boot_id!=usb_management->boot()) return false;
    std::lock_guard lock(usb_observation_mutex); usb_binding_fault=false; return true;
  }
  nlohmann::json usb_status() const {
    std::lock_guard lock(usb_observation_mutex);
    return {{"management",usb_observation},{"hostname",usb_name},{"name_confirmed",usb_name_confirmed},{"binding_fault",usb_binding_fault}};
  }
  void observe_usb_name(const PicoHttpResponse &response) {
    std::lock_guard connection(usb_connection_mutex);
    if (response.status != 200) return;
    try {
      auto value=nlohmann::json::parse(response.body);
      if (value.value("schema","") != "transmitter-hostname/1" || !usb_management ||
          value.at("target").value("device_id","") != usb_management->device() ||
          value.at("target").value("boot_id","") != usb_management->boot()) return;
      std::lock_guard lock(usb_observation_mutex); usb_name=std::move(value); usb_name_confirmed=true;
    } catch (...) {}
  }
  bool usb_identity_admitted() const {
    if (settings.transport != "usb") return true;
    const auto negotiated=app->status().identity;
    std::lock_guard lock(usb_observation_mutex);
    return !usb_binding_fault && (learned_usb_device.empty() || (negotiated && negotiated->device_id == learned_usb_device &&
        negotiated->boot_id == learned_usb_boot));
  }
  PicoManagementExchange engineering_transport_for_test;
  ConsumerRadioClient::Transport consumer_transport_for_test;

  ~WtpTargetRuntime() {
    // Admission callbacks use binding/lease mutexes declared after app. Join
    // and destroy the worker before automatic member destruction reaches them.
    usb_cancelled=true;
    if (app) { (void)app->stop(); app.reset(); }
    if (usb_management) usb_management->close();
  }
  bool credentials_current() const {
    return settings.transport != "network" ||
           (credentials && credentials->matches(wsprrypi::TlsCredentials(network_selection())));
  }
  bool reopen() {
    if (settings.transport == "usb" && pico_usb_update_blocked(settings.usb_serial) && !verified_update_reconnect) return false;
    if (const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
        disabled && std::string(disabled) == "1") return false;
    if (settings.transport == "network") {
      try {
        if (!credentials_current() || !tls.begin_open(network_selection(), credentials)) return false;
        while (tls.opening()) { tls.poll_open(); if (tls.opening()) clock.wait_ms(5); }
        return tls.ready();
      } catch (...) { return false; }
    }
    if (settings.transport == "network_plain") {
      try {
        if (!plain.begin_open(settings.hostname, static_cast<unsigned>(settings.tcp_port)))
          return false;
        while (plain.opening()) {
          plain.poll_open();
          if (plain.opening()) clock.wait_ms(5);
        }
        return plain.ready();
      } catch (...) { plain.close(); return false; }
    }
    stream.close();
    try { if (!updater_lease) updater_lease=pico_usb_runtime_lease(settings.usb_serial); }
    catch (...) { return false; }
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
  explicit WtpTargetRuntime(WtpSettings s, std::function<bool()> admission = {},
      std::shared_ptr<void> existing_lease = {})
      : settings(std::move(s)), output_lease(std::move(existing_lease)) {
    validate_wtp_settings(settings, true);
    if (output_lease) output_lease_device=settings.device_id;
    if (!settings.device_id.empty() && !reserve_output(settings.device_id))
      throw std::runtime_error("Output identity lease does not match selected device");
    if (settings.transport == "network")
      credentials = std::make_shared<wsprrypi::TlsCredentials>(network_selection());
    wsprrypi::wtp::ByteStream &selected = settings.transport == "network"
        ? static_cast<wsprrypi::wtp::ByteStream &>(tls)
        : settings.transport == "network_plain"
            ? static_cast<wsprrypi::wtp::ByteStream &>(plain)
            : static_cast<wsprrypi::wtp::ByteStream &>(stream);
    wsprrypi::wtp::SessionOptions options{wsprrypi::wtp_random_identity(),
                                         wsprrypi::wtp_random_identity(), settings.device_id};
    options.learn_device_identity = (settings.transport == "network_plain" || settings.transport == "usb") &&
                                    settings.device_id.empty();
    if (settings.transport == "usb") management_carrier = "usb_physical";
    app = std::make_unique<wsprrypi::WtpApplication>(
        clock, selected, settings, std::move(options), [this] { return reopen(); },
        [this, admission = std::move(admission)] {
          // Refresh after LOAD too: preserving maximum job capacity may have
          // reclaimed the optional UMP session. Existing ARM notice still wins.
          refresh_usb_time(true);
          if (!usb_identity_admitted()) return false;
          const auto identity=app->status().identity;
          if (!identity || !reserve_output(identity->device_id)) return false;
          return !admission || admission();
        });
  }
  WtpTargetRuntime(WtpSettings s, wsprrypi::WtpScheduleClock &c,
                wsprrypi::wtp::ByteStream &b,
                wsprrypi::wtp::SessionOptions options,
                std::function<bool()> reopen)
      : settings(std::move(s)),
        app(std::make_unique<wsprrypi::WtpApplication>(
            c, b, settings, std::move(options), std::move(reopen), [this]{refresh_usb_time(true);return usb_identity_admitted();})) {
    if (settings.transport == "network")
      credentials = std::make_shared<wsprrypi::TlsCredentials>(network_selection());
  }
};
} // namespace wsprrypi
