/**
 * @file web_socket_commands.cpp
 * @brief Dispatches JSON commands received by the WebSocket transport.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "web_socket.hpp"
#include "backend_capabilities.hpp"
#if WSPRRYPI_BACKEND_RP1_GPCLK
#include "WSPR-Transmitter/src/rp1_gpclk_transmit_backend.hpp"
#endif
#include "band_lookup.hpp"
#include "config_handler.hpp"
#include "json.hpp"
#include "logging.hpp"
#include "scheduling.hpp"
#include "test_tone_request.hpp"
#include "test_tone_response.hpp"
#include "wspr_band_catalog_response.hpp"
#include "wspr_transmit.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

namespace
{
#if WSPRRYPI_BACKEND_RP1_GPCLK
nlohmann::json rp1_operation_record_json()
{
    const auto record = wsprrypi::rp1GpclkOperationRecordSnapshot();
    return {{"schemaVersion", record.schema_version},
            {"operationId", record.operation_id},
            {"moduleId", record.module_id},
            {"moduleVersion", record.module_version},
            {"compatibilityId", record.compatibility_id},
            {"route", record.route == 1 ? "GPIO4" : record.route == 2 ? "GPIO20" : "unknown"},
            {"endpoint", record.endpoint},
            {"lease", record.lease},
            {"generation", record.generation},
            {"state", record.state},
            {"terminalReason", record.terminal_reason},
            {"terminalReasonName", record.terminal_reason_name},
            {"cleanupFault", record.cleanup_fault},
            {"elapsedNs", record.elapsed_ns},
            {"remainingNs", record.remaining_ns},
            {"cancellationRequested", record.cancellation_requested},
            {"cleanupAttempted", record.cleanup_attempted},
            {"cleanupComplete", record.cleanup_complete},
            {"endpointClosed", record.endpoint_closed},
            {"executionAuthorized", record.execution_authorized},
            {"processId", record.process_id},
            {"executable", record.executable},
            {"startedMonotonicNs", record.started_monotonic_ns},
            {"finishedMonotonicNs", record.finished_monotonic_ns},
            {"qualificationClaim", false}};
}
#endif
}

void WebSocketServer::cancelBoundedTone()
{
    std::lock_guard lock(bounded_tone_mutex_);
    bounded_tone_request_id_.clear();
    bounded_tone_generation_ = 0;
    bounded_tone_cv_.notify_all();
}

void WebSocketServer::stopBoundedTone()
{
    {
        std::lock_guard command_lock(test_tone_command_mutex_);
        // Order shutdown against natural completion/restoration in the scheduler
        // before waiting for the worker. A completion that already won remains
        // a completed transaction; shutdown never restores an owned tone.
        std::lock_guard transaction(test_tone_lifecycle_mutex());
        std::uint64_t generation;
        {
            std::lock_guard lock(bounded_tone_mutex_);
            bounded_tone_stop_requested_ = true;
            generation = bounded_tone_generation_;
            bounded_tone_request_id_.clear();
            bounded_tone_generation_ = 0;
        }
        if (generation) {
            const auto stopped = end_test_tone(generation, TestToneEndPolicy::Quiesce);
            if (stopped.restoration_failed)
                llog.logE(ERROR, "Bounded tone cleanup failed: ", stopped.message);
        }
    }
    bounded_tone_cv_.notify_all();
    // The worker may be waiting at the command boundary. No lock it needs
    // is held while joining, including on repeated stop/destruction.
    if (bounded_tone_watchdog_.joinable()) bounded_tone_watchdog_.join();
}

void WebSocketServer::boundedToneLoop()
{
    std::unique_lock state_lock(bounded_tone_mutex_);
    for (;;) {
        bounded_tone_cv_.wait(state_lock, [this] {
            return bounded_tone_stop_requested_ || !bounded_tone_request_id_.empty();
        });
        if (bounded_tone_stop_requested_) return;
        const auto generation = bounded_tone_generation_;
        const auto request_id = bounded_tone_request_id_;
        const auto deadline = bounded_tone_deadline_;
        if (bounded_tone_cv_.wait_until(state_lock, deadline, [this, generation] {
                return bounded_tone_stop_requested_ || bounded_tone_generation_ != generation;
            })) continue;
        state_lock.unlock();
        if (bounded_expiry_ready_for_test_) bounded_expiry_ready_for_test_();
        {
            std::lock_guard command_lock(test_tone_command_mutex_);
            bool current;
            {
                std::lock_guard lock(bounded_tone_mutex_);
                current = !bounded_tone_stop_requested_ && bounded_tone_generation_ == generation;
                if (current) { bounded_tone_request_id_.clear(); bounded_tone_generation_ = 0; }
            }
            if (current) {
                const auto stopped = end_test_tone(generation);
                nlohmann::json terminal = {
                    {"command", "bounded_tone"}, {"event", "completed"},
                    {"request_id", request_id}, {"stopped", stopped.stopped},
                    {"scheduler_restored", stopped.scheduler_restored},
                    {"restoration_failed", stopped.restoration_failed},
                    {"output_inactive", stopped.output_inactive},
                    {"test_tone", test_tone_status_json(stopped.lifecycle)},
                    {"status", stopped.stopped && !stopped.restoration_failed &&
                        stopped.lifecycle.state != "failed" && stopped.lifecycle.state != "unknown" ? "ok" : "error"},
                    {"message", stopped.message}};
#if WSPRRYPI_BACKEND_RP1_GPCLK
                attach_rp1_operation_record(terminal, rp1_operation_record_json(), true);
#endif
                if (running_) sendAllClients(terminal.dump());
            }
        }
        state_lock.lock();
    }
}

/**
 * @brief Handles and dispatches incoming JSON client messages.
 *
 * This function parses the raw input text as JSON, extracts the
 * "command" field, and dispatches the request to the appropriate
 * stubbed action (shutdown, reboot, get_tx_state, or echo). It
 * trims and lowercases the incoming message to support case- and
 * whitespace-insensitive command parsing, recognizes supported
 * command keywords, and sends a JSON response back to the client
 * via sendJSON().
 *
 * - "tx_status" → Responds with transmission status acknowledgment.
 * - "shutdown"  → Responds with shutdown acknowledgment.
 * - "reboot"    → Responds with reboot acknowledgment.
 * - "stop"      → Stops active transmission and disables transmit persistently.
 *
 * All other messages are echoed back with a generic reply.
 *
 * @param raw_message The raw text message received from the client.
 */
void WebSocketServer::handleMessage(const std::string &raw_message)
{
    using json = nlohmann::json;
    json reply = json::object();
    std::unique_lock<std::mutex> test_tone_command_lock;

    try
    {
        // Parse incoming text as JSON
        auto j = json::parse(raw_message);

        if (j.contains("client_request_id") && j["client_request_id"].is_string() &&
            j["client_request_id"].get_ref<const std::string &>().size() <= 128)
            reply["client_request_id"] = j["client_request_id"];
        const auto client_request_id = reply.value("client_request_id", std::string{});

        // Extract "command" (defaults to empty string if missing), lowercase it
        std::string cmd = j.value("command", "");
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "tone_start" || cmd == "tone_end" || cmd == "stop")
        {
            // Multiple clients have independent handler threads. Keep each
            // Test Tone lifecycle operation and its broadcast reply in one
            // transaction so Start and End cannot race or report out of order.
            test_tone_command_lock =
                std::unique_lock<std::mutex>(test_tone_command_mutex_);
        }
        else if (cmd == "bounded_tone")
        {
            test_tone_command_lock =
                std::unique_lock<std::mutex>(test_tone_command_mutex_);
        }

        if (cmd == "shutdown")
        {
            llog.logS(INFO, "Received websocket shutdown command.");
            reply["command"] = "shutdown";
            shutdown_system();
        }
        else if (cmd == "reboot")
        {
            llog.logS(INFO, "Received websocket reboot command.");
            reply["command"] = "reboot";
            reboot_system();
        }
        else if (cmd == "stop")
        {
            llog.logS(INFO, "Received websocket stop command.");
            cancelBoundedTone();
            const bool persist_transmit =
                !j.contains("persist_transmit") || j["persist_transmit"].get<bool>();
            const StopTransmissionResult stop_result =
                stop_transmission_by_user_request(persist_transmit);
            const bool stop_request_succeeded = stop_result.transmit_disabled;
            reply["command"] = "stop";
            reply["status"] = stop_request_succeeded ? "ok" : "error";
            reply["transmission_active"] = stop_result.transmission_active;
            reply["stop_performed"] = stop_result.stop_performed;
            reply["transmit_disabled"] = stop_result.transmit_disabled;
            reply["persisted"] = stop_result.persisted;
            reply["persist_transmit"] = persist_transmit;
            reply["message"] = stop_result.message;
        }
        else if (cmd == "get_tx_state")
        {
            llog.logS(DEBUG, "Received JSON get_tx_state command.");
            reply["test_tone"] = test_tone_status_json(current_test_tone_status());
            const WsprRuntimeStatusSnapshot snapshot =
                current_tx_runtime_status_snapshot();
            reply["tx_state"] = snapshot.tx_state;
            reply["runtime_mode"] = snapshot.runtime_mode;
            reply["transmit_backend"] = snapshot.transmit_backend;
            reply["next_transmission_at"] = snapshot.next_transmission_at;
            reply["frequency_hz"] = snapshot.frequency_hz;
            reply["offset_hz"] = snapshot.offset_hz;
            reply["frequency_is_skip"] = snapshot.frequency_is_skip;
            reply["selector_gpio_enabled"] = snapshot.selector_gpio_enabled;
            reply["selector_gpio"] = snapshot.selector_gpio;
            reply["selector_gpio_active_high"] = snapshot.selector_gpio_active_high;
            reply["plan_type"] = snapshot.plan_type;
            reply["power_dbm"] = snapshot.power_dbm;
            reply["frame_count"] = snapshot.frame_count;
            reply["current_frame"] = snapshot.current_frame;
            reply["callsign_raw"] = snapshot.callsign_raw;
            reply["callsign_normalized"] = snapshot.callsign_normalized;
            reply["locator_raw"] = snapshot.locator_raw;
            reply["locator_normalized"] = snapshot.locator_normalized;
            reply["frame_callsign"] = snapshot.frame_callsign;
            reply["frame_locator"] = snapshot.frame_locator;
            reply["cw_message"] = snapshot.cw_message;
            reply["cw_active_char_index"] = snapshot.cw_active_char_index;
            reply["frequency_estimate_qualification"] = snapshot.frequency_estimate_qualification;
            reply["frequency_estimate_provider"] = snapshot.frequency_estimate_provider;
            reply["frequency_estimate_provenance"] = snapshot.frequency_estimate_provenance;
            reply["frequency_correction_mode"] = snapshot.frequency_correction_mode;
            reply["frequency_estimate_reason"] = snapshot.frequency_estimate_reason;
            reply["frequency_estimate_ppm"] = snapshot.frequency_estimate_ppm_available
                ? json(snapshot.frequency_estimate_ppm)
                : json(nullptr);
            reply["gpio_frequency_residual_ppm"] = snapshot.gpio_frequency_residual_ppm;
            reply["effective_gpio_ppm"] = snapshot.additional_gpio_ppm;
            reply["frequency_estimate_age_seconds"] = snapshot.frequency_estimate_age_seconds;
            const auto provenance_json = [](const auto &value)
            {
                return json{
                    {"available", value.available}, {"active", value.active},
                    {"processor_profile", value.processor_profile},
                    {"selected_parent", value.selected_parent},
                    {"nominal_rate_hz", value.nominal_rate_hz},
                    {"selected_component_ppm", value.selected_component_ppm},
                    {"conducted_residual_ppm", value.conducted_residual_ppm},
                    {"correction_ppm", value.additional_ppm},
                    {"correction_mode", value.correction_mode},
                    {"provider_name", value.provider_name},
                    {"provider_source_signature", value.provider_source_signature},
                    {"provider_snapshot_time", value.provider_snapshot_time},
                    {"execution_identity", value.execution_identity}};
            };
            reply["gpio_correction_candidate"] =
                provenance_json(snapshot.gpio_correction_candidate);
            reply["gpio_correction_committed"] =
                provenance_json(snapshot.gpio_correction_committed);
            reply["rp1_route_requested"] = snapshot.rp1_route_requested;
            reply["rp1_route_persisted"] = snapshot.rp1_route_persisted;
            reply["rp1_route_configured"] = snapshot.rp1_route_configured;
            reply["rp1_route_active"] = snapshot.rp1_route_active;
            reply["rp1_eligibility"] = snapshot.rp1_eligibility;
            reply["rp1_cleanup_state"] = snapshot.rp1_cleanup_state;
            reply["rp1_journal_state"] = snapshot.rp1_journal_state;
            auto now = std::chrono::system_clock::now();
            auto now_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_utc{};
            gmtime_r(&now_t, &tm_utc);
            std::ostringstream oss;
            oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
            reply["timestamp"] = oss.str();
        }
        else if (cmd == "wspr_band_catalog")
        {
            try
            {
                const BandLookup lookup;
                const TestTonePlanningConfigSnapshot planning_snapshot =
                    current_test_tone_planning_config_snapshot();
                reply = json::parse(build_wspr_band_catalog_response_json(
                    lookup,
                    current_wspr_audio_offset_hz(),
                    planning_snapshot.wspr_frequency_profile,
                    planning_snapshot.wspr_band_preferences));
            }
            catch (const std::exception &error)
            {
                reply["command"] = "wspr_band_catalog";
                reply["status"] = "error";
                reply["error"] = "catalog unavailable";
                reply["message"] = error.what();
            }
        }
        else if (cmd == "tone_start")
        {
            llog.logS(DEBUG, "Received JSON test_tone_start command.");
            const TestToneRequestParseResult parsed_request =
                parse_test_tone_request(j);

            if (!running_) {
                reply = {{"command", "tone_start"}, {"started", false},
                         {"status", "error"}, {"tone_start", "rejected"},
                         {"message", "WebSocket server is stopping."}};
            }
            else if (!parsed_request)
            {
                reply["command"] = "tone_start";
                reply["started"] = false;
                reply["already_active"] = false;
                reply["blocked_by_active_transmission"] = false;
                reply["blocked_by_enabled_transmission"] = false;
                reply["message"] = parsed_request.error;
                reply["tone_start"] = "rejected";
                reply["status"] = "error";
            }
            else
            {
                const TestToneStartResult start_result =
                    start_test_tone(*parsed_request.request);
                reply = build_test_tone_response(*parsed_request.request, start_result);
            }
        }
        else if (cmd == "bounded_tone")
        {
            reply["command"] = "bounded_tone";
            if (!running_)
            {
                reply["status"] = "error";
                reply["started"] = false;
                reply["message"] = "WebSocket server is stopping";
            }
            else if (!loopback_only_)
            {
                reply["status"] = "error";
                reply["started"] = false;
                reply["message"] =
                    "bounded_tone requires a loopback-only server";
            }
            else
            {
                const BoundedTestToneRequestParseResult parsed =
                    parse_bounded_test_tone_request(j);
                if (!parsed)
                {
                    reply["status"] = "error";
                    reply["started"] = false;
                    reply["message"] = parsed.error;
                }
                else
                {
                    std::lock_guard<std::mutex> bounded_lock(
                        bounded_tone_mutex_);
                    if (!running_)
                    {
                        reply["status"] = "error";
                        reply["started"] = false;
                        reply["request_id"] = parsed.request->request_id;
                        reply["message"] = "WebSocket server is stopping";
                    }
                    else if (!bounded_tone_request_id_.empty() &&
                             current_test_tone_status().owned &&
                             current_test_tone_status().generation == bounded_tone_generation_)
                    {
                        reply["status"] = "error";
                        reply["started"] = false;
                        reply["request_id"] = parsed.request->request_id;
                        reply["message"] =
                            "another bounded tone transaction is active";
                    }
                    else
                    {
                        TestToneRequest bounded_request = parsed.request->tone;
                        bounded_request.duration = std::chrono::milliseconds(
                            parsed.request->duration_ms);
                        const TestToneStartResult start_result =
                            start_test_tone(bounded_request);
                        reply = build_test_tone_response(
                            parsed.request->tone, start_result);
                        reply["command"] = "bounded_tone";
                        reply["request_id"] = parsed.request->request_id;
                        reply["duration_ms"] = parsed.request->duration_ms;
#if WSPRRYPI_BACKEND_RP1_GPCLK
                        attach_rp1_operation_record(reply, rp1_operation_record_json(), false);
#endif
                        if (start_result.started)
                        {
                            try
                            {
                                if (!bounded_tone_watchdog_.joinable()) {
                                    if (fail_bounded_thread_creation_for_test_)
                                        throw std::runtime_error("injected watchdog creation failure");
                                    bounded_tone_stop_requested_ = false;
                                    bounded_tone_watchdog_ = std::thread([this] { boundedToneLoop(); });
                                }
                                bounded_tone_request_id_ = parsed.request->request_id;
                                bounded_tone_generation_ = start_result.lifecycle.generation;
                                bounded_tone_deadline_ = std::chrono::steady_clock::now() +
                                    start_result.bounded_cleanup_delay.value_or(
                                        std::chrono::milliseconds(parsed.request->duration_ms));
                                bounded_tone_cv_.notify_all();
                            }
                            catch (...)
                            {
                                bounded_tone_request_id_.clear();
                                bounded_tone_generation_ = 0;
                                const auto stopped = end_test_tone(
                                    start_result.lifecycle.generation, TestToneEndPolicy::Quiesce);
                                reply["test_tone"] = test_tone_status_json(stopped.lifecycle);
                                reply["status"] = "error";
                                reply["started"] = false;
                                reply["cleanup_stopped"] = stopped.stopped;
                                reply["message"] = "Unable to arm bounded tone watchdog; cleanup attempted.";
                            }
                        }
                    }
                }
            }
        }
        else if (cmd == "tone_end")
        {
            cancelBoundedTone();
            llog.logS(DEBUG, "Received JSON test_tone_stop command.");
            const TestToneStopResult stop_result = end_test_tone();
            reply["command"] = "tone_end";
            reply["stopped"] = stop_result.stopped;
            reply["test_tone"] = test_tone_status_json(stop_result.lifecycle);
            reply["restoration_failed"] = stop_result.restoration_failed;
            reply["output_inactive"] = stop_result.output_inactive;
            reply["tone_was_active"] = stop_result.tone_was_active;
            reply["scheduler_restored"] = stop_result.scheduler_restored;
            reply["deferred_reload_reconciled"] =
                stop_result.deferred_reload_reconciled;
            reply["message"] = stop_result.message;
            reply["tone_end"] = stop_result.stopped ? "ok" : "rejected";
            reply["status"] = stop_result.stopped && !stop_result.restoration_failed ? "ok" : "error";
        }
        else if (cmd == "echo")
        {
            llog.logS(INFO, "Received JSON echo command.");
            if (j.contains("payload"))
            {
                // Echo back the provided payload verbatim
                reply["payload"] = j["payload"];
            }
            else
            {
                reply["message"] = "missing payload";
            }
        }
        else
        {
            llog.logS(WARN, "Unknown command received: " + cmd);
            reply["status"] = "error";
            reply["message"] = "unknown command";
            reply["command"] = cmd;
        }
        if (!client_request_id.empty()) reply["client_request_id"] = client_request_id;
    }
    catch (const json::parse_error &e)
    {
        // JSON was invalid
        llog.logE(ERROR, "JSON parse error in handleMessage: " + std::string(e.what()));
        reply["status"] = "error";
        reply["error"] = "invalid JSON";
    }
    catch (const std::exception &e)
    {
        llog.logE(
            ERROR,
            "WebSocket command failed in handleMessage: " +
                std::string(e.what()));
        reply["status"] = "error";
        reply["error"] = "command failure";
        reply["message"] = "Unable to process the command.";
    }
    catch (...)
    {
        llog.logE(ERROR, "Unknown WebSocket command failure in handleMessage.");
        reply["status"] = "error";
        reply["error"] = "command failure";
        reply["message"] = "Unable to process the command.";
    }

    // Send the JSON‐formatted reply
    sendJSON(reply);
}

/**
 * @brief Serialize a given object to JSON and send it to the connected client.
 *
 * This function converts the provided object to a JSON string using
 * nlohmann::json and transmits it over the active WebSocket connection.
 * The object type T must be compatible with nlohmann::json.
 *
 * @tparam T Type that can be converted to nlohmann::json.
 * @param obj The object to serialize and send to the client.
 *
 * @throws nlohmann::json::type_error If the object cannot be serialized.
 * @throws std::runtime_error If sending the serialized data fails.
 */
template <typename T>
void WebSocketServer::sendJSON(const T &obj)
{
    // T could be nlohmann::json or any structure you convert to it
    const std::string payload = obj.dump();
    if (reply_ready_for_test_) reply_ready_for_test_(payload);
    sendAllClients(payload);
}
