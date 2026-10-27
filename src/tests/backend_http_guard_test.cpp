#include "../backend_http_guard.hpp"

#include <cassert>
#include <iostream>

int main() {
    const SupportRequestGuardSnapshot scoped_snapshot{
        true, "wsprrypi", {}, {{"fe80::10", "ffff:ffff:ffff:ffff::"}}};
    for (const std::string method : {"PUT", "PATCH"}) {
    assert(evaluate_backend_http_request(method, "/config", "127.0.0.1",
            "wsprrypi", "http://wsprrypi", scoped_snapshot,
            PrivilegedNetworkMode::enforced, {"fe80::42%wlan1"}) ==
            BackendHttpGuardDecision::allowed);
        assert(evaluate_backend_http_request(method, "/config", "127.0.0.1",
            "wsprrypi", "http://wsprrypi", scoped_snapshot,
            PrivilegedNetworkMode::enforced, {"fe80::42%wlan1%bad"}) ==
            BackendHttpGuardDecision::rejected);
    }
    const SupportRequestGuardSnapshot snapshot{
        true, "wsprrypi", {}, {{"192.168.50.10", "255.255.255.0"}}};
    const auto evaluate = [&](std::string method, std::string path,
                              std::string peer, std::string host,
                              std::optional<std::string> origin = std::nullopt) {
        return evaluate_backend_http_request(
            method, path, peer, host, origin, snapshot);
    };
    for (const std::string method : {"GET", "POST"}) {
        assert(evaluate(method, "/api/v1/member/configuration", "192.168.50.42", "wsprrypi", "http://wsprrypi") == BackendHttpGuardDecision::allowed);
        assert(evaluate(method, "/api/v1/member/configuration", "203.0.113.4", "wsprrypi", "http://wsprrypi") == BackendHttpGuardDecision::rejected);
        assert(evaluate(method, "/api/v1/member/configuration", "192.168.50.42", "wsprrypi", "http://evil") == BackendHttpGuardDecision::rejected);
    }

    for (const std::string path : {"/api/v1/config", "/api/v1/network", "/api/v1/schedules", "/api/v1/host/config", "/api/v1/host/logging", "/api/v1/member/logging"}) {
        for (const std::string method : {"GET", "PUT"}) {
            assert(evaluate(method, path, "192.168.50.42", "wsprrypi", "http://wsprrypi") == BackendHttpGuardDecision::allowed);
            assert(evaluate(method, path, "203.0.113.4", "wsprrypi", "http://wsprrypi") == BackendHttpGuardDecision::rejected);
            assert(evaluate(method, path, "192.168.50.42", "wsprrypi", "http://evil") == BackendHttpGuardDecision::rejected);
            assert(evaluate_backend_http_request(method, path, "127.0.0.1", "wsprrypi", "http://wsprrypi", snapshot,
                PrivilegedNetworkMode::enforced, {"192.168.50.42"}) == BackendHttpGuardDecision::allowed);
            assert(evaluate_backend_http_request(method, path, "127.0.0.1", "wsprrypi", "http://wsprrypi", snapshot,
                PrivilegedNetworkMode::enforced, {"203.0.113.4"}) == BackendHttpGuardDecision::rejected);
            assert(evaluate_backend_http_request(method, path, "203.0.113.4", "wsprrypi", "http://wsprrypi", snapshot,
                PrivilegedNetworkMode::enforced, {"192.168.50.42"}) == BackendHttpGuardDecision::rejected);
        }
    }
    assert(evaluate("POST", "/api/v1/unknown", "127.0.0.1", "wsprrypi") == BackendHttpGuardDecision::rejected);
    assert(evaluate("POST", "/api/wtp/recover", "192.168.50.42", "wsprrypi", "http://wsprrypi") == BackendHttpGuardDecision::allowed);
    assert(evaluate("POST", "/api/wtp/recover", "192.168.51.42", "wsprrypi") == BackendHttpGuardDecision::rejected);
    assert(evaluate("POST", "/api/wtp/recover", "192.168.50.42", "wsprrypi", "http://evil") == BackendHttpGuardDecision::rejected);
    assert(evaluate("PUT", "/api/wtp", "127.0.0.1", "wsprrypi") == BackendHttpGuardDecision::rejected);
    assert(evaluate("PUT", "/config", "127.0.0.1", "wsprrypi") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("PATCH", "/config", "192.168.50.42", "wsprrypi:31415",
                    "http://wsprrypi:31415") == BackendHttpGuardDecision::allowed);
    assert(evaluate("POST", "/control/stop", "192.168.51.42", "wsprrypi") ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate("POST", "/api/support-bundles", "192.168.50.42", "evil") ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate("POST", "/api/network-safety", "192.168.51.42", "wsprrypi") ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate("GET", "/api/network-safety", "192.168.50.42", "wsprrypi") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("POST", "/api/rp1-gpclk-route", "192.168.51.42", "wsprrypi") ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate("POST", "/api/rp1-gpclk-route", "192.168.50.42", "wsprrypi") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("GET", "/api/rp1-gpclk-route", "192.168.50.42", "wsprrypi") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("GET", "/api/support-bundles/job/download", "192.168.50.42",
                    "wsprrypi", "http://evil") == BackendHttpGuardDecision::rejected);
    assert(evaluate("GET", "/config", "203.0.113.4", "foreign") ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate("GET", "/config", "192.168.50.42", "wsprrypi") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("GET", "/config/si5351-addresses", "192.168.50.42", "wsprrypi") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("POST", "/future-operation", "127.0.0.1", "localhost") ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate("POST", "/config/future-operation", "127.0.0.1", "localhost") ==
           BackendHttpGuardDecision::rejected);

    // Forwarded client identity is intentionally absent from the guard API and
    // therefore cannot turn this actual off-LAN peer into an allowed request.
    assert(evaluate("PUT", "/config", "203.0.113.4", "wsprrypi") ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "203.0.113.4", "wsprrypi", std::nullopt,
               snapshot, PrivilegedNetworkMode::insecure_disabled) ==
           BackendHttpGuardDecision::allowed);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "203.0.113.4", "evil", std::nullopt,
               snapshot, PrivilegedNetworkMode::insecure_disabled) ==
           BackendHttpGuardDecision::rejected);
    assert(evaluate_backend_http_request(
               "GET", "/config", "203.0.113.4", "wsprrypi", std::nullopt,
               snapshot, PrivilegedNetworkMode::insecure_disabled) ==
           BackendHttpGuardDecision::rejected);

    // Apache loopback traffic is authorized using only the dedicated identity.
    assert(evaluate_backend_http_request(
               "PUT", "/config", "127.0.0.1", "wsprrypi", std::nullopt,
               snapshot, PrivilegedNetworkMode::enforced,
               {"192.168.50.42"}) == BackendHttpGuardDecision::allowed);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "127.0.0.1", "wsprrypi", std::nullopt,
               snapshot, PrivilegedNetworkMode::enforced,
               {"192.168.51.42"}) == BackendHttpGuardDecision::rejected);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "203.0.113.4", "wsprrypi", std::nullopt,
               snapshot, PrivilegedNetworkMode::enforced,
               {"192.168.50.42"}) == BackendHttpGuardDecision::rejected);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "127.0.0.1", "wsprrypi", std::nullopt,
               SupportRequestGuardSnapshot{true, "wsprrypi", {}, {}},
               PrivilegedNetworkMode::enforced,
               {"192.168.50.42"}) == BackendHttpGuardDecision::rejected);
    assert(evaluate_backend_http_request(
               "GET", "/config", "127.0.0.1", "wsprrypi", std::nullopt,
               SupportRequestGuardSnapshot{true, "wsprrypi", {}, {}},
               PrivilegedNetworkMode::enforced,
               {"192.168.50.42"}) == BackendHttpGuardDecision::allowed);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "127.0.0.1", "wsprrypi", std::nullopt,
               SupportRequestGuardSnapshot{true, "wsprrypi", {}, {}},
               PrivilegedNetworkMode::insecure_disabled,
               {"203.0.113.4"}) == BackendHttpGuardDecision::allowed);
    // A proxied future route fails closed until its operation is explicitly
    // classified, so adding a handler cannot accidentally inherit loopback.
    assert(evaluate_backend_http_request(
               "POST", "/api/future-operation", "127.0.0.1", "wsprrypi",
               std::nullopt, snapshot, PrivilegedNetworkMode::enforced,
               {"192.168.50.42"}) == BackendHttpGuardDecision::rejected);
    assert(evaluate_backend_http_request(
               "PUT", "/config", "127.0.0.1", "evil", std::nullopt,
               SupportRequestGuardSnapshot{true, "wsprrypi", {}, {}},
               PrivilegedNetworkMode::insecure_disabled,
               {"203.0.113.4"}) == BackendHttpGuardDecision::rejected);

    std::cout << "backend_http_guard_test: PASS\n";
}
