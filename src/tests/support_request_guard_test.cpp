#include "support_request_guard.hpp"
#include <cassert>
#include <iostream>

using Decision = SupportRequestGuardDecision;
static void expect(const SupportRequestGuard &g, const std::string &peer, const std::string &host, Decision d, std::optional<std::string> origin = {}) { const auto got = g.evaluate(peer, host, origin).decision; if (got != d) { std::cerr << "unexpected decision for " << peer << " / " << host << "\n"; assert(false); } }

int main(int argc, char **argv) {
    SupportRequestGuardSnapshot snapshot{true, "WsprryPi.Local.", {}, {{"192.168.50.10", "255.255.255.0"}, {"fd00::10", "ffff:ffff:ffff:ffff::"}}};
    SupportRequestGuard guard(snapshot);
    SupportRequestGuard link_local({true, "wsprrypi", {},
        {{"fe80::10", "ffff:ffff:ffff:ffff::"}}});
    for (const std::string zone : {"wlan1", "3", "en0", "vlan.2"}) {
        const auto peer = "fe80::42%" + zone;
        expect(link_local, peer, "wsprrypi", Decision::allowed);
        assert(link_local.evaluate("127.0.0.1", "wsprrypi", {}, true,
                                   {peer}).allowed());
        assert(guard.evaluate("127.0.0.1", "wsprrypi", {}, true,
                              {peer}).decision == Decision::rejected_peer);
    }
    for (const std::string bad : {"fe80::42%", "fe80::42%wlan1%2",
         "fe80::42%wlan 1", "fe80::42%wlan1,evil", "::1%lo",
         "::ffff:127.0.0.1%lo", "192.168.50.42%wlan1", "fd00::42%en0",
         "fe80::42%abcdefghijklmnop"}) {
        assert(link_local.evaluate("127.0.0.1", "wsprrypi", {}, true,
                                   {bad}).decision == Decision::invalid_trusted_proxy_identity);
    }
    expect(link_local, "fe80::42%wlan1", "evil.example", Decision::rejected_host);
    assert(link_local.evaluate("127.0.0.1", "wsprrypi", {}, true,
        {std::string("fe80::42\0junk%wlan1", 19)}).decision ==
        Decision::invalid_trusted_proxy_identity);
    expect(link_local, "fe80::42%wlan1", "wsprrypi", Decision::rejected_origin,
           "http://evil.example");
    // Peer policy.
    expect(guard, "127.0.0.1", "localhost", Decision::allowed); expect(guard, "::1", "[::1]", Decision::allowed); expect(guard, "::ffff:127.0.0.1", "localhost", Decision::allowed);
    expect(guard, "192.168.50.42", "wsprrypi", Decision::allowed); expect(guard, "fd00::42", "wsprrypi", Decision::allowed);
    expect(guard, "192.168.51.42", "wsprrypi", Decision::rejected_peer); expect(guard, "8.8.8.8", "wsprrypi", Decision::rejected_peer); expect(guard, "224.0.0.1", "wsprrypi", Decision::rejected_peer); expect(guard, "ff02::1", "wsprrypi", Decision::rejected_peer); expect(guard, "0.0.0.0", "wsprrypi", Decision::rejected_peer); expect(guard, "::", "wsprrypi", Decision::rejected_peer); expect(guard, "bad", "wsprrypi", Decision::rejected_peer);
    SupportRequestGuard no_network({true, "wsprrypi", {}, {}}); expect(no_network, "192.168.50.42", "wsprrypi", Decision::no_eligible_network); expect(no_network, "127.0.0.1", "localhost", Decision::allowed);
    SupportRequestGuard unavailable({false, "wsprrypi", {}, {}}); expect(unavailable, "192.168.50.42", "wsprrypi", Decision::interface_discovery_unavailable); expect(unavailable, "127.0.0.1", "localhost", Decision::allowed);
    // A dedicated proxy identity is trusted only from an actual loopback peer.
    assert(guard.evaluate("127.0.0.1", "wsprrypi", std::nullopt, true,
                          {"192.168.50.42"}).allowed());
    assert(guard.evaluate("127.0.0.1", "wsprrypi", std::nullopt, true,
                          {"192.168.51.42"}).decision == Decision::rejected_peer);
    assert(guard.evaluate("127.0.0.1", "wsprrypi", std::nullopt, true,
                          {"bad"}).decision ==
           Decision::invalid_trusted_proxy_identity);
    assert(guard.evaluate("127.0.0.1", "wsprrypi", std::nullopt, true,
                          {"192.168.50.42", "192.168.50.43"}).decision ==
           Decision::invalid_trusted_proxy_identity);
    assert(guard.evaluate("192.168.50.42", "wsprrypi", std::nullopt, true,
                          {"192.168.51.42"}).allowed());
    // Each new guard uses the current snapshot: no restart or retained subnet.
    SupportRequestGuard new_subnet({true, "wsprrypi", {},
        {{"10.20.30.2", "255.255.255.0"}}});
    expect(new_subnet, "192.168.50.42", "wsprrypi", Decision::rejected_peer);
    expect(new_subnet, "10.20.30.42", "wsprrypi", Decision::allowed);
    SupportRequestGuard reordered({true, "wsprrypi", {},
        {{"fd00::10", "ffff:ffff:ffff:ffff::"},
         {"192.168.50.10", "255.255.255.0"}}});
    expect(reordered, "192.168.50.42", "wsprrypi", Decision::allowed);
    // Host identities and strict ports.
    expect(guard, "192.168.50.42", "WSPRRYPI.LOCAL.", Decision::allowed); expect(guard, "192.168.50.42", "192.168.50.10:31415", Decision::allowed); expect(guard, "fd00::42", "[fd00:0:0:0:0:0:0:10]", Decision::allowed); expect(guard, "fd00::42", "[fd00::11]", Decision::rejected_host); expect(guard, "192.168.50.42", "192.168.1.1", Decision::rejected_host);
    expect(guard, "192.168.50.42", "wsprrypi:65535", Decision::allowed); expect(guard, "192.168.50.42", "wsprrypi:00000000000000000000000000000000001", Decision::allowed);
    for (const std::string bad : {"wsprrypi:0", "wsprrypi:65536", "wsprrypi:00000000000000000000000000000065536", "wsprrypi:999999999999999999999999999999999999999999999999", "wsprrypi:-1", "wsprrypi:+1", "wsprrypi:abc", "wsprrypi:12x", "wsprrypi: 1", "wsprrypi:1 ", "wsprrypi:"}) expect(guard, "192.168.50.42", bad, Decision::rejected_host);
    for (const std::string bad : {"http://wsprrypi", "wsprrypi/path", "user@wsprrypi", "[fd00::10", "fd00::10", " wsprrypi", "wsprrypi\t"}) expect(guard, "192.168.50.42", bad, Decision::rejected_host);
    // Origin: explicit ports match; absent Host port accepts the scheme default.
    expect(guard, "192.168.50.42", "wsprrypi", Decision::allowed); expect(guard, "192.168.50.42", "wsprrypi", Decision::allowed, "http://WSPRRYPI."); expect(guard, "192.168.50.42", "wsprrypi", Decision::allowed, "https://wsprrypi"); expect(guard, "192.168.50.42", "wsprrypi:31415", Decision::allowed, "http://wsprrypi:31415");
    expect(guard, "fd00::42", "[fd00::10]:31415", Decision::allowed, "http://[fd00:0:0:0:0:0:0:10]:31415"); expect(guard, "fd00::42", "[fd00::10]:31415", Decision::rejected_origin, "http://[fd00::11]:31415");
    expect(guard, "192.168.50.42", "192.168.50.10", Decision::allowed, "http://[::ffff:192.168.50.10]"); expect(guard, "192.168.50.42", "wsprrypi", Decision::rejected_origin, "http://192.168.50.10");
    for (const std::string bad : {"null", "http://evil.example", "http://localhost", "http://wsprrypi:80", "http://user@wsprrypi", "ftp://wsprrypi", "http://wsprrypi/path", "http://wsprrypi?x", "http://wsprrypi#x", "http://[fd00::10", "http://wsprrypi:12x"}) expect(guard, "192.168.50.42", "wsprrypi:31415", Decision::rejected_origin, bad);
    const auto discovered = SupportRequestGuard::discover_local_networks();
    for (const auto &record : discovered.networks)
        assert(!record.address.empty() && !record.netmask.empty());
    if (argc == 2 && std::string(argv[1]) == "--expect-no-eligible-network") {
        assert(discovered.discovery_succeeded);
        assert(discovered.networks.empty());
    } else {
        assert(argc == 1);
    }
    std::cout << "support_request_guard_test: PASS\n";
}
