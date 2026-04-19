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
        site_source.find("const TAB_STATE_STORAGE_PREFIX = \"wsprrypi.activeTab\";") != std::string::npos &&
            site_source.find("function initPersistedTabState()") != std::string::npos &&
            site_source.find("window.localStorage.setItem(storageKey, selector);") != std::string::npos &&
            site_source.find("restorePersistedTabState(tabList);") != std::string::npos,
        "site.js must persist and restore opted-in Bootstrap tab state through localStorage");

    const std::string config_view_source =
        read_text_file("/home/pi/WsprryPi/WsprryPi-UI/data/views/config.php");
    require(
        config_view_source.find("id=\"band-gpio-enabled-all\"") != std::string::npos &&
            config_view_source.find("id=\"band-gpio-active-high-all\"") != std::string::npos,
        "Band GPIO table must expose bulk-toggle header checkboxes for Enabled and Active High");
    require(
        config_view_source.find("id=\"configTabs\" role=\"tablist\" data-persist-tab-state=\"true\"") != std::string::npos,
        "Configuration tab list must opt into persisted sub-tab state");

    std::cout << "ui_source_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
