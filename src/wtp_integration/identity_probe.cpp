// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "identity_probe.hpp"
#include "application.hpp"
#include "tls.hpp"
#include "plain_tcp.hpp"
#include <cstdlib>
#include <stdexcept>

namespace wsprrypi {
nlohmann::json wtp_probe_network_identity(const WtpSettings &settings) {
  if (settings.transport != "network" && settings.transport != "network_plain")
    throw std::runtime_error("Identification requires a network endpoint");
  if (const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
      disabled && std::string(disabled) == "1")
    throw std::runtime_error("Network identification is disabled in hardware-free validation");
  validate_wtp_settings(settings, true);
  WtpSystemScheduleClock clock(wtp_host_utc_valid);
  TlsStream tls([&] { return clock.now_ms(); });
  PlainTcpStream plain([&] { return clock.now_ms(); });
  std::shared_ptr<TlsCredentials> credentials;
  TlsSelection selection{settings.hostname, settings.tls_identity, settings.tls_ca,
      settings.tls_certificate, settings.tls_key, static_cast<unsigned>(settings.tcp_port)};
  if (settings.transport == "network")
    credentials = std::make_shared<TlsCredentials>(selection);
  auto reopen = [&] {
    if (settings.transport == "network") {
      if (!tls.begin_open(selection, credentials)) return false;
      while (tls.opening()) { tls.poll_open(); if (tls.opening()) clock.wait_ms(5); }
      return tls.ready();
    }
    if (!plain.begin_open(settings.hostname, static_cast<unsigned>(settings.tcp_port))) return false;
    while (plain.opening()) { plain.poll_open(); if (plain.opening()) clock.wait_ms(5); }
    return plain.ready();
  };
  wtp::SessionOptions options{wtp_random_identity(), wtp_random_identity(), settings.device_id};
  options.learn_device_identity = settings.transport == "network_plain" &&
                                  settings.device_id.empty();
  WtpApplication app(clock, settings.transport == "network"
      ? static_cast<wtp::ByteStream &>(tls) : static_cast<wtp::ByteStream &>(plain),
      settings, options, reopen);
  const auto result = app.inspect();
  if (!result.ok) throw std::runtime_error("Read-only WTP identification failed: " + result.error);
  const auto observation = app.status();
  if (!observation.identity || !observation.capabilities || !observation.remote)
    throw std::runtime_error("Incomplete WTP identity, capabilities or status");
  return {{"device_id", observation.identity->device_id},
      {"boot_id", observation.identity->boot_id},
      {"binding", settings.transport == "network" ? "tls" : "plain"},
      {"authenticated", settings.transport == "network"},
      {"output_active", observation.remote->output_active}};
}
} // namespace wsprrypi
