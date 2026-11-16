// SPDX-License-Identifier: MIT
#pragma once
#include "managed_output_observation.hpp"
#include <functional>
#include <optional>
namespace wsprrypi {
// Read-only typed projection of the independently selected outgoing WTP
// application. Obtaining it never connects, claims, loads, arms, or saves.
struct WtpManagedOutput {
    ManagedOutputObservation output;
    std::optional<std::string> destination_id;
    bool unknown{};
    bool ready{};
    bool stop_reserved{};
    bool cancellable{};
    std::function<bool()> reserve_configuration;
    std::function<void()> release_configuration;
    std::function<bool(const ManagedOutputObservation &)> reserve_stop;
    std::function<void()> release_stop;
};
std::optional<WtpManagedOutput> wtp_runtime_managed_output();
} // namespace wsprrypi
