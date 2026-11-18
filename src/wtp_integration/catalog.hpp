// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "json.hpp"
#include "wtp_settings.hpp"
#include "pico_management_history.hpp"
#include "pi_management_anchor.hpp"
#include <mutex>
#include <optional>
#include <string>

namespace wsprrypi {
class PiPairRemoteController;
class WtpCatalog;
// Constructible only by the actual authenticated observation owner. This is
// private enrollment bookkeeping, never a browser-provided authority token.
class RemoteManagementEnrollment {
    friend class PiPairRemoteController;
    friend class WtpCatalog;
    RemoteManagementEnrollment(std::string device,nlohmann::json metadata):device_(std::move(device)),metadata_(std::move(metadata)){}
    std::string device_;
    nlohmann::json metadata_;
};
struct WtpCatalogProfile {
  std::string id, name, method, discovery_id;
  WtpSettings settings;
  bool legacy_unverified{};
  std::string generation{"1"};
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
  PicoManagementHistoryAuthority management_history_authority(
      const std::optional<WtpSettings> &active);
  // Private snapshots are not included in the public WTP catalog projection.
  nlohmann::json remote_management(const std::optional<WtpSettings> &active);
  bool enroll_remote_management(const RemoteManagementEnrollment &,const std::optional<WtpSettings> &active);
  PiManagementAnchor pi_management_anchor(const std::string &self,
      const std::optional<WtpSettings> &active);
private:
  std::string path_;
  std::mutex mutex_;
  nlohmann::json load(const std::optional<WtpSettings> &active,std::string *source_bytes=nullptr);
  void save(const nlohmann::json &value);
};
std::string wtp_catalog_revision(const nlohmann::json &);
} // namespace wsprrypi
