#include "../apache_privileged_network_policy.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path);
    assert(input.is_open());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
} // namespace

int main(int argc, char **argv) {
    const auto rendered = render_apache_privileged_network_policy();
    assert(rendered.valid() && rendered.error.empty());
    const std::string &policy = *rendered.configuration;
    if (argc == 2 && std::string(argv[1]) == "--render") {
        std::cout << policy;
        return 0;
    }
    assert(policy.find(
        "RequestHeader unset X-WsprryPi-Client-Address") !=
        std::string::npos);
    assert(policy.find(
        "RequestHeader set X-WsprryPi-Client-Address "
        "\"expr=%{CONN_REMOTE_ADDR}\"") != std::string::npos);
    assert(policy.find("Require ip") == std::string::npos);
    assert(policy.find("^/wsprrypi/(?:config(?:/|$)|control(?:/|$)|version$|api(?:/|$)|socket(?:/|$))") !=
           std::string::npos);
    assert(policy.find("Forwarded") == std::string::npos);
    assert(policy.find("X-Forwarded") == std::string::npos);
    assert(policy.find("RemoteIPHeader") == std::string::npos);

    const auto disabled = render_apache_privileged_network_policy(
        PrivilegedNetworkMode::insecure_disabled);
    assert(disabled.valid());
    assert(disabled.configuration->find("NETWORK SAFETY OFF") !=
           std::string::npos);
    assert(disabled.configuration->find("RequestHeader unset") !=
           std::string::npos);
    assert(disabled.configuration->find("RequestHeader set") !=
           std::string::npos);

    const auto source_root = std::filesystem::current_path();
    const std::string stock_vhost =
        read_file(source_root / "../config/wsprrypi.conf");
    const std::string installer =
        read_file(source_root / "../scripts/install.sh");
    assert(stock_vhost.find("ProxyPreserveHost On") != std::string::npos);
    assert(stock_vhost.find(
        "Include /usr/local/etc/wsprrypi-apache-network-policy.conf") !=
        std::string::npos);
    assert(installer.find(
        "a2enmod headers proxy proxy_http proxy_wstunnel") !=
        std::string::npos);
    for (const std::string *source : {&stock_vhost, &installer}) {
        assert(source->find("RequestHeader set Host") == std::string::npos);
        assert(source->find("RequestHeader set Origin") == std::string::npos);
        assert(source->find("RemoteIPHeader") == std::string::npos);
    }

    std::cout << "apache_privileged_network_policy_test: PASS\n";
}
