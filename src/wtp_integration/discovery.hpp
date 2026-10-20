// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "json.hpp"
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace wsprrypi {
struct WtpServiceKey {
  int interface_index{}, protocol{};
  std::string instance, domain;
  auto operator<=>(const WtpServiceKey &) const = default;
};
struct WtpService {
  WtpServiceKey key;
  std::string id, target, binding, state{"resolving"}, reason;
  std::vector<std::string> txt;
  unsigned port{};
  std::uint64_t first_seen{}, last_seen{};
  std::uint64_t state_changed_ms{};
};

// TXT strings are length-delimited DNS-SD entries, without the length byte.
// This parser does no I/O and is also the injected test seam.
std::optional<std::string> wtp_dns_sd_binding(
    const std::vector<std::string> &entries);

class WtpDiscovery {
public:
  void available(bool, std::string reason = {});
  void found(const WtpServiceKey &, std::uint64_t now);
  void resolved(const WtpServiceKey &, std::string target, unsigned port,
                const std::vector<std::string> &txt, std::uint64_t now);
  void failed(const WtpServiceKey &, std::string reason, std::uint64_t now);
  void removed(const WtpServiceKey &, std::uint64_t now);
  void restart(std::uint64_t now);
  nlohmann::json snapshot(std::uint64_t now) const;
  std::optional<WtpService> candidate(const std::string &id,
                                      std::uint64_t now) const;
private:
  mutable std::mutex mutex_;
  std::map<WtpServiceKey, WtpService> records_;
  bool available_{};
  std::string reason_{"Avahi discovery is unavailable"};
  static constexpr std::size_t maximum = 64;
};
WtpDiscovery &wtp_discovery();
// Starts the Linux Avahi browser once at server setup. Snapshot reads never
// start a browser or open a WTP connection. Other platforms report unavailable.
void start_wtp_discovery();
} // namespace wsprrypi
