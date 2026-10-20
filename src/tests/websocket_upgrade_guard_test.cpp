#include "../websocket_upgrade_guard.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::string request(std::string host = "wsprrypi:31416",
                    std::string origin = {},
                    std::string extra_headers = {}) {
    std::string value =
        "GET / HTTP/1.1\r\nHost: " + host +
        "\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    if (!origin.empty()) value += "Origin: " + origin + "\r\n";
    value += extra_headers;
    return value + "\r\n";
}
} // namespace

int main() {
    const SupportRequestGuardSnapshot scoped_snapshot{
        true, "wsprrypi", {}, {{"fe80::10", "ffff:ffff:ffff:ffff::"}}};
    assert(evaluate_websocket_upgrade(
        request("wsprrypi", "http://wsprrypi",
                "X-WsprryPi-Client-Address: fe80::42%wlan1\r\n"),
        "127.0.0.1", scoped_snapshot).decision == WebSocketUpgradeGuardDecision::allowed);
    assert(evaluate_websocket_upgrade(
        request("wsprrypi", "http://wsprrypi",
                "X-WsprryPi-Client-Address: fe80::42%wlan1%bad\r\n"),
        "127.0.0.1", scoped_snapshot).decision == WebSocketUpgradeGuardDecision::rejected);
    const SupportRequestGuardSnapshot snapshot{
        true, "wsprrypi", {}, {{"192.168.50.10", "255.255.255.0"}}};
    const auto evaluate = [&](const std::string &upgrade, const std::string &peer) {
        return evaluate_websocket_upgrade(upgrade, peer, snapshot).decision;
    };

    assert(evaluate(request(), "127.0.0.1") == WebSocketUpgradeGuardDecision::allowed);
    assert(evaluate(request("wsprrypi:31416", "http://wsprrypi:31416"),
                    "192.168.50.42") == WebSocketUpgradeGuardDecision::allowed);
    assert(evaluate(request(), "192.168.51.42") == WebSocketUpgradeGuardDecision::rejected);
    assert(evaluate_websocket_upgrade(
               request(), "192.168.51.42", snapshot,
               PrivilegedNetworkMode::insecure_disabled).decision ==
           WebSocketUpgradeGuardDecision::allowed);
    assert(evaluate_websocket_upgrade(
               request("evil"), "192.168.51.42", snapshot,
               PrivilegedNetworkMode::insecure_disabled).decision ==
           WebSocketUpgradeGuardDecision::rejected);
    assert(evaluate(request("evil"), "192.168.50.42") ==
           WebSocketUpgradeGuardDecision::rejected);
    assert(evaluate(request("wsprrypi:31416", "null"), "192.168.50.42") ==
           WebSocketUpgradeGuardDecision::rejected);

    std::string forwarded = request();
    forwarded.insert(forwarded.size() - 2,
                     "Forwarded: for=192.168.50.42\r\nX-Forwarded-For: 192.168.50.42\r\n");
    assert(evaluate(forwarded, "192.168.51.42") == WebSocketUpgradeGuardDecision::rejected);

    const std::string trusted =
        "X-WsprryPi-Client-Address: 192.168.50.42\r\n";
    assert(evaluate(request("wsprrypi:31416", {}, trusted), "127.0.0.1") ==
           WebSocketUpgradeGuardDecision::allowed);
    assert(evaluate(request("wsprrypi:31416", {}, trusted), "192.168.51.42") ==
           WebSocketUpgradeGuardDecision::rejected);
    assert(evaluate(request("wsprrypi:31416", {},
                            "X-WsprryPi-Client-Address: 192.168.51.42\r\n"),
                    "127.0.0.1") == WebSocketUpgradeGuardDecision::rejected);
    const auto duplicate_trusted = evaluate_websocket_upgrade(
        request("wsprrypi:31416", {},
                "X-WsprryPi-Client-Address: 192.168.50.42\r\n"
                "X-WsprryPi-Client-Address: 192.168.50.43\r\n"),
        "127.0.0.1", snapshot);
    assert(duplicate_trusted.decision == WebSocketUpgradeGuardDecision::rejected);
    assert(duplicate_trusted.rejection_reason ==
           SupportRequestGuardDecision::invalid_trusted_proxy_identity);
    assert(evaluate_websocket_upgrade(
               request("wsprrypi:31416", {}, trusted), "127.0.0.1",
               SupportRequestGuardSnapshot{true, "wsprrypi", {}, {}}).decision ==
           WebSocketUpgradeGuardDecision::rejected);
    assert(evaluate_websocket_upgrade(
               request("wsprrypi:31416", {},
                       "X-WsprryPi-Client-Address: 203.0.113.4\r\n"),
               "127.0.0.1",
               SupportRequestGuardSnapshot{true, "wsprrypi", {}, {}},
               PrivilegedNetworkMode::insecure_disabled).decision ==
           WebSocketUpgradeGuardDecision::allowed);

    for (const std::string &malformed : std::vector<std::string>{
             std::string{},
             "POST / HTTP/1.1\r\nHost: wsprrypi\r\n\r\n",
             "GET / HTTP/1.1\r\nUpgrade: websocket\r\n\r\n",
             "GET / HTTP/1.1\r\nHost : wsprrypi\r\nUpgrade: websocket\r\n"
             "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
             "Sec-WebSocket-Key: key\r\n\r\n",
             request() + "extra",
             "GET / HTTP/1.1\r\nHost: wsprrypi\r\nHost: wsprrypi\r\n"
             "Upgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: key\r\n\r\n"}) {
        assert(evaluate(malformed, "127.0.0.1") ==
               WebSocketUpgradeGuardDecision::malformed);
    }

    std::cout << "websocket_upgrade_guard_test: PASS\n";
}
