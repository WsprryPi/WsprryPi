/**
 * @file scheduling_websocket_status.cpp
 * @brief Renders scheduler status events for WebSocket clients.
 */

#include "scheduling.hpp"

#include "json.hpp"
#include "test_tone_response.hpp"
#include "logging.hpp"
#include "web_socket.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

/**
 * @brief Broadcasts a JSON-formatted WebSocket message to all connected clients.
 *
 * Builds a JSON object containing a message type, state, and current UTC
 * timestamp (ISO 8601), serializes it, and sends it over the WebSocket server.
 *
 * @param[in] type   The message category (e.g., "transmit", "status").
 * @param[in] state  The message state or payload (e.g., "starting", "finished").
 *
 * @note Requires <nlohmann/json.hpp>, <chrono>, <ctime>, <iomanip>, and <sstream>.
 */
void send_ws_message(
    std::string type,
    std::string state,
    std::string message,
    std::optional<int> cw_active_char_index_override)
{
    // Build JSON payload
    nlohmann::json j;
    j["type"] = type;
    j["state"] = state;
    j["test_tone"] = test_tone_status_json(current_test_tone_status());

    if (type == "transmit")
    {
        const WsprRuntimeStatusSnapshot snapshot = current_tx_runtime_status_snapshot();
        const std::string tx_state = websocket_tx_state_for_message(
            type,
            state,
            snapshot.tx_state);
        j["tx_state"] = tx_state;
        j["runtime_mode"] = snapshot.runtime_mode;
        j["transmit_backend"] = snapshot.transmit_backend;
        j["next_transmission_at"] = snapshot.next_transmission_at;
        j["frequency_hz"] = snapshot.frequency_hz;
        j["offset_hz"] = snapshot.offset_hz;
        j["frequency_is_skip"] = snapshot.frequency_is_skip;
        j["selector_gpio_enabled"] = snapshot.selector_gpio_enabled;
        j["selector_gpio"] = snapshot.selector_gpio;
        j["selector_gpio_active_high"] = snapshot.selector_gpio_active_high;
        j["plan_type"] = snapshot.plan_type;
        j["power_dbm"] = snapshot.power_dbm;
        j["frame_count"] = snapshot.frame_count;
        j["current_frame"] = snapshot.current_frame;
        j["callsign_raw"] = snapshot.callsign_raw;
        j["callsign_normalized"] = snapshot.callsign_normalized;
        j["locator_raw"] = snapshot.locator_raw;
        j["locator_normalized"] = snapshot.locator_normalized;
        j["frame_callsign"] = snapshot.frame_callsign;
        j["frame_locator"] = snapshot.frame_locator;
        j["cw_message"] = snapshot.cw_message;
        j["cw_active_char_index"] =
            cw_active_char_index_override.value_or(snapshot.cw_active_char_index);
        j["frequency_estimate_qualification"] = snapshot.frequency_estimate_qualification;
        j["frequency_estimate_provider"] = snapshot.frequency_estimate_provider;
        j["frequency_estimate_provenance"] = snapshot.frequency_estimate_provenance;
        j["frequency_correction_mode"] = snapshot.frequency_correction_mode;
        j["frequency_estimate_reason"] = snapshot.frequency_estimate_reason;
        j["frequency_estimate_ppm"] = snapshot.frequency_estimate_ppm_available
            ? nlohmann::json(snapshot.frequency_estimate_ppm)
            : nlohmann::json(nullptr);
        j["gpio_frequency_residual_ppm"] = snapshot.gpio_frequency_residual_ppm;
        j["effective_gpio_ppm"] = snapshot.additional_gpio_ppm;
        j["frequency_estimate_age_seconds"] = snapshot.frequency_estimate_age_seconds;
        const auto provenance_json = [](const auto &value)
        {
            return nlohmann::json{
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
        j["gpio_correction_candidate"] =
            provenance_json(snapshot.gpio_correction_candidate);
        j["gpio_correction_committed"] =
            provenance_json(snapshot.gpio_correction_committed);
    }

    if (!message.empty())
    {
        j["message"] = message;
    }

    // Capture current UTC time and format as ISO 8601 (YYYY-MM-DDThh:mm:ssZ)
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&now_t, &tm_utc);

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    j["timestamp"] = oss.str();

    // Serialize and send to all WebSocket clients
    const std::string payload = j.dump();
    if (type == "transmit")
    {
        llog.logS(
            DEBUG,
            "WebSocket transmit event prepared: state=",
            state,
            ", tx_state=",
            j.value("tx_state", std::string{}),
            ", plan_type=",
            j.value("plan_type", std::string{}),
            ", current_frame=",
            j.value("current_frame", 0),
            "/",
            j.value("frame_count", 0),
            ".");
    }
    else
    {
        llog.logS(
            DEBUG,
            "WebSocket event prepared: type=",
            type,
            ", state=",
            state,
            ".");
    }
    socketServer.sendAllClients(payload);
}

std::string websocket_tx_state_for_message(
    std::string_view type,
    std::string_view state,
    std::string_view current_tx_state)
{
    if (type != "transmit")
    {
        return std::string(current_tx_state);
    }

    if (state == "starting" || state == "progress")
    {
        return "transmitting";
    }
    if (state == "finished" || state == "skipped")
    {
        return "complete";
    }
    if (state == "canceled")
    {
        return "canceled";
    }
    if (state == "stopped")
    {
        return "disabled";
    }

    return std::string(current_tx_state);
}
