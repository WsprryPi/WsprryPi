#include "../privileged_network_policy.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using Classification = PrivilegedOperationClass;

static void expect_http(std::string_view method, std::string_view path,
                        Classification expected) {
    assert(classify_privileged_http_operation(method, path) == expected);
}

static PrivilegedInterfaceCandidate ethernet(std::string address,
                                             std::string netmask) {
    return {true, PrivilegedInterfaceKind::ethernet, false, false, false, false,
            false, false, std::move(address), std::move(netmask)};
}

int main() {
    assert(classify_privileged_interface_link({true, false, 1}) ==
           PrivilegedInterfaceKind::ethernet);
    assert(classify_privileged_interface_link({true, true, 1}) ==
           PrivilegedInterfaceKind::wifi);
    assert(classify_privileged_interface_link({false, false, 1}) ==
           PrivilegedInterfaceKind::other);
    assert(classify_privileged_interface_link({false, true, 1}) ==
           PrivilegedInterfaceKind::other);
    assert(classify_privileged_interface_link({true, false, 772}) ==
           PrivilegedInterfaceKind::other);

    for (std::string_view method : {"PUT", "PATCH"}) {
        expect_http(method, "/config", Classification::protected_operation);
    }
    expect_http("POST", "/config/repair", Classification::protected_operation);
    expect_http("POST", "/control/stop", Classification::protected_operation);
    expect_http("POST", "/api/wtp/recover", Classification::protected_operation);
    expect_http("GET", "/api/wtp", Classification::read_only);
    expect_http("GET", "/api/wtp/recover", Classification::reject);
    expect_http("PUT", "/api/wtp", Classification::reject);
    for (const auto* path : {"/api/v1/host/fleet", "/api/v1/host/wtp-endpoint"}) {
        expect_http("GET", path, Classification::protected_operation);
        expect_http("DELETE", path, Classification::reject);
    }
    for (const auto* path : {"/api/v1/host/fleet", "/api/v1/host/wtp-endpoint/enable", "/api/v1/host/wtp-endpoint/recover"}) {
        expect_http("POST", path, Classification::protected_operation);
        expect_http("PUT", path, Classification::reject);
    }
    expect_http("GET", "/api/v1/host/wtp-endpoint/enable", Classification::reject);
    expect_http("POST", "/api/v1/host/wtp-endpoint", Classification::reject);
    expect_http("POST", "/api/network-safety", Classification::protected_operation);
    expect_http("GET", "/api/network-safety", Classification::read_only);
    expect_http("PUT", "/api/network-safety", Classification::reject);
    expect_http("POST", "/api/rp1-gpclk-route", Classification::protected_operation);
    expect_http("GET", "/api/rp1-gpclk-route", Classification::read_only);
    expect_http("PUT", "/api/rp1-gpclk-route", Classification::reject);
    for (const auto &request : std::vector<std::pair<std::string, std::string>>{
             {"POST", "/api/support-bundles"},
             {"GET", "/api/support-bundles/job-id"},
             {"GET", "/api/support-bundles/job-id/download"},
             {"DELETE", "/api/support-bundles/job-id"},
             {"OPTIONS", "/api/support-bundles/job-id/download"},
             {"POST", "/api/support-bundles/job-id/future-operation"}}) {
        expect_http(request.first, request.second, Classification::protected_operation);
    }

    for (std::string_view path :
         {"/config", "/config/si5351-addresses", "/version", "/status",
          "/telemetry", "/api/support-intake"}) {
        expect_http("GET", path, Classification::read_only);
    }
    expect_http("POST", "/api/support-intake", Classification::reject);
    expect_http("GET", "/api/support-bundles-archive", Classification::reject);
    expect_http("get", "/config", Classification::reject);
    expect_http("GET", "config", Classification::reject);
    expect_http("GET", "/config?unsafe=1", Classification::reject);
    expect_http("POST", "/unknown", Classification::reject);

    for (std::string_view command :
         {"shutdown", "reboot", "stop", "tone_start", "tone_end", "bounded_tone"}) {
        assert(classify_privileged_websocket_command(command) ==
               Classification::protected_operation);
    }
    for (std::string_view command : {"get_tx_state", "wspr_band_catalog", "echo"}) {
        assert(classify_privileged_websocket_command(command) == Classification::read_only);
    }
    assert(classify_privileged_websocket_command("future_command") == Classification::reject);
    for (std::string_view operation : {"ping", "pong", "server_broadcast"}) {
        assert(classify_privileged_websocket_protocol_operation(operation) ==
               Classification::read_only);
    }
    assert(classify_privileged_websocket_protocol_operation("future_transport") ==
           Classification::reject);

    const auto missing = parse_privileged_network_mode(std::nullopt);
    assert(missing.mode == PrivilegedNetworkMode::enforced && !missing.valid && missing.missing);
    for (const std::string invalid : {"", "Enforced", "INSECURE-DISABLED", "true", "false",
                                      "0", "1", "disabled", " insecure-disabled"}) {
        const auto parsed = parse_privileged_network_mode(invalid);
        assert(parsed.mode == PrivilegedNetworkMode::enforced && !parsed.valid && !parsed.missing);
    }
    assert(parse_privileged_network_mode("enforced").valid);
    const auto disabled = parse_privileged_network_mode("insecure-disabled");
    assert(disabled.valid && disabled.mode == PrivilegedNetworkMode::insecure_disabled);

    assert(is_eligible_privileged_interface(ethernet("192.168.50.10", "255.255.255.0")));
    auto wifi6 = ethernet("fd00::10", "ffff:ffff:ffff:ffff::");
    wifi6.kind = PrivilegedInterfaceKind::wifi;
    assert(is_eligible_privileged_interface(wifi6));
    assert(has_eligible_privileged_subnet({
        ethernet("192.168.50.10", "255.255.255.0"), wifi6}));
    assert(!has_eligible_privileged_subnet({}));
    for (int excluded = 0; excluded < 6; ++excluded) {
        auto candidate = ethernet("192.168.50.10", "255.255.255.0");
        if (excluded == 0) candidate.loopback = true;
        if (excluded == 1) candidate.point_to_point = true;
        if (excluded == 2) candidate.tunnel = true;
        if (excluded == 3) candidate.vpn = true;
        if (excluded == 4) candidate.container = true;
        if (excluded == 5) candidate.software_bridge = true;
        assert(!is_eligible_privileged_interface(candidate));
    }
    auto inactive = ethernet("192.168.50.10", "255.255.255.0");
    inactive.active = false;
    assert(!is_eligible_privileged_interface(inactive));
    auto other = ethernet("192.168.50.10", "255.255.255.0");
    other.kind = PrivilegedInterfaceKind::other;
    assert(!is_eligible_privileged_interface(other));
    for (const auto &address_mask : std::vector<std::pair<std::string, std::string>>{
             {"", "255.255.255.0"}, {"192.168.50.10", ""},
             {"192.168.50.10", "255.0.255.0"},
             {"0.0.0.0", "255.255.255.0"}, {"127.0.0.1", "255.0.0.0"},
             {"224.0.0.1", "255.255.255.0"}, {"::", "ffff::"},
             {"::1", "ffff::"}, {"ff02::1", "ffff::"},
             {"fd00::10", "ffff:0:ffff::"}}) {
        assert(!is_eligible_privileged_interface(ethernet(address_mask.first,
                                                           address_mask.second)));
    }

    for (std::string_view name : {"Forwarded", "X-Forwarded-For", "x-real-ip",
                                  "X-Client-IP", "True-Client-IP", "CF-Connecting-IP"}) {
        assert(is_forwarded_client_identity_header(name));
    }
    assert(!is_forwarded_client_identity_header("Host"));
    assert(!is_forwarded_client_identity_header("Origin"));

    std::cout << "privileged_network_policy_test: PASS\n";
}
