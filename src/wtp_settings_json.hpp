// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "json.hpp"
#include "wtp_settings.hpp"
inline nlohmann::json wtp_settings_json(const WtpSettings &s) {
  return {{"Transport", s.transport},
          {"Hostname", s.hostname}, {"TCP Port", s.tcp_port},
          {"TLS Server Identity", s.tls_identity}, {"TLS CA File", s.tls_ca},
          {"TLS Client Certificate", s.tls_certificate}, {"TLS Client Key", s.tls_key},
          {"Endpoint", s.path},
          {"USB Serial", s.usb_serial},
          {"USB Vendor ID", s.vendor_id},
          {"USB Product ID", s.product_id},
          {"Device ID", s.device_id},
          {"Allow Frequency Adjustment", s.allow_frequency_adjustment},
          {"Start Uncertainty ns", s.start_uncertainty_ns}};
}
inline WtpSettings parse_wtp_settings(const nlohmann::json &j, bool selected) {
  WtpSettings s;
  if (!j.is_object())
    throw std::runtime_error("WTP settings must be an object.");
  s.transport = j.value("Transport", s.transport);
  s.hostname = j.value("Hostname", s.hostname);
  s.tls_identity = j.value("TLS Server Identity", s.tls_identity);
  s.tls_ca = j.value("TLS CA File", s.tls_ca);
  s.tls_certificate = j.value("TLS Client Certificate", s.tls_certificate);
  s.tls_key = j.value("TLS Client Key", s.tls_key);
  s.path = j.value("Endpoint", s.path);
  s.usb_serial = j.value("USB Serial", s.usb_serial);
  s.device_id = j.value("Device ID", s.device_id);
  const auto integer = [&](const char *key, std::uint64_t fallback,
                           std::uint64_t maximum) {
    if (!j.contains(key))
      return fallback;
    const auto &v = j.at(key);
    if ((!v.is_number_integer() && !v.is_number_unsigned()) ||
        (v.is_number_integer() && !v.is_number_unsigned() &&
         v.get<std::int64_t>() < 0) ||
        v.get<std::uint64_t>() > maximum)
      throw std::runtime_error(std::string("Invalid WTP.") + key);
    return v.get<std::uint64_t>();
  };
  s.tcp_port = static_cast<int>(integer("TCP Port", 0, 65535));
  s.vendor_id = static_cast<int>(integer("USB Vendor ID", 0, 65535));
  s.product_id = static_cast<int>(integer("USB Product ID", 0, 65535));
  s.start_uncertainty_ns = integer("Start Uncertainty ns", s.start_uncertainty_ns, 1000000000);
  s.allow_frequency_adjustment = j.value("Allow Frequency Adjustment", false);
  validate_wtp_settings(s, selected);
  return s;
}

// Current management connection projection. Retired execution policy remains
// recognizable only in private INI/catalog records, never in new API input.
inline nlohmann::json management_profile_settings_json(const WtpSettings &settings) {
    auto value=wtp_settings_json(settings);
    value.erase("Start Uncertainty ns");value.erase("Allow Frequency Adjustment");
    return value;
}
inline WtpSettings parse_management_profile_settings(const nlohmann::json &value,bool selected=true) {
    const auto fields=management_profile_settings_json(WtpSettings{});
    if(!value.is_object()||value.size()!=fields.size())throw std::runtime_error("Invalid management profile settings");
    for(const auto &entry:value.items())if(!fields.contains(entry.key()))throw std::runtime_error("Unknown management profile setting");
    return parse_wtp_settings(value,selected);
}
