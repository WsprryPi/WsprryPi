// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace wsprrypi {
struct PpsInputBinding {
    std::string identity;
    std::string device_name;
};
// Resolve an already configured kernel PPS input by BCM GPIO, never by a
// guessed pps index. Multiple matches and unsupported pins fail closed.
PpsInputBinding resolve_pps_input(unsigned gpio, const std::string& root = "/sys/class/pps");
}
