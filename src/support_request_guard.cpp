#include "support_request_guard.hpp"
#include "privileged_network_policy.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace {
struct ParsedAddress { int family = AF_UNSPEC; std::array<unsigned char, 16> bytes{}; };
struct ParsedHost { std::string identity; std::optional<unsigned short> port; };

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    while (!value.empty() && value.back() == '.') value.pop_back();
    return value;
}

std::optional<unsigned short> strict_port(const std::string &value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
    unsigned short number = 0;
    for (const unsigned char c : value) {
        const unsigned short digit = static_cast<unsigned short>(c - '0');
        if (number > 6553 || (number == 6553 && digit > 5)) return std::nullopt;
        number = static_cast<unsigned short>(number * 10 + digit);
    }
    return number == 0 ? std::nullopt : std::optional<unsigned short>(static_cast<unsigned short>(number));
}

std::optional<ParsedAddress> address(const std::string &text) {
    ParsedAddress result;
    if (text.find('\0') != std::string::npos) return std::nullopt;
    // Apache's connection peer can include the local interface zone. Parse it
    // only for link-local IPv6; it must never disguise IPv4 or loopback.
    const auto percent = text.find('%');
    if (percent != std::string::npos) {
        const auto literal = text.substr(0, percent);
        const auto zone = text.substr(percent + 1);
        if (zone.empty() || zone.size() >= IF_NAMESIZE ||
            !std::all_of(zone.begin(), zone.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            }) ||
            inet_pton(AF_INET6, literal.c_str(), result.bytes.data()) != 1 ||
            result.bytes[0] != 0xfe || (result.bytes[1] & 0xc0) != 0x80)
            return std::nullopt;
        result.family = AF_INET6;
        return result;
    }
    if (inet_pton(AF_INET, text.c_str(), result.bytes.data()) == 1) { result.family = AF_INET; return result; }
    if (inet_pton(AF_INET6, text.c_str(), result.bytes.data()) == 1) {
        // Normalize IPv4-mapped IPv6 peers and host literals.
        static constexpr unsigned char prefix[] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if (std::memcmp(result.bytes.data(), prefix, sizeof(prefix)) == 0) {
            std::memmove(result.bytes.data(), result.bytes.data() + 12, 4); result.family = AF_INET;
        } else result.family = AF_INET6;
        return result;
    }
    return std::nullopt;
}

bool loopback(const ParsedAddress &value) {
    if (value.family == AF_INET) return value.bytes[0] == 127;
    static constexpr unsigned char loop[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    return value.family == AF_INET6 && std::memcmp(value.bytes.data(), loop, sizeof(loop)) == 0;
}
bool prohibited(const ParsedAddress &value) {
    if (value.family == AF_INET) return value.bytes[0] == 0 || (value.bytes[0] & 0xf0) == 0xe0;
    static constexpr unsigned char zero[16] = {}; return value.family != AF_INET6 ||
        std::memcmp(value.bytes.data(), zero, 16) == 0 || value.bytes[0] == 0xff;
}
bool same_subnet(const ParsedAddress &peer, const ParsedAddress &network, const ParsedAddress &mask) {
    if (peer.family != network.family || network.family != mask.family) return false;
    const size_t length = peer.family == AF_INET ? 4 : 16;
    for (size_t index = 0; index < length; ++index)
        if ((peer.bytes[index] & mask.bytes[index]) != (network.bytes[index] & mask.bytes[index])) return false;
    return true;
}

std::optional<ParsedHost> host(const std::string &source) {
    if (source.empty() || source.find_first_of("/@?#") != std::string::npos) return std::nullopt;
    std::string name; std::optional<unsigned short> port;
    if (source.front() == '[') {
        const auto close = source.find(']'); if (close == std::string::npos) return std::nullopt;
        name = source.substr(1, close - 1); std::array<unsigned char, 16> literal{}; if (inet_pton(AF_INET6, name.c_str(), literal.data()) != 1) return std::nullopt;
        if (close + 1 < source.size()) { if (source[close + 1] != ':') return std::nullopt; port = strict_port(source.substr(close + 2)); if (!port) return std::nullopt; }
    } else {
        if (source.find(':') != source.rfind(':')) return std::nullopt;
        const auto colon = source.rfind(':'); name = colon == std::string::npos ? source : source.substr(0, colon);
        if (colon != std::string::npos) { port = strict_port(source.substr(colon + 1)); if (!port) return std::nullopt; }
    }
    if (name.empty() || name.find_first_of(" []\\") != std::string::npos) return std::nullopt;
    return ParsedHost{lower(name), port};
}

bool approved_host(const ParsedHost &candidate, const SupportRequestGuardSnapshot &snapshot) {
    const auto parsed = address(candidate.identity);
    if (parsed && loopback(*parsed)) return true;
    std::set<std::string> names{"localhost"};
    if (!snapshot.hostname.empty()) { const auto name = lower(snapshot.hostname); if (!name.empty()) { names.insert(name); if (name.ends_with(".local")) names.insert(name.substr(0, name.size() - 6)); else names.insert(name + ".local"); } }
    for (const auto &name : snapshot.configured_hostnames) { const auto normalized = lower(name); if (!normalized.empty()) names.insert(normalized); }
    if (names.contains(candidate.identity)) return true;
    if (parsed) {
        for (const auto &network : snapshot.networks) {
            const auto assigned = address(network.address);
            const size_t length = parsed->family == AF_INET ? 4 : 16;
            if (assigned && parsed->family == assigned->family && std::memcmp(parsed->bytes.data(), assigned->bytes.data(), length) == 0) return true;
        }
    }
    return false;
}

bool same_host_identity(const ParsedHost &left, const ParsedHost &right) {
    const auto left_address = address(left.identity);
    const auto right_address = address(right.identity);
    if (left_address || right_address) {
        if (!left_address || !right_address || left_address->family != right_address->family) return false;
        const size_t length = left_address->family == AF_INET ? 4 : 16;
        return std::memcmp(left_address->bytes.data(), right_address->bytes.data(), length) == 0;
    }
    return left.identity == right.identity;
}

std::optional<ParsedHost> origin_host(const std::string &origin, bool &https) {
    if (origin == "null") return std::nullopt;
    const auto scheme_end = origin.find("://"); if (scheme_end == std::string::npos) return std::nullopt;
    const auto scheme = lower(origin.substr(0, scheme_end)); if (scheme != "http" && scheme != "https") return std::nullopt;
    https = scheme == "https"; const auto authority = origin.substr(scheme_end + 3);
    if (authority.empty() || authority.find_first_of("/?#") != std::string::npos) return std::nullopt;
    return host(authority);
}

bool excluded_interface_name(const std::string &name) {
    static constexpr std::array<std::string_view, 14> prefixes = {
        "lo", "tun", "tap", "utun", "wg", "tailscale", "zt", "docker",
        "veth", "br", "virbr", "cni", "podman", "awdl"};
    return std::any_of(prefixes.begin(), prefixes.end(),
                       [&](std::string_view prefix) { return name.starts_with(prefix); });
}

std::optional<PrivilegedInterfaceKind> discovered_interface_kind(
    const std::string &name) {
#if defined(__linux__)
    const std::filesystem::path root =
        std::filesystem::path("/sys/class/net") / name;
    std::error_code error;
    const bool interface_exists = std::filesystem::exists(root, error);
    if (error || !interface_exists) return std::nullopt;
    error.clear();
    const bool has_device = std::filesystem::exists(root / "device", error);
    if (error) return std::nullopt;
    error.clear();
    const bool wireless = std::filesystem::exists(root / "wireless", error);
    if (error) return std::nullopt;
    std::ifstream type_file(root / "type");
    unsigned int link_type = 0;
    if (!(type_file >> link_type)) return std::nullopt;
    return classify_privileged_interface_link(
        {has_device, wireless, link_type});
#else
    // Production is Linux. This is a conservative portability path for
    // development builds, which never qualify target networking.
    const bool wireless = name.starts_with("wlan") || name.starts_with("wl");
    const bool ethernet = name.starts_with("eth") || name.starts_with("en");
    return wireless ? PrivilegedInterfaceKind::wifi
                    : ethernet ? PrivilegedInterfaceKind::ethernet
                               : PrivilegedInterfaceKind::other;
#endif
}
}

SupportRequestGuard::SupportRequestGuard(SupportRequestGuardSnapshot snapshot) : snapshot_(std::move(snapshot)) {}

SupportRequestGuardResult SupportRequestGuard::evaluate(
    const std::string &peer_address,
    const std::string &host_header,
    const std::optional<std::string> &origin_header,
    bool enforce_peer,
    const std::vector<std::string> &trusted_proxy_identities) const {
    const auto peer = address(peer_address);
    if (!peer || prohibited(*peer)) return {SupportRequestGuardDecision::rejected_peer};
    ParsedAddress client = *peer;
    if (loopback(*peer) && !trusted_proxy_identities.empty()) {
        if (trusted_proxy_identities.size() != 1)
            return {SupportRequestGuardDecision::invalid_trusted_proxy_identity};
        const auto proxied = address(trusted_proxy_identities.front());
        if (!proxied || prohibited(*proxied))
            return {SupportRequestGuardDecision::invalid_trusted_proxy_identity};
        client = *proxied;
    }
    if (enforce_peer && !loopback(client)) {
        if (!snapshot_.discovery_succeeded) return {SupportRequestGuardDecision::interface_discovery_unavailable};
        if (snapshot_.networks.empty()) return {SupportRequestGuardDecision::no_eligible_network};
        bool matched = false;
        for (const auto &item : snapshot_.networks) { const auto network = address(item.address); const auto mask = address(item.netmask); if (network && mask && same_subnet(client, *network, *mask)) { matched = true; break; } }
        if (!matched) return {SupportRequestGuardDecision::rejected_peer};
    }
    const auto request_host = host(host_header); if (!request_host || !approved_host(*request_host, snapshot_)) return {SupportRequestGuardDecision::rejected_host};
    if (!origin_header) return {SupportRequestGuardDecision::allowed};
    bool https = false; const auto origin = origin_host(*origin_header, https);
    if (!origin || !approved_host(*origin, snapshot_) || !same_host_identity(*origin, *request_host)) return {SupportRequestGuardDecision::rejected_origin};
    // Explicit ports must match. With no Host port, permit the scheme default (80 or 443).
    const auto expected = request_host->port.value_or(https ? 443 : 80);
    if (origin->port.value_or(https ? 443 : 80) != expected) return {SupportRequestGuardDecision::rejected_origin};
    return {SupportRequestGuardDecision::allowed};
}

SupportRequestGuardSnapshot SupportRequestGuard::discover_local_networks() {
    SupportRequestGuardSnapshot result; char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) result.hostname = hostname;
    ifaddrs *interfaces = nullptr; if (getifaddrs(&interfaces) != 0) return result;
    result.discovery_succeeded = true;
    for (auto *item = interfaces; item != nullptr; item = item->ifa_next) {
        if (!item->ifa_name || !item->ifa_addr || !item->ifa_netmask ||
            item->ifa_addr->sa_family != item->ifa_netmask->sa_family) continue;
        const std::string name = item->ifa_name;
        if (excluded_interface_name(name)) continue;
        char buffer[INET6_ADDRSTRLEN]{};
        std::string address_text;
        std::string mask_text;
        if (item->ifa_addr->sa_family == AF_INET) {
            const auto *a = reinterpret_cast<sockaddr_in *>(item->ifa_addr); const auto *m = reinterpret_cast<sockaddr_in *>(item->ifa_netmask);
            if (inet_ntop(AF_INET, &a->sin_addr, buffer, sizeof(buffer))) { address_text = buffer; if (inet_ntop(AF_INET, &m->sin_addr, buffer, sizeof(buffer))) mask_text = buffer; }
        } else if (item->ifa_addr->sa_family == AF_INET6) {
            const auto *a = reinterpret_cast<sockaddr_in6 *>(item->ifa_addr); const auto *m = reinterpret_cast<sockaddr_in6 *>(item->ifa_netmask);
            if (inet_ntop(AF_INET6, &a->sin6_addr, buffer, sizeof(buffer))) { address_text = buffer; if (inet_ntop(AF_INET6, &m->sin6_addr, buffer, sizeof(buffer))) mask_text = buffer; }
        }
        const auto interface_kind = discovered_interface_kind(name);
        if (!interface_kind) {
            result.discovery_succeeded = false;
            continue;
        }
        const PrivilegedInterfaceCandidate candidate{
            (item->ifa_flags & IFF_UP) != 0,
            *interface_kind,
            (item->ifa_flags & IFF_LOOPBACK) != 0,
            (item->ifa_flags & IFF_POINTOPOINT) != 0,
            false, false, false, false,
            address_text, mask_text};
        if (is_eligible_privileged_interface(candidate))
            result.networks.push_back({address_text, mask_text});
    }
    freeifaddrs(interfaces);
    if (!result.discovery_succeeded) result.networks.clear();
    return result;
}
