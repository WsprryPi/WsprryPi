#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    std::string read_text_file(const std::string &path)
    {
        std::ifstream in(path);
        require(in.is_open(), "test helper must open " + path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    std::string extract_input_tag_by_id(
        const std::string &source, const std::string &id)
    {
        const std::string needle = "id=\"" + id + "\"";
        const std::size_t id_pos = source.find(needle);
        require(id_pos != std::string::npos, "test helper must find field " + id);

        const std::size_t tag_start = source.rfind("<input", id_pos);
        require(
            tag_start != std::string::npos,
            "test helper must find input tag start for " + id);

        const std::size_t tag_end = source.find('>', id_pos);
        require(
            tag_end != std::string::npos,
            "test helper must find input tag end for " + id);

        return source.substr(tag_start, (tag_end - tag_start) + 1);
    }

    std::size_t count_occurrences(
        const std::string &source, const std::string &needle)
    {
        if (needle.empty())
        {
            return 0;
        }

        std::size_t count = 0;
        std::size_t pos = 0;
        while ((pos = source.find(needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }

        return count;
    }

} // namespace

int main()
{
    const std::string websocket_source =
        read_text_file("/home/pi/WsprryPi/src/web_socket.cpp");
    require(
        websocket_source.find("else if (cmd == \"stop\")") != std::string::npos &&
            websocket_source.find("stop_transmission_by_user_request(persist_transmit);") !=
                std::string::npos,
        "websocket stop command must route through stop_transmission_by_user_request() with persistence control");

    const std::string scheduling_source =
        read_text_file("/home/pi/WsprryPi/src/scheduling.cpp");
    require(
        scheduling_source.find("suppress_cancelled_ws_event_for_user_stop.store(true") != std::string::npos &&
            scheduling_source.find("Suppressing websocket canceled event because an explicit user stop will publish stopped.") !=
                std::string::npos,
        "explicit user stop must suppress the intermediate canceled websocket event");
    require(
        scheduling_source.find("TestToneStartResult start_test_tone()") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"starting\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"starting\");", scheduling_source.find("void transmitter_cb(")),
        "test tone start must rely on transmitter callback websocket ownership only");
    require(
        scheduling_source.find("TestToneStopResult end_test_tone()") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"finished\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"finished\");", scheduling_source.find("void transmitter_cb(")),
        "test tone end must rely on transmitter callback websocket ownership only");
    require(
        scheduling_source.find("wsprTransmitter.clearSoftOff();") != std::string::npos &&
            scheduling_source.find("wsprTransmitter.startAsync();", scheduling_source.find("TestToneStopResult end_test_tone()")) !=
                std::string::npos,
        "WSPR test tone stop recovery must explicitly re-arm the committed scheduler wait path");

    const std::string ui_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/index.js");
    const std::string maintenance_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/maintenance.js");
    const std::string operation_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/operation.js");
    require(
        ui_source.find("persist_transmit: persistTransmit") !=
                std::string::npos,
        "UI Stop button must send the explicit stop command over the websocket with persistence control");
    require(
        ui_source.find("let pendingModeChange = null;") != std::string::npos &&
            ui_source.find("let pendingPersistedMode = \"\";") != std::string::npos &&
            ui_source.find("function requestConfigModeChange(targetMode)") != std::string::npos &&
            ui_source.find("title: \"Disable transmissions to change mode\"") != std::string::npos &&
            ui_source.find("function persistDisabledModeChange(targetMode, previousMode = currentConfigModeSelection)") != std::string::npos &&
            ui_source.find("\"Mode\": normalizedTargetMode,") != std::string::npos &&
            ui_source.find("\"Transmit\": false,") != std::string::npos &&
            ui_source.find("applyCommittedConfigMode(normalizedTargetMode, { skipAutosave: true });") != std::string::npos &&
            ui_source.find("clearPendingModeChange();") != std::string::npos &&
            ui_source.find("pendingModeChange.awaitingRuntimeIdle === false") != std::string::npos &&
            ui_source.find("requestTransmitEnabledChange(false, true") == std::string::npos &&
            ui_source.find("setTransmitFromBackend(false);") != std::string::npos &&
            ui_source.find("if (!stopTransmission({ persistTransmit: false })) {") != std::string::npos &&
            ui_source.find("suspendConfigAutosave(true);") != std::string::npos &&
            ui_source.find("input:not(#transmit, [name=\"mode_toggle\"], [name=\"qrss_type\"])") != std::string::npos &&
            ui_source.find("configAutosaveNeedsRuntimeRefresh = true;") != std::string::npos &&
            ui_source.find("pendingPersistedMode = currentConfigModeSelection;") != std::string::npos &&
            ui_source.find("flushAutosave();") != std::string::npos &&
            ui_source.find("if (configAutosaveNeedsRuntimeRefresh && typeof getTxState === \"function\") {") != std::string::npos,
        "mode changes must be guarded behind stop/disable confirmation, collapse the guarded disable-plus-mode flow into one persisted save, exclude mode toggles from generic autosave scheduling, and refresh runtime state after the committed mode save lands");
    require(
        ui_source.find("if (enabled && pendingPersistedMode) {") != std::string::npos &&
            ui_source.find("Wait for the mode change to save before enabling transmissions.") != std::string::npos,
        "transmit enable must be blocked until a guarded mode change has actually been persisted");
    require(
        ui_source.find("const transmitting = runtimeStatus && runtimeStatus.txState === \"transmitting\";") !=
                std::string::npos &&
            ui_source.find("$stop.prop(\"disabled\", stopRequestInFlight || !transmitting);") !=
                std::string::npos &&
            ui_source.find("(!transmitEnabled && !transmitting)") == std::string::npos,
        "UI Stop button must only be enabled during an active transmission, not merely when transmit is enabled");
    require(
        ui_source.find("updateRuntimeControlStatusFromForm(null);") != std::string::npos &&
            ui_source.find("if (typeof getTxState === \"function\") {\n                getTxState();\n            }") !=
                std::string::npos,
        "successful transmit-toggle PATCHes must refresh runtime state so CW next-transmission timing updates immediately");
    require(
        ui_source.find("Operation: {\n                \"Transmit\": enabled,") !=
                std::string::npos &&
            ui_source.find("Runtime: {\n                \"Transmit\": enabled,") ==
                std::string::npos,
        "UI transmit toggle must patch canonical Operation.Transmit only");
    require(
        ui_source.find("var Operation = {\n        \"Mode\": mode,") !=
                std::string::npos &&
            ui_source.find("var Meta = {\n        \"Mode\": mode") ==
                std::string::npos,
        "UI save path must use canonical Operation.Mode only");
    require(
        ui_source.find("var GPIO = {\n        \"Power Level\": transmit_power,\n        \"Use NTP\": use_ntp,") !=
                std::string::npos &&
            ui_source.find("var Calibration = {\n        \"PPM\": ppm_val,\n        \"Use NTP\": use_ntp,") ==
                std::string::npos,
        "UI save path must use canonical GPIO.Use NTP only");
    require(
        ui_source.find("const CONFIG_AUTOSAVE_DELAY_MS = 800;") != std::string::npos &&
            ui_source.find("function buildConfigPayload()") != std::string::npos &&
            ui_source.find("function scheduleAutosave()") != std::string::npos &&
            ui_source.find("function flushAutosave()") != std::string::npos &&
            ui_source.find("payloadJson === lastSavedConfigPayload") != std::string::npos &&
            ui_source.find("payloadJson === lastFailedConfigPayload") != std::string::npos &&
            ui_source.find("Suppressing autosave retry for unchanged rejected payload.") != std::string::npos &&
            ui_source.find("setConfigSaveStatus(\"saved\", \"Saved\", \"\");") != std::string::npos &&
            ui_source.find("configAutosavePendingAfterFlight") != std::string::npos,
        "configuration autosave must debounce saves, clear stale invalid state for already-saved payloads, suppress unchanged failed payload retries, and suppress duplicate writes");
    require(
        ui_source.find("function validateCwMessage()") != std::string::npos &&
            ui_source.find("function validateCwDotSeconds()") != std::string::npos &&
            ui_source.find("function validateCwShiftHz()") != std::string::npos &&
            ui_source.find("function validateCwRepeatMinutes()") != std::string::npos &&
            ui_source.find("function validatePositiveCwField(fieldId, errorMessage)") != std::string::npos &&
            ui_source.find("function parseFrequencyWithOptionalUnits(rawValue)") != std::string::npos &&
            ui_source.find("Enter CW base frequency as whole-number Hz or as a value with Hz, kHz, MHz, or GHz.") != std::string::npos &&
            ui_source.find("Enter a positive CW dot length.") != std::string::npos &&
            ui_source.find("CW message is required.") != std::string::npos &&
            ui_source.find("Enter a whole-number CW frequency offset in Hz.") != std::string::npos &&
            ui_source.find("Enter a repeat interval of at least 1 minute.") != std::string::npos &&
            ui_source.find("Enter a positive CW intra-element gap.") != std::string::npos &&
            ui_source.find("Enter a positive CW inter-character gap.") != std::string::npos &&
            ui_source.find("Enter a positive CW inter-word gap.") != std::string::npos &&
            ui_source.find("let cw_message = String($('#qrss_message').val() || \"\").trim();") != std::string::npos &&
            ui_source.find("let cw_base_frequency = parseFrequencyWithOptionalUnits($('#qrss_frequency').val());") != std::string::npos &&
            ui_source.find("let cw_intra_element_gap = parseFloat($('#cw_intra_element_gap').val());") != std::string::npos &&
            ui_source.find("let cw_inter_character_gap = parseFloat($('#cw_inter_character_gap').val());") != std::string::npos &&
            ui_source.find("let cw_inter_word_gap = parseFloat($('#cw_inter_word_gap').val());") != std::string::npos &&
            ui_source.find("\"Intra Element Gap\": cw_intra_element_gap") != std::string::npos &&
            ui_source.find("\"Inter Character Gap\": cw_inter_character_gap") != std::string::npos &&
            ui_source.find("\"Inter Word Gap\": cw_inter_word_gap") != std::string::npos &&
            ui_source.find("let cw_base_frequency = parseFloat($('#qrss_frequency').val());") == std::string::npos &&
            ui_source.find("normalizedValue = value * 1e6;") != std::string::npos &&
            ui_source.find("normalizedValue = value * 1e3;") != std::string::npos &&
            ui_source.find("normalizedValue = value * 1e9;") != std::string::npos &&
            ui_source.find("const value = parseFrequencyWithOptionalUnits(raw);") != std::string::npos &&
            ui_source.find("if (!unit && numericPart.includes(\".\"))") != std::string::npos &&
            ui_source.find("const roundedValue = Math.round(normalizedValue);") != std::string::npos &&
            ui_source.find("Math.abs(normalizedValue - roundedValue) > 1e-6") != std::string::npos,
        "CW autosave validation and save-path serialization must share unit-aware base-frequency parsing, reject parseFloat-based truncation, and require explicit units for decimal inputs");
    require(
        ui_source.find("function splitWsprFrequencyTokens(raw)") != std::string::npos &&
            ui_source.find("function validateWsprFrequencyToken(token)") != std::string::npos &&
            ui_source.find("\"2200m\",") != std::string::npos &&
            ui_source.find("\"630m\",") != std::string::npos &&
            ui_source.find("\"22m\",") != std::string::npos &&
            ui_source.find("@GPIO, @GPIOH, or @GPIOL") != std::string::npos &&
            ui_source.find(".replace(/,/g, \" \")") != std::string::npos &&
            ui_source.find("const numericRx = /^-?(?:(?:\\\\d+(?:\\\\.\\\\d*)?)|(?:\\\\.\\\\d+))(?:hz|khz|mhz|ghz)?$/i;") == std::string::npos &&
            ui_source.find("-15") == std::string::npos,
        "WSPR frequency validation must support remaining band aliases, comma-separated lists, optional @GPIO suffixes, reject negative numeric tokens, and must not retain -15 aliases");
    require(
        ui_source.find("bindTestToneControls();") != std::string::npos,
        "configuration view must bind the shared Test Tone controls");
    require(
        ui_source.find("function si5351UiSupported()") != std::string::npos &&
            ui_source.find("function getSi5351OptionLabel(detected)") != std::string::npos &&
            ui_source.find("const si5351Supported = si5351UiSupported();") != std::string::npos &&
            ui_source.find("const si5351Detected = platform.si5351Detected !== false;") != std::string::npos &&
            ui_source.find("$si5351Option.text(getSi5351OptionLabel(si5351Detected));") != std::string::npos &&
            ui_source.find("$si5351Option.prop(\"disabled\", !si5351Supported);") != std::string::npos &&
            ui_source.find("$si5351Option.text(si5351Supported ? \"Si5351\" : \"Si5351 (Not detected)\");") ==
                std::string::npos,
        "Si5351 backend selection must remain available when the UI supports it, while detection only controls the option label");
    require(
        ui_source.find("function applyBandGpioColumnToggle(column, checked)") != std::string::npos &&
            ui_source.find("function syncBandGpioColumnHeaderStates()") != std::string::npos,
        "Band GPIO bulk-toggle behavior must use shared helper functions");
    require(
        ui_source.find("applyBandGpioColumnToggle(\"enabled\"") != std::string::npos &&
            ui_source.find("applyBandGpioColumnToggle(\"activeHigh\"") != std::string::npos,
        "Band GPIO header checkbox handlers must route through the shared bulk-toggle helper");
    require(
        ui_source.find("syncBandGpioColumnHeaderStates();\n    validateBandGpioFields();") != std::string::npos,
        "Band GPIO header state must be recomputed when row state changes");
    require(
        ui_source.find("Paired planning requires a compound callsign and 6-character locator.") != std::string::npos &&
            ui_source.find("detailActionLabel: \"More\"") != std::string::npos &&
            ui_source.find("title: \"Setup details\"") != std::string::npos &&
            ui_source.find("preserveLineBreaks: true") != std::string::npos,
        "paired planning save failures must collapse to a short setup-card message with a More dialog trigger that preserves full diagnostic line breaks");

    const std::string site_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/site.js");
    const std::string transmit_branch = "if (msg.type === \"transmit\")";
    const std::string tx_state_branch = "if (msg.tx_state !== undefined)";
    require(
        site_source.find(transmit_branch) != std::string::npos &&
            site_source.find(tx_state_branch) != std::string::npos &&
            site_source.find(transmit_branch) < site_source.find(tx_state_branch),
        "browser websocket handler must process pushed transmit events before generic tx_state replies");
    require(
        site_source.find("const PATHS = window.WSPRRYPI_PATHS || {};") != std::string::npos &&
            site_source.find("function normalizeSameOriginPath(path, fallback)") != std::string::npos &&
            site_source.find("function buildDirectRestFallbackUrl(path)") != std::string::npos &&
            site_source.find("function buildDirectWebSocketFallbackUrl(path)") != std::string::npos &&
            site_source.find("window.location.protocol === \"https:\" ? \"https:\" : \"http:\"") != std::string::npos &&
            site_source.find("window.location.protocol === \"https:\" ? \"wss:\" : \"ws:\"") != std::string::npos &&
            site_source.find("${protocol}//${window.location.hostname}:31415${path}") != std::string::npos &&
            site_source.find("${protocol}//${window.location.hostname}:31416${path}") != std::string::npos &&
            site_source.find("const SETTINGS_ENDPOINT = createEndpointDefinition(") != std::string::npos &&
            site_source.find("const VERSION_ENDPOINT = createEndpointDefinition(") != std::string::npos &&
            site_source.find("const REPAIR_ENDPOINT = createEndpointDefinition(") != std::string::npos &&
            site_source.find("const WEBSOCKET_ENDPOINT = createEndpointDefinition(") != std::string::npos &&
            site_source.find("const SETTINGS_URL = SETTINGS_ENDPOINT.proxyUrl;") != std::string::npos &&
            site_source.find("const VERSION_URL = VERSION_ENDPOINT.proxyUrl;") != std::string::npos &&
            site_source.find("const REPAIR_URL = REPAIR_ENDPOINT.proxyUrl;") != std::string::npos &&
            site_source.find("function ajaxWithEndpointFallback(endpoint, options = {})") != std::string::npos &&
            site_source.find("function fetchWithEndpointFallback(endpoint, init = {})") != std::string::npos &&
            site_source.find("function getJsonWithEndpointFallback(endpoint)") != std::string::npos &&
            site_source.find("warnRestFallback(endpoint, reason);") != std::string::npos &&
            site_source.find("warnWebSocketFallback(endpointConfig, reason);") != std::string::npos &&
            site_source.find("warnWebSocketFallbackAttempt(endpointConfig);") != std::string::npos &&
            site_source.find("warnWebSocketFallbackFailure(endpointConfig, reason);") != std::string::npos &&
            site_source.find("connectWebSocket(WEBSOCKET_ENDPOINT, WS_RECONNECT);") != std::string::npos &&
            site_source.find("getJsonWithEndpointFallback(SETTINGS_ENDPOINT)") != std::string::npos &&
            site_source.find("getJsonWithEndpointFallback(VERSION_ENDPOINT)") != std::string::npos &&
            site_source.find("new URL(path, window.location.href)") != std::string::npos &&
            site_source.find("url.protocol = window.location.protocol === \"https:\" ? \"wss:\" : \"ws:\";") != std::string::npos &&
            site_source.find("const HTTP_ORIGIN =") == std::string::npos &&
            site_source.find("const WS_ORIGIN =") == std::string::npos &&
            site_source.find("const HOSTNAME = window.location.hostname;") == std::string::npos &&
            site_source.find("const PORT = window.location.port ? `:${window.location.port}` : \"\";") == std::string::npos &&
            site_source.find("const SETTINGS_URL = SETTINGS_PATH;") == std::string::npos &&
            site_source.find("const VERSION_URL = VERSION_PATH;") == std::string::npos &&
            site_source.find("const REPAIR_URL = REPAIR_PATH;") == std::string::npos &&
            site_source.find("const WEBSOCKET_URL = WEBSOCKET_ENDPOINT.proxyUrl;") == std::string::npos &&
            site_source.find("const WEBSOCKET_URL = buildWebSocketUrl(WEBSOCKET_PATH);") == std::string::npos,
        "UI endpoint construction must remain proxy-first, allow direct ports only in centralized fallback helpers, and derive proxy websocket URLs from the current page protocol instead of rebuilding backend host:port origins");
    require(
        count_occurrences(site_source, ":31415") == 1 &&
            count_occurrences(site_source, ":31416") == 1,
        "site.js must mention direct backend ports only in the centralized fallback URL builders");
    require(
        ui_source.find("ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {") != std::string::npos &&
            operation_script_source.find("ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {") != std::string::npos &&
            maintenance_script_source.find("fetchWithEndpointFallback(SETTINGS_ENDPOINT, {") != std::string::npos &&
            maintenance_script_source.find("fetchWithEndpointFallback(REPAIR_ENDPOINT, {") != std::string::npos,
        "UI REST callers must route requests through centralized endpoint fallback helpers");
    require(
        site_source.find("Frequency Control GPIO Polarity") == std::string::npos,
        "UI config schema must not require the obsolete GPIO.Frequency Control GPIO Polarity key");
    require(
        site_source.find("let use_ntp = getConfigBoolValue(") != std::string::npos &&
            site_source.find("\"GPIO\",\n                    \"Use NTP\",\n                    true") != std::string::npos &&
            site_source.find("\"GPIO\",\n                    \"Use NTP\",\n                    false") == std::string::npos,
        "UI config loader must default GPIO.Use NTP to true to match backend normalization");
    require(
        site_source.find("let cw_base_frequency = getConfigFloatValue(cw, \"CW\", \"Base Frequency\", 14096900.0);") != std::string::npos &&
            site_source.find("let cw_base_frequency = getConfigFloatValue(cw, \"CW\", \"Base Frequency\", 3572000.0);") == std::string::npos,
        "UI config loader must default CW.Base Frequency to 14096900 Hz to match backend normalization");
    require(
        site_source.find("let cw_intra_element_gap = getConfigFloatValue(cw, \"CW\", \"Intra Element Gap\", 1.0);") != std::string::npos &&
            site_source.find("let cw_inter_character_gap = getConfigFloatValue(cw, \"CW\", \"Inter Character Gap\", 3.0);") != std::string::npos &&
            site_source.find("let cw_inter_word_gap = getConfigFloatValue(cw, \"CW\", \"Inter Word Gap\", 7.0);") != std::string::npos &&
            site_source.find("$(\"#cw_intra_element_gap\").val(cw_intra_element_gap).trigger(\"change\");") != std::string::npos &&
            site_source.find("$(\"#cw_inter_character_gap\").val(cw_inter_character_gap).trigger(\"change\");") != std::string::npos &&
            site_source.find("$(\"#cw_inter_word_gap\").val(cw_inter_word_gap).trigger(\"change\");") != std::string::npos,
        "UI config loader must round-trip CW gap settings with backend defaults");
    require(
        site_source.find("runtime_mode") != std::string::npos &&
            site_source.find("nextTransmissionAt") != std::string::npos &&
            site_source.find("frequencyHz") != std::string::npos &&
            site_source.find("offsetHz") != std::string::npos &&
            site_source.find("frequencyIsSkip") != std::string::npos &&
            site_source.find("powerDbm") != std::string::npos &&
            site_source.find("cw_message") != std::string::npos &&
            site_source.find("cw_active_char_index") != std::string::npos &&
            site_source.find("if (typeof handleRuntimeStatusUpdate === \"function\") {") != std::string::npos &&
            site_source.find("const selectedMode =\n        typeof selectedConfigMode === \"function\" ? selectedConfigMode() : \"\";") != std::string::npos &&
            site_source.find("renderRuntimeFrequencyPane(frequencyNode, currentMode, currentRuntimeStatus);") != std::string::npos &&
            site_source.find("function buildRuntimeFrequencyItems(currentMode, status)") != std::string::npos &&
            site_source.find("function runtimeFrequencyPrimaryLabel(currentMode, status)") != std::string::npos &&
            site_source.find("return \"Next Frequency\";") != std::string::npos &&
            site_source.find("return \"(Skip)\";") != std::string::npos &&
            site_source.find("queueRuntimeStatusRefresh();") != std::string::npos &&
            site_source.find("function formatDisplayFrequency(valueHz, options = {})") != std::string::npos &&
            site_source.find("forceUnit: \"Hz\"") != std::string::npos &&
            site_source.find("function renderCwRuntimeMessage(node, message, activeCharIndex)") != std::string::npos &&
            site_source.find("const isTransmitting =") != std::string::npos &&
            site_source.find("const transmitEnabled =") != std::string::npos &&
            site_source.find("planLabelNode.textContent = \"Message progression\";") != std::string::npos &&
            site_source.find("planLabelNode.textContent = \"Next message at:\";") != std::string::npos &&
            site_source.find("const idleValue = transmitEnabled") != std::string::npos &&
            site_source.find(": \"Disabled\";") != std::string::npos &&
            site_source.find("summary += ` ${currentRuntimeStatus.powerDbm}dBm`;") != std::string::npos &&
            site_source.find("charNode.textContent = character;") != std::string::npos &&
            site_source.find("renderCwRuntimeMessage(planNode, message, activeCharIndex);") != std::string::npos &&
            site_source.find("charNode.textContent = isActive && character === \" \" ? \"_\" : character;") == std::string::npos,
        "runtime status handling must switch CW runtime detail between next-transmission timing and live message progression without underscore substitution");
    require(
        scheduling_source.find("snapshot.next_transmission_at") != std::string::npos &&
            scheduling_source.find("snapshot.frequency_hz = current_transmission_request.dial_frequency_hz;") != std::string::npos &&
            scheduling_source.find("snapshot.offset_hz = current_transmission_request.applied_offset_hz;") != std::string::npos &&
            scheduling_source.find("snapshot.frequency_is_skip =") != std::string::npos &&
            scheduling_source.find("snapshot.frequency_hz = config.qrss.frequency_hz;") != std::string::npos &&
            scheduling_source.find("snapshot.frequency_hz = config.fskcw.space_frequency_hz;") != std::string::npos &&
            scheduling_source.find("snapshot.frequency_hz = config.dfcw.dot_frequency_hz;") != std::string::npos &&
            websocket_source.find("reply[\"next_transmission_at\"] = snapshot.next_transmission_at;") != std::string::npos &&
            websocket_source.find("reply[\"frequency_hz\"] = snapshot.frequency_hz;") != std::string::npos &&
            websocket_source.find("reply[\"offset_hz\"] = snapshot.offset_hz;") != std::string::npos &&
            websocket_source.find("reply[\"frequency_is_skip\"] = snapshot.frequency_is_skip;") != std::string::npos &&
            websocket_source.find("reply[\"power_dbm\"] = snapshot.power_dbm;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_hz\"] = snapshot.frequency_hz;") != std::string::npos &&
            scheduling_source.find("j[\"offset_hz\"] = snapshot.offset_hz;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_is_skip\"] = snapshot.frequency_is_skip;") != std::string::npos &&
            scheduling_source.find("j[\"power_dbm\"] = snapshot.power_dbm;") != std::string::npos &&
            scheduling_source.find("snapshot.power_dbm = plan.power_dbm;") != std::string::npos &&
            scheduling_source.find("if (snapshot.tx_state == \"transmitting\")") != std::string::npos &&
            scheduling_source.find("snapshot.runtime_mode = mode_type_name(config.mode);") != std::string::npos &&
            scheduling_source.find("runtime_transmit_enabled(config)") != std::string::npos &&
            scheduling_source.find("if (config.mode != ModeType::WSPR ||\n        current_transmission_request.mode != TransmissionMode::WSPR ||") != std::string::npos &&
            scheduling_source.find("snapshot.runtime_mode == mode_type_name(config.mode)") == std::string::npos &&
            scheduling_source.find("if (runtime_status.mode != wsprrypi::TransmissionMode::WSPR ||\n        current_transmission_request.mode != TransmissionMode::WSPR ||") == std::string::npos,
        "runtime snapshot must expose active frequency fields alongside next_transmission_at, follow the committed scheduler mode while idle instead of stale backend execution state, suppress stale WSPR plan data after mode changes, expose committed idle WSPR plan data without requiring runtime mode to already be WSPR, and expose CW next-transmission timing without requiring the transmitter runtime mode string to match first");
    require(
        site_source.find("const TAB_STATE_STORAGE_PREFIX = \"wsprrypi.activeTab\";") != std::string::npos &&
            site_source.find("function shouldRestorePersistedTabState(tabList)") != std::string::npos &&
            site_source.find("getNavigationType() === \"reload\"") != std::string::npos &&
            site_source.find("clearPersistedTabState(tabList);") != std::string::npos &&
            site_source.find("suspendConfigAutosave(true);") != std::string::npos &&
            site_source.find("syncConfigAutosaveBaseline();") != std::string::npos &&
            site_source.find("function initPersistedTabState()") != std::string::npos &&
            site_source.find("window.localStorage.setItem(storageKey, selector);") != std::string::npos &&
            site_source.find("restorePersistedTabState(tabList);") != std::string::npos,
        "site.js must support reload-scoped persisted Bootstrap tab state through localStorage");
    require(
        site_source.find("confirm-modal-body--preformatted") != std::string::npos &&
            site_source.find("preserveLineBreaks = options.preserveLineBreaks === true") != std::string::npos,
        "shared confirm modal must support preserving diagnostic line breaks for detail dialogs");
    require(
        site_source.find("function initFooterMetaPanelInteractions()") != std::string::npos &&
            site_source.find("document.addEventListener(\"click\", function (event) {") != std::string::npos &&
            site_source.find("footerMeta.contains(event.target)") != std::string::npos &&
            site_source.find("document.addEventListener(\"keydown\", function (event) {") != std::string::npos &&
            site_source.find("event.key !== \"Escape\"") != std::string::npos &&
            site_source.find("closeFooterMetaPanel();") != std::string::npos,
        "site.js must close the footer About panel on outside click and Escape while keeping click-based toggling");
    require(
        ui_source.find("function isWsprConfigMode()") != std::string::npos &&
            ui_source.find("const useNtp = isWsprMode && $(\"#use_ntp\").is(\":checked\");") != std::string::npos &&
            ui_source.find("backend === \"gpio\" && $(\"#use_ntp\").is(\":checked\");") == std::string::npos &&
            ui_source.find("$(\"#ntp_calibration_control\")") != std::string::npos &&
            ui_source.find("$ppm.prop(\"disabled\", !isWsprMode || useNtp);") != std::string::npos &&
            ui_source.find("$ppmCw.prop(\"disabled\", isWsprMode);") != std::string::npos &&
            ui_source.find("syncCalibrationControls();") != std::string::npos,
        "config UI must make calibration controls mode-aware so WSPR follows NTP while CW keeps PPM editable");

    const std::string config_view_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/config.php");
    require(
        config_view_source.find("id=\"modeChangeGuardModal\"") != std::string::npos &&
            config_view_source.find("id=\"modeChangeGuardConfirmBtn\"") != std::string::npos,
        "Configuration view must expose the guarded mode-change confirmation modal");
    const std::string footer_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/footer.php");
    require(
        footer_source.find("<details class=\"footer-meta\">") != std::string::npos &&
            footer_source.find("<summary>About</summary>") != std::string::npos,
        "footer markup must keep the native click-based About disclosure");
    const std::string fetch_spots_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/fetch_spots.php");
    require(
        fetch_spots_source.find("function normalizeLookupBaseCallsign(string $value): string") != std::string::npos &&
            fetch_spots_source.find("function buildLookupCallsignCandidates(string $value): array") != std::string::npos &&
            fetch_spots_source.find("'%/' . $baseCallsign") != std::string::npos &&
            fetch_spots_source.find("$baseCallsign . '/%'") != std::string::npos &&
            fetch_spots_source.find("'<' . $baseCallsign . '>'") != std::string::npos &&
            fetch_spots_source.find("function fetchDownloaderData(array $txCandidates, string $rxSign, string $start, string $end, string $format): array") != std::string::npos &&
            fetch_spots_source.find("function fetchClickhouseData(array $txRegexes, ?string $rxSign, DateTimeImmutable $start, DateTimeImmutable $end): array") != std::string::npos &&
            fetch_spots_source.find("$txLookup = buildLookupCallsignCandidates($txSignRaw);") != std::string::npos &&
            fetch_spots_source.find("$txLookupBase = sanitizeClickhouseCallsign($txLookup['base'], 'Transmitter callsign');") != std::string::npos,
        "spot lookup must normalize compound configured callsigns into shared downloader and clickhouse lookup candidates without changing non-lookup callsign handling");

    const std::string operation_view_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/operation.php");
    require(
        operation_view_source.find("class=\"btn btn-danger operation-stop-button\"") != std::string::npos &&
            operation_view_source.find("id=\"stop_transmit\"") != std::string::npos &&
            operation_view_source.find("aria-label=\"Runtime controls\"") != std::string::npos &&
            operation_view_source.find("operation-hero__controls") != std::string::npos,
        "Operation view must host the Stop transmission control in the runtime controls section");
    require(
        operation_view_source.find("<label class=\"form-check-label\" for=\"transmit\">Transmit enabled</label>") != std::string::npos &&
            operation_view_source.find("id=\"runtime_plan_label\"") != std::string::npos &&
            operation_view_source.find("operation-hero__controls") != std::string::npos,
        "Operation view must host the primary Transmit enabled control and runtime plan label in the runtime controls context");
    const std::size_t runtime_mode_position =
        operation_view_source.find("id=\"runtime_mode_value\"");
    const std::size_t runtime_frequency_position =
        operation_view_source.find("id=\"runtime_frequency_value\"");
    const std::size_t runtime_plan_position =
        operation_view_source.find("id=\"runtime_plan_label\"");
    require(
            runtime_mode_position != std::string::npos &&
            runtime_frequency_position != std::string::npos &&
            runtime_plan_position != std::string::npos &&
            operation_view_source.find("operation-panel__label operation-panel__label--split") != std::string::npos &&
            operation_view_source.find("id=\"runtime_frequency_primary_label\"") != std::string::npos &&
            operation_view_source.find(">Frequency</span>") != std::string::npos &&
            runtime_mode_position < runtime_frequency_position &&
            runtime_frequency_position < runtime_plan_position,
        "Operation view must place the Frequency pane between the Current mode and mode-specific runtime panes");

    const std::string operation_css_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/operation.css");
    require(
        operation_css_source.find("grid-template-columns: minmax(0, 0.85fr) minmax(0, 1fr) minmax(0, 1.25fr);") != std::string::npos &&
            operation_css_source.find(".operation-panel__stack") != std::string::npos &&
            operation_css_source.find(".operation-panel__item-label") != std::string::npos &&
            operation_css_source.find(".operation-panel__item-value") != std::string::npos &&
            operation_css_source.find(".operation-panel--wide {\n        grid-column: 1 / -1;") != std::string::npos,
        "Operation page styles must support the native Frequency pane and preserve responsive summary-grid layout");
    require(
        config_view_source.find("config-runtime-item config-runtime-item--action") == std::string::npos,
        "Runtime state grid must no longer dedicate a large action tile to Stop transmission");
    require(
        config_view_source.find("id=\"ntp_calibration_control\"") != std::string::npos &&
            config_view_source.find("id=\"use_ntp\"") != std::string::npos,
        "Configuration view must expose the dedicated NTP calibration control wrapper without changing the existing field binding");
    require(
        config_view_source.find("config-runtime-item config-runtime-item--switch") == std::string::npos,
        "Runtime state body must no longer dedicate a body tile to the Transmit enabled control");
    require(
        config_view_source.find("id=\"test_tone\"") == std::string::npos &&
            config_view_source.find("id=\"testToneModal\"") == std::string::npos,
        "configuration view must no longer render the Test Tone control inside the config form");
    require(
        config_view_source.find("id=\"configSaveStatus\"") != std::string::npos &&
            config_view_source.find("Next safe action") == std::string::npos &&
            config_view_source.find("Save changes") == std::string::npos &&
            config_view_source.find("Reload saved") == std::string::npos,
        "configuration view must expose the inline save-status indicator and remove the manual action panel");
    require(
        config_view_source.find("class=\"config-wspr-top-row\"") != std::string::npos &&
            config_view_source.find("config-wspr-top-row__item config-wspr-top-row__field config-wspr-top-row__field--wide") != std::string::npos &&
            config_view_source.find("config-wspr-top-row__item config-wspr-top-row__field config-wspr-top-row__field--dbm") != std::string::npos &&
            config_view_source.find("class=\"config-wspr-secondary-row\"") != std::string::npos &&
            config_view_source.find("config-wspr-top-row__item config-wspr-top-row__field config-wspr-top-row__planner") != std::string::npos &&
            config_view_source.find("for=\"useoffset\">\n                                                Random offset\n") != std::string::npos &&
            config_view_source.find("id=\"ppm\"") != std::string::npos &&
            config_view_source.find("min=\"-200\"") != std::string::npos &&
            config_view_source.find("max=\"200\"") != std::string::npos &&
            config_view_source.find("id=\"use_ntp\"") != std::string::npos &&
            config_view_source.find("id=\"planner_preference\"") != std::string::npos &&
            config_view_source.find("id=\"dbm\"") != std::string::npos &&
            config_view_source.find("<option value=\"60\">60</option>") != std::string::npos &&
            config_view_source.find("spaces or commas") != std::string::npos &&
            config_view_source.find("@GPIO, @GPIOH, or @GPIOL") != std::string::npos &&
            config_view_source.find("-15") == std::string::npos &&
            config_view_source.find("class=\"form-select config-planner-field__select\"") == std::string::npos,
        "WSPR transmission settings must keep TX dBm as a fixed-value select and expose planner_preference in the WSPR planning controls");
    require(
        config_view_source.find("id=\"fsk_offset\"") != std::string::npos &&
            config_view_source.find("value=\"5\"") != std::string::npos &&
            config_view_source.find("id=\"dot_length\"") != std::string::npos &&
            config_view_source.find("id=\"tx_repeat_every\"") != std::string::npos,
        "configuration view must keep CW defaults while removing UI-only max caps for dot length, shift, and repeat interval");
    const std::string dot_length_input =
        extract_input_tag_by_id(config_view_source, "dot_length");
    const std::string fsk_offset_input =
        extract_input_tag_by_id(config_view_source, "fsk_offset");
    const std::string tx_repeat_every_input =
        extract_input_tag_by_id(config_view_source, "tx_repeat_every");
    const std::string cw_intra_element_gap_input =
        extract_input_tag_by_id(config_view_source, "cw_intra_element_gap");
    const std::string cw_inter_character_gap_input =
        extract_input_tag_by_id(config_view_source, "cw_inter_character_gap");
    const std::string cw_inter_word_gap_input =
        extract_input_tag_by_id(config_view_source, "cw_inter_word_gap");
    require(
        dot_length_input.find("step=\"any\"") != std::string::npos &&
            dot_length_input.find("min=\"0.000000001\"") != std::string::npos &&
            dot_length_input.find("max=\"60\"") == std::string::npos &&
            dot_length_input.find("step=\"1\"") == std::string::npos,
        "CW dot-length markup must advertise strictly positive fractional input without restoring the old max cap");
    require(
        fsk_offset_input.find("min=\"1\"") != std::string::npos &&
            fsk_offset_input.find("step=\"1\"") != std::string::npos &&
            fsk_offset_input.find("max=\"1000\"") == std::string::npos,
        "CW shift markup must require a positive whole-number lower bound while removing the old UI-only max cap");
    require(
        tx_repeat_every_input.find("min=\"1\"") != std::string::npos &&
            tx_repeat_every_input.find("max=\"60\"") == std::string::npos,
        "CW repeat-interval markup must retain the minimum while removing the old UI-only max cap");
    require(
        config_view_source.find("id=\"cw_intra_element_gap\"") != std::string::npos &&
            config_view_source.find("id=\"cw_inter_character_gap\"") != std::string::npos &&
            config_view_source.find("id=\"cw_inter_word_gap\"") != std::string::npos &&
            config_view_source.find("Intra-Element Gap:") != std::string::npos &&
            config_view_source.find("Inter-Character Gap:") != std::string::npos &&
            config_view_source.find("Inter-Word Gap:") != std::string::npos,
        "configuration view must expose the three CW gap controls");
    require(
        cw_intra_element_gap_input.find("min=\"0.000000001\"") != std::string::npos &&
            cw_intra_element_gap_input.find("step=\"any\"") != std::string::npos &&
            cw_intra_element_gap_input.find("value=\"1\"") != std::string::npos &&
            cw_inter_character_gap_input.find("min=\"0.000000001\"") != std::string::npos &&
            cw_inter_character_gap_input.find("step=\"any\"") != std::string::npos &&
            cw_inter_character_gap_input.find("value=\"3\"") != std::string::npos &&
            cw_inter_word_gap_input.find("min=\"0.000000001\"") != std::string::npos &&
            cw_inter_word_gap_input.find("step=\"any\"") != std::string::npos &&
            cw_inter_word_gap_input.find("value=\"7\"") != std::string::npos,
        "CW gap markup must use strictly positive fractional defaults that match backend config defaults");
    require(
        config_view_source.find("Fade Shape") == std::string::npos &&
            config_view_source.find("Fade In Ms") == std::string::npos &&
            config_view_source.find("Fade Out Ms") == std::string::npos &&
            config_view_source.find("Fade Slice Ms") == std::string::npos,
        "configuration view must keep CW fade settings hidden from the normal UI");
    require(
        config_view_source.find("id=\"qrss_frequency\"") != std::string::npos &&
            config_view_source.find("value=\"14096900\"") != std::string::npos &&
            config_view_source.find("14.0969MHz") != std::string::npos &&
            config_view_source.find("14096.9kHz") != std::string::npos &&
            config_view_source.find("0.0140969GHz") != std::string::npos,
        "configuration view must default CW base frequency markup to 14096900 Hz");
    require(
        config_view_source.find("id=\"band-gpio-enabled-all\"") != std::string::npos &&
            config_view_source.find("id=\"band-gpio-active-high-all\"") != std::string::npos,
        "Band GPIO table must expose bulk-toggle header checkboxes for Enabled and Active High");
    require(
        config_view_source.find("class=\"form-check-input band-gpio-enabled\"") != std::string::npos &&
            config_view_source.find("class=\"form-check-input band-gpio-enabled\"\n                                                                type=\"checkbox\"") != std::string::npos &&
            config_view_source.find("class=\"form-check-input band-gpio-active-high\"") != std::string::npos &&
            config_view_source.find("class=\"form-check-input band-gpio-active-high\"\n                                                                type=\"checkbox\"") != std::string::npos &&
            config_view_source.find("class=\"form-check-input band-gpio-active-high\"\n                                                                type=\"checkbox\"\n                                                                id=\"band-gpio-active-high-<?= htmlspecialchars($band) ?>\"\n                                                                data-band=\"<?= htmlspecialchars($band) ?>\"\n                                                                checked") == std::string::npos,
        "Band GPIO row defaults must render unchecked and inactive until explicitly configured");
    require(
        config_view_source.find("id=\"configTabs\"") != std::string::npos &&
            config_view_source.find("role=\"tablist\"") != std::string::npos &&
            config_view_source.find("data-persist-tab-state=\"true\"") != std::string::npos &&
            config_view_source.find("data-persist-tab-state-scope=\"reload\"") != std::string::npos &&
            config_view_source.find("data-persist-tab-query-param=\"setup_tab\"") != std::string::npos,
        "Configuration tab list must opt into reload-scoped persisted sub-tab state");

    const std::string maintenance_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/maintenance.php");
    require(
        maintenance_source.find("id=\"test_tone\"") != std::string::npos &&
            maintenance_source.find("id=\"testToneModal\"") != std::string::npos,
        "maintenance view must host the relocated Test Tone control and modal");

    const std::string maintenance_test_tone_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/maintenance.js");
    require(
        maintenance_test_tone_script_source.find("bindTestToneControls();") != std::string::npos,
        "maintenance view must bind the shared Test Tone controls");
    require(
        site_source.find("function hasActiveManagedTransmissionForTestTone()") != std::string::npos &&
            site_source.find("function handleTestToneCommandResponse(message)") != std::string::npos &&
            site_source.find("if (hasActiveManagedTransmissionForTestTone()) {") != std::string::npos &&
            site_source.find("showTestToneBlockedModal();") != std::string::npos &&
            site_source.find("You have to stop and disable the active scheduled transmission before starting a test tone.") != std::string::npos &&
            site_source.find("if (msg.command === \"tone_start\" || msg.command === \"tone_end\")") != std::string::npos,
        "shared Test Tone controls must reject unsafe starts with the existing modal path and reconcile websocket command replies");
    require(
        site_source.find("const normalizedArgs = args.map((arg) => {") != std::string::npos &&
            site_source.find("return JSON.stringify(arg);") != std::string::npos &&
            site_source.find("return Object.prototype.toString.call(arg);") != std::string::npos,
        "debug console logging must serialize array and object payloads into useful text");

    std::cout << "ui_source_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
