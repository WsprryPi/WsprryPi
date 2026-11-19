// SPDX-License-Identifier: MIT
#pragma once
#include "config_types.hpp"
#include "json.hpp"
#include <string>
#include <algorithm>
#include <cctype>

namespace wsprrypi {
// Stored configuration recognition only. This helper neither edits configuration
// nor attests to controller retirement, output cleanup, or migration readiness.
// Active legacy state is refused before runtime selection or writes.
inline std::string wtp_retirement_configuration_refusal(const ArgParserConfig &configuration) {
    const char *reason = (configuration.stored_wtp_backend || configuration.transmit_backend == TransmitBackendKind::WTP)
        ? "the selected WTP transmitter backend"
        : configuration.wtp_server.enabled || configuration.wtp_server_port_override
            ? "the enabled inbound WTP listener" : nullptr;
    if (!reason) return {};
    return std::string("Migration required: retire ") + reason +
        " with WTP-capable software before replacing it. Disable legacy recurrence, "
        "confirm inactive and unowned output, and complete controller retirement. "
        "Then save and read back the native schedule disabled before explicitly enabling it.";
}
// Read the stored source before defaults or transient backend overrides. No
// value is rewritten, and inactive legacy credential/endpoint fields are inert.
inline bool wtp_retirement_source_active(const nlohmann::json &source) {
    if (!source.is_object()) return false;
    if (source.contains("Operation") && source["Operation"].is_object()) {
        const auto &operation=source["Operation"];
        if (operation.contains("Transmit Backend") && operation["Transmit Backend"].is_string()) {
            auto backend=operation["Transmit Backend"].get<std::string>();
            backend.erase(std::remove_if(backend.begin(),backend.end(),[](unsigned char c){return std::isspace(c);}),backend.end());
            std::transform(backend.begin(),backend.end(),backend.begin(),[](unsigned char c){return std::tolower(c);});
            if (backend=="wtp") return true;
        }
    }
    return source.contains("WTP Server") && source["WTP Server"].is_object() &&
        source["WTP Server"].value("Enabled",nlohmann::json(nullptr))==true;
}
} // namespace wsprrypi
