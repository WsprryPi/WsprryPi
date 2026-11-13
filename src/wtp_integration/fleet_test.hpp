// SPDX-License-Identifier: MIT
#pragma once
#include "fleet_runtime.hpp"
#include "wtp_settings.hpp"
#include <functional>
#include <memory>
namespace wsprrypi {
struct WtpTargetRuntime;
// Explicit typed fixture construction only. No CLI, INI, environment, or UI
// selector installs these hooks. Captured fixture state, clocks, and streams
// must outlive the Fleet.
struct WtpFleetTestHooks {
    std::function<std::unique_ptr<WtpTargetRuntime>(WtpSettings,
        std::function<bool()>, std::shared_ptr<void>)> create_target;
    std::function<nlohmann::json(const WtpSettings&, unsigned)> revocation_status;
    // Deterministic blocked-filesystem tests; never runs under HTTP/publication locks.
    std::function<void()> before_persist;
    // Hold a committed write before publication to exercise owner ordering.
    std::function<void()> after_persist;
    // Observe admission fencing before shutdown waits for any old executor.
    std::function<void()> before_shutdown_join;
    // Host policy clock only; transport clocks remain the intended fixture's.
    std::function<std::uint64_t()> monotonic_now;
};
void start_wtp_fleet_for_test(const std::string& ini_path,
    const std::string& self_id, WtpFleetTestHooks hooks);
}
