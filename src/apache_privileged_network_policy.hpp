#pragma once

#include "privileged_network_policy.hpp"

#include <optional>
#include <string>

struct ApachePrivilegedNetworkPolicyResult {
    std::optional<std::string> configuration;
    std::string error;

    [[nodiscard]] bool valid() const noexcept { return configuration.has_value(); }
};

[[nodiscard]] ApachePrivilegedNetworkPolicyResult
render_apache_privileged_network_policy(
    PrivilegedNetworkMode mode = PrivilegedNetworkMode::enforced);
