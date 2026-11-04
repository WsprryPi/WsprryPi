#pragma once
#include <cstddef>
#include <string>
namespace usb_adapter_identity {
inline bool hex(const std::string& value, std::size_t size) {
    return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
inline std::string encode(const std::string& value) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
inline bool valid(const std::string& identity) {
    if (identity.empty()) return true; // Explicit legacy numeric-bus selection.
    if (identity.size() > 2048 || identity.rfind("usb:", 0) != 0 ||
        identity.size() < 21 || identity[8] != ':' || identity[13] != ':' ||
        !hex(identity.substr(4, 4), 4) || !hex(identity.substr(9, 4), 4)) return false;
    const auto kind_end = identity.find(':', 14);
    if (kind_end == std::string::npos) return false;
    const auto kind = identity.substr(14, kind_end - 14);
    const auto token = identity.substr(kind_end + 1);
    return (kind == "serial" || kind == "path") && !token.empty() &&
        token.size() % 2 == 0 && hex(token, token.size());
}
}
