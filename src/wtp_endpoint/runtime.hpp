#pragma once
#ifndef _WIN32
#include <sys/types.h>
#endif

#include "config_types.hpp"
#include "owned_state_directory.hpp"
#include "wtp_endpoint/management_observation.hpp"

#include <string>
#include <functional>
#include <optional>

namespace wsprrypi {

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
// OS account home; typed actual-main fixtures retain their private owner root.
struct ControllerInstallationOwner {
    std::string home;
#ifdef _WIN32
    std::wstring sid;
#else
    uid_t uid;
#endif
};
ControllerInstallationOwner controller_installation_owner();
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
void set_controller_installation_owner_home(const std::string &);

}

} // namespace wsprrypi
