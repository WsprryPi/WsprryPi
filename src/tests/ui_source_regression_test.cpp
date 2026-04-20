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

} // namespace

int main()
{
    const std::string websocket_source =
        read_text_file("/home/pi/WsprryPi/src/web_socket.cpp");
    require(
        websocket_source.find("else if (cmd == \"stop\")") != std::string::npos &&
            websocket_source.find("stop_transmission_by_user_request();") !=
                std::string::npos,
        "websocket stop command must route through stop_transmission_by_user_request()");

    const std::string scheduling_source =
        read_text_file("/home/pi/WsprryPi/src/scheduling.cpp");
    require(
        scheduling_source.find("suppress_cancelled_ws_event_for_user_stop.store(true") != std::string::npos &&
            scheduling_source.find("Suppressing websocket canceled event because an explicit user stop will publish stopped.") !=
                std::string::npos,
        "explicit user stop must suppress the intermediate canceled websocket event");
    require(
        scheduling_source.find("void start_test_tone()") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"starting\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"starting\");", scheduling_source.find("void transmitter_cb(")),
        "test tone start must rely on transmitter callback websocket ownership only");
    require(
        scheduling_source.find("void end_test_tone()") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"finished\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"finished\");", scheduling_source.find("void transmitter_cb(")),
        "test tone end must rely on transmitter callback websocket ownership only");

    const std::string ui_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/index.js");
    require(
        ui_source.find("ws.send(JSON.stringify({ command: \"stop\" }))") !=
                std::string::npos,
        "UI Stop button must send the explicit stop command over the websocket");
    require(
        ui_source.find("let pendingModeChange = null;") != std::string::npos &&
            ui_source.find("let pendingPersistedMode = \"\";") != std::string::npos &&
            ui_source.find("function requestConfigModeChange(targetMode)") != std::string::npos &&
            ui_source.find("title: \"Stop transmission to change mode\"") != std::string::npos &&
            ui_source.find("title: \"Disable transmissions to change mode\"") != std::string::npos &&
            ui_source.find("requestTransmitEnabledChange(false, true") != std::string::npos &&
            ui_source.find("const requestedMode = normalizedTargetMode;") != std::string::npos &&
            ui_source.find("finalizePendingModeChange(requestedMode);") != std::string::npos &&
            ui_source.find("syncAutosaveBaselineOnSuccess: false,") != std::string::npos &&
            ui_source.find("if (!stopTransmission()) {") != std::string::npos &&
            ui_source.find("suspendConfigAutosave(true);") != std::string::npos &&
            ui_source.find("input:not(#transmit, [name=\"mode_toggle\"], [name=\"qrss_type\"])") != std::string::npos &&
            ui_source.find("configAutosaveNeedsRuntimeRefresh = true;") != std::string::npos &&
            ui_source.find("pendingPersistedMode = currentConfigModeSelection;") != std::string::npos &&
            ui_source.find("flushAutosave();") != std::string::npos &&
            ui_source.find("if (configAutosaveNeedsRuntimeRefresh && typeof getTxState === \"function\") {") != std::string::npos,
        "mode changes must be guarded behind stop/disable confirmation, preserve the unsaved target mode through the disable step, exclude mode toggles from generic autosave scheduling, and refresh runtime state after the committed mode save lands");
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
            ui_source.find("setConfigSaveStatus(\"saved\", \"Saved\");") != std::string::npos &&
            ui_source.find("configAutosavePendingAfterFlight") != std::string::npos,
        "configuration autosave must debounce saves, clear stale invalid state for already-saved payloads, suppress unchanged failed payload retries, and suppress duplicate writes");
    require(
        ui_source.find("function validateCwMessage()") != std::string::npos &&
            ui_source.find("function validateCwShiftHz()") != std::string::npos &&
            ui_source.find("function parseFrequencyWithOptionalUnits(rawValue)") != std::string::npos &&
            ui_source.find("Enter a positive CW base frequency.") != std::string::npos &&
            ui_source.find("CW message is required.") != std::string::npos &&
            ui_source.find("Enter a positive CW frequency offset.") != std::string::npos &&
            ui_source.find("let cw_message = String($('#qrss_message').val() || \"\").trim();") != std::string::npos &&
            ui_source.find("let cw_base_frequency = parseFrequencyWithOptionalUnits($('#qrss_frequency').val());") != std::string::npos &&
            ui_source.find("let cw_base_frequency = parseFloat($('#qrss_frequency').val());") == std::string::npos &&
            ui_source.find("return value * 1e6;") != std::string::npos &&
            ui_source.find("return value * 1e3;") != std::string::npos &&
            ui_source.find("return value * 1e9;") != std::string::npos &&
            ui_source.find("const value = parseFrequencyWithOptionalUnits(raw);") != std::string::npos,
        "CW autosave validation and save-path serialization must share unit-aware base-frequency parsing and reject parseFloat-based truncation");
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
        site_source.find("Frequency Control GPIO Polarity") == std::string::npos,
        "UI config schema must not require the obsolete GPIO.Frequency Control GPIO Polarity key");
    require(
        site_source.find("let use_ntp = getConfigBoolValue(") != std::string::npos &&
            site_source.find("\"GPIO\",\n                    \"Use NTP\",\n                    true") != std::string::npos &&
            site_source.find("\"GPIO\",\n                    \"Use NTP\",\n                    false") == std::string::npos,
        "UI config loader must default GPIO.Use NTP to true to match backend normalization");
    require(
        site_source.find("runtime_mode") != std::string::npos &&
            site_source.find("nextTransmissionAt") != std::string::npos &&
            site_source.find("cw_message") != std::string::npos &&
            site_source.find("cw_active_char_index") != std::string::npos &&
            site_source.find("if (typeof handleRuntimeStatusUpdate === \"function\") {") != std::string::npos &&
            site_source.find("const selectedMode =\n        typeof selectedConfigMode === \"function\" ? selectedConfigMode() : \"\";") != std::string::npos &&
            site_source.find("function renderCwRuntimeMessage(node, message, activeCharIndex)") != std::string::npos &&
            site_source.find("const isTransmitting =") != std::string::npos &&
            site_source.find("const transmitEnabled =") != std::string::npos &&
            site_source.find("planLabelNode.textContent = \"Message progression\";") != std::string::npos &&
            site_source.find("planLabelNode.textContent = \"Next message at:\";") != std::string::npos &&
            site_source.find("const idleValue = transmitEnabled") != std::string::npos &&
            site_source.find(": \"Disabled\";") != std::string::npos &&
            site_source.find("charNode.textContent = character;") != std::string::npos &&
            site_source.find("renderCwRuntimeMessage(planNode, message, activeCharIndex);") != std::string::npos &&
            site_source.find("charNode.textContent = isActive && character === \" \" ? \"_\" : character;") == std::string::npos,
        "runtime status handling must switch CW runtime detail between next-transmission timing and live message progression without underscore substitution");
    require(
        scheduling_source.find("snapshot.next_transmission_at") != std::string::npos &&
            websocket_source.find("reply[\"next_transmission_at\"] = snapshot.next_transmission_at;") != std::string::npos &&
            scheduling_source.find("if (snapshot.tx_state == \"transmitting\")") != std::string::npos &&
            scheduling_source.find("snapshot.runtime_mode = mode_type_name(config.mode);") != std::string::npos &&
            scheduling_source.find("runtime_transmit_enabled(config)") != std::string::npos &&
            scheduling_source.find("if (config.mode != ModeType::WSPR ||\n        current_transmission_request.mode != TransmissionMode::WSPR ||") != std::string::npos &&
            scheduling_source.find("snapshot.runtime_mode == mode_type_name(config.mode)") == std::string::npos &&
            scheduling_source.find("if (runtime_status.mode != wsprrypi::TransmissionMode::WSPR ||\n        current_transmission_request.mode != TransmissionMode::WSPR ||") == std::string::npos,
        "runtime snapshot must expose next_transmission_at for CW display only when transmissions are actually enabled, follow the committed scheduler mode while idle instead of stale backend execution state, suppress stale WSPR plan data after mode changes, expose committed idle WSPR plan data without requiring runtime mode to already be WSPR, and expose CW next-transmission timing without requiring the transmitter runtime mode string to match first");
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

    const std::string config_view_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/config.php");
    require(
        config_view_source.find("class=\"btn btn-danger btn-sm config-header-stop\"") != std::string::npos &&
            config_view_source.find("id=\"stop_transmit\"") != std::string::npos,
        "Configuration header must host the compact Stop transmission control");
    require(
        config_view_source.find("class=\"config-runtime-header\"") != std::string::npos &&
            config_view_source.find("<label class=\"form-check-label\" for=\"transmit\">Transmit enabled</label>") != std::string::npos &&
            config_view_source.find("id=\"runtime_plan_label\"") != std::string::npos &&
            config_view_source.find("id=\"modeChangeGuardModal\"") != std::string::npos &&
            config_view_source.find("id=\"modeChangeGuardConfirmBtn\"") != std::string::npos,
        "Runtime state header must host the primary Transmit enabled control and the config view must expose the guarded mode-change confirmation modal");
    require(
        config_view_source.find("config-runtime-item config-runtime-item--action") == std::string::npos,
        "Runtime state grid must no longer dedicate a large action tile to Stop transmission");
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
            config_view_source.find("config-wspr-top-row__item config-wspr-top-row__field config-wspr-top-row__planner") != std::string::npos &&
            config_view_source.find("for=\"useoffset\">\n                                                    Randomize\n") != std::string::npos &&
            config_view_source.find("id=\"ppm\"") != std::string::npos &&
            config_view_source.find("id=\"use_ntp\"") != std::string::npos &&
            config_view_source.find("id=\"planner_preference\"") != std::string::npos &&
            config_view_source.find("id=\"dbm\"") != std::string::npos &&
            config_view_source.find("<option value=\"60\">60</option>") != std::string::npos &&
            config_view_source.find("spaces or commas") != std::string::npos &&
            config_view_source.find("@GPIO, @GPIOH, or @GPIOL") != std::string::npos &&
            config_view_source.find("-15") == std::string::npos &&
            config_view_source.find("class=\"form-select config-planner-field__select\"") == std::string::npos,
        "WSPR transmission settings must keep planner_preference in the compact top row and render TX dBm as a fixed-value select");
    require(
        config_view_source.find("id=\"band-gpio-enabled-all\"") != std::string::npos &&
            config_view_source.find("id=\"band-gpio-active-high-all\"") != std::string::npos,
        "Band GPIO table must expose bulk-toggle header checkboxes for Enabled and Active High");
    require(
        config_view_source.find("id=\"configTabs\" role=\"tablist\" data-persist-tab-state=\"true\" data-persist-tab-state-scope=\"reload\"") != std::string::npos,
        "Configuration tab list must opt into reload-scoped persisted sub-tab state");

    const std::string maintenance_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/maintenance.php");
    require(
        maintenance_source.find("id=\"test_tone\"") != std::string::npos &&
            maintenance_source.find("id=\"testToneModal\"") != std::string::npos,
        "maintenance view must host the relocated Test Tone control and modal");

    const std::string maintenance_script_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/maintenance.js");
    require(
        maintenance_script_source.find("bindTestToneControls();") != std::string::npos,
        "maintenance view must bind the shared Test Tone controls");

    std::cout << "ui_source_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
