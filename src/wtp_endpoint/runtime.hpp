#pragma once

#include "config_types.hpp"
#include "wtp_endpoint/management_observation.hpp"

#include <string>
#include <functional>
#include <optional>

namespace wsprrypi {

// Lives in the normal singleton-owning managed service, not a second daemon.
bool start_wtp_pi_endpoint(const ArgParserConfig& config, std::string* error);
void activate_wtp_pi_endpoint();
void poll_wtp_pi_endpoint() noexcept;
void stop_wtp_pi_endpoint() noexcept;
std::string wtp_pi_endpoint_status_json();
void wtp_pi_enable_interactive(bool finish_current, bool confirmed,
                               const std::function<void()>& persist,
                               const std::string& observed_owner, const std::string& observed_job);
void wtp_pi_configuration_committed(const ArgParserConfig& config);
// Pins the exact native authority across the callback. Refuses if startup,
// output ownership, or endpoint reconfiguration cannot reserve a safe edit.
// The caller supplies its configuration lock and must not perform network IO.
void wtp_pi_with_configuration(const std::function<void()>&);
std::optional<PiManagementObservation> wtp_pi_management_observation();
bool wtp_pi_stop_remote_work();
bool wtp_pi_recover_remote_output();

namespace testing {
// Isolated native-main fixtures only. No CLI, INI, environment, or HTTP setting
// exposes this path; production continues to use the invoking user's ledger.
void set_wtp_pi_private_state_path(const std::string &);
}

} // namespace wsprrypi
