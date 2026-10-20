#pragma once

#include "privileged_network_policy.hpp"

#include <functional>
#include <optional>
#include <string>

struct PrivilegedNetworkTransactionSnapshot {
    PrivilegedNetworkMode configured = PrivilegedNetworkMode::enforced;
    PrivilegedNetworkMode active = PrivilegedNetworkMode::enforced;
    bool configured_known = true;
    bool active_known = true;
    std::string persisted_setting;
    std::string apache_policy;
    std::string application_configuration;
    bool setting_was_valid = false;
    bool setting_was_missing = true;
};

struct PrivilegedNetworkTransactionCandidate {
    PrivilegedNetworkMode requested = PrivilegedNetworkMode::enforced;
    std::string persisted_setting;
    std::string apache_policy;
    bool setting_was_valid = false;
    bool setting_was_missing = false;
};

enum class PrivilegedNetworkTransactionStatus {
    applied,
    render_failed,
    application_validation_failed,
    apache_validation_failed,
    publish_failed,
    reload_failed_rolled_back,
    confirmation_failed_rolled_back,
    rollback_failed
};

[[nodiscard]] const char *privileged_network_transaction_status_name(
    PrivilegedNetworkTransactionStatus status) noexcept;

struct PrivilegedNetworkTransactionResult {
    PrivilegedNetworkTransactionStatus status;
    PrivilegedNetworkTransactionSnapshot state;
    bool warning_defaulted_to_enforced = false;
    [[nodiscard]] bool applied() const noexcept {
        return status == PrivilegedNetworkTransactionStatus::applied;
    }
    [[nodiscard]] std::string status_text() const {
        if (!state.active_known)
            return "NETWORK SAFETY STATE UNKNOWN";
        return state.active == PrivilegedNetworkMode::insecure_disabled
            ? "NETWORK SAFETY OFF" : "NETWORK SAFETY ENFORCED";
    }
};

struct PrivilegedNetworkTransactionOperations {
    std::function<bool(const PrivilegedNetworkTransactionCandidate &)> validate_application;
    std::function<bool(const std::string &)> validate_apache;
    std::function<bool(const PrivilegedNetworkTransactionCandidate &)> publish;
    std::function<bool()> reload_apache;
    std::function<bool(PrivilegedNetworkMode)> confirm_active;
    std::function<bool(const PrivilegedNetworkTransactionSnapshot &)> restore;
};

class PrivilegedNetworkTransaction {
public:
    PrivilegedNetworkTransaction(
        PrivilegedNetworkTransactionSnapshot initial,
        PrivilegedNetworkTransactionOperations operations);

    [[nodiscard]] PrivilegedNetworkTransactionResult apply(
        const std::optional<std::string> &requested_value);
    [[nodiscard]] const PrivilegedNetworkTransactionSnapshot &state() const noexcept;

private:
    PrivilegedNetworkTransactionSnapshot state_;
    PrivilegedNetworkTransactionOperations operations_;
};
