#pragma once

#include "support_request_guard.hpp"
#include "privileged_network_policy.hpp"

#include <string>

enum class WebSocketUpgradeGuardDecision {
    allowed,
    malformed,
    rejected
};

struct WebSocketUpgradeGuardResult {
    WebSocketUpgradeGuardDecision decision = WebSocketUpgradeGuardDecision::malformed;
    std::string key;
    SupportRequestGuardDecision rejection_reason =
        SupportRequestGuardDecision::rejected_peer;
};

[[nodiscard]] WebSocketUpgradeGuardResult evaluate_websocket_upgrade(
    const std::string &request,
    const std::string &peer_address,
    const SupportRequestGuardSnapshot &snapshot,
    PrivilegedNetworkMode mode = PrivilegedNetworkMode::enforced);
