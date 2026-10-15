// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "network_http.hpp"
#include "json.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <set>
namespace wsprrypi {
struct BrowserRequest {
  std::string method, path, body, revision;
  bool mutation_context{}; // Guard + same-origin JSON intent checked by HTTP adapter.
  std::string member_context{}; // Host editor epoch; never a remote revision.
};
class WtpBrowserApi {
public:
  using Status = std::function<nlohmann::json()>;
  using Management = std::function<PicoHttpResponse(const std::string &, const std::string &, const std::string &, const std::string &, const std::string &)>;
  using Cancel = std::function<PicoHttpResponse(const std::string &)>;
  WtpBrowserApi(Status status, Management management, Cancel cancel)
      : status_(std::move(status)), management_(std::move(management)), cancel_(std::move(cancel)) {}
  PicoHttpResponse handle(const BrowserRequest &);
private:
  Status status_;
  Management management_;
  Cancel cancel_;
  std::mutex management_mutex_;
  std::string member_context_, member_revision_, member_selection_;
  std::set<std::string> alias_reads_required_;
  std::string legacy_selection_;
  std::map<std::string,std::string> alias_selections_;
  bool legacy_read_required_{};
  std::mutex mutex_;
  struct Replay { std::string body; PicoHttpResponse response; };
  // Fail closed at capacity instead of evicting request history and permitting
  // a delayed browser retry to affect later work. Process restart resets scope.
  std::map<std::string, Replay> replays_;
};
nlohmann::json strict_browser_json(const std::string &);
} // namespace wsprrypi
