// SPDX-License-Identifier: MIT
#pragma once
#include "application.hpp"
#include "usb_cdc.hpp"
#include "tls.hpp"
#include "plain_tcp.hpp"
#include "output_lease.hpp"
#include <cstdlib>
#include <memory>
namespace wsprrypi {
struct WtpTargetRuntime {
  WtpSettings settings;
  std::shared_ptr<void> output_lease;
  wsprrypi::WtpSystemScheduleClock clock{wsprrypi::wtp_host_utc_valid};
  wsprrypi::PosixCdcSystem system;
  wsprrypi::UsbCdcStream stream{system};
  wsprrypi::TlsStream tls{[this] { return clock.now_ms(); }};
  wsprrypi::PlainTcpStream plain{[this] { return clock.now_ms(); }};
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
    if (!settings.device_id.empty() && !output_lease)
      output_lease = wtp_reserve_output_identity(settings.device_id);
    if (settings.transport == "network")
      credentials = std::make_shared<wsprrypi::TlsCredentials>(network_selection());
    wsprrypi::wtp::ByteStream &selected = settings.transport == "network"
        ? static_cast<wsprrypi::wtp::ByteStream &>(tls)
        : settings.transport == "network_plain"
            ? static_cast<wsprrypi::wtp::ByteStream &>(plain)
            : static_cast<wsprrypi::wtp::ByteStream &>(stream);
    wsprrypi::wtp::SessionOptions options{wsprrypi::wtp_random_identity(),
                                         wsprrypi::wtp_random_identity(), settings.device_id};
    options.learn_device_identity = settings.transport == "network_plain" &&
                                    settings.device_id.empty();
    app = std::make_unique<wsprrypi::WtpApplication>(
        clock, selected, settings, std::move(options), [this] { return reopen(); },
        [this, admission = std::move(admission)] {
          if (!output_lease) {
            const auto identity = app->status().identity;
            if (!identity) return false;
            output_lease = wtp_reserve_output_identity(identity->device_id);
          }
          return !admission || admission();
        });
  }
  WtpTargetRuntime(WtpSettings s, wsprrypi::WtpScheduleClock &c,
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
} // namespace wsprrypi
