#pragma once

#include <functional>
namespace wsprrypi {
struct WtpManagedOutput;
enum class WtpExternalReconcileResult {NotApplicable,Confirmed,Held};
WtpExternalReconcileResult wtp_pi_reconcile_external_output(const WtpManagedOutput &) noexcept;
bool wtp_pi_external_output_admitted() noexcept;
}

// Application-side hook surface. Production installs callbacks only after the
// managed WTP endpoint and startup quiescence are ready. Without an endpoint,
// existing direct CLI and portable test behavior is unchanged.
struct WtpPiControlHooks {
    std::function<bool()> local_effective;
    std::function<bool()> begin_scheduled;
    std::function<bool()> begin_test_tone;
    std::function<void(bool)> end_local;
    std::function<void(bool)> config_transmit_committed;
    std::function<void(bool)> end_scheduled;
    std::function<bool()> begin_configuration;
    std::function<void()> end_configuration;
    std::function<bool()> external_output_admitted{};
    std::function<wsprrypi::WtpExternalReconcileResult(const wsprrypi::WtpManagedOutput &)> reconcile_external_output{};
};

void set_wtp_pi_control_hooks(WtpPiControlHooks hooks);
void clear_wtp_pi_control_hooks();
// Independent durable-state admission fence. Install before configuration
// parsing and clear only after output/listener teardown. Existing direct CLI
// callers without a managed owner retain their original behavior.
void set_wtp_pi_management_readiness(std::function<bool()>,std::function<void()> stopping = {});
void wtp_pi_management_stopping() noexcept;
bool wtp_pi_management_ready() noexcept;
bool wtp_pi_local_effective() noexcept;
bool wtp_pi_begin_scheduled() noexcept;
bool wtp_pi_begin_test_tone() noexcept;
void wtp_pi_end_local(bool output_inactive) noexcept;
void wtp_pi_config_transmit_committed(bool enabled) noexcept;
void wtp_pi_end_scheduled(bool output_inactive) noexcept;
bool wtp_pi_begin_configuration() noexcept;
void wtp_pi_end_configuration() noexcept;
