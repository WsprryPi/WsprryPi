// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include "network_http.hpp"
#include "pi_pair_retirement.hpp"
#include <string>
namespace wsprrypi {
// Ordinary transmitter callers use the inbound endpoint's machine identity.
// The controller-only role supplies its actual process identity; explicit
// fixture identities do not provision host pairing storage.
void start_wtp_fleet(const std::string& ini_path, const std::string& self_id = {});
void stop_wtp_fleet() noexcept;
// Trusted capture pins the actual lifecycle/retirement owners; no carrier work.
void wtp_fleet_with_backup_files(const std::function<void(const std::map<std::string,std::string> &)> &);
// Read-only positive closure from the actual source owner; unavailable holds.
bool wtp_fleet_retirement_scope_closed(const nlohmann::json &,const nlohmann::json &);
PicoHttpResponse wtp_fleet_retirement(const PiPairRetirementPrincipal &,PiPairHistory &,const std::function<bool()> &cancelled);
PicoHttpResponse wtp_fleet_api(const std::string& method, const std::string& body,
                              const std::string& revision,
                              const std::string& resource = "fleet", const std::string& ids = {});
}
