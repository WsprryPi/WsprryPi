#pragma once

#include <functional>

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
};

void set_wtp_pi_control_hooks(WtpPiControlHooks hooks);
void clear_wtp_pi_control_hooks();
bool wtp_pi_local_effective() noexcept;
bool wtp_pi_begin_scheduled() noexcept;
bool wtp_pi_begin_test_tone() noexcept;
void wtp_pi_end_local(bool output_inactive) noexcept;
void wtp_pi_config_transmit_committed(bool enabled) noexcept;
void wtp_pi_end_scheduled(bool output_inactive) noexcept;
bool wtp_pi_begin_configuration() noexcept;
void wtp_pi_end_configuration() noexcept;
