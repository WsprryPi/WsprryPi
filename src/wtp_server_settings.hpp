#pragma once

#include <cstdint>
#include <string>

struct WtpServerSettings {
    bool enabled = false;
    std::uint16_t port = 31417;
    std::string listen_interface = "auto";
    bool operator==(const WtpServerSettings&) const = default;
};
