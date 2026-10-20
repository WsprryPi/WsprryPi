#include "../privileged_network_transaction.hpp"
#include "../INI-Handler/src/ini_file.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Controls {
    bool application_valid = true;
    bool apache_valid = true;
    bool publish_ok = true;
    bool confirm_ok = true;
    bool restore_ok = true;
    std::vector<bool> reload_results{true};
    std::vector<std::string> calls;
};

PrivilegedNetworkTransaction make_transaction(
    Controls &controls,
    PrivilegedNetworkTransactionSnapshot initial = {}) {
    auto operations = PrivilegedNetworkTransactionOperations{
        [&](const PrivilegedNetworkTransactionCandidate &) {
            controls.calls.push_back("validate_application");
            return controls.application_valid;
        },
        [&](const std::string &) {
            controls.calls.push_back("validate_apache");
            return controls.apache_valid;
        },
        [&](const PrivilegedNetworkTransactionCandidate &) {
            controls.calls.push_back("publish");
            return controls.publish_ok;
        },
        [&] {
            controls.calls.push_back("reload");
            const bool result = controls.reload_results.empty()
                ? true : controls.reload_results.front();
            if (!controls.reload_results.empty())
                controls.reload_results.erase(controls.reload_results.begin());
            return result;
        },
        [&](PrivilegedNetworkMode) {
            controls.calls.push_back("confirm");
            return controls.confirm_ok;
        },
        [&](const PrivilegedNetworkTransactionSnapshot &) {
            controls.calls.push_back("restore");
            return controls.restore_ok;
        }};
    return PrivilegedNetworkTransaction(std::move(initial), std::move(operations));
}

void require_calls(const Controls &controls, std::vector<std::string> expected) {
    assert(controls.calls == expected);
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    assert(std::string(privileged_network_transaction_status_name(
               PrivilegedNetworkTransactionStatus::applied)) == "applied");
    assert(std::string(privileged_network_transaction_status_name(
               PrivilegedNetworkTransactionStatus::rollback_failed)) ==
           "rollback_failed");
    {
        Controls controls;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("enforced");
        assert(result.applied() && !result.warning_defaulted_to_enforced);
        assert(result.state.active == PrivilegedNetworkMode::enforced);
        assert(result.state.persisted_setting == "enforced");
        require_calls(controls, {"validate_application",
                                 "validate_apache", "publish", "reload", "confirm"});
    }
    {
        Controls controls;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("insecure-disabled");
        assert(result.applied());
        assert(result.status_text() == "NETWORK SAFETY OFF");
        assert(result.state.apache_policy.find("NETWORK SAFETY OFF") != std::string::npos);
        require_calls(controls, {"validate_application", "validate_apache",
                                 "publish", "reload", "confirm"});
    }
    {
        Controls controls;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("TRUE");
        assert(result.applied() && result.warning_defaulted_to_enforced);
        assert(result.state.active == PrivilegedNetworkMode::enforced);
    }
    {
        Controls controls;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply(std::nullopt);
        assert(result.applied() && result.warning_defaulted_to_enforced);
        assert(result.state.active == PrivilegedNetworkMode::enforced);
    }
    {
        Controls controls;
        controls.application_valid = false;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("enforced");
        assert(result.status ==
               PrivilegedNetworkTransactionStatus::application_validation_failed);
        require_calls(controls, {"validate_application"});
    }
    {
        Controls controls;
        controls.apache_valid = false;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("enforced");
        assert(result.status == PrivilegedNetworkTransactionStatus::apache_validation_failed);
        require_calls(controls, {"validate_application", "validate_apache"});
    }
    {
        Controls controls;
        controls.publish_ok = false;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("insecure-disabled");
        assert(result.status == PrivilegedNetworkTransactionStatus::publish_failed);
        require_calls(controls, {"validate_application", "validate_apache", "publish",
                                 "restore"});
    }
    {
        Controls controls;
        controls.publish_ok = false;
        controls.restore_ok = false;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("insecure-disabled");
        assert(result.status == PrivilegedNetworkTransactionStatus::rollback_failed);
        assert(!result.state.configured_known && !result.state.active_known);
        require_calls(controls, {"validate_application", "validate_apache", "publish",
                                 "restore"});
    }
    {
        Controls controls;
        controls.reload_results = {false, true};
        const PrivilegedNetworkTransactionSnapshot initial{
            PrivilegedNetworkMode::enforced, PrivilegedNetworkMode::enforced,
            true, true, "enforced", "old-policy"};
        auto transaction = make_transaction(controls, initial);
        const auto result = transaction.apply("insecure-disabled");
        assert(result.status ==
               PrivilegedNetworkTransactionStatus::reload_failed_rolled_back);
        assert(result.state.apache_policy == "old-policy");
        require_calls(controls, {"validate_application", "validate_apache", "publish",
                                 "reload", "restore", "reload"});
    }
    {
        Controls controls;
        controls.confirm_ok = false;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("insecure-disabled");
        assert(result.status ==
               PrivilegedNetworkTransactionStatus::confirmation_failed_rolled_back);
        require_calls(controls, {"validate_application", "validate_apache", "publish",
                                 "reload", "confirm", "restore", "reload"});
    }
    {
        Controls controls;
        controls.reload_results = {false};
        controls.restore_ok = false;
        auto transaction = make_transaction(controls);
        const auto result = transaction.apply("insecure-disabled");
        assert(result.status == PrivilegedNetworkTransactionStatus::rollback_failed);
        assert(!result.state.configured_known && !result.state.active_known);
        assert(result.status_text() == "NETWORK SAFETY STATE UNKNOWN");
        require_calls(controls, {"validate_application", "validate_apache", "publish",
                                 "reload", "restore"});
    }

    const auto ini_path = std::filesystem::temp_directory_path() /
        "wsprrypi-privileged-network-passthrough.ini";
    {
        std::ofstream output(ini_path, std::ios::binary | std::ios::trunc);
        output << "[Security]\n"
               << "Privileged Network Safety=insecure-disabled  ; retain exact spacing\n"
               << "Ordinary = before\n";
    }
    auto &ini = IniFile::instance();
    ini.set_raw_passthrough_keys({{"Security", "Privileged Network Safety"}});
    ini.set_filename(ini_path.string());
    ini.set_string_value("Security", "Privileged Network Safety", "enforced");
    ini.set_string_value("Security", "Ordinary", "after");
    ini.save();
    const std::string saved = read_file(ini_path);
    assert(saved.find(
        "Privileged Network Safety=insecure-disabled  ; retain exact spacing\n") !=
        std::string::npos);
    assert(saved.find("Ordinary = after\n") != std::string::npos);
    std::filesystem::remove(ini_path);

    std::cout << "privileged_network_transaction_test: PASS\n";
}
