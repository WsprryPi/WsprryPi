#include "backend_http_guard.hpp"

#include "privileged_network_policy.hpp"

BackendHttpGuardDecision evaluate_backend_http_request(
    std::string_view method,
    std::string_view path,
    const std::string &peer_address,
    const std::string &host_header,
    const std::optional<std::string> &origin_header,
    const SupportRequestGuardSnapshot &snapshot,
    PrivilegedNetworkMode mode,
    const std::vector<std::string> &trusted_proxy_identities,
    SupportRequestGuardDecision *rejection_reason) {
    const auto classification = classify_privileged_http_operation(method, path);
    if (classification == PrivilegedOperationClass::reject) {
        const bool protected_candidate =
            !trusted_proxy_identities.empty() ||
            path == "/config" || path.starts_with("/config/") ||
            path == "/control" || path.starts_with("/control/") ||
            path == "/api/support-bundles" ||
            path.starts_with("/api/support-bundles/") ||
            path == "/api/network-safety" ||
            path.starts_with("/api/network-safety/") ||
            path == "/api/rp1-gpclk-route" ||
            path.starts_with("/api/rp1-gpclk-route/") ||
            path == "/api/wtp" || path.starts_with("/api/wtp/") ||
            path == "/api/v1" || path.starts_with("/api/v1/");
        if (protected_candidate) {
            if (rejection_reason)
                *rejection_reason = SupportRequestGuardDecision::invalid_request;
            return BackendHttpGuardDecision::rejected;
        }
    }
    const bool protected_operation =
        classification == PrivilegedOperationClass::protected_operation;
    const bool enforce_peer = !protected_operation ||
        mode != PrivilegedNetworkMode::insecure_disabled;
    const std::vector<std::string> no_proxy_identity;
    const auto result = SupportRequestGuard(snapshot).evaluate(
        peer_address, host_header, origin_header, enforce_peer,
        protected_operation ? trusted_proxy_identities : no_proxy_identity);
    if (rejection_reason) *rejection_reason = result.decision;
    return result.allowed()
               ? BackendHttpGuardDecision::allowed
               : BackendHttpGuardDecision::rejected;
}
