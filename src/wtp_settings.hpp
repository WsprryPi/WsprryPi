// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include <algorithm>
#include "wtp_integration/identity.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>

struct WtpSettings {
  std::string path, usb_serial, device_id;
  int vendor_id{}, product_id{};
  std::uint64_t start_uncertainty_ns{1000000};
  bool allow_frequency_adjustment{false};
  std::string transport{"usb"};
  std::string hostname{}, tls_identity{}, tls_ca{}, tls_certificate{}, tls_key{};
  int tcp_port{};
  bool operator==(const WtpSettings &) const = default;
};
inline void validate_wtp_settings(const WtpSettings &s, bool selected) {
  const auto text = [](const std::string &v, std::size_t limit) {
    return v.size() <= limit &&
           std::none_of(v.begin(), v.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
  };
  if (!text(s.path, 512) || !text(s.usb_serial, 128) ||
      !text(s.device_id, 32) || s.vendor_id < 0 || s.vendor_id > 65535 ||
      s.product_id < 0 || s.product_id > 65535 || s.start_uncertainty_ns == 0 ||
      s.start_uncertainty_ns > 1000000000)
    throw std::runtime_error(
        "Invalid WTP endpoint or start uncertainty (1–1000000000 ns).");
  const auto name = [](const std::string &v) {
    return v.empty() || wsprrypi::canonical_network_identity(v).has_value();
  };
  if ((s.transport != "usb" && s.transport != "network") ||
      !name(s.hostname) || !name(s.tls_identity) ||
      !text(s.tls_ca, 512) || !text(s.tls_certificate, 512) || !text(s.tls_key, 512) ||
      s.tcp_port < 0 || s.tcp_port > 65535)
    throw std::runtime_error("Invalid WTP transport or network settings.");
  if (!selected)
    return;
  if (s.device_id.size() != 32 ||
      !std::all_of(s.device_id.begin(), s.device_id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      })) throw std::runtime_error("Pico requires a 32 lowercase hex device ID.");
  if (s.transport == "network") {
    if (s.hostname.empty() || !s.tcp_port || !s.tls_ca.starts_with("/") ||
        !s.tls_certificate.starts_with("/") || !s.tls_key.starts_with("/"))
      throw std::runtime_error("Network Pico requires a hostname/IP, TCP port and absolute local CA, client certificate and private-key references.");
    return;
  }
  if (!s.path.starts_with("/dev/") ||
      s.path.find("/../") != std::string::npos || s.usb_serial.empty() ||
      !s.vendor_id || !s.product_id || s.device_id.size() != 32 ||
      !std::all_of(s.device_id.begin(), s.device_id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      }))
    throw std::runtime_error(
        "Pico requires an explicit /dev/ WTP endpoint, USB serial, nonzero "
        "VID/PID and 32 lowercase hex device ID.");
}
