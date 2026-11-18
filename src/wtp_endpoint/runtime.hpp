#pragma once

#include "config_types.hpp"
#include "owned_state_directory.hpp"
#include "wtp_endpoint/management_observation.hpp"

#include <string>
#include <functional>
#include <optional>

namespace wsprrypi {

// Actual ready HTTP listener publication, owned by its web-server instance.
// Zero/unknown configured ports never qualify a legacy handback binding.
void wtp_pi_publish_management_http(const void *owner,const std::string &self,unsigned port,std::function<bool()> ready);
void wtp_pi_withdraw_management_http(const void *owner) noexcept;
// Pre-startup observation only: no directory creation, output, or ledger writes.
// Existing or unreadable prior endpoint state prevents new-install eligibility.
bool wtp_pi_has_prior_output_history() noexcept;
struct PiRecurrenceStorage {
    std::string path;
    bool existed{},prior_output_history{};
};
// Trusted pre-parser startup only. Fixed OS-owner/device namespace, independent
// of INI filename, HTTP parameters, HOME overrides, or selected output backend.
enum class PrivateDeviceDomain {Recurrence,ControllerRetirements,Pairing};
std::string private_device_state_directory(PrivateDeviceDomain,const std::string &device_id);
PiRecurrenceStorage prepare_pi_recurrence_storage(const std::string &device_id,const std::string &ini,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint = {});

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
// Explicit simulated-only process fixture: loopback/ephemeral listener, no DNS-SD.
// Requires hardware access disabled, a private fixture ledger and no live owner.
// No CLI/INI/environment selector or automatic host/backend fallback exists.
void set_wtp_pi_loopback_fixture(bool);
}

} // namespace wsprrypi
