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
    const std::string web_server_source =
        read_text_file("/home/pi/WsprryPi/src/web_server.cpp");
    const std::string makefile_source =
        read_text_file("/home/pi/WsprryPi/src/Makefile");
    const std::string version_source =
        read_text_file("/home/pi/WsprryPi/src/version.cpp");
    const std::string version_header_source =
        read_text_file("/home/pi/WsprryPi/src/version.hpp");
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
        scheduling_source.find("TestToneStartResult start_test_tone(") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"starting\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"starting\");", scheduling_source.find("void transmitter_cb(")),
        "test tone start must rely on transmitter callback websocket ownership only");
    require(
        scheduling_source.find("TestToneStopResult end_test_tone()") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"finished\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"finished\");", scheduling_source.find("void transmitter_cb(")),
        "test tone end must rely on transmitter callback websocket ownership only");
    require(
        scheduling_source.find("finalize_transmission_stop_cleanup(") != std::string::npos &&
            scheduling_source.find("wsprTransmitter.clearSoftOff();") != std::string::npos &&
            scheduling_source.find("wsprTransmitter.startAsync();", scheduling_source.find("TestToneStopResult end_test_tone()")) !=
                std::string::npos,
        "WSPR test tone stop recovery must route through shared stop cleanup and explicitly re-arm the committed scheduler wait path");

    const std::string ui_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/index.js");
    const std::string config_view_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/config.php");
    const std::string maintenance_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/maintenance.js");
    const std::string operation_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/operation.js");
    const std::string cw_message_input =
        extract_input_tag_by_id(config_view_source, "qrss_message");
    require(
        cw_message_input.find("type=\"text\"") != std::string::npos &&
            cw_message_input.find("step=") == std::string::npos,
        "CW message input must remain textual so numeric-only operator messages such as 73 are not treated as numbers");
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
            ui_source.find("applyCommittedConfigMode(normalizedTargetMode, {\n                skipAutosave: true,\n                keepAutosaveSuspended: true,\n            });") != std::string::npos &&
            ui_source.find("options.keepAutosaveSuspended !== true") != std::string::npos &&
            ui_source.find("function suppressNextPersistedConfigDraftRestore()") != std::string::npos &&
            ui_source.find("populateConfig();") != std::string::npos &&
            ui_source.find("shouldSuppressDisabledModeSwitchReloadFailure(message)") != std::string::npos &&
            ui_source.find("clearPendingModeChange();") != std::string::npos &&
            ui_source.find("pendingModeChange.awaitingRuntimeIdle === false") != std::string::npos &&
            ui_source.find("requestTransmitEnabledChange(false, true") == std::string::npos &&
            ui_source.find("setTransmitFromBackend(false);") != std::string::npos &&
            ui_source.find("function startGuardedActiveModeChange()") != std::string::npos &&
            ui_source.find("completeGuardedActiveModeChange();") != std::string::npos &&
            ui_source.find("guardedActiveModeChange: true,") != std::string::npos &&
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
        operation_script_source.find("function rebootBehaviorConfigValueFromRadio(value)") !=
                std::string::npos &&
            operation_script_source.find("return \"Always\";") !=
                std::string::npos &&
            operation_script_source.find("\"Enable on Boot\": nextValue,") !=
                std::string::npos &&
            operation_script_source.find("input[name=\"operation_reboot_behavior\"]") !=
                std::string::npos &&
            operation_script_source.find("Aways") == std::string::npos,
        "Operation page reboot behavior radios must patch canonical Operation.Enable on Boot and preserve the Always spelling");
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
            ui_source.find("\"Message\": cw_message,") != std::string::npos &&
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
        ui_source.find("const PAIRED_PLANNING_SHORT_MESSAGE =") != std::string::npos &&
            ui_source.find("Paired planning requires a compound callsign and 6-character locator.") != std::string::npos &&
            ui_source.find("function isPairedPlanningUnavailableError(data)") != std::string::npos &&
            ui_source.find("data.plan_status.trim() === \"PairedTransmissionUnavailable\"") != std::string::npos &&
            ui_source.find("const isPairedPlanningFailure =\n                isPairedPlanningUnavailableError(parsedError);") != std::string::npos &&
            ui_source.find("PAIRED_PLANNING_SHORT_MESSAGE,\n                    message,") != std::string::npos &&
            ui_source.find("detailActionLabel: \"More\"") != std::string::npos &&
            ui_source.find("onDetailAction: () => openSetupDetailsDialog(message)") != std::string::npos &&
            ui_source.find("title: \"Setup details\"") != std::string::npos &&
            ui_source.find("preserveLineBreaks: true") != std::string::npos,
        "paired planning save failures must collapse to a short setup-card message with a More dialog trigger that preserves full diagnostic line breaks");

    const std::string site_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/site.js");
    const std::string stock_ini_source =
        read_text_file("/home/pi/WsprryPi/config/wsprrypi.ini");
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
            site_source.find("selectorGpioEnabled") != std::string::npos &&
            site_source.find("selectorGpio") != std::string::npos &&
            site_source.find("selectorGpioActiveHigh") != std::string::npos &&
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
            site_source.find("function formatDisplayFrequencyWithSelector(valueHz, status, options = {})") != std::string::npos &&
            site_source.find("function formatSelectorGpioSuffix(status)") != std::string::npos &&
            site_source.find("status.selectorGpioEnabled !== true") != std::string::npos &&
            site_source.find("return ` @ GPIO${status.selectorGpio}${status.selectorGpioActiveHigh ? \"H\" : \"L\"}`;") != std::string::npos &&
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
            websocket_source.find("reply[\"selector_gpio_enabled\"] = snapshot.selector_gpio_enabled;") != std::string::npos &&
            websocket_source.find("reply[\"selector_gpio\"] = snapshot.selector_gpio;") != std::string::npos &&
            websocket_source.find("reply[\"selector_gpio_active_high\"] = snapshot.selector_gpio_active_high;") != std::string::npos &&
            websocket_source.find("reply[\"power_dbm\"] = snapshot.power_dbm;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_hz\"] = snapshot.frequency_hz;") != std::string::npos &&
            scheduling_source.find("j[\"offset_hz\"] = snapshot.offset_hz;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_is_skip\"] = snapshot.frequency_is_skip;") != std::string::npos &&
            scheduling_source.find("j[\"selector_gpio_enabled\"] = snapshot.selector_gpio_enabled;") != std::string::npos &&
            scheduling_source.find("j[\"selector_gpio\"] = snapshot.selector_gpio;") != std::string::npos &&
            scheduling_source.find("j[\"selector_gpio_active_high\"] = snapshot.selector_gpio_active_high;") != std::string::npos &&
            scheduling_source.find("j[\"power_dbm\"] = snapshot.power_dbm;") != std::string::npos &&
            scheduling_source.find("snapshot.selector_gpio_enabled =\n        current_transmission_request.hasSelectorGPIO();") != std::string::npos &&
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
        site_source.find("let dismissedUiRefreshVersion = null;") != std::string::npos &&
            site_source.find("let dismissedUiRefreshBuildId = null;") != std::string::npos &&
            site_source.find("const UI_BUILD_POLL_INTERVAL_MS = 60 * 1000;") != std::string::npos &&
            site_source.find("const GITHUB_UPDATE_POLL_INTERVAL_MS = 60 * 60 * 1000;") != std::string::npos &&
            site_source.find("window.WSPRRYPI_UI_VERSION") != std::string::npos &&
            site_source.find("window.WSPRRYPI_UI_BUILD_ID") != std::string::npos &&
            site_source.find("let githubUpdatePollTimer = null;") != std::string::npos &&
            site_source.find("function checkUiBuildVersion()") != std::string::npos &&
            site_source.find("function initUiBuildChangePolling()") != std::string::npos &&
            site_source.find("function initGithubUpdatePolling()") != std::string::npos &&
            site_source.find("if (githubUpdatePollTimer !== null)") != std::string::npos &&
            site_source.find("githubUpdatePollTimer = window.setInterval(\n        updateWsprryPiVersion,\n        GITHUB_UPDATE_POLL_INTERVAL_MS\n    );") != std::string::npos &&
            site_source.find("document.addEventListener(\"visibilitychange\"") != std::string::npos &&
            site_source.find("function maybePromptForUiRefresh(versionResponse)") != std::string::npos &&
            site_source.find("function sharedConfirmModalIsVisible()") != std::string::npos &&
            site_source.find("uiRefreshPromptActive && !sharedConfirmModalIsVisible()") != std::string::npos &&
            site_source.find("const modalEl = document.getElementById(\"confirmModal\");") != std::string::npos &&
            site_source.find("const promptShown = showConfirmationDialog({") != std::string::npos &&
            site_source.find("uiRefreshPromptActive = promptShown === true;") != std::string::npos &&
            site_source.find("getJsonWithEndpointFallback(VERSION_ENDPOINT)\n        .done(function (response) {\n            if (response && (response.ui_build_id || response.ui_version)) {\n                maybePromptForUiRefresh(response);") != std::string::npos &&
            site_source.find("// Start UI build polling from global script initialization as soon as site.js") != std::string::npos &&
            site_source.find("initUiBuildChangePolling();\ninitGithubUpdatePolling();\n\nfunction getPersistedTabStorageKey") != std::string::npos &&
            site_source.find("initUiBuildChangePolling();\n    initGithubUpdatePolling();\n    populateConfig();") != std::string::npos &&
            site_source.find("initGithubUpdatePolling();\n    populateConfig();") != std::string::npos &&
            site_source.find("if (uiBuildPollTimer !== null)") != std::string::npos &&
            site_source.find("const canCompareBuildId = loadedBuildId && normalizedServerBuildId;") != std::string::npos &&
            site_source.find("normalizedServerBuildId === dismissedUiRefreshBuildId") != std::string::npos &&
            web_server_source.find("j[\"ui_version\"] = get_raw_version_string();") != std::string::npos &&
            web_server_source.find("j[\"wspr_version_raw\"] = get_exe_version();") != std::string::npos &&
            web_server_source.find("j[\"wspr_version_parsed\"] = parse_version_for_update_metadata(get_exe_version());") != std::string::npos &&
            web_server_source.find("j[\"wspr_branch\"] = get_exe_raw_branch();") != std::string::npos &&
            web_server_source.find("j[\"wspr_branch_state\"] = get_exe_branch_state();") != std::string::npos &&
            web_server_source.find("j[\"wspr_display_branch\"] = get_exe_branch();") != std::string::npos &&
            web_server_source.find("j[\"wspr_exe_version\"] = get_exe_version();") != std::string::npos &&
            web_server_source.find("j[\"wspr_commit\"] = get_exe_commit();") != std::string::npos,
        "site.js must detect backend UI version changes using the existing /version response, and backend /version must expose raw and parsed executable version, raw branch, branch state, executable version, and commit metadata");
    require(
        site_source.find("title: \"UI refresh required\"") != std::string::npos &&
            site_source.find("The WsprryPi web interface has been updated. Refresh this page to load the new web pages, CSS, and JavaScript.") != std::string::npos &&
            site_source.find("confirmLabel: \"Refresh\"") != std::string::npos &&
            site_source.find("cancelLabel: \"Cancel\"") != std::string::npos &&
            site_source.find("showConfirmationDialog({") != std::string::npos,
        "UI refresh prompt must use the shared Bootstrap confirmation modal path with the required copy and labels");
    require(
        site_source.find("function refreshUiForVersion(serverVersion, serverBuildId = \"\")") != std::string::npos &&
            site_source.find("const url = new URL(window.location.href);") != std::string::npos &&
            site_source.find("url.searchParams.set(\n        \"ui_refresh\",") != std::string::npos &&
            site_source.find("window.location.replace(url.toString());") != std::string::npos &&
            site_source.find("normalizedBuildId || normalizedVersion || Date.now().toString()") != std::string::npos &&
            site_source.find("refreshUiForVersion(normalizedServerVersion, normalizedServerBuildId);") != std::string::npos &&
            site_source.find("dismissedUiRefreshBuildId = normalizedServerBuildId;") != std::string::npos &&
            site_source.find("if (typeof options.onCancel === \"function\")") != std::string::npos &&
            site_source.find("if (typeof options.onHidden === \"function\")") != std::string::npos &&
            site_source.find("confirmModal.show();\n        return true;") != std::string::npos &&
            site_source.find("return false;") != std::string::npos,
        "UI refresh prompt must replace the current URL with a ui_refresh cache-busting query parameter on OK and suppress repeat prompts for the same server build id on Cancel or dismiss");
    require(
        site_source.find("function initFooterMetaPanelInteractions()") != std::string::npos &&
            site_source.find("document.addEventListener(\"click\", function (event) {") != std::string::npos &&
            site_source.find("footerMeta.contains(event.target)") != std::string::npos &&
            site_source.find("document.addEventListener(\"keydown\", function (event) {") != std::string::npos &&
            site_source.find("event.key !== \"Escape\"") != std::string::npos &&
            site_source.find("closeFooterMetaPanel();") != std::string::npos,
        "site.js must close the footer About panel on outside click and Escape while keeping click-based toggling");
    const std::string version_check_display_branch = "version-check";
    const std::string version_check_raw_branch = "version_check";
    const std::string version_check_short_sha = "7b05546";
    const std::string version_check_head_sha =
        "7b05546abcdef0123456789abcdef0123456789";
    require(
        version_check_head_sha.rfind(version_check_short_sha, 0) == 0 &&
            version_check_display_branch == "version-check" &&
            version_check_raw_branch == "version_check",
        "test fixture must model a displayed version-check branch, raw version_check branch, and matching GitHub HEAD short SHA");
    require(
        makefile_source.find("RAW_BRH :=") != std::string::npos &&
            makefile_source.find("BRANCH_STATE :=") != std::string::npos &&
            makefile_source.find("git symbolic-ref --quiet --short HEAD") != std::string::npos &&
            makefile_source.find("BRH := $(shell printf '%s\\n' \"$(RAW_BRH)\"") != std::string::npos &&
            makefile_source.find("-DMAKE_BRH=\\\"$(BRH)\\\"") != std::string::npos &&
            makefile_source.find("-DMAKE_RAW_BRH=\\\"$(RAW_BRH)\\\"") != std::string::npos &&
            makefile_source.find("-DMAKE_BRANCH_STATE=\\\"$(BRANCH_STATE)\\\"") != std::string::npos &&
            makefile_source.find("-DMAKE_COMMIT=\\\"$(FULL_COMMIT)\\\"") != std::string::npos &&
            makefile_source.find("GIT_DIRTY :=") != std::string::npos &&
            makefile_source.find("git rev-parse --is-inside-work-tree") != std::string::npos &&
            makefile_source.find("git status --porcelain --untracked-files=no") != std::string::npos &&
            makefile_source.find("-DMAKE_DIRTY=\\\"$(GIT_DIRTY)\\\"") != std::string::npos &&
            version_source.find("#define MAKE_BRANCH_STATE \"unknown\"") != std::string::npos &&
            version_source.find("constexpr std::string_view BRANCH_STATE = to_string_view(MAKE_BRANCH_STATE);") != std::string::npos &&
            version_source.find("std::string get_exe_branch_state()") != std::string::npos &&
            version_source.find("does not treat \"HEAD\" as an upstream") != std::string::npos &&
            version_header_source.find("extern std::string get_exe_branch_state();") != std::string::npos &&
            version_source.find("#define MAKE_DIRTY \"unknown\"") != std::string::npos &&
            version_source.find("constexpr std::string_view BUILD_DIRTY = to_string_view(MAKE_DIRTY);") != std::string::npos &&
            version_source.find("std::string get_exe_build_dirty()") != std::string::npos &&
            version_source.find("It does not indicate whether a remote update exists.") != std::string::npos &&
            version_header_source.find("extern std::string get_exe_build_dirty();") != std::string::npos &&
            web_server_source.find("nlohmann::json parse_version_for_update_metadata(const std::string &version)") != std::string::npos &&
            web_server_source.find("{\"valid\", false}") != std::string::npos &&
            web_server_source.find("{\"major\", nullptr}") != std::string::npos &&
            web_server_source.find("{\"prerelease\", nlohmann::json::array()}") != std::string::npos &&
            web_server_source.find("j[\"wspr_build_dirty\"] = get_exe_build_dirty();") != std::string::npos &&
            web_server_source.find("j[\"wspr_build_dirty_state\"] = build_dirty_metadata(get_exe_build_dirty());") != std::string::npos,
        "build metadata must pass sanitized MAKE_BRH for display, raw MAKE_RAW_BRH and branch-state for update lookup, full commit and build-time dirty state to version.cpp, and expose structured version, branch, commit, and dirty metadata in /version");
    require(
        site_source.find("const UPDATE_CHECK_CACHE_SCHEMA_VERSION = 6;") != std::string::npos &&
            site_source.find("function updateCheckShaMatches(currentSha, targetHeadSha)") != std::string::npos &&
            site_source.find("return normalizedHead.startsWith(normalizedCurrent);") != std::string::npos &&
            site_source.find("function updateCheckNoUpdateResult()") != std::string::npos &&
            site_source.find("${versionInfo.branchState || \"branch\"}:${versionInfo.currentBranch}") != std::string::npos &&
            site_source.find("cached.branchState !== versionInfo.branchState") != std::string::npos &&
            site_source.find("branchState: versionInfo.branchState || \"branch\"") != std::string::npos &&
            site_source.find("updateCheckShaMatches(versionInfo.currentSha, selectedBranch.headSha)") != std::string::npos &&
            site_source.find("? updateCheckCommitComparisonResult(versionInfo.currentSha, selectedBranch.headSha, \"identical\")") != std::string::npos &&
            site_source.find(": await compareGithubCommits(versionInfo.currentSha, selectedBranch.headSha)") != std::string::npos &&
            site_source.find("if (error.status === 404)") != std::string::npos &&
            site_source.find("updateAvailable: !updateCheckShaMatches(currentSha, headSha)") != std::string::npos &&
            site_source.find("throw error;") != std::string::npos &&
            site_source.find("completed:") == std::string::npos &&
            site_source.find("currentShaLabel") == std::string::npos,
        "update checker must invalidate stale fallback cache entries, short-circuit matching full or short SHAs before GitHub compare, and avoid misleading unused comparison fields");
    require(
        site_source.find("Dirty means the local source tree had uncommitted or staged modifications") != std::string::npos &&
            site_source.find("function parseBuildDirtyState(response, rawDisplayVersion, rawUiVersion, rawExeVersion)") != std::string::npos &&
            site_source.find("const structuredDirtyState = response?.wspr_build_dirty_state;") != std::string::npos &&
            site_source.find("structuredDirtyState.known === true") != std::string::npos &&
            site_source.find("response?.wspr_build_dirty ?? response?.wspr_dirty") != std::string::npos &&
            site_source.find("source: \"structured\"") != std::string::npos &&
            site_source.find("source: \"version_text\"") != std::string::npos &&
            site_source.find("source: \"unavailable\"") != std::string::npos &&
            site_source.find("buildDirtyKnown: dirtyState.known") != std::string::npos &&
            site_source.find("buildDirty: dirtyState.dirty") != std::string::npos &&
            site_source.find("buildDirtySource: dirtyState.source") != std::string::npos &&
            site_source.find("versionInfo.buildDirtyKnown") != std::string::npos &&
            site_source.find("? versionInfo.buildDirty ? \"dirty\" : \"clean\"") != std::string::npos &&
            site_source.find(": \"dirty-unknown\"") != std::string::npos &&
            site_source.find("function applyDirtyBuildMetadata(versionInfo, result)") != std::string::npos &&
            site_source.find("if (!versionInfo.buildDirtyKnown)") != std::string::npos &&
            site_source.find("if (!versionInfo.buildDirty)") != std::string::npos &&
            site_source.find("localBuildState: \"dirty_build\"") != std::string::npos &&
            site_source.find("versionComparisonStatus: result.updateAvailable ? result.versionComparisonStatus : \"local_modified\"") != std::string::npos &&
            site_source.find("dirty means local modifications, not a remote update") != std::string::npos &&
            site_source.find("return applyDirtyBuildMetadata(versionInfo, semanticResult);") != std::string::npos &&
            site_source.find("return applyDirtyBuildMetadata(versionInfo, commitResult);") != std::string::npos &&
            site_source.find("if (!result || result.updateAvailable !== true)") != std::string::npos &&
            site_source.find("markWsprryPiLocalUpdateState(result);") != std::string::npos,
        "update checker must prefer structured wspr_build_dirty metadata over display-string parsing, keep unknown dirty metadata from changing behavior, separate clean/dirty cache entries for the same branch and SHA, mark dirty no-update results as local_modified/dirty_build, preserve dirty older-commit update-available results, and avoid showing footer/modal updates solely because a build is dirty");
    require(
        site_source.find("detached_target_unknown: \"Update check failed: detached or unknown branch state has no safe update target.\"") != std::string::npos &&
            site_source.find("function parseBranchState(response, currentBranch)") != std::string::npos &&
            site_source.find("response?.wspr_branch_state") != std::string::npos &&
            site_source.find("if (currentBranch === \"HEAD\")") != std::string::npos &&
            site_source.find("return \"detached\";") != std::string::npos &&
            site_source.find("return \"unknown\";") != std::string::npos &&
            site_source.find("branchState,") != std::string::npos &&
            site_source.find("branchState=${versionInfo.branchState}") != std::string::npos &&
            site_source.find("function isDetachedOrUnknownBranchBuild(versionInfo)") != std::string::npos &&
            site_source.find("versionInfo.currentBranch === \"HEAD\"") != std::string::npos &&
            site_source.find("versionInfo.currentBranch === \"unknown\"") != std::string::npos &&
            site_source.find("async function selectDetachedOrUnknownUpdateBranch(versionInfo)") != std::string::npos &&
            site_source.find("const candidates = [\"main\", \"devel\"];") != std::string::npos &&
            site_source.find("Detached HEAD and unknown-branch builds do not have a trustworthy") != std::string::npos &&
            site_source.find("same-name upstream branch. Commit fallback is allowed only after proving") != std::string::npos &&
            site_source.find("detached/unknown branch commit reachable from upstream") != std::string::npos &&
            site_source.find("Update check detached/unknown build resolved to upstream") != std::string::npos &&
            site_source.find("Update check detached/unknown build is not reachable from upstream") != std::string::npos &&
            site_source.find("buildUpdateCheckFailure(\n        \"detached_target_unknown\"") != std::string::npos &&
            site_source.find("is not reachable from upstream main or devel") != std::string::npos &&
            site_source.find("if (isDetachedOrUnknownBranchBuild(versionInfo))") != std::string::npos &&
            site_source.find("return selectDetachedOrUnknownUpdateBranch(versionInfo);") != std::string::npos &&
            site_source.find("lookupGithubBranch(currentBranch)") != std::string::npos,
        "detached HEAD or unknown branch-state builds must keep valid semver as the primary path, avoid treating HEAD/unknown as same-name upstream branches in commit fallback, target main/devel only when reachability is proven with an explicit reason, and report check-failed/unknown when no safe target exists");
    require(
            site_source.find("function parseSemanticVersion(value)") != std::string::npos &&
            site_source.find("function parseStructuredSemanticVersion(value)") != std::string::npos &&
            site_source.find("value.valid !== true") != std::string::npos &&
            site_source.find("const normalizedPrerelease = normalizeSemanticIdentifiers(prerelease.join(\".\"));") != std::string::npos &&
            site_source.find("const normalizedBuild = normalizeSemanticIdentifiers(build.join(\".\"), true);") != std::string::npos &&
            site_source.find("const localVersion = versionInfo.localVersionParsedObject ||") != std::string::npos &&
            site_source.find("function normalizeSemanticIdentifiers(value, allowLeadingZeroNumeric = false)") != std::string::npos &&
            site_source.find("return identifiers.map((identifier) => identifier.toLowerCase());") != std::string::npos &&
            site_source.find("!identifier ||") != std::string::npos &&
            site_source.find("identifier.length > 1 && identifier.startsWith(\"0\")") != std::string::npos &&
            site_source.find("if (prerelease === null || build === null)") != std::string::npos &&
            site_source.find("v?(\\d+)\\.(\\d+)\\.(\\d+)") != std::string::npos &&
            site_source.find("function compareSemanticVersions(left, right)") != std::string::npos &&
            site_source.find("function comparePrereleaseIdentifier(left, right)") != std::string::npos &&
            site_source.find("return Number(left) - Number(right);") != std::string::npos &&
            site_source.find("[\"alpha\", 0]") != std::string::npos &&
            site_source.find("[\"beta\", 1]") != std::string::npos &&
            site_source.find("[\"rc\", 2]") != std::string::npos &&
            site_source.find("Unknown channels still fall back to normal lexical SemVer ordering.") != std::string::npos &&
            site_source.find("function fetchGithubReleases()") != std::string::npos &&
            site_source.find("`${UPDATE_CHECK_API_BASE}/releases?per_page=100`") != std::string::npos &&
            site_source.find("function summarizeSemanticReleases(releases)") != std::string::npos &&
            site_source.find("latestStable") != std::string::npos &&
            site_source.find("latestPrerelease") != std::string::npos &&
            site_source.find("prereleasesByChannel") != std::string::npos &&
            site_source.find("async function buildSemanticVersionUpdateResult(versionInfo, options = {})") != std::string::npos &&
            site_source.find("semantic version compared against GitHub release") != std::string::npos &&
            site_source.find("local semantic version has build metadata/commits past tag") != std::string::npos &&
            site_source.find("local semantic version could not be parsed") != std::string::npos &&
            site_source.find("GitHub release data unavailable") != std::string::npos &&
            site_source.find("Stable builds compare only with latest stable release and never") != std::string::npos &&
            site_source.find("upgrade to a prerelease.") != std::string::npos &&
            site_source.find("Prerelease builds compare with newer stable") != std::string::npos &&
            site_source.find("from the same prerelease channel") != std::string::npos &&
            site_source.find("Different\n    // prerelease channels are intentionally ignored by default.") != std::string::npos &&
            site_source.find("const channel = localVersion.prerelease[0];") != std::string::npos &&
            site_source.find("summary.prereleasesByChannel.get(channel)") != std::string::npos &&
            site_source.find("versionComparisonUsed: \"semver\"") != std::string::npos &&
            site_source.find("versionComparisonStatus: comparison === 0 ? \"equal\" : \"local_ahead\"") != std::string::npos &&
            site_source.find("remoteVersionSelected: summary.latestStable.normalized") != std::string::npos &&
            site_source.find("remoteVersionSelected: latestSameChannelPrerelease.normalized") != std::string::npos &&
            site_source.find("async function buildCommitBasedWsprryPiUpdateResult(versionInfo, semanticFallback = null)") != std::string::npos &&
            site_source.find("versionComparisonUsed: \"commit\"") != std::string::npos &&
            site_source.find("function branchAllowsCommitUpdate(branch)") != std::string::npos &&
            site_source.find("return branch !== \"main\";") != std::string::npos &&
            site_source.find("const commitUpdatesAllowed = branchAllowsCommitUpdate(versionInfo.currentBranch);") != std::string::npos &&
            site_source.find("ignoreLocalBuildMetadata: !commitUpdatesAllowed") != std::string::npos &&
            site_source.find("main branch requires a newer tagged release for update notification") != std::string::npos &&
            site_source.find("main_commit_diff_without_release") != std::string::npos &&
            site_source.find("const branchCommitComparisonHasPriority = versionInfo.branchState === \"branch\"") != std::string::npos &&
            site_source.find("commitResult.targetBranch === versionInfo.currentBranch") != std::string::npos &&
            site_source.find("Update check using same-branch commit comparison priority over semantic version metadata.") != std::string::npos &&
            site_source.find("if (!semanticResult.useCommitFallback)") != std::string::npos &&
            site_source.find("Update check using commit fallback: ${semanticResult.reason}") != std::string::npos &&
            site_source.find("const commitResult = await buildCommitBasedWsprryPiUpdateResult(versionInfo, semanticResult);") != std::string::npos,
        "update checker must use GitHub release semver as the primary update signal, normalize and validate prerelease identifiers, handle stable/prerelease ordering and numeric prerelease segments, keep prerelease updates on the same channel by default, treat local newer versions as no-update/local-ahead, fall back to commit checks for extra commits, invalid local semver, malformed prerelease semver, or unavailable release data, and expose comparison metadata");
    require(
        site_source.find("const UPDATE_CHECK_FAILURE_CACHE_PREFIX = \"wsprrypi.updateCheckFailure\";") != std::string::npos &&
            site_source.find("const UPDATE_CHECK_FAILURE_RATE_LIMIT_MS = 5 * 60 * 1000;") != std::string::npos &&
        site_source.find("const UPDATE_CHECK_ERROR_MESSAGES = Object.freeze({") != std::string::npos &&
            site_source.find("missing_version_data: \"Update check failed: local version metadata is incomplete.\"") != std::string::npos &&
            site_source.find("missing_commit: \"Update check failed: local commit metadata is missing.\"") != std::string::npos &&
            site_source.find("missing_branch: \"Update check failed: local branch metadata is missing.\"") != std::string::npos &&
            site_source.find("branch_missing: \"Update check failed: the update branch could not be found on GitHub.\"") != std::string::npos &&
            site_source.find("rate_limited: \"Update check failed: GitHub API rate limit reached.\"") != std::string::npos &&
            site_source.find("network: \"Update check failed: GitHub could not be reached.\"") != std::string::npos &&
            site_source.find("malformed_response: \"Update check failed: GitHub returned malformed update data.\"") != std::string::npos &&
            site_source.find("detached_target_unknown: \"Update check failed: detached or unknown branch state has no safe update target.\"") != std::string::npos &&
            site_source.find("function buildUpdateCheckFailure(code, detail = \"\")") != std::string::npos &&
            site_source.find("function normalizeUpdateCheckFailure(error)") != std::string::npos &&
            site_source.find("function updateCheckFailureCacheKey(versionInfo)") != std::string::npos &&
            site_source.find("`${UPDATE_CHECK_FAILURE_CACHE_PREFIX}:`") != std::string::npos &&
            site_source.find("function readUpdateCheckFailureCache(versionInfo)") != std::string::npos &&
            site_source.find("cached.updateCheckFailed !== true") != std::string::npos &&
            site_source.find("Date.now() - Number(cached.checkedAt || 0) >= UPDATE_CHECK_FAILURE_RATE_LIMIT_MS") != std::string::npos &&
            site_source.find("function writeUpdateCheckFailureCache(versionInfo, error)") != std::string::npos &&
            site_source.find("window.localStorage.removeItem(updateCheckFailureCacheKey(versionInfo));") != std::string::npos &&
            site_source.find("const cachedFailure = options.bypassCache === true ? null : readUpdateCheckFailureCache(versionInfo);") != std::string::npos &&
            site_source.find("Update check failure rate limit active") != std::string::npos &&
            site_source.find("markWsprryPiUpdateCheckFailed(cachedFailure);") != std::string::npos &&
            site_source.find("const failure = writeUpdateCheckFailureCache(versionInfo, error);") != std::string::npos &&
            site_source.find("function markWsprryPiUpdateCheckFailed(error)") != std::string::npos &&
            site_source.find("versionElement.classList.add(\"update-check-failed\");") != std::string::npos &&
            site_source.find("markWsprryPiUpdateCheckFailed(failure);") != std::string::npos &&
            site_source.find("writeFailedUpdateCheckCache") == std::string::npos &&
            site_source.find("Site-global key: do not include page path") != std::string::npos &&
            site_source.find("return `${UPDATE_CHECK_CACHE_PREFIX}:${versionInfo.branchState || \"branch\"}:${versionInfo.currentBranch}:${versionInfo.currentSha}:${dirtyKey}`;") != std::string::npos &&
            site_source.find("Use the same site-global identity as successful checks") != std::string::npos,
        "update checker failures must have explicit UI and console states, must classify local metadata, branch, network, rate-limit, malformed-response, and detached-target failures, must not cache failed checks as no-update results, may rate-limit repeated failed checks while preserving failed state, and must clear matching failure state when a successful result is cached");
    require(
        site_source.find("function buildLocalUpdateStateTitle(result)") != std::string::npos &&
            site_source.find("result?.versionComparisonStatus === \"local_modified\"") != std::string::npos &&
            site_source.find("result?.localBuildState === \"dirty_build\"") != std::string::npos &&
            site_source.find("Local build has modifications. No remote update is being shown.") != std::string::npos &&
            site_source.find("result?.versionComparisonStatus === \"local_ahead\"") != std::string::npos &&
            site_source.find("Local build is newer than the selected remote version. No update is available.") != std::string::npos &&
            site_source.find("function markWsprryPiLocalUpdateState(result)") != std::string::npos &&
            site_source.find("clearWsprryPiUpdateFooter();\n        return;") != std::string::npos &&
            site_source.find("versionElement.classList.remove(\"update-available\");\n        versionElement.classList.remove(\"update-check-failed\");\n        versionElement.title = title;") != std::string::npos &&
            site_source.find("updateLink.classList.add(\"d-none\");\n        updateLink.href = UPDATE_CHECK_RELEASES_URL;\n        updateLink.title = title;\n        updateLink.setAttribute(\"aria-label\", title);") != std::string::npos &&
            site_source.find("displayState=${localStateTitle}") != std::string::npos &&
            site_source.find("markWsprryPiLocalUpdateState(result);\n        return;\n    }\n\n    markWsprryPiUpdateFooter(result);") != std::string::npos &&
            site_source.find("if (options.suppressModal !== true)") != std::string::npos &&
            site_source.find("showWsprryPiUpdateModal(versionInfo, result);") != std::string::npos &&
            site_source.find("markWsprryPiUpdateCheckFailed(failure);") != std::string::npos &&
            site_source.find("writeUpdateCheckCache(versionInfo, result);") != std::string::npos,
        "footer update display must keep update-available behavior and modal timing unchanged, clear warning/update indicators for clean no-update, label dirty/local-modified and local-ahead states distinctly in title/ARIA without showing the modal, show failed/unknown checks through the failed-check path, and avoid caching check failures as no-update");
    const std::size_t update_footer_pos =
        site_source.find("markWsprryPiUpdateFooter(result);");
    const std::size_t update_modal_pos =
        site_source.find("showWsprryPiUpdateModal(versionInfo, result);");
    require(
        site_source.find("const UPDATE_MODAL_STATE_KEY = \"wsprrypi.updateModalState\";") != std::string::npos &&
            site_source.find("const UPDATE_CHECK_DISABLED_KEY = \"wsprrypi.updateCheckDisabled\";") != std::string::npos &&
            site_source.find("const UPDATE_MODAL_RATE_LIMIT_MS = 2 * 60 * 60 * 1000;") != std::string::npos &&
            site_source.find("let fallbackUpdateModalState = null;") != std::string::npos &&
            site_source.find("let activeUpdateModalIdentity = null;") != std::string::npos &&
            site_source.find("function updateModalIdentity(versionInfo, result)") != std::string::npos &&
            site_source.find("Modal rate limiting is also site-global; the current page path is not") != std::string::npos &&
            site_source.find("branch: result.targetBranch || \"\",") != std::string::npos &&
            site_source.find("currentSha: versionInfo.currentSha || result.currentSha || \"\",") != std::string::npos &&
            site_source.find("targetSha: result.targetHeadSha || result.remoteVersionSelected || \"\",") != std::string::npos &&
            site_source.find("updateUrl: result.releaseUrl || UPDATE_CHECK_RELEASES_URL") != std::string::npos &&
            site_source.find("function updateModalStateMatches(state, identity)") != std::string::npos &&
            site_source.find("state.targetSha === identity.targetSha") != std::string::npos &&
            site_source.find("state.updateUrl === identity.updateUrl") != std::string::npos &&
            site_source.find("function shouldShowUpdateModal(versionInfo, result)") != std::string::npos &&
            site_source.find("if (!updateModalStateMatches(state, identity))") != std::string::npos &&
            site_source.find("if (lastSeenAt > Date.now())") != std::string::npos &&
            site_source.find("return Date.now() - lastSeenAt >= UPDATE_MODAL_RATE_LIMIT_MS;") != std::string::npos &&
            site_source.find("function handleUpdateCheckStorageEvent(event)") != std::string::npos &&
            site_source.find("event.key === UPDATE_CHECK_DISABLED_KEY") != std::string::npos &&
            site_source.find("event.key !== UPDATE_MODAL_STATE_KEY || !activeUpdateModalIdentity") != std::string::npos &&
            site_source.find("updateModalStateMatches(state, activeUpdateModalIdentity)") != std::string::npos &&
            site_source.find("state.reason === \"dismissed\" || state.reason === \"opened\"") != std::string::npos &&
            site_source.find("writeUpdateModalState(versionInfo, result, \"shown\");") != std::string::npos &&
            site_source.find("activeUpdateModalIdentity = updateModalIdentity(versionInfo, result);") != std::string::npos &&
            site_source.find("writeUpdateModalState(versionInfo, result, \"dismissed\");") != std::string::npos &&
            site_source.find("window.localStorage.setItem(UPDATE_MODAL_STATE_KEY, JSON.stringify(state));") != std::string::npos &&
            site_source.find("Never check again (re-enable in About)") != std::string::npos &&
            site_source.find("setUpdateCheckDisabled(true);") != std::string::npos &&
            site_source.find("Site-global localStorage preference. When enabled, checkForWsprryPiUpdate()") != std::string::npos &&
            site_source.find("returns before any GitHub update-check API calls are made.") != std::string::npos &&
            site_source.find("Footer About is the user-facing re-enable path after \"Never check again\".") != std::string::npos &&
            site_source.find("The footer About toggle can remove this state and re-enable checks.") != std::string::npos &&
            site_source.find("if (isUpdateCheckDisabled())") != std::string::npos &&
            site_source.find("Update checks disabled by user preference.") != std::string::npos &&
            site_source.find("function markWsprryPiUpdateChecksDisabled()") != std::string::npos &&
            site_source.find("function initUpdateCheckControls()") != std::string::npos &&
            site_source.find("function renderUpdateCheckPanel(versionInfo = null, result = null)") != std::string::npos &&
            site_source.find("function renderUpdateCheckPanelFailure(error, versionInfo = null)") != std::string::npos &&
            site_source.find("function renderUpdateCheckPanelDisabled()") != std::string::npos &&
            site_source.find("function getUserFacingUpdateSummary(result = null)") != std::string::npos &&
            site_source.find("function updateCheckPanelTitleText(result = null)") != std::string::npos &&
            site_source.find("function updateCheckPanelHasReleaseLink(result = null)") != std::string::npos &&
            site_source.find("function renderUpdateCheckPanelTitle(elements, result = null, overrideText = \"\")") != std::string::npos &&
            site_source.find("title: document.getElementById(\"updateCheckPanelTitle\"),") != std::string::npos &&
            site_source.find("return \"You are on the current version\";") != std::string::npos &&
            site_source.find("return \"An update is available\";") != std::string::npos &&
            site_source.find("return \"Local build has modifications\";") != std::string::npos &&
            site_source.find("return \"Local build is newer than the latest published version\";") != std::string::npos &&
            site_source.find("elements.title.appendChild(document.createTextNode(\"An update is available: \"));") != std::string::npos &&
            site_source.find("link.href = result.releaseUrl;") != std::string::npos &&
            site_source.find("link.textContent = result.remoteVersionSelected;") != std::string::npos &&
            site_source.find("result.versionComparisonUsed === \"semver\"") != std::string::npos &&
            site_source.find("renderUpdateCheckPanelTitle(elements, result);") != std::string::npos &&
            site_source.find("renderUpdateCheckPanelTitle(elements, null, \"Unable to check for updates\");") != std::string::npos &&
            site_source.find("renderUpdateCheckPanelTitle(elements, null, \"Update checks are disabled\");") != std::string::npos &&
            site_source.find("return \"You are on the current version.\";") == std::string::npos &&
            site_source.find("return \"An update is available.\";") == std::string::npos &&
            site_source.find("return \"Local build has modifications.\";") == std::string::npos &&
            site_source.find("return \"Local build is newer than the latest published version.\";") == std::string::npos &&
            site_source.find("renderUpdateCheckPanelTitle(elements, null, \"Unable to check for updates.\");") == std::string::npos &&
            site_source.find("renderUpdateCheckPanelTitle(elements, null, \"Update checks are disabled.\");") == std::string::npos &&
            site_source.find("function buildTechnicalDetails(versionInfo = null, result = null, failure = null)") != std::string::npos &&
            site_source.find("function renderUpdateCheckTechnicalDetails(elements, details)") != std::string::npos &&
            site_source.find("function appendUpdateCheckCodeText(parent, value)") != std::string::npos &&
            site_source.find("function appendUpdateCheckLinkText(parent, value)") != std::string::npos &&
            site_source.find("function formatUpdateCheckTitleCase(value)") != std::string::npos &&
            site_source.find("function formatUpdateCheckSentence(value)") != std::string::npos &&
            site_source.find("function buildUpdateCheckTargetParts(result = null)") != std::string::npos &&
            site_source.find("const code = document.createElement(\"code\");") != std::string::npos &&
            site_source.find("const link = document.createElement(\"a\");") != std::string::npos &&
            site_source.find("link.href = value;") != std::string::npos &&
            site_source.find("label: \"Branch\", value: result.targetBranch") != std::string::npos &&
            site_source.find("label: \"Commit\", value: shortSha(result.targetHeadSha)") != std::string::npos &&
            site_source.find("appendUpdateCheckTechnicalDetail(\n        details,\n        \"Current\",\n        updateCheckPanelCurrentText(versionInfo),\n        { code: true }\n    );") != std::string::npos &&
            site_source.find("appendUpdateCheckTechnicalDetail(\n        details,\n        \"Branch\",\n        versionInfo?.currentBranch || result?.currentBranch,\n        { code: true }\n    );") != std::string::npos &&
            site_source.find("appendUpdateCheckTechnicalDetail(\n        details,\n        \"Current SHA\",\n        versionInfo?.currentSha || result?.currentSha,\n        { code: true }\n    );") != std::string::npos &&
            site_source.find("appendUpdateCheckTechnicalDetail(details, \"Update URL\", result?.releaseUrl, { link: true });") != std::string::npos &&
            site_source.find("formatUpdateCheckTitleCase(result?.versionComparisonUsed)") != std::string::npos &&
            site_source.find("formatUpdateCheckSentence(result?.versionComparisonStatus)") != std::string::npos &&
            site_source.find("result?.localVersionParsed || formatUpdateCheckSemver(versionInfo?.localVersionParsedObject),\n        { code: true }") != std::string::npos &&
            site_source.find("appendUpdateCheckTechnicalParts(details, \"Target\", targetParts);") != std::string::npos &&
            site_source.find("description.appendChild(document.createTextNode(\" - \"));") != std::string::npos &&
            site_source.find("description.appendChild(document.createTextNode(`${part.label}: `));") != std::string::npos &&
            site_source.find("appendUpdateCheckCodeText(description, part.value);") != std::string::npos &&
            site_source.find("appendUpdateCheckLinkText(description, detail.value);") != std::string::npos &&
            site_source.find("label: \"Summary\"") != std::string::npos &&
            site_source.find("Technical details ▼") != std::string::npos &&
            site_source.find("Technical details ▲") != std::string::npos &&
            site_source.find("A newer version is available.") != std::string::npos &&
            site_source.find("You are running the latest version.") != std::string::npos &&
            site_source.find("This build includes local modifications.") != std::string::npos &&
            site_source.find("This build is newer than the latest published version.") != std::string::npos &&
            site_source.find("Unable to check for updates.") != std::string::npos &&
            site_source.find("Update checks are disabled.") != std::string::npos &&
            site_source.find("dedupeUpdateCheckTechnicalDetails") != std::string::npos &&
            site_source.find("function forceUpdateCheckNow()") != std::string::npos &&
            site_source.find("bypassCache: true") != std::string::npos &&
            site_source.find("suppressModal: true") == std::string::npos &&
            site_source.find("const checkNowButton = document.getElementById(\"updateCheckNowBtn\");") != std::string::npos &&
            site_source.find("checkNowButton.addEventListener(\"click\", forceUpdateCheckNow);") != std::string::npos &&
            site_source.find("renderUpdateCheckPanel(versionInfo, result);") != std::string::npos &&
            site_source.find("renderUpdateCheckPanelFailure(failure, versionInfo);") != std::string::npos &&
            site_source.find("lastWsprryPiVersionResponse = response;") != std::string::npos &&
            site_source.find("event.key?.startsWith(`${UPDATE_CHECK_CACHE_PREFIX}:`)") != std::string::npos &&
            site_source.find("window.addEventListener(\"storage\", handleUpdateCheckStorageEvent);") != std::string::npos &&
            site_source.find("fallbackUpdateModalState = state;") != std::string::npos &&
            update_footer_pos != std::string::npos &&
            update_modal_pos != std::string::npos &&
            update_footer_pos < update_modal_pos &&
            site_source.find("const UPDATE_CHECK_DISMISS_PREFIX") == std::string::npos &&
            site_source.find("updateDismissalKey") == std::string::npos &&
            site_source.find("${status.details} ${result.selectionReason}") == std::string::npos,
        "update-available modal display must use a separate two-hour site-global localStorage state keyed by branch/current SHA/target SHA/update URL, treat future timestamps as expired, allow immediate display when the update URL changes or after unrelated failures, fall back safely when localStorage is unavailable, propagate matching dismissal across tabs, support a site-global never-check-again state, and keep the footer indicator independent from modal suppression");
    require(
        site_source.find("GitHub network request failed") != std::string::npos &&
            site_source.find("response.headers.get(\"x-ratelimit-remaining\") === \"0\"") != std::string::npos &&
            site_source.find("GitHub returned malformed JSON") != std::string::npos &&
            site_source.find("GitHub compare response did not include a status") != std::string::npos &&
            site_source.find("GitHub branch ${branch} did not include a HEAD SHA") != std::string::npos &&
            site_source.find("Semantic version flow is primary when the local build is at a parseable") != std::string::npos &&
            site_source.find("comparison is fallback only and must not override a valid semantic") != std::string::npos,
        "update checker must surface GitHub network, rate-limit, malformed JSON, malformed compare, and malformed branch responses while documenting that semver is primary and commit/branch comparison is fallback");
    require(
        site_source.find("async function isCurrentShaReachableFromBranchHead(currentSha, branchInfo)") != std::string::npos &&
            site_source.find("normalizedCurrent.length >= 40 && normalizedHead.length >= 40 && normalizedCurrent === normalizedHead") != std::string::npos &&
            site_source.find("status: \"short_sha_match\"") != std::string::npos &&
            site_source.find("uncertain: true") != std::string::npos &&
            site_source.find("GitHub compare direction is base=currentSha, head=branch HEAD.") != std::string::npos &&
            site_source.find("status \"ahead\" means the branch HEAD is ahead of") != std::string::npos &&
            site_source.find("Status\n        // \"behind\" means currentSha is ahead of the branch and is not contained.") != std::string::npos &&
            site_source.find("contained: true,") != std::string::npos &&
            site_source.find("status: \"identical\"") != std::string::npos &&
            site_source.find("contained: status === \"identical\" || status === \"ahead\",") != std::string::npos &&
            site_source.find("contained: status === \"identical\" || status === \"behind\",") == std::string::npos &&
            site_source.find("if (status === \"ahead\" || status === \"diverged\")") == std::string::npos &&
            site_source.find("function selectedUpdateBranch(branchInfo, reason, fallbackUsed = false)") != std::string::npos &&
            site_source.find("selectionReason: reason") != std::string::npos &&
            site_source.find("async function selectGithubUpdateBranch(versionInfo)") != std::string::npos &&
            site_source.find("const currentBranch = versionInfo.currentBranch;") != std::string::npos &&
            site_source.find("Rule 1: local main tracks upstream main directly.") != std::string::npos &&
            site_source.find("if (currentBranch === \"main\")") != std::string::npos &&
            site_source.find("selectedUpdateBranch(await lookupGithubBranch(\"main\"), \"local main targets upstream main\")") != std::string::npos &&
            site_source.find("Rule 2: local devel tracks upstream devel unless the local commit is") != std::string::npos &&
            site_source.find("if (currentBranch === \"devel\")") != std::string::npos &&
            site_source.find("develBranch = await lookupGithubBranch(\"devel\");") != std::string::npos &&
            site_source.find("const mainBranch = await lookupGithubBranch(\"main\");") != std::string::npos &&
            site_source.find("const mainContainment = await isCurrentShaReachableFromBranchHead(versionInfo.currentSha, mainBranch);") != std::string::npos &&
            site_source.find("if (mainContainment.contained)") != std::string::npos &&
            site_source.find("current SHA is reachable from main") != std::string::npos &&
            site_source.find("current SHA is not reachable from main") != std::string::npos &&
            site_source.find("main containment probe failed") != std::string::npos &&
            site_source.find("Update check local devel target remains upstream devel.") != std::string::npos &&
            site_source.find("upstream devel missing; explicit fallback to upstream main") != std::string::npos &&
            site_source.find("local devel commit reachable from upstream main") != std::string::npos &&
            site_source.find("selectedUpdateBranch(develBranch, \"local devel targets upstream devel\")") != std::string::npos &&
            site_source.find("Rule 3: feature and release local branches target the same-name upstream") != std::string::npos &&
            site_source.find("Detached HEAD and unknown branch-state builds are handled") != std::string::npos &&
            site_source.find("await lookupGithubBranch(currentBranch),") != std::string::npos &&
            site_source.find("local branch targets same-name upstream branch") != std::string::npos &&
            site_source.find("Rule 4: if a non-main/non-devel branch is missing upstream,") != std::string::npos &&
            site_source.find("missing; explicit fallback to upstream devel") != std::string::npos &&
            site_source.find("missing branch alone does not imply an update") != std::string::npos &&
            site_source.find("fallbackUsed: selectedBranch.fallbackUsed === true,") != std::string::npos &&
            site_source.find("selectionReason: selectedBranch.selectionReason || \"\"") != std::string::npos &&
            site_source.find("? { updateAvailable: true }") == std::string::npos &&
            site_source.find("const selectedBranch = await selectGithubUpdateBranch(versionInfo);") != std::string::npos &&
            site_source.find("reason=${result.selectionReason || \"unspecified\"}") != std::string::npos,
        "update checker must target upstream main for local main, target upstream devel by default for local devel, switch devel to main only when compare current...main proves reachability, avoid treating ahead/diverged/malformed/404 containment probes as contained, prefer full SHA exact matches, explicitly mark short-SHA containment as uncertain, target same-name upstream branches for feature branches, keep detached/unknown branch-state builds out of same-name branch lookup, and make missing-branch fallback explicit and reasoned without forcing a false update");
    require(
        site_source.find("local devel falling back to upstream main because upstream devel returned HTTP 404") != std::string::npos &&
            site_source.find("local devel resolved to upstream main because current SHA is reachable from main") != std::string::npos &&
            site_source.find("local devel staying on upstream devel because current SHA is not reachable from main") != std::string::npos &&
            site_source.find("local devel target remains upstream devel") != std::string::npos,
        "update checker must log devel fallback, devel-to-main reachability resolution, and devel-stays-devel decisions");
    require(
        site_source.find("async function resolveReleaseTargetSha(targetCommitish, memo = null)") != std::string::npos &&
            site_source.find("if (memo instanceof Map && memo.has(target))") != std::string::npos &&
            site_source.find("return memo.get(target);") != std::string::npos &&
            site_source.find("return rememberResolvedTarget((await lookupGithubBranch(target)).headSha);") != std::string::npos &&
            site_source.find("`${UPDATE_CHECK_API_BASE}/git/ref/tags/${encodeURIComponent(target)}`") != std::string::npos &&
            site_source.find("const resolvedTargets = new Map();") != std::string::npos &&
            site_source.find("resolvedSha = await resolveReleaseTargetSha(target, resolvedTargets);") != std::string::npos,
        "release matching must resolve each target_commitish once per scan, try branch lookup before tag lookup, and preserve tag dereferencing");
    require(
        site_source.find("const rawBackendBranch = typeof response?.wspr_branch === \"string\"") != std::string::npos &&
            site_source.find("const rawVersion = typeof response?.wspr_version_raw === \"string\"") != std::string::npos &&
            site_source.find("const branchFieldPresent = Object.prototype.hasOwnProperty.call(response || {}, \"wspr_branch\");") != std::string::npos &&
            site_source.find("const commitFieldPresent = Object.prototype.hasOwnProperty.call(response || {}, \"wspr_commit\");") != std::string::npos &&
            site_source.find("const branchStateFieldPresent = Object.prototype.hasOwnProperty.call(response || {}, \"wspr_branch_state\");") != std::string::npos &&
            site_source.find("const structuredVersionPresent = Object.prototype.hasOwnProperty.call(response || {}, \"wspr_version_parsed\");") != std::string::npos &&
            site_source.find("const branchState = parseBranchState(response, currentBranch);") != std::string::npos &&
            site_source.find("const rawBackendCommit = typeof response?.wspr_commit === \"string\"") != std::string::npos &&
            site_source.find("const rawExeVersion = rawVersion || (typeof response?.wspr_exe_version === \"string\"") != std::string::npos &&
            site_source.find("const currentDisplayVersion = rawDisplayVersion || rawUiVersion;") != std::string::npos &&
            site_source.find("Update-check precedence is structured backend metadata first.") != std::string::npos &&
            site_source.find("currentModalVersion: rawExeVersion || rawUiVersion || rawDisplayVersion,") != std::string::npos &&
            site_source.find("const displayBranch = branchMatch ? branchMatch[1].trim() : \"\";") != std::string::npos &&
            site_source.find("const currentBranch = rawBackendBranch || displayBranch;") != std::string::npos &&
            site_source.find("if (branchFieldPresent && !rawBackendBranch)") != std::string::npos &&
            site_source.find("if (commitFieldPresent && !backendCommit)") != std::string::npos &&
            site_source.find("structuredVersionPresent && response?.wspr_version_parsed?.valid === true && !localVersionParsedObject") != std::string::npos &&
            site_source.find("rawBranchState !== \"branch\"") != std::string::npos &&
            site_source.find("localVersionParsedObject,") != std::string::npos &&
            site_source.find("branchState,") != std::string::npos &&
            site_source.find("displayBranch,") != std::string::npos &&
            site_source.find("rawBranch=${versionInfo.currentBranch}") != std::string::npos &&
            site_source.find("version-check") == std::string::npos &&
            site_source.find("version_check") == std::string::npos,
        "update checker must prefer structured backend version, branch, branch-state, commit, and dirty fields, keep display text independent, use display-string parsing only as a legacy fallback when structured fields are absent, and fail malformed present structured fields instead of guessing");
    require(
        site_source.find("const summaryText = result.versionComparisonUsed === \"semver\" && result.remoteVersionSelected") != std::string::npos &&
            site_source.find("`${result.localVersionParsed || versionInfo.currentModalVersion} is behind release ${result.remoteVersionSelected}.`") != std::string::npos &&
            site_source.find("`${versionInfo.currentModalVersion} is behind ${formatUpdateModalBranchTarget(result)}.`") != std::string::npos &&
            site_source.find("`${versionInfo.currentDisplayVersion} is behind ${result.targetBranch} ${targetShaLabel}.`") == std::string::npos &&
            site_source.find("function isTaggedReleaseUpdate(result)") != std::string::npos &&
            site_source.find("Boolean(result?.releaseTitle)") != std::string::npos &&
            site_source.find("Boolean(result?.remoteVersionSelected)") != std::string::npos &&
            site_source.find("function formatUpdateModalBranchTarget(result)") != std::string::npos &&
            site_source.find("return sha ? `${branch}+${sha}` : branch;") != std::string::npos &&
            site_source.find("const taggedReleaseUpdate = isTaggedReleaseUpdate(result);") != std::string::npos &&
            site_source.find("Review the update channel given to you for this pre-release version.") != std::string::npos &&
            site_source.find("if (taggedReleaseUpdate) {\n        appendUpdateModalBodyLink(body, result, taggedReleaseUpdate);") != std::string::npos &&
            site_source.find(".toggleClass(\"d-none\", !taggedReleaseUpdate)") != std::string::npos &&
            site_source.find(".text(\"View release\")") != std::string::npos &&
            site_source.find("Review the latest releases: ") == std::string::npos &&
            site_source.find("Review the latest WsprryPi releases before updating.") == std::string::npos,
        "update modal must use semantic release wording for semver updates, preserve wspr_exe_version in commit fallback summaries, explain fallback checks, and suppress exact-release wording when fallback is used");
    require(
        site_source.find("await lookupGithubBranch(currentBranch),") != std::string::npos &&
            site_source.find("if (error.status !== 404)") != std::string::npos &&
            site_source.find("await lookupGithubBranch(\"devel\"),") != std::string::npos &&
            site_source.find("same-name upstream branch '${currentBranch}' missing; explicit fallback to upstream devel") != std::string::npos,
        "update checker must compare existing non-main/non-devel branches against that branch and fall back to devel only on a true 404 with an explicit reason");
    require(
        site_source.find("Update check parsed displayBranch=") != std::string::npos &&
            site_source.find("Update check branch lookup:") != std::string::npos &&
            site_source.find("Update check branch lookup failed for") != std::string::npos &&
            site_source.find("Update check branch lookup result for") != std::string::npos &&
            site_source.find("Update check selected targetBranch=") != std::string::npos,
        "update checker must log parsed display branch, raw branch/SHA, branch lookup URL/status/result, selected target branch, fallback state, and target HEAD for debugging");
    require(
        ui_source.find("function isWsprConfigMode()") != std::string::npos &&
            ui_source.find("const useNtp = isWsprMode && $(\"#use_ntp\").is(\":checked\");") != std::string::npos &&
            ui_source.find("backend === \"gpio\" && $(\"#use_ntp\").is(\":checked\");") == std::string::npos &&
            ui_source.find("$(\"#ntp_calibration_control\")") != std::string::npos &&
            ui_source.find("$ppm.prop(\"disabled\", !isWsprMode || useNtp);") != std::string::npos &&
            ui_source.find("$ppmCw.prop(\"disabled\", isWsprMode);") != std::string::npos &&
            ui_source.find("syncCalibrationControls();") != std::string::npos,
        "config UI must make calibration controls mode-aware so WSPR follows NTP while CW keeps PPM editable");

    require(
        config_view_source.find("id=\"modeChangeGuardModal\"") != std::string::npos &&
            config_view_source.find("id=\"modeChangeGuardConfirmBtn\"") != std::string::npos,
        "Configuration view must expose the guarded mode-change confirmation modal");
    require(
        config_view_source.find("Amp Control") != std::string::npos &&
            config_view_source.find("Activate Amp:") != std::string::npos &&
            config_view_source.find("Amp Pin") != std::string::npos &&
            config_view_source.find("Active High") != std::string::npos &&
            config_view_source.find("Control an external amplifier by activating it prior to transmitting and deactivating it after the transmission is complete.") != std::string::npos,
        "Configuration Pi I/O view must expose Activate Amp, Amp Pin, Active High, and the full amplifier-control description");
    require(
        site_source.find("\"Use Amp\": { required: false, type: \"boolean\" }") != std::string::npos &&
            site_source.find("\"Amp Pin\": { required: false, type: \"number\" }") != std::string::npos &&
            site_source.find("\"Amp Pin Active High\": { required: false, type: \"boolean\" }") != std::string::npos,
        "site.js config schema must accept Use Amp, Amp Pin, and Amp Pin Active High");
    require(
        stock_ini_source.find("Use Amp = false") != std::string::npos &&
            stock_ini_source.find("Amp Pin =") != std::string::npos &&
            stock_ini_source.find("Amp Pin Active High = false") != std::string::npos,
        "stock INI must explicitly include disabled Use Amp, Amp Pin, and Amp Pin Active High fields");
    require(
        ui_source.find("function setUseAmp(enabled)") != std::string::npos &&
            ui_source.find("function getUseAmp()") != std::string::npos &&
            ui_source.find("\"Use Amp\": use_amp") != std::string::npos &&
            ui_source.find("\"Amp Pin\": amp_pin") != std::string::npos &&
            ui_source.find("\"Amp Pin Active High\": amp_pin_active_high") != std::string::npos &&
            ui_source.find("const amp_pin = Number.isInteger(amp_pin_value) ? amp_pin_value : -1;") != std::string::npos &&
            ui_source.find("getUseAmp() ? getAmpPin() : null") != std::string::npos &&
            ui_source.find("function validateGpioConflictFields()") != std::string::npos,
        "index.js must serialize Use Amp, retain Amp Pin as data, include Amp Pin Active High, and validate Amp GPIO conflicts only when enabled");
    const std::string footer_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/footer.php");
    const std::string site_css_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/site.css");
    const std::string header_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/header.php");
    const std::string index_page_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/index.php");
    const std::string script_include_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/site.js.includes.php");
    const std::string ui_version_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/ui_version.php");
    const std::string html_cache_headers_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/html_cache_headers.php");
    const std::string version_endpoint_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/version.php");
    const std::string fetch_spots_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/fetch_spots.php");
    const std::string log_stream_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/log_stream.php");
    const std::string diagnostic_logs_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/view_diag_logs.php");
    require(
        footer_source.find("<details class=\"footer-meta\">") != std::string::npos &&
            footer_source.find("<summary>About</summary>") != std::string::npos &&
            footer_source.find("id=\"updateCheckToggle\"") != std::string::npos &&
            footer_source.find("Disable update checks") != std::string::npos &&
            site_source.find("toggle.textContent = disabled ? \"Enable update checks\" : \"Disable update checks\";") != std::string::npos &&
            site_css_source.find(".footer-meta__action") != std::string::npos,
        "footer markup must keep the native click-based About disclosure and provide a site-global update-check toggle for disabling or re-enabling checks");
    require(
        header_source.find("window.WSPRRYPI_UI_VERSION = <?= json_encode(getWsprryPiUiVersion()) ?>;") != std::string::npos &&
            header_source.find("window.WSPRRYPI_UI_BUILD_ID = <?= json_encode(getWsprryPiUiBuildId()) ?>;") != std::string::npos &&
            ui_version_source.find("function getWsprryPiUiVersion(): string") != std::string::npos &&
            ui_version_source.find("function wsprrypiUiBuildFileRecords(): array") != std::string::npos &&
            ui_version_source.find("function getWsprryPiUiBuildId(): string") != std::string::npos &&
            ui_version_source.find("'view_diag_logs.php' => true") != std::string::npos &&
            ui_version_source.find("'cache' => true") != std::string::npos &&
            ui_version_source.find("'%s|%d|%d'") != std::string::npos &&
            ui_version_source.find("'mtime-' . substr(hash('sha256', implode(\"\\n\", $records)), 0, 16)") != std::string::npos &&
            ui_version_source.find("function wsprrypiAssetUrl(string $path): string") != std::string::npos &&
            ui_version_source.find("'v=' . rawurlencode($buildId)") != std::string::npos &&
            version_endpoint_source.find("'ui_build_id' => $uiBuildId") != std::string::npos &&
            version_endpoint_source.find("Cache-Control") != std::string::npos &&
            version_endpoint_source.find("no-store") != std::string::npos &&
            header_source.find("wsprrypiAssetUrl('site.css')") != std::string::npos &&
            footer_source.find("wsprrypiAssetUrl('site.js')") != std::string::npos &&
            index_page_source.find("wsprrypiAssetUrl($stylesheet)") != std::string::npos &&
            index_page_source.find("wsprrypiAssetUrl($script)") != std::string::npos &&
            script_include_source.find("wsprrypiAssetUrl('vendor/js/jquery-3.7.1.min.js')") != std::string::npos &&
            script_include_source.find("wsprrypiAssetUrl('vendor/js/bootstrap.bundle-5.3.8.min.js')") != std::string::npos,
        "PHP template assets must use the centralized WsprryPi UI build id query string for CSS and JS cache busting, and /version must expose the same no-store UI build id while excluding diagnostic logs and transient paths");
    require(
        html_cache_headers_source.find("header('Cache-Control: no-cache, must-revalidate');") != std::string::npos &&
            index_page_source.find("<?php require_once __DIR__ . '/html_cache_headers.php'; ?>") == 0 &&
            diagnostic_logs_source.find("require_once __DIR__ . '/html_cache_headers.php';") != std::string::npos &&
            diagnostic_logs_source.find("require_once __DIR__ . '/html_cache_headers.php';") < diagnostic_logs_source.find("<!doctype html>") &&
            read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/template.php").find("<?php require_once __DIR__ . '/html_cache_headers.php'; ?>") == 0 &&
            version_endpoint_source.find("html_cache_headers.php") == std::string::npos &&
            fetch_spots_source.find("html_cache_headers.php") == std::string::npos &&
            log_stream_source.find("html_cache_headers.php") == std::string::npos,
        "HTML PHP shell pages must send no-cache revalidation headers before output while JSON/API endpoints keep their existing cache behavior");
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
    const std::string maintenance_css_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/maintenance.css");
    require(
        maintenance_source.find("id=\"test_tone\"") != std::string::npos &&
            maintenance_source.find("id=\"testToneModal\"") != std::string::npos &&
            maintenance_source.find("id=\"testToneFrequencyHz\"") != std::string::npos &&
            maintenance_source.find("Test tone transmit frequency, Hz") != std::string::npos,
        "maintenance view must host the relocated Test Tone control, modal, and editable transmit-frequency field");
    require(
        maintenance_source.find("class=\"maintenance-utility__grid\"") != std::string::npos &&
            maintenance_source.find("class=\"maintenance-pane maintenance-pane--utility\"") != std::string::npos &&
            maintenance_source.find("class=\"maintenance-action maintenance-action--start\"") != std::string::npos &&
            maintenance_source.find("maintenance-action maintenance-action--end") == std::string::npos &&
            maintenance_source.find("id=\"updateCheckPanel\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckPanelTitle\"") != std::string::npos &&
            maintenance_source.find("Review web UI update status.") == std::string::npos &&
            maintenance_source.find("Checking update status</h2>") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckStatus\"") != std::string::npos &&
            maintenance_source.find("class=\"maintenance-update-status visually-hidden\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckCurrent\"") == std::string::npos &&
            maintenance_source.find("id=\"updateCheckTarget\"") == std::string::npos &&
            maintenance_source.find("id=\"updateCheckSummary\"") == std::string::npos &&
            maintenance_source.find("<dt>Current</dt>") == std::string::npos &&
            maintenance_source.find("<dt>Target</dt>") == std::string::npos &&
            maintenance_source.find("<dt>Summary</dt>") == std::string::npos &&
            maintenance_source.find("id=\"updateCheckDetails\"") == std::string::npos &&
            maintenance_source.find("id=\"updateCheckTechnical\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckTechnicalSummary\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckTechnicalList\"") != std::string::npos &&
            maintenance_source.find("Technical details ▼") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckTechnical\" class=\"maintenance-update-technical d-none\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckTechnical\" open") == std::string::npos &&
            maintenance_source.find("id=\"updateCheckAction\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckNowBtn\"") != std::string::npos &&
            maintenance_source.find("id=\"updateCheckToggleBtn\"") != std::string::npos &&
            maintenance_source.find("aria-live=\"polite\"") != std::string::npos &&
            maintenance_css_source.find(".maintenance-utility__grid") != std::string::npos &&
            maintenance_css_source.find("grid-template-columns: repeat(2, minmax(0, 1fr));") != std::string::npos &&
            maintenance_css_source.find(".maintenance-update-status") != std::string::npos &&
            maintenance_css_source.find(".maintenance-update-technical summary") != std::string::npos,
        "maintenance view must split Utility into side-by-side Test Tone and Update Check panels, keep Test Tone left-aligned, expose update-check panel hooks and controls, show user-facing summary text, and keep technical details collapsed by default");

    const std::string maintenance_test_tone_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/maintenance.js");
    require(
        maintenance_test_tone_script_source.find("bindTestToneControls();") != std::string::npos,
        "maintenance view must bind the shared Test Tone controls");
    require(
        site_source.find("function hasActiveManagedTransmissionForTestTone()") != std::string::npos &&
            site_source.find("function hasEnabledManagedTransmissionForTestTone()") != std::string::npos &&
            site_source.find("function handleTestToneCommandResponse(message)") != std::string::npos &&
            site_source.find("const startButton = document.getElementById(\"testToneStart\");") != std::string::npos &&
            site_source.find("toggleButtonLoading(startButton, false);") != std::string::npos &&
            site_source.find("toggleButtonLoading(endButton, false);") != std::string::npos &&
            site_source.find("if (response.started === true) {\n            syncTestToneControlState(true);") != std::string::npos &&
            site_source.find("if (response.stopped !== true) {") != std::string::npos &&
            site_source.find("if (hasActiveManagedTransmissionForTestTone()) {") != std::string::npos &&
            site_source.find("if (hasEnabledManagedTransmissionForTestTone()) {") != std::string::npos &&
            site_source.find("showTestToneBlockedModal(\"active\");") != std::string::npos &&
            site_source.find("showTestToneBlockedModal(\"enabled\");") != std::string::npos &&
            site_source.find("Stop and disable transmissions") != std::string::npos &&
            site_source.find("Disable transmissions") != std::string::npos &&
            site_source.find("Test tones require scheduled transmissions to be stopped and disabled first.") != std::string::npos &&
            site_source.find("const confirmLabel = blockedByActive ? \"Stop and Disable\" : \"Disable\";") != std::string::npos &&
            site_source.find("currentRuntimeConfigStatus.transmitEnabled === true") != std::string::npos &&
            site_source.find("if (msg.command === \"tone_start\" || msg.command === \"tone_end\")") != std::string::npos,
        "shared Test Tone controls must reject active and merely enabled scheduled transmissions with the existing modal path and reconcile websocket command replies");
    require(
        site_source.find("let pendingTestToneStartRequest = false;") != std::string::npos &&
            site_source.find("let pendingTestToneStartTimeoutHandle = null;") != std::string::npos &&
            site_source.find("const TEST_TONE_COMMAND_TIMEOUT_MS = 15000;") != std::string::npos &&
            site_source.find("TEST_TONE_DISABLE_REQUEST_TIMEOUT_MS") == std::string::npos &&
            site_source.find("function markPendingTestToneStartRequest()") != std::string::npos &&
            site_source.find("function clearPendingTestToneStartRequest()") != std::string::npos &&
            site_source.find("pendingTestToneStartTimeoutHandle = window.setTimeout(() => {\n        clearPendingTestToneStartRequest();\n    }, TEST_TONE_COMMAND_TIMEOUT_MS);") != std::string::npos &&
            site_source.find("function testToneFrequencyOverridePayload()") != std::string::npos &&
            site_source.find("const toneStartPayload = {\n        command: \"tone_start\",\n        ...testToneFrequencyOverridePayload()\n    };") != std::string::npos &&
            site_source.find("if (!sendCommand(toneStartPayload))") != std::string::npos &&
            site_source.find("clearPendingTestToneStartRequest();\n        toggleButtonLoading(btn, false);") != std::string::npos &&
            site_source.find("clearPendingTestToneStartRequest();\n        communicationInterrupted = true;") != std::string::npos,
        "Test Tone start requests must use a local per-tab pending flag, include optional frequency override payload data, and clear state on send failure or websocket disconnect");
    require(
        site_source.find("const locallyRequested = pendingTestToneStartRequest === true;") != std::string::npos &&
            site_source.find("clearPendingTestToneStartRequest();\n        if (response.started === true)") != std::string::npos &&
            site_source.find("if (locallyRequested && response.blocked_by_active_transmission === true)") != std::string::npos &&
            site_source.find("} else if (locallyRequested && response.blocked_by_enabled_transmission === true)") != std::string::npos,
        "Test Tone blocked modal handling must be gated by the local pending tone_start request flag");
    require(
        site_source.find("!locallyRequested &&") != std::string::npos &&
            site_source.find("response.blocked_by_active_transmission === true ||\n                    response.blocked_by_enabled_transmission === true") != std::string::npos &&
            site_source.find("Passive test tone start rejection received:") != std::string::npos &&
            site_source.find("syncTestToneControlState(false);") != std::string::npos,
        "passive websocket tone_start rejections must still update Test Tone state without showing blocked modals");
    require(
        site_source.find("function disableScheduledTransmissionsForTestTone(reason, actionButton = null)") != std::string::npos &&
            site_source.find("function handleTestToneStopDisableResponse(message)") != std::string::npos &&
            site_source.find("handleTestToneStopDisableResponse(msg);") != std::string::npos &&
            site_source.find("timeout: TEST_TONE_COMMAND_TIMEOUT_MS") != std::string::npos &&
            site_source.find("}, TEST_TONE_COMMAND_TIMEOUT_MS);") != std::string::npos &&
            site_source.find("response.transmit_disabled === true || response.stop_performed === true") != std::string::npos &&
            site_source.find("command: \"stop\",") != std::string::npos &&
            site_source.find("persist_transmit: true,") != std::string::npos &&
            site_source.find("finishTestToneStopDisableAction(stopSucceeded, responseMessage);") != std::string::npos &&
            websocket_source.find("reply[\"stop_performed\"] = stop_result.stop_performed;") != std::string::npos &&
            websocket_source.find("reply[\"transmit_disabled\"] = stop_result.transmit_disabled;") != std::string::npos &&
            websocket_source.find("reply[\"persist_transmit\"] = persist_transmit;") != std::string::npos &&
            websocket_source.find("reply[\"status\"] = stop_request_succeeded ? \"ok\" : \"error\";") != std::string::npos,
        "active-transmission Test Tone blocked action must use the existing websocket stop command and its stable stop_performed/transmit_disabled response fields");
    require(
        site_source.find("function requestTestToneTransmitDisable()") != std::string::npos &&
            site_source.find("ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {") != std::string::npos &&
            site_source.find("contentType: \"application/merge-patch+json\"") != std::string::npos &&
            site_source.find("\"Transmit\": false,") != std::string::npos &&
            site_source.find("requestTestToneTransmitDisable()\n        .done(function () {\n            finishTestToneStopDisableAction(true);") != std::string::npos,
        "enabled-idle Test Tone blocked action must disable Operation.Transmit through the existing settings PATCH path");
    require(
        site_source.find("cancelLabel: \"Cancel\"") != std::string::npos &&
            site_source.find("onCancel() {\n            },") != std::string::npos &&
            site_source.find("sendCommand(toneStartPayload)") != std::string::npos &&
            site_source.find("disableScheduledTransmissionsForTestTone(\n                    reason,") != std::string::npos &&
            site_source.find("disableScheduledTransmissionsForTestTone(\n                    reason,") <
                site_source.find("sendCommand(toneStartPayload)"),
        "Test Tone blocked modal Cancel must make no state changes and the stop/disable action must not automatically start the test tone");
    require(
        scheduling_source.find("scheduler_managed_transmission_active_for_test_tone()") != std::string::npos &&
            scheduling_source.find("result.blocked_by_active_transmission = true;") != std::string::npos &&
            scheduling_source.find("scheduler_managed_transmission_enabled_for_test_tone()") != std::string::npos &&
            scheduling_source.find("result.blocked_by_enabled_transmission = true;") != std::string::npos,
        "backend-side Test Tone rejection must remain present for active and enabled scheduled transmissions");
    require(
        site_source.find("const normalizedArgs = args.map((arg) => {") != std::string::npos &&
            site_source.find("return JSON.stringify(arg);") != std::string::npos &&
            site_source.find("return Object.prototype.toString.call(arg);") != std::string::npos,
        "debug console logging must serialize array and object payloads into useful text");

    std::cout << "ui_source_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
