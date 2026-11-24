// SPDX-License-Identifier: MIT
// Zero-output implementation of the parent runtime boundary. No transmitter
// component instance, backend factory, callback worker, or output is created.
#include "transmitter_runtime_bridge.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
[[noreturn]] void unavailable() { throw std::logic_error("No local transmitter in the controller build"); }
}
WsprTransmitState transmitter_state() noexcept { return WsprTransmitState::DISABLED; }
std::string transmitter_state_string_lower(WsprTransmitState state) {
    std::string name = wsprTransmitStateToString(state);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return name;
}
bool transmitter_active_execution_is_tone() noexcept { return false; }
TransmitterRuntimeStatus transmitter_runtime_status() { unavailable(); }
std::string transmitter_reload_defer_debug_state() { return "local output unavailable"; }
void transmitter_clear_execution_state_after_stop() noexcept {}
void transmitter_configure_execution(const TransmissionRequest &) { unavailable(); }
void transmitter_configure_execution(const wsprrypi::TransmissionRequest &, const TransmissionRequest &,
    std::function<bool()>, TransmitterRuntimeCallback) { unavailable(); }
TransmitterRuntimeCallback transmitter_callbacks() { return {}; }
wsprrypi::StartupQuiesceResult transmitter_quiesce_for_startup() { unavailable(); }
void transmitter_set_thread_scheduling(int, int) { unavailable(); }
void transmitter_set_callbacks(TransmitterRuntimeCallback) { unavailable(); }
std::string transmitter_format_frequency_mhz(double) { unavailable(); }
void transmitter_start_async() { unavailable(); }
wsprrypi::CleanupResult transmitter_stop_and_join() { return {true, {}}; }
bool transmitter_output_inactive_confirmed() noexcept { return true; }
void transmitter_shutdown_for_process_exit() {}
void transmitter_clear_soft_off() noexcept {}
void transmitter_poll_events() {}
