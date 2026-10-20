#pragma once

#include "privileged_network_transaction.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct PrivilegedNetworkAdminPaths {
    std::string ini_file;
    std::string apache_policy_file =
        "/usr/local/etc/wsprrypi-apache-network-policy.conf";
    std::string apache_control = "/usr/sbin/apache2ctl";
    std::string system_control = "/usr/bin/systemctl";
};

using PrivilegedNetworkCommandRunner =
    std::function<bool(const std::vector<std::string> &)>;

[[nodiscard]] std::optional<std::string> read_text_file(
    const std::string &path);
[[nodiscard]] std::optional<std::string> render_privileged_network_ini(
    const std::string &existing, const std::string &value);
[[nodiscard]] std::optional<std::string> read_privileged_network_ini_value(
    const std::string &contents);
[[nodiscard]] bool write_text_file_atomically(
    const std::string &path, const std::string &contents);
[[nodiscard]] bool run_privileged_network_command(
    const std::vector<std::string> &arguments);

class PrivilegedNetworkAdmin {
public:
    explicit PrivilegedNetworkAdmin(
        PrivilegedNetworkAdminPaths paths,
        PrivilegedNetworkCommandRunner runner = run_privileged_network_command);

    [[nodiscard]] PrivilegedNetworkTransactionSnapshot status();
    [[nodiscard]] PrivilegedNetworkTransactionResult apply(
        const std::optional<std::string> &requested_value);

private:
    [[nodiscard]] PrivilegedNetworkTransactionSnapshot snapshot() const;
    [[nodiscard]] bool validate_apache_candidate(const std::string &policy) const;

    PrivilegedNetworkAdminPaths paths_;
    PrivilegedNetworkCommandRunner runner_;
    mutable std::mutex mutex_;
};
