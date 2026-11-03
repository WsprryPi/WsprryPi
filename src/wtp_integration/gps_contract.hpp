// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <algorithm>
#include <initializer_list>
#include <regex>
#include <set>
#include <string>

namespace wsprrypi::gps_contract {
using Json = nlohmann::json;
inline bool fields(const Json &v, std::initializer_list<const char *> names) {
  if (!v.is_object() || v.size() != names.size())
    return false;
  for (const auto name : names)
    if (!v.contains(name))
      return false;
  return true;
}
inline bool integer(const Json &v, std::int64_t low, std::int64_t high) {
  return v.is_number_integer() && v >= low && v <= high;
}
inline bool gpio(const Json &v) {
  return integer(v, 0, 22) || integer(v, 26, 28);
}
inline int uart(const Json &gp, bool tx = false) {
  if (!integer(gp, 0, 28))
    return -1;
  const auto n = gp.get<unsigned>();
  if (tx)
    return n == 0 || n == 12 || n == 16 || n == 28 ? 0
           : n == 4 || n == 8 || n == 20           ? 1
                                                   : -1;
  return n == 1 || n == 13 || n == 17  ? 0
         : n == 5 || n == 9 || n == 21 ? 1
                                       : -1;
}
inline bool settings(const Json &v) {
  if (!fields(v, {"enabled", "receiver", "rx_gp", "pps_gp", "tx_gp", "baud",
                  "pps_active_high", "pairing_qualified", "utc_second_offset",
                  "pps_offset_ns", "minimum_message_delay_ms",
                  "maximum_message_delay_ms", "receiver_uncertainty_us",
                  "capture_uncertainty_us", "location",
                  "position_maximum_age_s", "automatic_rf_calibration",
                  "calibration_maximum_age_s", "daylight"}))
    return false;
  for (const auto key : {"enabled", "pps_active_high", "pairing_qualified",
                         "automatic_rf_calibration"})
    if (!v[key].is_boolean())
      return false;
  if ((v["receiver"] != "qlg3" && v["receiver"] != "e108") ||
      uart(v["rx_gp"]) < 0 || !gpio(v["pps_gp"]) ||
      v["pps_gp"].get<unsigned>() % 2 != 1 || v["pps_gp"] == v["rx_gp"] ||
      (v["baud"] != 9600 && v["baud"] != 19200 && v["baud"] != 38400 &&
       v["baud"] != 115200) ||
      !v["baud"].is_number_integer())
    return false;
  if (!v["tx_gp"].is_null() &&
      (v["receiver"] == "qlg3" || uart(v["tx_gp"], true) < 0 ||
       uart(v["tx_gp"], true) != uart(v["rx_gp"]) || v["tx_gp"] == v["pps_gp"]))
    return false;
  if (!integer(v["utc_second_offset"], -1, 1) ||
      !integer(v["pps_offset_ns"], -500000000, 500000000) ||
      !integer(v["minimum_message_delay_ms"], 0, 1999) ||
      !integer(v["maximum_message_delay_ms"], 0, 1999) ||
      v["minimum_message_delay_ms"] > v["maximum_message_delay_ms"] ||
      v["maximum_message_delay_ms"].get<unsigned>() -
          v["minimum_message_delay_ms"].get<unsigned>() >= 1000 ||
      !integer(v["receiver_uncertainty_us"], 1, 100000) ||
      !integer(v["capture_uncertainty_us"], 1, 100000) ||
      !integer(v["position_maximum_age_s"], 1, 3600) ||
      !integer(v["calibration_maximum_age_s"], 10, 86400) ||
      (v["location"] != "manual" && v["location"] != "gps") ||
      (v["daylight"] != "always" && v["daylight"] != "day" &&
       v["daylight"] != "night"))
    return false;
  if (v["enabled"] == false &&
      (v["location"] != "manual" || v["daylight"] != "always" ||
       v["automatic_rf_calibration"] == true))
    return false;
  return v["automatic_rf_calibration"] == false ||
         v["pairing_qualified"] == true;
}
inline bool allocation(const Json &config,
                       const Json &selectors = Json::array()) {
  if (!config.contains("gps") || config["gps"].is_null())
    return true;
  const auto &gps = config["gps"];
  if (!settings(gps))
    return false;
  if (gps["enabled"] == false)
    return true;
  if (!config.contains("pins") || !config["pins"].is_object() ||
      !selectors.is_array())
    return false;
  const auto &p = config["pins"];
  std::set<unsigned> used;
  const auto claim = [&](const Json &pin) {
    return pin.is_null() ||
           (gpio(pin) && used.insert(pin.get<unsigned>()).second);
  };
  for (const auto key : {"rf_gp", "button_gp", "amplifier_gp"})
    if (!p.contains(key) || !claim(p[key]))
      return false;
  if (!p.contains("indicator") || !p.contains("indicator_gp") ||
      (p["indicator"] == "external" && !claim(p["indicator_gp"])))
    return false;
  if (p["engine"] == "i2c") {
    static constexpr unsigned pairs[][2] = {
        {0, 1}, {4, 5}, {8, 9},   {12, 13}, {16, 17}, {20, 21},
        {2, 3}, {6, 7}, {10, 11}, {14, 15}, {18, 19}, {26, 27}};
    if (!p.contains("i2c_pair") || !integer(p["i2c_pair"], 0, 11) ||
        !config.contains("si5351") || !config["si5351"].is_object())
      return false;
    const auto &si = config["si5351"];
    const auto pair = p["i2c_pair"].get<unsigned>();
    if (!claim(pairs[pair][0]) || !claim(pairs[pair][1]) ||
        !si.contains("loopback_input_gp") || !si.contains("rf_gate_gp") ||
        !claim(si["loopback_input_gp"]) || !claim(si["rf_gate_gp"]))
      return false;
    if (!si["loopback_input_gp"].is_null() &&
        ((si["loopback_input_gp"].get<unsigned>() >> 1) & 7) ==
            ((gps["pps_gp"].get<unsigned>() >> 1) & 7))
      return false;
  }
  for (const auto &output : selectors)
    if (!output.is_object() || !output.contains("gp") || !claim(output["gp"]))
      return false;
  for (const auto key : {"rx_gp", "pps_gp", "tx_gp"})
    if (!claim(gps[key]))
      return false;
  return true;
}
inline bool decimal(const Json &v) {
  if (!v.is_string())
    return false;
  const auto &s = v.get_ref<const std::string &>();
  return !s.empty() && s.size() <= 20 && (s.size() == 1 || s[0] != '0') &&
         std::all_of(s.begin(), s.end(),
                     [](char c) { return c >= '0' && c <= '9'; }) &&
         (s.size() < 20 || s <= "18446744073709551615");
}
inline bool calibration(const Json &v) {
  if (v.is_null())
    return true;
  if (!fields(v, {"oscillator_error_ppb", "uncertainty_ppb", "observed_raw_ns",
                  "age_ns", "span_ns", "generation", "fresh"}) ||
      !integer(v["oscillator_error_ppb"], -100000, 100000) ||
      !integer(v["uncertainty_ppb"], 1, UINT32_MAX) ||
      !v["fresh"].is_boolean() ||
      (v["fresh"] == true && v["uncertainty_ppb"] > 100001))
    return false;
  for (const auto key : {"observed_raw_ns", "age_ns", "span_ns", "generation"})
    if (!decimal(v[key]))
      return false;
  const auto span = std::stoull(v["span_ns"].get<std::string>());
  const auto raw = std::stoull(v["observed_raw_ns"].get<std::string>());
  const auto age = std::stoull(v["age_ns"].get<std::string>());
  return raw && v["generation"] != "0" && span >= 10000000000ULL &&
         span <= 60000000000ULL && raw <= UINT64_MAX - age;
}
inline bool runtime(const Json &v) {
  if (v.is_null())
    return true;
  if (!fields(v, {"serial_present",
                  "pps_present",
                  "time_valid",
                  "position_valid",
                  "satellites",
                  "observation_raw_ns",
                  "age_ns",
                  "position",
                  "candidate_utc_ns",
                  "candidate_raw_ns",
                  "candidate_pps_raw_ns",
                  "observations",
                  "rejected_sentences",
                  "dropped",
                  "uart_errors",
                  "irq_latency_ns",
                  "maximum_irq_latency_ns",
                  "capture_error_bound_ns",
                  "capture_rejections",
                  "error",
                  "timer_calibration",
                  "synthesizer_calibration"}))
    return false;
  for (const auto key :
       {"serial_present", "pps_present", "time_valid", "position_valid"})
    if (!v[key].is_boolean())
      return false;
  for (const auto key : {"observation_raw_ns", "age_ns", "candidate_utc_ns",
                         "candidate_raw_ns", "candidate_pps_raw_ns"})
    if (!decimal(v[key]))
      return false;
  for (const auto key : {"observations", "rejected_sentences", "dropped",
                         "uart_errors", "capture_rejections"})
    if (!integer(v[key], 0, UINT32_MAX))
      return false;
  for (const auto key :
       {"irq_latency_ns", "maximum_irq_latency_ns", "capture_error_bound_ns"})
    if (!decimal(v[key]) || std::stoull(v[key].get<std::string>()) > UINT32_MAX)
      return false;
  if (std::stoull(v["maximum_irq_latency_ns"].get<std::string>()) <
          std::stoull(v["irq_latency_ns"].get<std::string>()) ||
      (v["time_valid"] == true && v["capture_error_bound_ns"] == "0"))
    return false;
  if (std::stoull(v["observation_raw_ns"].get<std::string>()) >
          UINT64_MAX - std::stoull(v["age_ns"].get<std::string>()) ||
      (v["time_valid"] == true &&
       (std::stoull(v["age_ns"].get<std::string>()) > 3000000000ULL ||
        v["observations"] == 0)))
    return false;
  if (!integer(v["satellites"], 0, 99) || !v["error"].is_string() ||
      !std::regex_match(v["error"].get<std::string>(),
                        std::regex("([a-z][a-z0-9_]{0,79})?")) ||
      (v["time_valid"] == true && v["observation_raw_ns"] == "0") ||
      (v["position_valid"] == true) != !v["position"].is_null())
    return false;
  if (!v["position"].is_null()) {
    const auto &p = v["position"];
    if (!fields(p, {"latitude_e7", "longitude_e7", "locator", "observed_raw_ns",
                    "age_ns"}) ||
        !integer(p["latitude_e7"], -900000000, 900000000) ||
        !integer(p["longitude_e7"], -1800000000, 1800000000) ||
        !p["locator"].is_string() ||
        !std::regex_match(p["locator"].get<std::string>(),
                          std::regex("[A-R]{2}[0-9]{2}[A-X]{2}")) ||
        !decimal(p["observed_raw_ns"]) || p["observed_raw_ns"] == "0" ||
        !decimal(p["age_ns"]) ||
        std::stoull(p["observed_raw_ns"].get<std::string>()) >
            UINT64_MAX - std::stoull(p["age_ns"].get<std::string>()))
      return false;
    const auto lon = std::min<std::int64_t>(
        p["longitude_e7"].get<std::int64_t>() + 1800000000, 3599999999LL);
    const auto lat = std::min<std::int64_t>(
        p["latitude_e7"].get<std::int64_t>() + 900000000, 1799999999LL);
    const std::string grid{char('A' + lon / 200000000),
                           char('A' + lat / 100000000),
                           char('0' + lon / 20000000 % 10),
                           char('0' + lat / 10000000 % 10),
                           char('A' + lon % 20000000 * 24 / 20000000),
                           char('A' + lat % 10000000 * 24 / 10000000)};
    if (p["locator"] != grid)
      return false;
  }
  return calibration(v["timer_calibration"]) &&
         calibration(v["synthesizer_calibration"]);
}
} // namespace wsprrypi::gps_contract
