// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "catalog.hpp"
#include "wtp_settings_json.hpp"
#include <openssl/sha.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <random>

namespace wsprrypi {
namespace {
bool identity(const std::string &id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
std::string local_id() {
  std::random_device source;
  std::string id(32, '0');
  for (auto &c : id) c = "0123456789abcdef"[source() & 15];
  return id;
}
bool safe_name(const std::string &name) {
  return !name.empty() && name.size() <= 80 &&
      std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
nlohmann::json serialize(const WtpCatalogProfile &p) {
  return {{"id", p.id}, {"name", p.name}, {"method", p.method},
      {"discovery_id", p.discovery_id}, {"legacy_unverified", p.legacy_unverified},
      {"settings", wtp_settings_json(p.settings)}};
}
WtpCatalogProfile parse(const nlohmann::json &value) {
  if (!value.is_object() || value.size() != 6)
    throw std::runtime_error("Invalid WTP catalog profile");
  WtpCatalogProfile p;
  p.id = value.at("id").get<std::string>();
  p.name = value.at("name").get<std::string>();
  p.method = value.at("method").get<std::string>();
  p.discovery_id = value.at("discovery_id").get<std::string>();
  p.legacy_unverified = value.at("legacy_unverified").get<bool>();
  const auto allowed_settings = wtp_settings_json(WtpSettings{});
  if (!value.at("settings").is_object())
    throw std::runtime_error("Invalid WTP catalog settings");
  for (auto it = value.at("settings").begin(); it != value.at("settings").end(); ++it)
    if (!allowed_settings.contains(it.key()))
      throw std::runtime_error("Unexpected WTP catalog setting");
  p.settings = parse_wtp_settings(value.at("settings"), true);
  if (!identity(p.id) || !safe_name(p.name) ||
      (p.method != "manual" && p.method != "dns_sd") ||
      (p.method == "dns_sd" && (p.discovery_id.empty() || p.discovery_id.size() > 64)) ||
      (p.method == "manual" && !p.discovery_id.empty()) ||
      (!p.legacy_unverified && !identity(p.settings.device_id)) ||
      (p.legacy_unverified && (p.settings.transport != "network_plain" ||
                               !p.settings.device_id.empty())))
    throw std::runtime_error("Invalid WTP catalog identity or connection method");
  // All TLS material is referenced by absolute host paths, never embedded.
  return p;
}
void write_all(int fd, const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (n <= 0) throw std::runtime_error("Could not write WTP catalog");
    offset += static_cast<std::size_t>(n);
  }
}
} // namespace
std::string wtp_catalog_revision(const nlohmann::json &value) {
  const auto bytes = value.dump();
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
  std::string result = "\"";
  for (auto c : digest) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; }
  return result + "\"";
}
void WtpCatalog::save(const nlohmann::json &value) {
  const auto temp = path_ + ".tmp." + local_id();
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) throw std::runtime_error("Could not create WTP catalog");
  bool open = true;
  try {
    write_all(fd, value.dump(2) + "\n");
    if (::fsync(fd) != 0) throw std::runtime_error("Could not sync WTP catalog");
    const int closed = ::close(fd); open = false;
    if (closed != 0) throw std::runtime_error("Could not close WTP catalog");
    if (::rename(temp.c_str(), path_.c_str()) != 0) throw std::runtime_error("Could not publish WTP catalog");
  } catch (...) {
    if (open) ::close(fd);
    ::unlink(temp.c_str()); throw;
  }
}
nlohmann::json WtpCatalog::load(const std::optional<WtpSettings> &active) {
  struct stat information{};
  if (::lstat(path_.c_str(), &information) == 0 &&
      (!S_ISREG(information.st_mode) || (information.st_mode & 0077) != 0 ||
       information.st_uid != ::geteuid()))
    throw std::runtime_error("WTP catalog must be a private regular host-owned file");
  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    if (::access(path_.c_str(), F_OK) == 0) throw std::runtime_error("WTP catalog is unreadable");
    nlohmann::json fresh = {{"version", 1}, {"profiles", nlohmann::json::array()}};
    if (active) {
      WtpCatalogProfile legacy{local_id(), "Current WTP device", "manual", "",
                               *active, active->device_id.empty()};
      fresh["profiles"].push_back(serialize(legacy));
    }
    save(fresh);
    return fresh;
  }
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  if (bytes.size() > 131072) throw std::runtime_error("WTP catalog exceeds size limit");
  auto value = nlohmann::json::parse(bytes);
  if (!value.is_object() || value.size() != 2 || value.at("version") != 1 ||
      !value.at("profiles").is_array() || value.at("profiles").size() > 64)
    throw std::runtime_error("Unsupported WTP catalog version or size");
  std::set<std::string> ids;
  std::vector<WtpSettings> settings;
  for (const auto &item : value.at("profiles"))
  {
    const auto profile = parse(item);
    if (!ids.insert(profile.id).second ||
        std::find(settings.begin(), settings.end(), profile.settings) != settings.end())
      throw std::runtime_error("Duplicate WTP catalog ID or endpoint");
    settings.push_back(profile.settings);
  }
  return value;
}
std::pair<nlohmann::json, std::string> WtpCatalog::snapshot(
    const std::optional<WtpSettings> &active) {
  std::lock_guard lock(mutex_);
  auto value = load(active);
  std::string active_id;
  if (active) for (const auto &item : value.at("profiles")) {
    const auto p = parse(item);
    if (p.settings == *active) { active_id = p.id; break; }
  }
  auto result = value;
  result["scope"] = "wsprrypi-wtp-catalog/1";
  result["active_id"] = active_id;
  result["active_unmatched"] = active.has_value() && active_id.empty();
  return {result, wtp_catalog_revision(value)};
}
std::pair<nlohmann::json, std::string> WtpCatalog::mutate(
    const nlohmann::json &request, const std::string &revision,
    const std::optional<WtpSettings> &active) {
  std::lock_guard lock(mutex_);
  auto value = load(active);
  if (revision != wtp_catalog_revision(value)) throw std::runtime_error("revision_conflict");
  if (!request.is_object() || !request.contains("operation") ||
      !request.at("operation").is_string()) throw std::runtime_error("Invalid catalog request");
  const auto operation = request.at("operation").get<std::string>();
  auto &profiles = value["profiles"];
  if (operation == "add") {
    if (request.size() != 6 || profiles.size() >= 64) throw std::runtime_error("Invalid or full catalog");
    WtpCatalogProfile p{local_id(), request.at("name").get<std::string>(),
        request.at("method").get<std::string>(), "", parse_wtp_settings(request.at("settings"), true)};
    if (p.method == "dns_sd") p.discovery_id = request.at("discovery_id").get<std::string>();
    else if (request.at("discovery_id") != "") throw std::runtime_error("Manual profile cannot bind discovery");
    if (p.settings.transport == "network_plain" && request.at("consent_plain") != true)
      throw std::runtime_error("Plain LAN requires explicit consent");
    profiles.push_back(serialize(parse(serialize(p))));
  } else if (operation == "edit") {
    if (request.size() != 5) throw std::runtime_error("Invalid edit request");
    bool found = false;
    for (auto &item : profiles) if (item.at("id") == request.at("id")) {
      auto p = parse(item);
      p.name = request.at("name").get<std::string>();
      p.settings = parse_wtp_settings(request.at("settings"), true);
      if (p.settings.transport == "network_plain" && request.at("consent_plain") != true)
        throw std::runtime_error("Plain LAN requires explicit consent");
      p.legacy_unverified = p.settings.device_id.empty() && p.legacy_unverified;
      item = serialize(parse(serialize(p)));
      found = true; break;
    }
    if (!found) throw std::runtime_error("Unknown profile");
  } else if (operation == "rename") {
    if (request.size() != 3) throw std::runtime_error("Invalid rename request");
    bool found = false;
    for (auto &item : profiles) if (item.at("id") == request.at("id")) {
      auto p = parse(item); p.name = request.at("name").get<std::string>();
      item = serialize(parse(serialize(p))); found = true; break;
    }
    if (!found) throw std::runtime_error("Unknown profile");
  } else if (operation == "remove") {
    if (request.size() != 2) throw std::runtime_error("Invalid remove request");
    const auto before = profiles.size();
    for (auto it = profiles.begin(); it != profiles.end(); ++it)
      if (it->at("id") == request.at("id")) { profiles.erase(it); break; }
    if (profiles.size() == before) throw std::runtime_error("Unknown profile");
  } else throw std::runtime_error("Unknown catalog operation");
  for (std::size_t i = 0; i < profiles.size(); ++i)
    for (std::size_t j = i + 1; j < profiles.size(); ++j)
      if (parse(profiles[i]).settings == parse(profiles[j]).settings)
        throw std::runtime_error("An identical endpoint is already saved");
  save(value);
  return {value, wtp_catalog_revision(value)};
}
WtpCatalogProfile WtpCatalog::profile(const std::string &id,
    const std::string &revision, const std::optional<WtpSettings> &active) {
  std::lock_guard lock(mutex_);
  auto value = load(active);
  if (revision != wtp_catalog_revision(value)) throw std::runtime_error("revision_conflict");
  for (const auto &item : value.at("profiles")) {
    auto p = parse(item);
    if (p.id == id) return p;
  }
  throw std::runtime_error("Unknown profile");
}
} // namespace wsprrypi
