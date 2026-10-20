#include "privileged_network_runtime.hpp"

#include <atomic>

namespace {
std::atomic<PrivilegedNetworkMode> configured_mode{PrivilegedNetworkMode::enforced};
std::atomic<PrivilegedNetworkMode> active_mode{PrivilegedNetworkMode::enforced};
std::atomic<bool> setting_valid{false};
std::atomic<bool> setting_missing{true};
std::atomic<bool> active_state_known{true};
}

void initialize_privileged_network_runtime(
    const std::optional<std::string> &configured_value) {
    const auto parsed = parse_privileged_network_mode(configured_value);
    configured_mode.store(parsed.mode, std::memory_order_release);
    active_mode.store(parsed.mode, std::memory_order_release);
    setting_valid.store(parsed.valid, std::memory_order_release);
    setting_missing.store(parsed.missing, std::memory_order_release);
    active_state_known.store(true, std::memory_order_release);
}

void set_active_privileged_network_mode(PrivilegedNetworkMode mode) noexcept {
    active_mode.store(mode, std::memory_order_release);
    active_state_known.store(true, std::memory_order_release);
}

void set_privileged_network_runtime_mode(PrivilegedNetworkMode mode) noexcept {
    configured_mode.store(mode, std::memory_order_release);
    active_mode.store(mode, std::memory_order_release);
    setting_valid.store(true, std::memory_order_release);
    setting_missing.store(false, std::memory_order_release);
    active_state_known.store(true, std::memory_order_release);
}

void set_privileged_network_runtime_unknown() noexcept {
    active_state_known.store(false, std::memory_order_release);
}

PrivilegedNetworkRuntimeState privileged_network_runtime_state() noexcept {
    return {
        configured_mode.load(std::memory_order_acquire),
        active_mode.load(std::memory_order_acquire),
        setting_valid.load(std::memory_order_acquire),
        setting_missing.load(std::memory_order_acquire),
        active_state_known.load(std::memory_order_acquire)};
}

PrivilegedNetworkMode active_privileged_network_mode() noexcept {
    return active_mode.load(std::memory_order_acquire);
}

std::string privileged_network_runtime_status_text() {
    const auto state = privileged_network_runtime_state();
    if (!state.active_known) return "NETWORK SAFETY STATE UNKNOWN";
    return state.active == PrivilegedNetworkMode::insecure_disabled
        ? "NETWORK SAFETY OFF" : "NETWORK SAFETY ENFORCED";
}
