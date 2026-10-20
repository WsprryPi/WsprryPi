#pragma once

#include "support_request_guard.hpp"
#include "privileged_network_policy.hpp"

#include <optional>
#include <string>
#include <string_view>

enum class BackendHttpGuardDecision {
    allowed,
    rejected
};

[[nodiscard]] BackendHttpGuardDecision evaluate_backend_http_request(
    std::string_view method,
    std::string_view path,
    const std::string &peer_address,
    const std::string &host_header,
    const std::optional<std::string> &origin_header,
    const SupportRequestGuardSnapshot &snapshot,
    PrivilegedNetworkMode mode = PrivilegedNetworkMode::enforced,
    const std::vector<std::string> &trusted_proxy_identities = {},
    SupportRequestGuardDecision *rejection_reason = nullptr);
