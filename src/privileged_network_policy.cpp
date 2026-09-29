#include "privileged_network_policy.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cctype>

namespace {

bool has_valid_path_syntax(std::string_view path) {
    return !path.empty() && path.front() == '/' &&
           path.find_first_of("?#\r\n") == std::string_view::npos;
}

bool is_support_bundle_path(std::string_view path) {
    constexpr std::string_view root = "/api/support-bundles";
    return path == root ||
           (path.size() > root.size() && path.substr(0, root.size()) == root &&
            path[root.size()] == '/');
}

bool is_usable_address_and_mask(const std::string &address, const std::string &netmask) {
    const auto is_contiguous_mask = [](const unsigned char *bytes, std::size_t size) {
        bool saw_zero = false;
        bool saw_one = false;
        for (std::size_t index = 0; index < size; ++index) {
            for (int bit = 7; bit >= 0; --bit) {
                const bool one = (bytes[index] & (1U << bit)) != 0;
                saw_one = saw_one || one;
                if (!one) saw_zero = true;
                if (one && saw_zero) return false;
            }
        }
        return saw_one;
    };

    in_addr address4{};
    in_addr mask4{};
    if (inet_pton(AF_INET, address.c_str(), &address4) == 1 &&
        inet_pton(AF_INET, netmask.c_str(), &mask4) == 1) {
        const auto host_address = ntohl(address4.s_addr);
        return host_address != INADDR_ANY &&
               (host_address & 0xf0000000U) != 0xe0000000U &&
               (host_address & 0xff000000U) != 0x7f000000U &&
               is_contiguous_mask(reinterpret_cast<const unsigned char *>(&mask4),
                                  sizeof(mask4));
    }

    in6_addr address6{};
    in6_addr mask6{};
    if (inet_pton(AF_INET6, address.c_str(), &address6) != 1 ||
        inet_pton(AF_INET6, netmask.c_str(), &mask6) != 1) {
        return false;
    }

    if (IN6_IS_ADDR_UNSPECIFIED(&address6) || IN6_IS_ADDR_MULTICAST(&address6) ||
        IN6_IS_ADDR_LOOPBACK(&address6)) {
        return false;
    }
    return is_contiguous_mask(mask6.s6_addr, sizeof(mask6.s6_addr));
}

std::string lower_ascii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

} // namespace

PrivilegedInterfaceKind classify_privileged_interface_link(
    const PrivilegedInterfaceLinkEvidence &evidence) noexcept {
    // Linux ARPHRD_ETHER is 1 for both ordinary Ethernet and Wi-Fi links.
    // Requiring a physical device prevents renamed software links from
    // becoming eligible merely because their names resemble Ethernet.
    if (!evidence.has_physical_device || evidence.link_type != 1U)
        return PrivilegedInterfaceKind::other;
    return evidence.wireless ? PrivilegedInterfaceKind::wifi
                             : PrivilegedInterfaceKind::ethernet;
}

PrivilegedOperationClass classify_privileged_http_operation(
    std::string_view method, std::string_view path) {
    if (!has_valid_path_syntax(path) || method.empty() ||
        std::any_of(method.begin(), method.end(), [](unsigned char ch) {
            return ch < 'A' || ch > 'Z';
        })) {
        return PrivilegedOperationClass::reject;
    }

    if (path.starts_with("/api/v1/")) {
        if (method == "GET" && (path == "/api/v1/status" || path == "/api/v1/jobs" || path == "/api/v1/capabilities"))
            return PrivilegedOperationClass::read_only;
        // Remote management reads consume the sole TLS slot and host config
        // reveals local administrator-managed references: protect these too.
        if ((method == "GET" || method == "PUT") &&
            (path == "/api/v1/config" || path == "/api/v1/schedules" || path == "/api/v1/network" || path == "/api/v1/host/config"))
            return PrivilegedOperationClass::protected_operation;
        if ((method == "GET" && (path == "/api/v1/host/devices" ||
                                 path == "/api/v1/host/discovery" ||
                                 path == "/api/v1/host/fleet" ||
                                 path == "/api/v1/host/wtp-endpoint")) ||
            (method == "POST" && (path == "/api/v1/host/devices" ||
                                  path == "/api/v1/host/devices/use" ||
                                  path == "/api/v1/host/fleet" ||
                                  path == "/api/v1/host/wtp-endpoint/enable" ||
                                  path == "/api/v1/host/wtp-endpoint/recover" ||
                                  path == "/api/v1/host/discovery/identify")))
            return PrivilegedOperationClass::protected_operation;
        if (method == "POST" && (path == "/api/v1/jobs" ||
            (path.starts_with("/api/v1/jobs/") && path.ends_with("/abort") && path.size() == 51)))
            return PrivilegedOperationClass::protected_operation;
        return PrivilegedOperationClass::reject;
    }
    if (is_support_bundle_path(path)) {
        return PrivilegedOperationClass::protected_operation;
    }
    if ((method == "PUT" || method == "PATCH") && path == "/config") {
        return PrivilegedOperationClass::protected_operation;
    }
    if (method == "POST" &&
        (path == "/config/repair" || path == "/control/stop" ||
         path == "/api/network-safety" ||
         path == "/api/rp1-gpclk-route" || path == "/api/wtp/recover")) {
        return PrivilegedOperationClass::protected_operation;
    }
    if (method == "GET" &&
        (path == "/config" || path == "/config/si5351-addresses" ||
         path == "/version" || path == "/status" ||
         path == "/telemetry" || path == "/api/support-intake")) {
        return PrivilegedOperationClass::read_only;
    }
    if (method == "GET" && path == "/api/network-safety") {
        return PrivilegedOperationClass::read_only;
    }
    if (method == "GET" && (path == "/api/rp1-gpclk-route" || path == "/api/wtp")) {
        return PrivilegedOperationClass::read_only;
    }
    return PrivilegedOperationClass::reject;
}

PrivilegedOperationClass classify_privileged_websocket_command(std::string_view command) {
    if (command == "shutdown" || command == "reboot" || command == "stop" ||
        command == "tone_start" || command == "tone_end" || command == "bounded_tone") {
        return PrivilegedOperationClass::protected_operation;
    }
    if (command == "get_tx_state" || command == "wspr_band_catalog" || command == "echo") {
        return PrivilegedOperationClass::read_only;
    }
    return PrivilegedOperationClass::reject;
}

PrivilegedOperationClass classify_privileged_websocket_protocol_operation(
    std::string_view operation) {
    if (operation == "ping" || operation == "pong" || operation == "server_broadcast") {
        return PrivilegedOperationClass::read_only;
    }
    return PrivilegedOperationClass::reject;
}

PrivilegedNetworkModeParseResult parse_privileged_network_mode(
    const std::optional<std::string> &value) {
    if (!value.has_value()) {
        return {PrivilegedNetworkMode::enforced, false, true};
    }
    if (*value == "enforced") {
        return {PrivilegedNetworkMode::enforced, true, false};
    }
    if (*value == "insecure-disabled") {
        return {PrivilegedNetworkMode::insecure_disabled, true, false};
    }
    return {PrivilegedNetworkMode::enforced, false, false};
}

bool is_eligible_privileged_interface(const PrivilegedInterfaceCandidate &candidate) {
    const bool physical = candidate.kind == PrivilegedInterfaceKind::ethernet ||
                          candidate.kind == PrivilegedInterfaceKind::wifi;
    return candidate.active && physical && !candidate.loopback &&
           !candidate.point_to_point && !candidate.tunnel && !candidate.vpn &&
           !candidate.container && !candidate.software_bridge &&
           is_usable_address_and_mask(candidate.address, candidate.netmask);
}

bool has_eligible_privileged_subnet(
    const std::vector<PrivilegedInterfaceCandidate> &candidates) {
    return std::any_of(candidates.begin(), candidates.end(),
                       is_eligible_privileged_interface);
}

bool is_forwarded_client_identity_header(std::string_view name) {
    const std::string normalized = lower_ascii(name);
    constexpr std::array<std::string_view, 6> distrusted = {
        "forwarded", "x-forwarded-for", "x-real-ip", "x-client-ip",
        "true-client-ip", "cf-connecting-ip"};
    return std::find(distrusted.begin(), distrusted.end(), normalized) != distrusted.end();
}
