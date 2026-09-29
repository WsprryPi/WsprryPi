#pragma once

#include "config_types.hpp"

#include <string>
#include <functional>

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
bool wtp_pi_stop_remote_work();
bool wtp_pi_recover_remote_output();

} // namespace wsprrypi
