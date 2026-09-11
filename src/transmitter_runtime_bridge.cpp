/**
 * @file transmitter_runtime_bridge.cpp
 * @brief Adapts scheduler operations to the concrete transmitter runtime.
 */

#include "transmitter_runtime_bridge.hpp"

#include "wspr_transmit.hpp"
#include "wtp_runtime_bridge.hpp"
#include "logging.hpp"
#include <mutex>
#include <stdexcept>
namespace { std::mutex callback_mutex; TransmitterRuntimeCallback runtime_callback; }

#include <utility>

WsprTransmitState transmitter_state() noexcept
{
    return wtp_runtime_selected() ? wtp_runtime_state() : wsprTransmitter.getState();
}

std::string transmitter_state_string_lower(WsprTransmitState state)
{
    return wsprTransmitter.stateToStringLower(state);
}

bool transmitter_active_execution_is_tone() noexcept
{
    return wtp_runtime_selected() ? wtp_runtime_mode() == wsprrypi::TransmissionMode::TONE : wsprTransmitter.activeExecutionIsTone();
}

TransmitterRuntimeStatus transmitter_runtime_status()
{
    if (wtp_runtime_selected()) return {wtp_runtime_mode(), {}, -1};
    const auto status = wsprTransmitter.runtimeExecutionStatusSnapshot();
    return {status.mode, status.cw_message, status.cw_active_char_index};
}

std::string transmitter_reload_defer_debug_state()
{
    return wtp_runtime_selected() ? wtp_runtime_json() : wsprTransmitter.reloadDeferDebugState();
}

void transmitter_clear_execution_state_after_stop() noexcept
{
    if (!wtp_runtime_selected()) wsprTransmitter.clearExecutionStateAfterStop();
}

void transmitter_configure_execution(const TransmissionRequest &request)
{
    if (wtp_runtime_selected()) {
        if (request.isSkipWindow()) { wtp_runtime_prepare_skip(); return; }
        throw std::runtime_error("Pico requires a complete canonical finite job");
    }
    wsprTransmitter.configureExecution(request);
}

void transmitter_configure_execution(
    const wsprrypi::TransmissionRequest &request,
    const TransmissionRequest &legacy_request)
{
    if (wtp_runtime_selected()) { wtp_runtime_prepare(request); return; }
    wsprTransmitter.configureExecution(request, legacy_request);
}

wsprrypi::StartupQuiesceResult transmitter_quiesce_for_startup()
{
    return wtp_runtime_selected() ? wtp_runtime_inspect() : wsprTransmitter.quiesceForStartup();
}

void transmitter_set_thread_scheduling(int policy, int priority)
{
    wsprTransmitter.setThreadScheduling(policy, priority);
}

void transmitter_set_callbacks(TransmitterRuntimeCallback callback)
{
    { std::lock_guard lock(callback_mutex); runtime_callback = callback; }
    wsprTransmitter.setTransmissionCallbacks(std::move(callback));
}

std::string transmitter_format_frequency_mhz(double frequency_hz)
{
    return WsprTransmitter::formatFrequencyMHz(frequency_hz);
}

void transmitter_start_async()
{
    if (wtp_runtime_selected()) { wtp_runtime_start(); return; }
    wsprTransmitter.startAsync();
}

void transmitter_stop_and_join()
{
    if (wtp_runtime_selected()) {
        auto result = wtp_runtime_stop();
        if (!result.ok) llog.logS(ERROR, "Pico output remains unresolved: ", result.error);
        return;
    }
    wsprTransmitter.stopAndJoin();
}

void transmitter_shutdown_for_process_exit()
{
    if (wtp_runtime_selected()) { transmitter_stop_and_join(); return; }
    wsprTransmitter.shutdownForProcessExit();
}

void transmitter_clear_soft_off() noexcept
{
    if (!wtp_runtime_selected()) wsprTransmitter.clearSoftOff();
}

void transmitter_poll_events()
{
    wtp_runtime_poll_idle();
    auto result = wtp_runtime_completion();
    if (!result) return;
    TransmitterRuntimeCallback callback;
    { std::lock_guard lock(callback_mutex); callback = runtime_callback; }
    if (!callback) return;
    const auto outcome = result->outcome;
    callback(result->skipped ? WsprTransmissionCallbackEvent::SKIPPED
             : outcome == wsprrypi::WtpScheduleOutcome::Complete ? WsprTransmissionCallbackEvent::COMPLETE
             : outcome == wsprrypi::WtpScheduleOutcome::Cancelled || outcome == wsprrypi::WtpScheduleOutcome::Invalidated
                 ? WsprTransmissionCallbackEvent::CANCELLED : WsprTransmissionCallbackEvent::FAILED,
             WsprTransmitLogLevel::INFO, result->error, 0);
}
