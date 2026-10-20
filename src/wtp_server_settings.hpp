#pragma once

#include <cstdint>
#include <string>

struct WtpServerSettings {
    bool enabled = true;
    std::uint16_t port = 31417;
    std::string interface = "auto";
    bool operator==(const WtpServerSettings&) const = default;
};
