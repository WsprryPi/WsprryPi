#include <cstdlib>
#include <fstream>
#include <filesystem>
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

    std::string repository_root()
    {
        if (const char *configured = std::getenv("WSPRRYPI_REPO_ROOT"))
        {
            return configured;
        }

        const std::filesystem::path current = std::filesystem::current_path();
        if (std::filesystem::exists(current / "src" / "web_socket.cpp"))
        {
            return current.string();
        }
        if (std::filesystem::exists(current / "web_socket.cpp"))
        {
            return current.parent_path().string();
        }
        return "/home/pi/WsprryPi";
    }

    std::string read_repo_text_file(const std::string &relative_path)
    {
        return read_text_file(repository_root() + relative_path);
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
        read_repo_text_file("/src/web_socket.cpp") +
        read_repo_text_file("/src/web_socket_commands.cpp");
    const std::string websocket_header_source =
        read_repo_text_file("/src/web_socket.hpp");
    const std::string web_server_source =
        read_repo_text_file("/src/web_server.cpp") +
        read_repo_text_file("/src/web_server_version_routes.cpp");
    const std::string makefile_source =
        read_repo_text_file("/src/Makefile");
    const std::string version_source =
        read_repo_text_file("/src/version.cpp");
    const std::string build_metadata_generator_source =
        read_repo_text_file("/scripts/generate_build_metadata.py");
    const std::string version_header_source =
        read_repo_text_file("/src/version.hpp");
    require(
        websocket_source.find("else if (cmd == \"stop\")") != std::string::npos &&
            websocket_source.find("stop_transmission_by_user_request(persist_transmit);") !=
                std::string::npos,
        "websocket stop command must route through stop_transmission_by_user_request() with persistence control");
    require(
        websocket_header_source.find("std::mutex test_tone_command_mutex_;") !=
                std::string::npos &&
            websocket_source.find("if (cmd == \"tone_start\" || cmd == \"tone_end\" || cmd == \"stop\")") !=
                std::string::npos &&
            websocket_source.find("std::unique_lock<std::mutex>(test_tone_command_mutex_)") !=
                std::string::npos,
        "websocket Test Tone Start and End commands must share one transaction lock through their broadcast reply");

    const std::string scheduling_source =
        read_repo_text_file("/src/scheduling.cpp") +
        read_repo_text_file("/src/scheduling_config.cpp") +
        read_repo_text_file("/src/scheduling_websocket_status.cpp") +
        read_repo_text_file("/src/scheduling_test_tone.cpp") +
        read_repo_text_file("/src/scheduling_runtime.cpp") +
        read_repo_text_file("/src/transmitter_runtime_bridge.cpp");
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
        scheduling_source.find("validate_non_wspr_repeat_interval_policy(\n                    working_config") != std::string::npos &&
            scheduling_source.find("\"reload_failed\",\n                        policy_error") != std::string::npos,
        "managed non-web reloads must retain runtime CW duration-policy validation and reload failure reporting");
    require(
        scheduling_source.find("TestToneStopResult end_test_tone(") != std::string::npos &&
            scheduling_source.find("send_ws_message(\"transmit\", \"finished\");") ==
                scheduling_source.find("send_ws_message(\"transmit\", \"finished\");", scheduling_source.find("void transmitter_cb(")),
        "test tone end must rely on transmitter callback websocket ownership only");
    require(
        scheduling_source.find("finalize_transmission_stop_cleanup(") != std::string::npos &&
            scheduling_source.find("wsprTransmitter.clearSoftOff();") != std::string::npos &&
            scheduling_source.find("wsprTransmitter.startAsync();", scheduling_source.find("TestToneStopResult end_test_tone(")) !=
                std::string::npos,
        "WSPR test tone stop recovery must route through shared stop cleanup and explicitly re-arm the committed scheduler wait path");

    const std::string ui_source =
        read_repo_text_file("/WsprryPi-UI/data/index.js");
    const std::string ui_stylesheet_source =
        read_repo_text_file("/WsprryPi-UI/data/index.css");
    const std::string main_source =
        read_repo_text_file("/src/main.cpp");
    const std::string config_view_source =
        read_repo_text_file("/WsprryPi-UI/data/views/config.php");
    const std::string maintenance_script_source =
        read_repo_text_file("/WsprryPi-UI/data/maintenance.js");
    const std::string operation_script_source =
        read_repo_text_file("/WsprryPi-UI/data/operation.js");
    require(
        main_source.find("#ifdef DEBUG_WSPR\n    const char *qualification_gpio_test_mode") !=
                std::string::npos &&
            main_source.find("WSPRRYPI_QUALIFICATION_GPIO_TEST_MODE") !=
                std::string::npos &&
            main_source.find("GPIOOutput::setTestMode(true);") !=
                std::string::npos &&
            main_source.find("physical GPIO output requests, writes, and releases are suppressed") !=
                std::string::npos,
        "the debug-only qualification GPIO seam must require explicit activation and clearly state that physical output operations are suppressed");
    require(
        config_view_source.find("id=\"cw_message_length_estimate\"") != std::string::npos &&
            config_view_source.find("Estimated Message Length: unavailable") != std::string::npos &&
            config_view_source.find("aria-live=\"polite\"") != std::string::npos,
        "configuration view must expose a live CW message length estimate near the CW message field");
    require(
        config_view_source.find("name=\"cw_speed\"") != std::string::npos &&
            config_view_source.find("['QRSS1', 'QRSS3', 'QRSS6', 'Advanced']") != std::string::npos &&
            config_view_source.find("name=\"cw_spacing\"") != std::string::npos &&
            config_view_source.find("Review QRSS/FSKCW spacing") != std::string::npos &&
            config_view_source.find("Review DFCW spacing") != std::string::npos &&
            config_view_source.find("id=\"cw_frequency_controls\"") != std::string::npos &&
            config_view_source.find("id=\"cw_schedule_controls\"") != std::string::npos &&
            ui_source.find("detailActionControls") != std::string::npos &&
            ui_source.find("actionButton.setAttribute(\"aria-controls\", detailActionControls)") != std::string::npos &&
            ui_source.find("actionButton.setAttribute(\"aria-expanded\", \"false\")") != std::string::npos,
        "CW Control must expose accessible speed, mode-aware spacing, inactive-repair, frequency, and schedule groups");
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
        ui_source.find("var Operation = {\n        \"Mode\": mode,") !=
                std::string::npos &&
            ui_source.find("var Meta = {\n        \"Mode\": mode") ==
                std::string::npos,
        "UI save path must use canonical Operation.Mode only");
    require(
        ui_source.find("var GPIO = {\n        \"Power Level\": transmit_power,\n        \"RP1 Drive mA\": rp1_gpio_drive_ma,\n        \"Use System Clock Frequency Estimate\": use_system_clock_frequency_estimate,") !=
                std::string::npos &&
            ui_source.find("function validateRp1GpioDrive()") != std::string::npos &&
            ui_source.find("function syncGpioDriveControls()") != std::string::npos &&
            ui_source.find("function rp1GpioOperatorVisible()") != std::string::npos &&
            ui_source.find("\"Frequency Residual PPM\": gpio_frequency_residual_ppm,") != std::string::npos &&
            ui_source.find("\"Manual PPM\": gpio_manual_ppm,") != std::string::npos &&
            ui_source.find("Use NTP") == std::string::npos &&
            ui_source.find("var Calibration = {\n        \"PPM\": ppm_val,") !=
                std::string::npos,
        "UI save path must use only canonical backend-specific calibration keys");
    require(
        ui_source.find("bindTestToneControls();") != std::string::npos,
        "configuration view must bind the shared Test Tone controls");

    const std::string site_source =
        read_repo_text_file("/WsprryPi-UI/data/site.js");
    const std::string ui_header_source =
        read_repo_text_file("/WsprryPi-UI/data/header.php");
    const std::string apache_proxy_source =
        read_repo_text_file("/config/wsprrypi.conf");
    const std::string installer_source =
        read_repo_text_file("/scripts/install.sh");
    const std::string stock_ini_source =
        read_repo_text_file("/config/wsprrypi.ini");
    const std::string transmit_branch = "if (msg.type === \"transmit\")";
    const std::string tx_state_branch = "if (msg.tx_state !== undefined)";
    require(
        site_source.find(transmit_branch) != std::string::npos &&
            site_source.find(tx_state_branch) != std::string::npos &&
            site_source.find(transmit_branch) < site_source.find(tx_state_branch),
        "browser websocket handler must process pushed transmit events before generic tx_state replies");
    require(
        ui_header_source.find("'supportBundlesPath' => $basePath . '/api/support-bundles'") != std::string::npos &&
            site_source.find("PATHS.supportBundlesPath") != std::string::npos &&
            site_source.find("`${APP_BASE_PATH}/api/support-bundles`") != std::string::npos &&
            site_source.find("buildDirectRestFallbackUrl(\"/api/support-bundles\")") != std::string::npos &&
            apache_proxy_source.find("ProxyPreserveHost On") != std::string::npos &&
            apache_proxy_source.find("ProxyPass        /wsprrypi/api/support-bundles http://127.0.0.1:31415/api/support-bundles") != std::string::npos &&
            apache_proxy_source.find("ProxyPassReverse /wsprrypi/api/support-bundles http://127.0.0.1:31415/api/support-bundles") != std::string::npos &&
            apache_proxy_source.find("ProxyPass        /wsprrypi/api/support-intake http://127.0.0.1:31415/api/support-intake") != std::string::npos &&
            apache_proxy_source.find("ProxyPassReverse /wsprrypi/api/support-intake http://127.0.0.1:31415/api/support-intake") != std::string::npos &&
            apache_proxy_source.find("ProxyPass        /wsprrypi/api ") == std::string::npos &&
            apache_proxy_source.find("Access-Control-Allow-Origin *") == std::string::npos &&
            apache_proxy_source.find("RequestHeader set Host") == std::string::npos &&
            apache_proxy_source.find("RequestHeader set Origin") == std::string::npos &&
            installer_source.find("ProxyPass        /wsprrypi/api/support-bundles http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-bundles") != std::string::npos &&
            installer_source.find("ProxyPassReverse /wsprrypi/api/support-bundles http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-bundles") != std::string::npos &&
            installer_source.find("ProxyPass        /wsprrypi/api/support-intake http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-intake") != std::string::npos &&
            installer_source.find("ProxyPassReverse /wsprrypi/api/support-intake http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-intake") != std::string::npos,
        "Support Bundle must use the same-origin guarded proxy family without broad API proxying, CORS relaxation, or Host/Origin rewriting");
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
            websocket_source.find("reply[\"frequency_estimate_qualification\"] = snapshot.frequency_estimate_qualification;") != std::string::npos &&
            websocket_source.find("reply[\"effective_gpio_ppm\"] = snapshot.additional_gpio_ppm;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_hz\"] = snapshot.frequency_hz;") != std::string::npos &&
            scheduling_source.find("j[\"offset_hz\"] = snapshot.offset_hz;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_is_skip\"] = snapshot.frequency_is_skip;") != std::string::npos &&
            scheduling_source.find("j[\"selector_gpio_enabled\"] = snapshot.selector_gpio_enabled;") != std::string::npos &&
            scheduling_source.find("j[\"selector_gpio\"] = snapshot.selector_gpio;") != std::string::npos &&
            scheduling_source.find("j[\"selector_gpio_active_high\"] = snapshot.selector_gpio_active_high;") != std::string::npos &&
            scheduling_source.find("j[\"power_dbm\"] = snapshot.power_dbm;") != std::string::npos &&
            scheduling_source.find("j[\"frequency_correction_mode\"] = snapshot.frequency_correction_mode;") != std::string::npos &&
            scheduling_source.find("j[\"effective_gpio_ppm\"] = snapshot.additional_gpio_ppm;") != std::string::npos &&
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
        site_source.find("let dismissedUiRefreshVersion = null;") == std::string::npos &&
            site_source.find("let dismissedUiRefreshBuildId = null;") != std::string::npos &&
            site_source.find("const UI_BUILD_POLL_INTERVAL_MS = 60 * 1000;") != std::string::npos &&
            site_source.find("const UI_REFRESH_REQUEST_PARAM = \"ui_refresh\";") != std::string::npos &&
            site_source.find("const GITHUB_UPDATE_POLL_INTERVAL_MS = 60 * 60 * 1000;") != std::string::npos &&
            site_source.find("window.WSPRRYPI_INSTALLED_UI_BUILD_ID") != std::string::npos &&
            site_source.find("window.WSPRRYPI_UI_BUILD_ID") == std::string::npos &&
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
            site_source.find("getJsonWithEndpointFallback(UI_IDENTITY_ENDPOINT)\n        .done(function (response) {\n            if (response && response.installed_ui_build_id) {\n                maybePromptForUiRefresh(response);") != std::string::npos &&
            site_source.find("// Start UI build polling from global script initialization as soon as site.js") != std::string::npos &&
            site_source.find("initUiBuildChangePolling();\ninitGithubUpdatePolling();\n\nfunction getPersistedTabStorageKey") != std::string::npos &&
            site_source.find("initUiBuildChangePolling();\n    initGithubUpdatePolling();\n    populateConfig();") != std::string::npos &&
            site_source.find("initGithubUpdatePolling();\n    populateConfig();") != std::string::npos &&
            site_source.find("if (uiBuildPollTimer !== null)") != std::string::npos &&
            site_source.find("installedBuildId === dismissedUiRefreshBuildId") != std::string::npos &&
            site_source.find("maybePromptForUiRefresh(response);\n                checkForWsprryPiUpdate(response);") == std::string::npos &&
            web_server_source.find("j[\"ui_version\"] = get_raw_version_string();") != std::string::npos &&
            web_server_source.find("j[\"wspr_version_raw\"] = get_exe_version();") != std::string::npos &&
            web_server_source.find("j[\"wspr_version_parsed\"] = parse_version_for_update_metadata(") != std::string::npos &&
            web_server_source.find("j[\"wspr_branch\"] = get_exe_raw_branch();") != std::string::npos &&
            web_server_source.find("j[\"wspr_branch_state\"] = get_exe_branch_state();") != std::string::npos &&
            web_server_source.find("j[\"wspr_display_branch\"] = get_exe_branch();") != std::string::npos &&
            web_server_source.find("j[\"wspr_exe_version\"] = get_exe_version();") != std::string::npos &&
            web_server_source.find("j[\"wspr_commit\"] = get_exe_commit();") != std::string::npos,
        "site.js must poll the UI-owned installed identity independently while backend /version remains available only for service and update metadata");
    require(
            makefile_source.find("BUILD_METADATA_HEADER := $(BUILD_METADATA_DIR)/build_metadata.hpp") != std::string::npos &&
            makefile_source.find("generate_build_metadata.py") != std::string::npos &&
            makefile_source.find("include ../scripts/build_metadata_rules.mk") != std::string::npos &&
            build_metadata_generator_source.find("\"rev-parse\", \"--is-inside-work-tree\"") != std::string::npos &&
            build_metadata_generator_source.find("\"symbolic-ref\", \"--quiet\", \"--short\", \"HEAD\"") != std::string::npos &&
            build_metadata_generator_source.find("\"status\", \"--porcelain\", \"--untracked-files=no\"") != std::string::npos &&
            version_source.find("#include \"build_metadata.hpp\"") != std::string::npos &&
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
            web_server_source.find("j[\"wspr_build_dirty_state\"] = build_dirty_metadata(") != std::string::npos,
        "build metadata must pass sanitized MAKE_BRH for display, raw MAKE_RAW_BRH and branch-state for update lookup, full commit and build-time dirty state to version.cpp, and expose structured version, branch, commit, and dirty metadata in /version");
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
            site_source.find(": \"An update is available\";") != std::string::npos &&
            site_source.find("? \"A newer branch build is available\"") != std::string::npos &&
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
            site_source.find("A newer branch build is available.") != std::string::npos &&
            site_source.find("This build and the selected branch have diverged.") != std::string::npos &&
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
        site_source.find("local devel probing upstream main because upstream devel returned HTTP 404") != std::string::npos &&
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
        site_source.find("Update check parsed displayBranch=") != std::string::npos &&
            site_source.find("Update check branch lookup:") != std::string::npos &&
            site_source.find("Update check branch lookup failed for") != std::string::npos &&
            site_source.find("Update check branch lookup result for") != std::string::npos &&
            site_source.find("Update check selected targetBranch=") != std::string::npos,
        "update checker must log parsed display branch, raw branch/SHA, branch lookup URL/status/result, selected target branch, fallback state, and target HEAD for debugging");

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
    require(
        ui_source.find("function handleBandGpioInputChange() {\n    refreshGpioConflictOptions();\n    validateBandGpioFields();\n    scheduleAutosave();") !=
            std::string::npos,
        "Band GPIO pin and polarity changes must schedule autosave after revalidation");
    const std::string footer_source =
        read_repo_text_file("/WsprryPi-UI/data/footer.php");
    const std::string site_css_source =
        read_repo_text_file("/WsprryPi-UI/data/site.css");
    const std::string header_source =
        read_repo_text_file("/WsprryPi-UI/data/header.php");
    const std::string index_page_source =
        read_repo_text_file("/WsprryPi-UI/data/index.php");
    const std::string script_include_source =
        read_repo_text_file("/WsprryPi-UI/data/site.js.includes.php");
    const std::string ui_version_source =
        read_repo_text_file("/WsprryPi-UI/data/ui_version.php");
    const std::string html_cache_headers_source =
        read_repo_text_file("/WsprryPi-UI/data/html_cache_headers.php");
    const std::string version_endpoint_source =
        read_repo_text_file("/WsprryPi-UI/data/version.php");
    const std::string ui_identity_endpoint_source =
        read_repo_text_file("/WsprryPi-UI/data/ui-version.php");
    const std::string ui_manifest_endpoint_source =
        read_repo_text_file("/WsprryPi-UI/data/ui-manifest.php");
    const std::string fetch_spots_source =
        read_repo_text_file("/WsprryPi-UI/data/fetch_spots.php");
    const std::string log_stream_source =
        read_repo_text_file("/WsprryPi-UI/data/log_stream.php");
    const std::string diagnostic_logs_source =
        read_repo_text_file("/WsprryPi-UI/data/view_diag_logs.php");
    require(
        footer_source.find("<details class=\"footer-meta\">") != std::string::npos &&
            footer_source.find("<summary>About</summary>") != std::string::npos &&
            footer_source.find("id=\"updateCheckToggle\"") != std::string::npos &&
            footer_source.find("Disable update checks") != std::string::npos &&
            site_source.find("toggle.textContent = disabled ? \"Enable update checks\" : \"Disable update checks\";") != std::string::npos &&
            site_css_source.find(".footer-meta__action") != std::string::npos,
        "footer markup must keep the native click-based About disclosure and provide a site-global update-check toggle for disabling or re-enabling checks");
    require(
        header_source.find("window.WSPRRYPI_UI_VERSION") == std::string::npos &&
            header_source.find("window.WSPRRYPI_INSTALLED_UI_BUILD_ID = <?= json_encode(getWsprryPiInstalledUiBuildId()) ?>;") != std::string::npos &&
            header_source.find("window.WSPRRYPI_UI_BUILD_ID") == std::string::npos &&
            header_source.find("'uiIdentityPath' => $basePath . '/ui-version.php'") != std::string::npos &&
            header_source.find("'uiManifestPath' => $basePath . '/ui-manifest.php'") != std::string::npos &&
            ui_version_source.find("function getWsprryPiUiVersion(): string") != std::string::npos &&
            ui_version_source.find("function wsprrypiUiBuildFileRecords(?string $uiRoot = null): array") != std::string::npos &&
            ui_version_source.find("function wsprrypiUiIdentityStatus(") != std::string::npos &&
            ui_version_source.find("function getWsprryPiInstalledUiBuildId(): string") != std::string::npos &&
            ui_version_source.find("function getWsprryPiUiBuildId(): string") != std::string::npos &&
            ui_version_source.find("WSPRRYPI_UI_IDENTITY_DOMAIN") != std::string::npos &&
            ui_version_source.find("hash_file('sha256'") != std::string::npos &&
            ui_version_source.find("'cache', 'backups'") != std::string::npos &&
            ui_version_source.find("'installed_state' => $state") != std::string::npos &&
            ui_version_source.find("'modified_files' => $modifiedFiles") != std::string::npos &&
            ui_version_source.find("'added_files' => $addedFiles") != std::string::npos &&
            ui_version_source.find("'missing_files' => $missingFiles") != std::string::npos &&
            ui_version_source.find("function wsprrypiAssetUrl(string $path): string") != std::string::npos &&
            ui_version_source.find("'v=' . rawurlencode($buildId)") != std::string::npos &&
            version_endpoint_source.find("'ui_build_id' => $uiBuildId") != std::string::npos &&
            version_endpoint_source.find("Cache-Control") != std::string::npos &&
            version_endpoint_source.find("no-store") != std::string::npos &&
            ui_identity_endpoint_source.find("wsprrypiUiIdentityStatus()") != std::string::npos &&
            ui_identity_endpoint_source.find("Cache-Control: no-store") != std::string::npos &&
            ui_manifest_endpoint_source.find("wsprrypiUiLoadManifest($manifestPath)") != std::string::npos &&
            ui_manifest_endpoint_source.find("Cache-Control: no-store") != std::string::npos &&
            header_source.find("wsprrypiAssetUrl('site.css')") != std::string::npos &&
            header_source.find("wsprrypiAssetUrl('site.webmanifest')") != std::string::npos &&
            header_source.find("wsprrypiAssetUrl('favicon.ico')") != std::string::npos &&
            footer_source.find("wsprrypiAssetUrl('site.js')") != std::string::npos &&
            index_page_source.find("wsprrypiAssetUrl($stylesheet)") != std::string::npos &&
            index_page_source.find("wsprrypiAssetUrl($script)") != std::string::npos &&
            script_include_source.find("wsprrypiAssetUrl('vendor/js/jquery-4.0.0.min.js')") != std::string::npos &&
            script_include_source.find("wsprrypiAssetUrl('vendor/js/bootstrap.bundle-5.3.8.min.js')") != std::string::npos,
        "PHP pages and UI-owned endpoints must use the installed content identity for asset cache busting and report packaged, modified, added, missing, and unknown state without replacing the running-service /version contract");
    require(
        html_cache_headers_source.find("header('Cache-Control: no-cache, must-revalidate');") != std::string::npos &&
            index_page_source.find("<?php require_once __DIR__ . '/html_cache_headers.php'; ?>") == 0 &&
            diagnostic_logs_source.find("require_once __DIR__ . '/html_cache_headers.php';") != std::string::npos &&
            diagnostic_logs_source.find("require_once __DIR__ . '/html_cache_headers.php';") < diagnostic_logs_source.find("<!doctype html>") &&
            read_repo_text_file("/WsprryPi-UI/data/template.php").find("<?php require_once __DIR__ . '/html_cache_headers.php'; ?>") == 0 &&
            version_endpoint_source.find("html_cache_headers.php") == std::string::npos &&
            ui_identity_endpoint_source.find("html_cache_headers.php") == std::string::npos &&
            ui_manifest_endpoint_source.find("html_cache_headers.php") == std::string::npos &&
            fetch_spots_source.find("html_cache_headers.php") == std::string::npos &&
            log_stream_source.find("html_cache_headers.php") == std::string::npos,
        "HTML PHP shell pages must send no-cache revalidation headers before output while JSON/API endpoints keep their existing cache behavior");

    const std::string operation_view_source =
        read_repo_text_file("/WsprryPi-UI/data/views/operation.php");
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
        read_repo_text_file("/WsprryPi-UI/data/operation.css");
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
        config_view_source.find("id=\"gpio-calibration-heading\"") != std::string::npos &&
            config_view_source.find("id=\"si5351-calibration-heading\"") != std::string::npos &&
            config_view_source.find("id=\"use_system_clock_frequency_estimate\"") != std::string::npos &&
            config_view_source.find("id=\"gpio_frequency_residual_ppm\"") != std::string::npos &&
            config_view_source.find("id=\"gpio_manual_ppm\"") != std::string::npos &&
            config_view_source.find(">Fallback PPM</label>") != std::string::npos &&
            config_view_source.find("Used when the selected estimate is disabled or unavailable.") != std::string::npos &&
            config_view_source.find("id=\"use_direct_pps_calibration\"") != std::string::npos &&
            config_view_source.find("id=\"direct-pps-status\"") != std::string::npos,
        "Configuration view must place familiar backend-specific frequency calibration sections in both transmitter panels");
    require(
        operation_view_source.find("id=\"gpio_frequency_correction_panel\"") != std::string::npos &&
            operation_view_source.find("id=\"gpio_frequency_correction_value\"") != std::string::npos &&
            operation_view_source.find("GPIO frequency correction") != std::string::npos &&
            operation_css_source.find("#gpio_frequency_correction_panel {\n    grid-column: 1 / -1;") != std::string::npos &&
            site_source.find("function renderGpioFrequencyCorrection(status)") != std::string::npos &&
            site_source.find("gpioCorrectionCandidate") != std::string::npos &&
            site_source.find("gpioCorrectionCommitted") != std::string::npos &&
            site_source.find("Candidate refreshes do not alter a committed transmission.") != std::string::npos &&
            websocket_source.find("reply[\"gpio_correction_candidate\"]") != std::string::npos &&
            websocket_source.find("reply[\"gpio_correction_committed\"]") != std::string::npos &&
            scheduling_source.find("j[\"gpio_correction_candidate\"]") != std::string::npos &&
            scheduling_source.find("j[\"gpio_correction_committed\"]") != std::string::npos,
        "Operation view must distinguish current candidate correction provenance from the frozen committed execution");
    require(
        site_source.find("intrinsicPpm") == std::string::npos &&
            site_source.find("finalPpm") == std::string::npos &&
            websocket_source.find("{\"intrinsic_ppm\"") == std::string::npos &&
            websocket_source.find("{\"final_ppm\"") == std::string::npos &&
            scheduling_source.find("{\"intrinsic_ppm\"") == std::string::npos &&
            scheduling_source.find("{\"final_ppm\"") == std::string::npos &&
            websocket_source.find("{\"correction_ppm\", value.additional_ppm}") != std::string::npos &&
            scheduling_source.find("{\"correction_ppm\", value.additional_ppm}") != std::string::npos,
        "operator UI and status payloads must exclude internal chipset compensation and composed totals");
    require(
        config_view_source.find("config-runtime-item config-runtime-item--switch") == std::string::npos,
        "Runtime state body must no longer dedicate a body tile to the Transmit enabled control");
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
            config_view_source.find("id=\"use_system_clock_frequency_estimate\"") != std::string::npos &&
            config_view_source.find("id=\"planner_preference\"") != std::string::npos &&
            config_view_source.find("id=\"dbm\"") != std::string::npos &&
            config_view_source.find("<option value=\"60\">60</option>") != std::string::npos &&
            config_view_source.find("spaces or commas") != std::string::npos &&
            config_view_source.find("`@GPIO`, `@GPIOH`, or `@GPIOL`") != std::string::npos &&
            config_view_source.find("20m-15") == std::string::npos &&
            config_view_source.find("class=\"form-select config-planner-field__select\"") == std::string::npos,
        "WSPR transmission settings must keep TX dBm as a fixed-value select and expose planner_preference in the WSPR planning controls");
    require(
        ui_stylesheet_source.find("@media (max-width: 991.98px) {\n    .config-wspr-top-row,\n    .config-wspr-secondary-row {\n        grid-template-columns: minmax(0, 1fr);") != std::string::npos &&
            ui_stylesheet_source.find(".config-wspr-top-row .form-label,\n    .config-wspr-secondary-row .form-label {\n        white-space: normal;") != std::string::npos &&
            ui_stylesheet_source.find(".config-wspr-top-row__field--toggle {\n        min-height: 0;") != std::string::npos &&
            ui_stylesheet_source.find(".config-wspr-top-row__toggle") == std::string::npos,
        "WSPR transmission-plan rows must stack in one column with wrap-safe labels at the established narrow breakpoint");
    require(
        config_view_source.find("Fade Shape") == std::string::npos &&
            config_view_source.find("Fade In Ms") == std::string::npos &&
            config_view_source.find("Fade Out Ms") == std::string::npos &&
            config_view_source.find("Fade Slice Ms") == std::string::npos,
        "configuration view must keep CW fade settings hidden from the normal UI");

    const std::string maintenance_source =
        read_repo_text_file("/WsprryPi-UI/data/views/maintenance.php");
    const std::string maintenance_css_source =
        read_repo_text_file("/WsprryPi-UI/data/maintenance.css");
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
            maintenance_css_source.find(".maintenance-update-technical summary") != std::string::npos &&
            maintenance_css_source.find(".maintenance-update-technical-list .maintenance-fact,\n.maintenance-update-technical-list dd") != std::string::npos &&
            maintenance_css_source.find(".maintenance-update-technical-list dd code,\n.maintenance-update-technical-list dd a") != std::string::npos &&
            maintenance_css_source.find("overflow-wrap: anywhere;") != std::string::npos,
        "maintenance view must split Utility into side-by-side Test Tone and Update Check panels, keep Test Tone left-aligned, expose update-check panel hooks and controls, show user-facing summary text, and keep technical details collapsed by default");

    require(
        scheduling_source.find("scheduler_managed_transmission_active_for_test_tone()") != std::string::npos &&
            scheduling_source.find("result.blocked_by_active_transmission = true;") != std::string::npos &&
            scheduling_source.find("scheduler_managed_transmission_enabled_for_test_tone()") != std::string::npos &&
            scheduling_source.find("result.blocked_by_enabled_transmission = true;") != std::string::npos,
        "backend-side Test Tone rejection must remain present for active and enabled scheduled transmissions");

    require(
        site_source.find("const locallyRequested = pendingTestToneStartRequest === true && Boolean(correlatedStart);") != std::string::npos &&
            site_source.find("if (response.started === true)") != std::string::npos &&
            site_source.find("if (locallyRequested && response.blocked_by_active_transmission === true)") != std::string::npos &&
            site_source.find("} else if (locallyRequested && response.blocked_by_enabled_transmission === true)") != std::string::npos,
        "Test Tone blocked modal handling must be gated by the local pending tone_start request flag");
    require(
        site_source.find("!correlatedStart && response.started !== true") != std::string::npos &&
            site_source.find("reconcileTestToneStatus(response.test_tone)") != std::string::npos &&
            site_source.find("Passive test tone start rejection received:") != std::string::npos &&
            site_source.find("syncTestToneControlState(false);") != std::string::npos,
        "passive websocket tone_start rejections must still update Test Tone state without showing blocked modals");
    std::cout << "ui_source_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
