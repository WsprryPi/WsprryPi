#include "websocket_upgrade_guard.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>

namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool token_list_contains(std::string value, std::string_view expected) {
    std::istringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (lower_ascii(trim(item)) == expected) return true;
    }
    return false;
}

} // namespace

WebSocketUpgradeGuardResult evaluate_websocket_upgrade(
    const std::string &request,
    const std::string &peer_address,
    const SupportRequestGuardSnapshot &snapshot,
    PrivilegedNetworkMode mode) {
    if (request.size() > 4096 || !request.ends_with("\r\n\r\n")) {
        return {};
    }

    const auto request_line_end = request.find("\r\n");
    if (request_line_end == std::string::npos) return {};
    std::istringstream request_line(request.substr(0, request_line_end));
    std::string method;
    std::string target;
    std::string version;
    std::string extra;
    if (!(request_line >> method >> target >> version) || request_line >> extra ||
        method != "GET" || target.empty() || target.front() != '/' ||
        version != "HTTP/1.1") {
        return {};
    }

    std::map<std::string, std::string> headers;
    std::vector<std::string> trusted_proxy_identities;
    std::size_t position = request_line_end + 2;
    while (position < request.size() - 2) {
        const auto end = request.find("\r\n", position);
        if (end == std::string::npos || end == position) break;
        const std::string line = request.substr(position, end - position);
        if (line.front() == ' ' || line.front() == '\t') return {};
        const auto colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return {};
        const std::string raw_name = line.substr(0, colon);
        if (raw_name != trim(raw_name) ||
            !std::all_of(raw_name.begin(), raw_name.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '-';
            })) return {};
        const std::string name = lower_ascii(raw_name);
        const std::string value = trim(line.substr(colon + 1));
        if (name.empty() || value.empty()) return {};
        if (name == lower_ascii(
                std::string(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER))) {
            trusted_proxy_identities.push_back(value);
            if (headers.contains(name)) {
                position = end + 2;
                continue;
            }
        } else if (headers.contains(name)) {
            return {};
        }
        headers.emplace(name, value);
        position = end + 2;
    }

    const auto host = headers.find("host");
    const auto upgrade = headers.find("upgrade");
    const auto connection = headers.find("connection");
    const auto version_header = headers.find("sec-websocket-version");
    const auto key = headers.find("sec-websocket-key");
    if (host == headers.end() || upgrade == headers.end() ||
        connection == headers.end() || version_header == headers.end() ||
        key == headers.end() || lower_ascii(upgrade->second) != "websocket" ||
        !token_list_contains(connection->second, "upgrade") ||
        version_header->second != "13") {
        return {};
    }

    const auto origin = headers.find("origin");
    const std::optional<std::string> origin_value =
        origin == headers.end() ? std::nullopt
                                : std::optional<std::string>(origin->second);
    const auto guard = SupportRequestGuard(snapshot).evaluate(
            peer_address, host->second, origin_value,
            mode != PrivilegedNetworkMode::insecure_disabled,
            trusted_proxy_identities);
    if (!guard.allowed()) {
        return {WebSocketUpgradeGuardDecision::rejected, {}, guard.decision};
    }
    return {WebSocketUpgradeGuardDecision::allowed, key->second,
            SupportRequestGuardDecision::allowed};
}
