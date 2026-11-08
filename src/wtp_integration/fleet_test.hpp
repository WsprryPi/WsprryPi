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
};
void start_wtp_fleet_for_test(const std::string& ini_path,
    const std::string& self_id, WtpFleetTestHooks hooks);
}
