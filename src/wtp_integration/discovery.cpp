// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "discovery.hpp"
#include "identity.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <openssl/sha.h>

namespace wsprrypi {
namespace {
bool safe_label(const std::string &s, std::size_t limit) {
  return !s.empty() && s.size() <= limit &&
      std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::string lower(std::string value) {
  for (auto &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}
bool current(const WtpService &s, std::uint64_t now) {
  // Avahi owns mDNS TTL expiry and reports REMOVE. A stable advertisement need
  // not generate another NEW callback every few minutes.
  (void)now;
  return s.state == "online";
}
std::string service_id(const WtpServiceKey &key) {
  const auto bytes = std::to_string(key.interface_index) + "\n" +
      std::to_string(key.protocol) + "\n" + key.instance + "\n" + key.domain;
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
  std::string result = "service-";
  for (unsigned i = 0; i < 16; ++i) {
    result += "0123456789abcdef"[digest[i] >> 4];
    result += "0123456789abcdef"[digest[i] & 15];
  }
  return result;
}
} // namespace
std::optional<std::string> wtp_dns_sd_binding(const std::vector<std::string> &entries) {
  if (entries.empty() || entries.size() > 32) return {};
  std::set<std::string> keys;
  std::string binding;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    if (entry.empty() || entry.size() > 255) return {};
    const auto eq = entry.find('=');
    if (eq == 0) return {};
    const auto key = lower(entry.substr(0, eq));
    if (!std::all_of(key.begin(), key.end(), [](unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        })) return {};
    if ((key == "txtvers" || key == "binding") && !keys.insert(key).second) return {};
    const auto value = eq == std::string::npos ? std::string{} : entry.substr(eq + 1);
    if (i == 0 && (key != "txtvers" || value != "1")) return {};
    if (key == "txtvers" && (i != 0 || value != "1")) return {};
    if (key == "binding") {
      if (value != "tls" && value != "plain") return {};
      binding = value;
    }
  }
  if (binding.empty()) return {};
  return binding;
}
void WtpDiscovery::available(bool value, std::string reason) {
  std::lock_guard lock(mutex_);
  available_ = value;
  reason_ = value ? "" : (reason.empty() ? "Avahi discovery is unavailable" : std::move(reason));
  if (!value) for (auto &[key, record] : records_)
    if (record.state == "online" || record.state == "resolving") {
      record.state = "stale";
      record.state_changed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
    }
}
void WtpDiscovery::found(const WtpServiceKey &key, std::uint64_t now) {
  if (key.interface_index < 0 || !safe_label(key.instance, 128) ||
      !safe_label(key.domain, 253) ||
      (lower(key.domain) != "local" && lower(key.domain) != "local.")) return;
  std::lock_guard lock(mutex_);
  if (!records_.contains(key)) {
    if (records_.size() >= maximum) {
      auto old = std::find_if(records_.begin(), records_.end(), [](const auto &item) {
        return item.second.state != "online" && item.second.state != "resolving";
      });
      if (old == records_.end()) return;
      records_.erase(old);
    }
    WtpService record;
    record.key = key;
    record.id = service_id(key);
    record.first_seen = now;
    records_.emplace(key, std::move(record));
  }
  auto &record = records_.at(key);
  record.last_seen = now;
  record.state_changed_ms = now;
  record.state = "resolving";
  record.reason.clear();
  record.target.clear();
  record.binding.clear();
  record.txt.clear();
  record.port = 0;
}
void WtpDiscovery::resolved(const WtpServiceKey &key, std::string target,
                            unsigned port, const std::vector<std::string> &txt,
                            std::uint64_t now) {
  const auto binding = wtp_dns_sd_binding(txt);
  const auto canonical = canonical_network_identity(target);
  std::lock_guard lock(mutex_);
  auto it = records_.find(key);
  if (it == records_.end()) return; // Late resolver callback after remove/restart.
  auto &record = it->second;
  record.last_seen = now;
  record.state_changed_ms = now;
  std::size_t txt_bytes = 0;
  for (const auto &entry : txt) txt_bytes += entry.size();
  if (txt.size() <= 32 && txt_bytes <= 4096) record.txt = txt;
  else record.txt.clear();
  if (!binding || !canonical || port == 0 || port > 65535 || txt_bytes > 4096) {
    record.state = "invalid";
    record.reason = "Invalid TXT binding or SRV target/port";
    record.target.clear(); record.port = 0; record.binding.clear();
    return;
  }
  record.target = std::move(target);
  record.port = port;
  record.binding = *binding;
  record.state = "online";
  record.reason.clear();
}
void WtpDiscovery::failed(const WtpServiceKey &key, std::string reason,
                          std::uint64_t now) {
  std::lock_guard lock(mutex_);
  auto it = records_.find(key);
  if (it == records_.end()) return;
  it->second.state = "failed";
  it->second.reason = reason.substr(0, 160);
  it->second.state_changed_ms = now;
}
void WtpDiscovery::removed(const WtpServiceKey &key, std::uint64_t now) {
  std::lock_guard lock(mutex_);
  auto it = records_.find(key);
  if (it == records_.end()) return;
  it->second.state = "removed";
  it->second.reason = "Service was withdrawn";
  it->second.state_changed_ms = now;
}
void WtpDiscovery::restart(std::uint64_t now) {
  std::lock_guard lock(mutex_);
  for (auto &[key, record] : records_) {
    if (record.state == "online" || record.state == "resolving") {
      record.state = "stale";
      record.reason = "Avahi restarted; waiting for a fresh advertisement";
      record.state_changed_ms = now;
    }
  }
}
nlohmann::json WtpDiscovery::snapshot(std::uint64_t now) const {
  std::lock_guard lock(mutex_);
  auto items = nlohmann::json::array();
  for (const auto &[key, s] : records_) {
    const auto state = current(s, now) ? "online" :
        s.state == "online" ? "stale" : s.state;
    items.push_back({{"id", s.id}, {"interface", key.interface_index},
        {"protocol", key.protocol}, {"instance", key.instance},
        {"domain", key.domain}, {"target", s.target}, {"port", s.port},
        {"binding", s.binding}, {"txt", s.txt}, {"state", state}, {"reason", s.reason},
        {"first_seen_ms", s.first_seen}, {"last_seen_ms", s.last_seen},
        {"state_changed_ms", s.state_changed_ms}});
  }
  return {{"scope", "wtp-dns-sd/1"}, {"service", "_wtp._tcp.local."},
      {"available", available_}, {"reason", reason_}, {"candidates", items}};
}
std::optional<WtpService> WtpDiscovery::candidate(const std::string &id,
                                                   std::uint64_t now) const {
  std::lock_guard lock(mutex_);
  for (const auto &[key, record] : records_)
    if (record.id == id && available_ && current(record, now)) return record;
  return {};
}
WtpDiscovery &wtp_discovery() { static WtpDiscovery value; return value; }
} // namespace wsprrypi
