#include "privileged_network_transaction.hpp"

#include "apache_privileged_network_policy.hpp"

#include <utility>

const char *privileged_network_transaction_status_name(
    PrivilegedNetworkTransactionStatus status) noexcept {
    switch (status) {
    case PrivilegedNetworkTransactionStatus::applied: return "applied";
    case PrivilegedNetworkTransactionStatus::render_failed: return "render_failed";
    case PrivilegedNetworkTransactionStatus::application_validation_failed:
        return "application_validation_failed";
    case PrivilegedNetworkTransactionStatus::apache_validation_failed:
        return "apache_validation_failed";
    case PrivilegedNetworkTransactionStatus::publish_failed: return "publish_failed";
    case PrivilegedNetworkTransactionStatus::reload_failed_rolled_back:
        return "reload_failed_rolled_back";
    case PrivilegedNetworkTransactionStatus::confirmation_failed_rolled_back:
        return "confirmation_failed_rolled_back";
    case PrivilegedNetworkTransactionStatus::rollback_failed: return "rollback_failed";
    }
    return "unknown";
}

PrivilegedNetworkTransaction::PrivilegedNetworkTransaction(
    PrivilegedNetworkTransactionSnapshot initial,
    PrivilegedNetworkTransactionOperations operations)
    : state_(std::move(initial)), operations_(std::move(operations)) {}

const PrivilegedNetworkTransactionSnapshot &
PrivilegedNetworkTransaction::state() const noexcept {
    return state_;
}

PrivilegedNetworkTransactionResult PrivilegedNetworkTransaction::apply(
    const std::optional<std::string> &requested_value) {
    const auto parsed = parse_privileged_network_mode(requested_value);
    PrivilegedNetworkTransactionCandidate candidate;
    candidate.requested = parsed.mode;
    candidate.persisted_setting = parsed.mode == PrivilegedNetworkMode::enforced
        ? "enforced" : "insecure-disabled";
    candidate.setting_was_valid = parsed.valid;
    candidate.setting_was_missing = parsed.missing;
    const bool warning = !parsed.valid;

    const auto rendered = render_apache_privileged_network_policy(parsed.mode);
    if (!rendered.valid())
        return {PrivilegedNetworkTransactionStatus::render_failed, state_, warning};
    candidate.apache_policy = *rendered.configuration;

    if (!operations_.validate_application(candidate))
        return {PrivilegedNetworkTransactionStatus::application_validation_failed, state_, warning};
    if (!operations_.validate_apache(candidate.apache_policy))
        return {PrivilegedNetworkTransactionStatus::apache_validation_failed, state_, warning};

    const auto previous = state_;

    const auto rollback = [&](PrivilegedNetworkTransactionStatus failure,
                              bool reload_required = true) {
        if (!operations_.restore(previous) ||
            (reload_required && !operations_.reload_apache())) {
            state_.configured_known = false;
            state_.active_known = false;
            return PrivilegedNetworkTransactionResult{
                PrivilegedNetworkTransactionStatus::rollback_failed, state_, warning};
        }
        state_ = previous;
        return PrivilegedNetworkTransactionResult{failure, state_, warning};
    };

    if (!operations_.publish(candidate))
        return rollback(PrivilegedNetworkTransactionStatus::publish_failed, false);

    if (!operations_.reload_apache())
        return rollback(PrivilegedNetworkTransactionStatus::reload_failed_rolled_back);
    if (!operations_.confirm_active(parsed.mode))
        return rollback(PrivilegedNetworkTransactionStatus::confirmation_failed_rolled_back);

    state_ = {parsed.mode, parsed.mode, true, true, candidate.persisted_setting,
              candidate.apache_policy, "", true, false};
    return {PrivilegedNetworkTransactionStatus::applied, state_, warning};
}
