/**
 * @file transmitter_runtime_bridge.hpp
 * @brief Narrow scheduler-facing access to the concrete transmitter runtime.
 */

#ifndef TRANSMITTER_RUNTIME_BRIDGE_HPP
#define TRANSMITTER_RUNTIME_BRIDGE_HPP

#include "transmission_backend.hpp"
#include "transmission_request.hpp"
#include "wspr_transmit_types.hpp"

#include <functional>
#include <string>

struct TransmitterRuntimeStatus
{
    wsprrypi::TransmissionMode mode{wsprrypi::TransmissionMode::WSPR};
    std::string cw_message;
    int cw_active_char_index{-1};
};

using TransmitterRuntimeCallback = std::function<void(
    WsprTransmissionCallbackEvent,
    WsprTransmitLogLevel,
    const std::string &,
    double)>;

WsprTransmitState transmitter_state() noexcept;
std::string transmitter_state_string_lower(WsprTransmitState state);
bool transmitter_active_execution_is_tone() noexcept;
TransmitterRuntimeStatus transmitter_runtime_status();
std::string transmitter_reload_defer_debug_state();
void transmitter_clear_execution_state_after_stop() noexcept;
void transmitter_configure_execution(const TransmissionRequest &request);
void transmitter_configure_execution(
    const wsprrypi::TransmissionRequest &request,
    const TransmissionRequest &legacy_request);
wsprrypi::StartupQuiesceResult transmitter_quiesce_for_startup();
void transmitter_set_thread_scheduling(int policy, int priority);
void transmitter_set_callbacks(TransmitterRuntimeCallback callback);
std::string transmitter_format_frequency_mhz(double frequency_hz);
void transmitter_start_async();
void transmitter_stop_and_join();
bool transmitter_output_inactive_confirmed() noexcept;
void transmitter_shutdown_for_process_exit();
void transmitter_clear_soft_off() noexcept;
void transmitter_poll_events();

#endif // TRANSMITTER_RUNTIME_BRIDGE_HPP
