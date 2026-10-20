#pragma once

#include "privileged_network_policy.hpp"

#include <optional>
#include <string>

struct PrivilegedNetworkRuntimeState {
    PrivilegedNetworkMode configured = PrivilegedNetworkMode::enforced;
    PrivilegedNetworkMode active = PrivilegedNetworkMode::enforced;
    bool setting_was_valid = false;
    bool setting_was_missing = true;
    bool active_known = false;
};

void initialize_privileged_network_runtime(
    const std::optional<std::string> &configured_value);
void set_active_privileged_network_mode(PrivilegedNetworkMode mode) noexcept;
void set_privileged_network_runtime_mode(PrivilegedNetworkMode mode) noexcept;
void set_privileged_network_runtime_unknown() noexcept;
[[nodiscard]] PrivilegedNetworkRuntimeState privileged_network_runtime_state() noexcept;
[[nodiscard]] PrivilegedNetworkMode active_privileged_network_mode() noexcept;
[[nodiscard]] std::string privileged_network_runtime_status_text();
