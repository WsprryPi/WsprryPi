// SPDX-License-Identifier: MIT
#pragma once
#include "sqlite_state_store.hpp"
#include "json.hpp"

namespace wsprrypi {
enum class SavedNetworkSafety { Enforced, InsecureDisabled };
struct PrivateConfiguration {
    bool debug_logging{};
    SavedNetworkSafety network_safety{SavedNetworkSafety::Enforced};
    bool allow_unqualified_frequency{}, allow_non_amateur_frequency{};
    bool startup_loop_tx{};
    int startup_tx_iterations{};
    bool operator==(const PrivateConfiguration &) const = default;
};
struct PersistentConfiguration {
    StateRole role;
    nlohmann::json configuration;
    PrivateConfiguration private_settings;
    bool operator==(const PersistentConfiguration &) const = default;
};

// Pure storage codec, not a runtime, migration, admission or hardware validator.
// The owner supplies its validated saved values and separate private policy.
// Native fields use the current closed control definitions; private settings
// have their own closed storage shape. No runtime counters/platform discovery.
std::string encode_persistent_configuration(const nlohmann::json &internal,
    const PrivateConfiguration &, StateRole);
PersistentConfiguration decode_persistent_configuration(const std::string &, StateRole);
PersistentConfiguration configuration_from_snapshot(const StateSnapshot &, StateRole);
} // namespace wsprrypi
