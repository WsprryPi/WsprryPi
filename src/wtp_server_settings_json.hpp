#pragma once

#include "json.hpp"
#include "wtp_server_settings.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

inline nlohmann::json wtp_server_settings_json(const WtpServerSettings& settings) {
    return {{"Enabled", settings.enabled},
            {"Port", settings.port},
            {"Interface", settings.interface}};
}

inline WtpServerSettings parse_wtp_server_settings(const nlohmann::json& value) {
    if (!value.is_object())
        throw std::runtime_error("WTP Server settings must be an object.");
    const auto allowed = wtp_server_settings_json(WtpServerSettings{});
    for (const auto& item : value.items())
        if (!allowed.contains(item.key()))
            throw std::runtime_error("Unknown WTP Server setting: " + item.key());
    WtpServerSettings settings;
    if (value.contains("Enabled")) {
        if (!value.at("Enabled").is_boolean())
            throw std::runtime_error("WTP Server.Enabled must be boolean.");
        settings.enabled = value.at("Enabled").get<bool>();
    }
    if (value.contains("Port")) {
        const auto& port = value.at("Port");
        if ((!port.is_number_integer() && !port.is_number_unsigned()) ||
            (port.is_number_integer() && !port.is_number_unsigned() &&
             port.get<std::int64_t>() < 1) ||
            port.get<std::uint64_t>() < 1 || port.get<std::uint64_t>() > 65535)
            throw std::runtime_error("WTP Server.Port must be 1 through 65535.");
        settings.port = static_cast<std::uint16_t>(port.get<std::uint64_t>());
    }
    if (value.contains("Interface")) {
        if (!value.at("Interface").is_string())
            throw std::runtime_error("WTP Server.Interface must be a string.");
        settings.interface = value.at("Interface").get<std::string>();
    }
    if (settings.interface != "auto") {
        if (settings.interface.empty() || settings.interface.size() > 15)
            throw std::runtime_error("WTP Server.Interface must name a network interface.");
        for (const char c : settings.interface)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
                throw std::runtime_error("WTP Server.Interface has an invalid character.");
    }
    return settings;
}
