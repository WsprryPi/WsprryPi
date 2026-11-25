// SPDX-License-Identifier: MIT
#pragma once
#include "authority.hpp"
#include <functional>
namespace wsprrypi {
struct PiManagementObservation {
    std::string device_id;
    WtpPiAuthority::Snapshot authority;
    WtpPiAuthority::ObservedOutput output;
    std::function<bool()> reserve_configuration;
    std::function<void()> release_configuration;
    std::function<bool(const WtpPiAuthority::ObservedOutput &)> reserve_stop{};
    std::function<void()> release_stop{};
    std::function<void()> cancel_reserved_stop{};
    std::optional<std::string> destination_id{};
};
} // namespace wsprrypi
