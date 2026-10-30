// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "udp_logging/udp_wire.hpp"
#include <set>
#include <stdexcept>
namespace wsprrypi::udp {
using Json = nlohmann::json;
inline void require(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(reason);
}
inline bool fields(const Json &v, std::initializer_list<const char *> names) {
  if (!v.is_object() || v.size() != names.size())
    return false;
  for (auto name : names)
    if (!v.contains(name))
      return false;
  return true;
}
inline Json parse(const std::string &text, std::size_t limit = 16384) {
  require(text.size() <= limit, "udp_document_size");
  std::vector<std::set<std::string>> keys;
  return Json::parse(
      text, [&](int depth, Json::parse_event_t kind, Json &value) {
        require(depth <= 8, "udp_document_depth");
        if (kind == Json::parse_event_t::object_start)
          keys.emplace_back();
        if (kind == Json::parse_event_t::object_end)
          keys.pop_back();
        if (kind == Json::parse_event_t::key)
          require(keys.back().insert(value.get<std::string>()).second,
                  "udp_duplicate_key");
        return true;
      });
}
inline bool text(const Json &v, std::size_t maximum, bool empty = false) {
  if (!v.is_string())
    return false;
  const auto &s = v.get_ref<const std::string &>();
  return (empty || !s.empty()) && s.size() <= maximum &&
         std::all_of(s.begin(), s.end(),
                     [](unsigned char c) { return c >= 32 && c <= 126; });
}
inline bool integer(const Json &v, unsigned minimum, unsigned maximum) {
  return v.is_number_integer() && !v.is_boolean() && v >= minimum &&
         v <= maximum;
}
inline std::optional<std::uint32_t> ipv4(std::string_view s) {
  std::uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const auto at = s.find('.');
    if ((i < 3 && at == s.npos) || (i == 3 && at != s.npos))
      return {};
    std::uint64_t v{};
    if (!wsprry::logging::decimal(i == 3 ? s : s.substr(0, at), v) || v > 255)
      return {};
    n = (n << 8) | static_cast<std::uint32_t>(v);
    if (i < 3)
      s.remove_prefix(at + 1);
  }
  if ((n >> 24) == 0 || (n >> 24) == 127 || (n >> 24) >= 224)
    return {};
  return n;
}
inline bool slot(const Json &v, std::size_t bound = 32) {
  if (!text(v, bound))
    return false;
  const auto s = v.get<std::string>();
  return std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
  });
}
inline bool interface_name(const Json &v) {
  if (!text(v, 15))
    return false;
  const auto value = v.get<std::string>();
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
           c == ':';
  });
}
inline void sender(const Json &s, bool host = false) {
  require(host
              ? fields(s, {"enabled", "destination_ipv4", "destination_port",
                           "maximum_severity", "event_codes", "rate", "burst",
                           "interface", "native_enabled", "usb_sources"})
              : fields(s, {"enabled", "destination_ipv4", "destination_port",
                           "maximum_severity", "event_codes", "rate", "burst"}),
          "udp_sender_shape");
  require(s["enabled"].is_boolean() &&
              integer(s["destination_port"], 1, 65535) &&
              integer(s["maximum_severity"], 0, 7) &&
              integer(s["rate"], 1, host ? 64 : 8) &&
              integer(s["burst"], 1, host ? 128 : 16),
          "udp_sender_types");
  require(s["destination_ipv4"].is_null() ||
              (text(s["destination_ipv4"], 15) &&
               ipv4(s["destination_ipv4"].get<std::string>()).has_value()),
          "udp_destination");
  require(!s["enabled"].get<bool>() || !s["destination_ipv4"].is_null(),
          "udp_destination_required");
  require(s["event_codes"].is_array() && s["event_codes"].size() <= 16,
          "udp_event_codes");
  std::set<std::string> codes;
  for (const auto &code : s["event_codes"])
    require(text(code, 32) &&
                wsprry::logging::token(code.get<std::string>(), 32) &&
                codes.insert(code.get<std::string>()).second,
            "udp_event_code");
  if (host) {
    require(s["native_enabled"].is_boolean() &&
                (s["interface"].is_null() || interface_name(s["interface"])) &&
                (!s["enabled"].get<bool>() || !s["interface"].is_null()) &&
                s["usb_sources"].is_array() && s["usb_sources"].size() <= 8,
            "udp_host_sender");
    std::set<std::string> devices, profiles;
    for (const auto &source : s["usb_sources"])
      require(
          fields(source, {"profile_id", "device_id"}) &&
              source["profile_id"].is_string() &&
              wsprry::logging::hex_id(
                  source["profile_id"].get<std::string>()) &&
              source["device_id"].is_string() &&
              wsprry::logging::hex_id(source["device_id"].get<std::string>()) &&
              devices.insert(source["device_id"].get<std::string>()).second &&
              profiles.insert(source["profile_id"].get<std::string>()).second,
          "udp_usb_sources");
  }
}
inline void collector(const Json &c) {
  require(fields(c, {"enabled", "bind_ipv4", "interface", "port", "peers"}),
          "udp_collector_shape");
  require(c["enabled"].is_boolean() && integer(c["port"], 1, 65535) &&
              (c["interface"].is_null() || interface_name(c["interface"])) &&
              (c["bind_ipv4"].is_null() ||
               (text(c["bind_ipv4"], 15) &&
                ipv4(c["bind_ipv4"].get<std::string>()).has_value())) &&
              (!c["enabled"].get<bool>() ||
               (!c["bind_ipv4"].is_null() && !c["interface"].is_null())) &&
              c["peers"].is_array() && c["peers"].size() <= 32,
          "udp_collector_types");
  std::set<std::string> slots, addresses;
  for (const auto &p : c["peers"])
    require(fields(p, {"slot", "ipv4"}) && slot(p["slot"]) &&
                text(p["ipv4"], 15) &&
                ipv4(p["ipv4"].get<std::string>()).has_value() &&
                slots.insert(p["slot"].get<std::string>()).second &&
                addresses.insert(p["ipv4"].get<std::string>()).second,
            "udp_peers");
}
inline void settings(const Json &s) {
  require(fields(s, {"sender", "collector"}), "udp_settings_shape");
  sender(s["sender"], true);
  collector(s["collector"]);
}
inline Json defaults() {
  return {{"sender",
           {{"enabled", false},
            {"destination_ipv4", nullptr},
            {"destination_port", 5514},
            {"maximum_severity", 6},
            {"event_codes", Json::array()},
            {"rate", 16},
            {"burst", 32},
            {"interface", nullptr},
            {"native_enabled", false},
            {"usb_sources", Json::array()}}},
          {"collector",
           {{"enabled", false},
            {"bind_ipv4", nullptr},
            {"interface", nullptr},
            {"port", 5514},
            {"peers", Json::array()}}}};
}
inline Json counters() {
  Json j = Json::object();
  for (auto key : {"generated", "filtered", "redacted_rejected", "queued",
                   "overflow", "expired", "reconfigured_drops", "truncated",
                   "attempted", "locally_submitted", "locally_failed"})
    j[key] = "0";
  return j;
}
inline Json collector_counters() {
  Json j = Json::object();
  for (auto key : {"received", "oversize", "disallowed", "malformed",
                   "duplicate", "conflict", "limited", "overflow",
                   "journal_failed", "journal_submitted", "discontinuity"})
    j[key] = "0";
  return j;
}
inline void count(Json &c, const char *key) {
  std::uint64_t n{};
  require(c[key].is_string() &&
              wsprry::logging::decimal(c[key].get<std::string>(), n),
          "udp_counter");
  if (n != UINT64_MAX)
    c[key] = std::to_string(n + 1);
}
inline void target(const Json &t, std::string_view device,
                   std::string_view boot) {
  require(fields(t, {"scope", "device_id", "boot_id"}) &&
              t["scope"] == "member" && t["device_id"] == device &&
              t["boot_id"] == boot,
          "udp_member_binding");
}
inline void capabilities(const Json &c, std::string_view device,
                         std::string_view boot) {
  require(fields(c, {"schema", "target", "resource", "settings_schema",
                     "wire_schema", "limits"}) &&
              c["schema"] == "udp-logging-capabilities" &&
              c["resource"] == "/api/control/v1/udp-logging" &&
              c["settings_schema"] == "udp-logging" &&
              c["wire_schema"] == "wsprry-udp/1",
          "udp_capabilities");
  target(c["target"], device, boot);
  const Json limits = {{"payload_bytes", 480},    {"queue_records", 16},
                       {"maximum_sources", 0},    {"maximum_peers", 0},
                       {"maximum_rate", 8},       {"maximum_burst", 16},
                       {"address_family", "ipv4"}};
  require(c["limits"] == limits, "udp_capability_limits");
  for (auto key : {"payload_bytes", "queue_records", "maximum_sources",
                   "maximum_peers", "maximum_rate", "maximum_burst"})
    require(c["limits"][key].is_number_integer(), "udp_capability_limit_type");
}
inline void member_readback(const Json &v, std::string_view device,
                            std::string_view boot) {
  require(fields(v, {"schema", "target", "saved", "active"}) &&
              v["schema"] == "udp-logging",
          "udp_member_resource");
  target(v["target"], device, boot);
  sender(v["saved"]);
  const auto &a = v["active"];
  require(
      fields(a,
             {"state", "applied_revision", "receipt", "queued", "counters"}) &&
          a["receipt"] == "unknown" && integer(a["queued"], 0, 16) &&
          (a["applied_revision"].is_null() || text(a["applied_revision"], 128)),
      "udp_active_shape");
  require(a["state"].is_string() &&
              std::set<std::string>{"disabled", "awaiting_network",
                                    "invalid_destination", "ready",
                                    "submission_failed", "unavailable"}
                  .contains(a["state"].get<std::string>()),
          "udp_active_state");
  auto expected = counters();
  require(a["counters"].is_object() && a["counters"].size() == expected.size(),
          "udp_counters");
  for (const auto &item : expected.items()) {
    std::uint64_t n{};
    require(a["counters"].contains(item.key()) &&
                a["counters"][item.key()].is_string() &&
                wsprry::logging::decimal(
                    a["counters"][item.key()].get<std::string>(), n),
            "udp_counter");
  }
}
inline void host_active(const Json &v, const Json &saved) {
  require(
      fields(v, {"applied_revision", "sender", "collector"}) &&
          (v["applied_revision"].is_null() || text(v["applied_revision"], 128)),
      "udp_host_active");
  const auto &a = v["sender"];
  require(fields(a, {"state", "receipt", "queued", "counters"}) &&
              a["receipt"] == "unknown" && integer(a["queued"], 0, 256) &&
              a["state"].is_string() &&
              std::set<std::string>{"disabled", "awaiting_network",
                                    "invalid_destination", "ready",
                                    "submission_failed", "unavailable"}
                  .contains(a["state"].get<std::string>()),
          "udp_sender_active");
  const auto check = [](const Json &actual, const Json &expected) {
    require(actual.is_object() && actual.size() == expected.size(),
            "udp_active_counters");
    for (const auto &key : expected.items()) {
      std::uint64_t n{};
      require(
          actual.contains(key.key()) && actual[key.key()].is_string() &&
              wsprry::logging::decimal(actual[key.key()].get<std::string>(), n),
          "udp_active_counter");
    }
  };
  check(a["counters"], counters());
  const auto &c = v["collector"];
  require(fields(c, {"state", "bind_ipv4", "port", "counters", "peers"}) &&
              c["state"].is_string() &&
              std::set<std::string>{"disabled", "awaiting_network", "ready",
                                    "bind_failed", "unavailable",
                                    "policy_invalid"}
                  .contains(c["state"].get<std::string>()) &&
              (c["bind_ipv4"].is_null() ||
               (text(c["bind_ipv4"], 15) &&
                ipv4(c["bind_ipv4"].get<std::string>()).has_value())) &&
              (c["port"].is_null() || integer(c["port"], 1, 65535)) &&
              c["peers"].is_array() && c["peers"].size() <= 32,
          "udp_collector_active");
  check(c["counters"], collector_counters());
  std::set<std::string> slots;
  for (const auto &p : c["peers"]) {
    std::uint64_t n{};
    require(fields(p, {"slot", "received", "last_receipt_us"}) &&
                slot(p["slot"]) &&
                slots.insert(p["slot"].get<std::string>()).second &&
                p["received"].is_string() &&
                wsprry::logging::decimal(p["received"].get<std::string>(), n) &&
                (p["last_receipt_us"].is_null() ||
                 (p["last_receipt_us"].is_string() &&
                  wsprry::logging::decimal(
                      p["last_receipt_us"].get<std::string>(), n))),
            "udp_peer_status");
    require(std::any_of(saved["collector"]["peers"].begin(),
                        saved["collector"]["peers"].end(),
                        [&](const auto &allowed) {
                          return allowed["slot"] == p["slot"];
                        }),
            "udp_status_peer_binding");
  }
}
} // namespace wsprrypi::udp
