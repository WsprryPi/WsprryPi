// SPDX-License-Identifier: MIT
#pragma once
#include "fleet_runtime.hpp"
#include "wtp_settings.hpp"
#include <functional>
#include <memory>
namespace wsprrypi {
// Typed observers are confined to source-level refusal fixtures. There is no
// execution carrier factory in the product or fixture construction path.
struct WtpFleetTestHooks {
    std::string retirement_directory;
    std::function<void()> before_execution_contact;
    // Actual isolated pairing history for multi-owner process fixtures. The
    // production owner always resolves its process-selected pairing history.
    std::function<std::optional<nlohmann::json>(const nlohmann::json &,const nlohmann::json &)> retirement_signer;
    // Deterministic blocked-filesystem tests; never runs under HTTP/publication locks.
    std::function<void()> before_persist;
    // Hold a fenced retirement before any durable decision exists.
    std::function<void()> before_retirement_persist;
    // Hold a committed write before publication to exercise owner ordering.
    std::function<void()> after_persist;
    // Observe admission fencing before shutdown waits for any old executor.
    std::function<void()> before_shutdown_join;
    // Hold a removed owner after its exit decision, before resource teardown.
    std::function<void(const std::string&)> before_owner_exit;
    // Host policy clock only; transport clocks remain the intended fixture's.
    std::function<std::uint64_t()> monotonic_now;
};
void start_wtp_fleet_private_storage_for_test(const std::string &ini_path,const std::string &self_id,const std::string &retirement_directory);
void start_wtp_fleet_for_test(const std::string& ini_path,
    const std::string& self_id, WtpFleetTestHooks hooks);
}
