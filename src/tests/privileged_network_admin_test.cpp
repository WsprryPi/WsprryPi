#include "../privileged_network_admin.hpp"
#include "../privileged_network_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
void write_file(const std::filesystem::path &path, const std::string &contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output.is_open());
    output << contents;
}
}

int main() {
    const std::string original =
        "; keep this comment\n[Security]\n"
        "Privileged Network Safety = enforced\n"
        "Ordinary = untouched\n[Operation]\nMode = WSPR\n";
    const auto rendered = render_privileged_network_ini(
        original, "insecure-disabled");
    assert(rendered.has_value());
    assert(rendered->find("; keep this comment") != std::string::npos);
    assert(rendered->find("Ordinary = untouched") != std::string::npos);
    assert(read_privileged_network_ini_value(*rendered) ==
           std::optional<std::string>("insecure-disabled"));
    assert(!render_privileged_network_ini(original, "true").has_value());
    assert(!render_privileged_network_ini(
        "[Security]\n[Security]\n", "enforced").has_value());

    const auto root = std::filesystem::temp_directory_path() /
        ("wsprrypi-network-admin-" + std::to_string(::getpid()));
    std::filesystem::create_directories(root);
    const auto ini = root / "wsprrypi.ini";
    const auto policy = root / "policy.conf";
    write_file(ini, original);
    write_file(policy, "old-policy\n");

    initialize_privileged_network_runtime("enforced");
    std::vector<std::vector<std::string>> calls;
    PrivilegedNetworkAdmin admin(
        {ini.string(), policy.string(), "/mock/apache2ctl", "/mock/systemctl"},
        [&](const std::vector<std::string> &arguments) {
            calls.push_back(arguments);
            return true;
        });
    const auto before_apply = admin.status();
    assert(before_apply.configured_known && before_apply.active_known);
    assert(before_apply.setting_was_valid && !before_apply.setting_was_missing);
    const auto applied = admin.apply("insecure-disabled");
    assert(applied.applied());
    assert(applied.status_text() == "NETWORK SAFETY OFF");
    assert(calls.size() == 2);
    assert(calls[0] == std::vector<std::string>({
        "/mock/apache2ctl", "-t", "-C",
        "Include \"" + policy.string() + ".candidate." +
            std::to_string(static_cast<long long>(::getpid())) + "\""}));
    assert(calls[1] == std::vector<std::string>(
        {"/mock/systemctl", "reload", "apache2"}));
    assert(read_privileged_network_ini_value(*read_text_file(ini.string())) ==
           std::optional<std::string>("insecure-disabled"));
    assert(read_text_file(policy.string())->find("NETWORK SAFETY OFF") !=
           std::string::npos);
    assert(active_privileged_network_mode() ==
           PrivilegedNetworkMode::insecure_disabled);

    write_file(ini, original);
    write_file(policy, "old-policy\n");
    initialize_privileged_network_runtime("enforced");
    set_active_privileged_network_mode(PrivilegedNetworkMode::enforced);
    std::vector<bool> outcomes{true, false, true};
    PrivilegedNetworkAdmin rollback_admin(
        {ini.string(), policy.string(), "/mock/apache2ctl", "/mock/systemctl"},
        [&](const std::vector<std::string> &) {
            assert(!outcomes.empty());
            const bool result = outcomes.front();
            outcomes.erase(outcomes.begin());
            return result;
        });
    const auto rolled_back = rollback_admin.apply("insecure-disabled");
    assert(rolled_back.status ==
           PrivilegedNetworkTransactionStatus::reload_failed_rolled_back);
    assert(read_text_file(ini.string()) == std::optional<std::string>(original));
    assert(read_text_file(policy.string()) ==
           std::optional<std::string>("old-policy\n"));
    assert(active_privileged_network_mode() == PrivilegedNetworkMode::enforced);

    std::filesystem::remove_all(root);
    std::cout << "privileged_network_admin_test: PASS\n";
}
