// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "json.hpp"
#include "wtp_settings.hpp"
#include <mutex>
#include <optional>
#include <string>

namespace wsprrypi {
struct WtpCatalogProfile {
  std::string id, name, method, discovery_id;
  WtpSettings settings;
  bool legacy_unverified{};
};

// One host-owned file adjacent to the active INI. The active [WTP] section is
// never inferred from, or overwritten by, loading this catalog.
class WtpCatalog {
public:
  explicit WtpCatalog(std::string path) : path_(std::move(path)) {}
  std::pair<nlohmann::json, std::string> snapshot(
      const std::optional<WtpSettings> &active);
  std::pair<nlohmann::json, std::string> mutate(
      const nlohmann::json &request, const std::string &revision,
      const std::optional<WtpSettings> &active);
  WtpCatalogProfile profile(const std::string &id, const std::string &revision,
      const std::optional<WtpSettings> &active);
private:
  std::string path_;
  std::mutex mutex_;
  nlohmann::json load(const std::optional<WtpSettings> &active);
  void save(const nlohmann::json &value);
};
std::string wtp_catalog_revision(const nlohmann::json &);
} // namespace wsprrypi
