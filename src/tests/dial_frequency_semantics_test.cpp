#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "frequency_semantics.hpp"
#include "scheduling.hpp"
#include "wspr_band_lookup.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define private public
#include "wspr_transmit.hpp"
#undef private

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

    bool nearly_equal(double lhs, double rhs, double epsilon = 0.01)
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    void reset_getopt_state()
    {
        optind = 1;
        opterr = 0;
        optopt = 0;
        optarg = nullptr;
    }

    std::vector<char *> argv_for(std::vector<std::string> &args)
    {
        std::vector<char *> argv;
        argv.reserve(args.size());
        for (std::string &arg : args)
        {
            argv.push_back(arg.data());
        }
        return argv;
    }

    std::map<std::string, std::unordered_map<std::string, std::string>>
    make_managed_ini_data(
        const std::string &callsign,
        const std::string &grid_square,
        const std::string &frequency,
        bool transmit,
        WsprPlannerPreference planner_preference = WsprPlannerPreference::Auto)
    {
        auto data = std::map<std::string, std::unordered_map<std::string, std::string>>{
            {"Meta",
             {{"Planner Preference",
               wspr_planner_preference_to_string(planner_preference)}}},
            {"Control", {{"Transmit", transmit ? "true" : "false"}}},
            {"Common",
             {{"Call Sign", callsign},
              {"Grid Square", grid_square},
              {"TX Power", "20"},
              {"Frequency", frequency},
              {"Transmit Pin", "4"}}},
            {"Extended",
             {{"PPM", "0"},
              {"Use NTP", "false"},
              {"Offset", "false"},
              {"WSPR Audio Offset Hz", "1500"},
              {"Use LED", "false"},
              {"LED Pin", "-1"},
              {"Power Level", "7"}}},
            {"Server",
             {{"Web Port", "-1"},
              {"Socket Port", "-1"},
              {"Use Shutdown", "false"},
              {"Shutdown Button", "-1"}}}};
        return data;
    }

    void write_managed_ini_file(
        const std::string &path,
        const std::map<std::string, std::unordered_map<std::string, std::string>> &data)
    {
        std::ofstream out(path, std::ios::trunc);
        require(out.is_open(), "managed INI regression helper must open temp INI file");

        for (const auto &section_pair : data)
        {
            out << "[" << section_pair.first << "]\n";
            for (const auto &kv : section_pair.second)
            {
                out << kv.first << " = " << kv.second << "\n";
            }
            out << "\n";
        }

        require(static_cast<bool>(out), "managed INI regression helper must write temp INI file");
    }

    void prime_valid_runtime_identity_config()
    {
        init_default_config();
        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        config.wspr_planner_preference = WsprPlannerPreference::Auto;
        config_to_json();
    }

    nlohmann::json make_identity_patch(
        const std::string &callsign,
        const std::string &grid_square,
        WsprPlannerPreference planner_preference = WsprPlannerPreference::Auto)
    {
        return {
            {"Meta",
             {{"Planner Preference",
               wspr_planner_preference_to_string(planner_preference)}}},
            {"Control", {{"Transmit", true}}},
            {"Common",
             {{"Call Sign", callsign},
              {"Grid Square", grid_square},
              {"TX Power", 20},
              {"Frequency", "20m"}}}};
    }

    void reset_runtime_planning_state_for_identity_test()
    {
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        set_scheduler_execution_suppressed_for_test(true);
    }

    void finish_runtime_planning_state_for_identity_test()
    {
        set_scheduler_execution_suppressed_for_test(false);
    }

    void require_patch_accepts_and_runtime_plans(
        const nlohmann::json &patch,
        const std::string &expected_plan_type,
        const std::string &expected_callsign,
        const std::string &expected_locator,
        const std::string &message,
        std::size_t expected_frame_count = 1U,
        std::size_t expected_current_frame = 1U,
        const std::string &expected_frame_callsign = std::string(),
        const std::string &expected_frame_locator = std::string())
    {
        prime_valid_runtime_identity_config();
        patch_all_from_web(patch);

        reset_runtime_planning_state_for_identity_test();
        require(set_config(true), message + " must succeed during scheduler planning");
        const WsprTransmissionRequest request = current_transmission_request_for_test();
        require(
            request.mode == WsprTransmissionMode::WSPR,
            message + " must commit a WSPR execution request");
        require(
            request.wspr_plan.plan_type == expected_plan_type,
            message + " must commit the expected plan type");
        require(
            request.wspr_plan.callsign == expected_callsign,
            message + " must commit the normalized callsign expected by runtime planning");
        require(
            request.wspr_plan.locator == expected_locator,
            message + " must commit the normalized locator expected by runtime planning");

        const WsprRuntimeStatusSnapshot snapshot =
            current_tx_runtime_status_snapshot();
        require(
            snapshot.plan_type == expected_plan_type,
            message + " must expose the expected runtime plan type");
        require(
            snapshot.frame_count == expected_frame_count &&
                snapshot.current_frame == expected_current_frame,
            message + " must expose the expected runtime frame progress");
        require(
            snapshot.callsign_normalized == expected_callsign &&
                snapshot.locator_normalized == expected_locator &&
                snapshot.frame_callsign ==
                    (expected_frame_callsign.empty() ? expected_callsign : expected_frame_callsign) &&
                snapshot.frame_locator ==
                    (expected_frame_locator.empty() ? expected_locator : expected_frame_locator),
            message + " must expose normalized runtime identity metadata");
        finish_runtime_planning_state_for_identity_test();
    }

    void require_patch_rejects_with_plan_status(
        const nlohmann::json &patch,
        const std::string &expected_plan_status,
        const std::string &message)
    {
        prime_valid_runtime_identity_config();
        const std::string original_callsign = config.callsign;
        const std::string original_locator = config.grid_square;
        const WsprPlannerPreference original_planner_preference =
            config.wspr_planner_preference;

        bool threw = false;
        try
        {
            patch_all_from_web(patch);
        }
        catch (const ConfigValidationError &e)
        {
            threw = true;
            require(
                e.details().is_object(),
                message + " must expose structured planner validation details");
            require(
                e.details().value("plan_status", "") == expected_plan_status,
                message + " must report the expected planner status");
        }

        require(threw, message + " must be rejected at config acceptance time");
        require(
            config.callsign == original_callsign &&
                config.grid_square == original_locator &&
                config.wspr_planner_preference == original_planner_preference,
            message + " must not mutate live config after rejection");
    }
} // namespace

int main()
{
    WSPRBandLookup lookup;

    require(
        nearly_equal(lookup.parse_string_to_frequency("20m", false), 14095600.0),
        "20m must resolve to the WSPR dial frequency");
    require(
        lookup.lookup_ham_band(14095600.0).has_value(),
        "band lookup should still succeed on WSPR dial frequency values");

    require(
        nearly_equal(
            resolve_actual_rf_frequency_hz(14095600.0, 1500.0, FrequencyPath::WsprDial),
            14097100.0),
        "WSPR dial frequency must convert to actual RF exactly once");

    require(
        nearly_equal(
            resolve_actual_rf_frequency_hz(730000.0, 1500.0, FrequencyPath::DirectRf),
            730000.0),
        "Direct-RF paths must not apply the WSPR audio offset");

    {
        init_config_json();
        jConfig["Meta"]["Center Frequency Set"] = nlohmann::json::array({14095600.0});
        jConfig["Common"]["Frequency"] = "20m";
        jConfig["Extended"].erase("WSPR Audio Offset Hz");
        json_to_config();

        require(
            config.wspr_dial_freq_set.size() == 1 &&
                nearly_equal(config.wspr_dial_freq_set.front(), 14095600.0),
            "legacy Meta.Center Frequency Set must still load as a WSPR dial-frequency list");
        require(
            nearly_equal(config.wspr_audio_offset_hz, 1500.0),
            "missing WSPR Audio Offset Hz must default to 1500.0");
    }

    {
        init_config_json();
        jConfig["Meta"]["WSPR Dial Frequency Set"] = nlohmann::json::array({14095600.0});
        jConfig["Common"]["Frequency"] = "20m";
        jConfig["Extended"]["WSPR Audio Offset Hz"] = 1600.0;
        json_to_config();

        require(
            config.wspr_dial_freq_set.size() == 1 &&
                nearly_equal(config.wspr_dial_freq_set.front(), 14095600.0),
            "Meta.WSPR Dial Frequency Set must load as the active WSPR dial-frequency list");
        require(
            nearly_equal(config.wspr_audio_offset_hz, 1600.0),
            "explicit WSPR Audio Offset Hz must round-trip through JSON config");
    }

    {
        init_config_json();
        jConfig["Meta"].erase("WSPR Dial Frequency Set");
        jConfig["Meta"].erase("Center Frequency Set");
        json_to_config();

        require(
            config.wspr_dial_freq_set.empty(),
            "missing WSPR frequency-set keys must load as an empty dial-frequency list");
    }

    {
        init_config_json();
        jConfig["Meta"]["WSPR Dial Frequency Set"] = nlohmann::json::array();
        jConfig["Meta"]["Center Frequency Set"] = nlohmann::json::array({10138700.0});
        json_to_config();

        require(
            config.wspr_dial_freq_set.size() == 1 &&
                nearly_equal(config.wspr_dial_freq_set.front(), 10138700.0),
            "empty Meta.WSPR Dial Frequency Set must fall back to non-empty legacy Meta.Center Frequency Set");
    }

    {
        const std::optional<std::string> legacy_alias =
            lookup.legacy_actual_wspr_alias_for_frequency(14097100.0);
        require(
            legacy_alias.has_value() && *legacy_alias == "20m",
            "legacy actual RF alias detection must identify 20m");
    }

    {
        init_config_json();
        json_to_config();
        config.transmit = true;
        config.frequencies = "80m@17,40m@27,20m,14.097100MHz@22";

        require(
            set_frequencies(config),
            "mixed frequency lists with optional @GPIO suffixes must parse");
        require(
            config.wspr_dial_frequency_entries.size() == 4,
            "parsed frequency entries must preserve all configured tokens");
        require(
            config.wspr_dial_frequency_entries[0].control_gpio == 17 &&
                nearly_equal(config.wspr_dial_frequency_entries[0].dial_frequency_hz, 3568600.0),
            "80m@17 must retain the GPIO mapping and dial frequency");
        require(
            config.wspr_dial_frequency_entries[1].control_gpio == 27 &&
                nearly_equal(config.wspr_dial_frequency_entries[1].dial_frequency_hz, 7038600.0),
            "40m@27 must retain the GPIO mapping and dial frequency");
        require(
            config.wspr_dial_frequency_entries[2].control_gpio == kFrequencyEntryControlGpioUnset &&
                nearly_equal(config.wspr_dial_frequency_entries[2].dial_frequency_hz, 14095600.0),
            "entries without @GPIO must remain unmapped");
        require(
            config.wspr_dial_frequency_entries[3].control_gpio == 22 &&
                nearly_equal(config.wspr_dial_frequency_entries[3].dial_frequency_hz, 14097100.0),
            "unit-qualified frequencies must also support @GPIO");
    }

    {
        init_config_json();
        json_to_config();
        config.transmit = true;
        config.frequencies = "80m@";

        require(
            !set_frequencies(config),
            "missing @GPIO suffix values must be rejected");
    }

    {
        init_config_json();
        json_to_config();
        config.transmit = true;
        config.frequencies = "80m@99";

        require(
            !set_frequencies(config),
            "out-of-range @GPIO values must be rejected");
    }

    {
        WsprTransmitter transmitter;

        WsprTransmissionRequest committed_request;
        committed_request.mode = WsprTransmissionMode::WSPR;
        committed_request.actual_rf_frequency_hz = 14097100.0;
        committed_request.ppm = 1.75;
        committed_request.power_level = 5;
        committed_request.tx_gpio = 20;
        committed_request.use_offset = true;
        committed_request.applied_offset_hz = 42.0;
        committed_request.frequency_control_gpio = 17;
        committed_request.frequency_control_active_high = true;
        committed_request.frequency_entry_label = "20m@17";
        committed_request.wspr_plan.frames.resize(2);

        transmitter.current_request_ = committed_request;

        init_config_json();
        json_to_config();
        config.ppm = 99.0;
        config.tx_pin = 4;

        const WsprTransmissionPlan plan = transmitter.buildTransmissionPlan();

        require(
            nearly_equal(plan.frequency_hz, committed_request.actual_rf_frequency_hz),
            "transmitter plan must use committed RF frequency");
        require(
            nearly_equal(plan.ppm, committed_request.ppm),
            "transmitter plan must carry committed PPM from the execution request");
        require(
            plan.power_level == committed_request.power_level,
            "transmitter plan must use committed power level");
        require(
            plan.tx_gpio == committed_request.tx_gpio,
            "transmitter plan must use committed transmit GPIO");
        require(
            plan.total_symbol_count == 2U * WSPR_SYMBOL_COUNT,
            "transmitter plan must derive symbol count from the committed request only");
        require(
            WsprTransmitter::TransmissionCallbackEvent::CANCELLED !=
                WsprTransmitter::TransmissionCallbackEvent::COMPLETE,
            "transmitter callback contract must expose cancellation as a distinct typed event");
        require(
            !committed_request.isSkipWindow(),
            "zero-frequency detection must not be inferred for normal committed WSPR requests");

        committed_request.actual_rf_frequency_hz = 0.0;
        require(
            !committed_request.isSkipWindow(),
            "zero RF frequency alone must not create a skip-window request");

        committed_request.skip_window = true;
        require(
            committed_request.isSkipWindow(),
            "skip-window requests must be explicitly marked by the scheduler");
    }

    {
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--test-tone",
            "730000",
            "--transmit-gpio",
            "4"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "--test-tone CLI parsing must succeed");
        require(
            config.mode == ModeType::TONE,
            "--test-tone must place runtime config into transient tone mode");
        require(
            has_direct_tone_startup_request(),
            "--test-tone must create a transient direct-tone startup request");
        require(
            jConfig["Meta"]["Mode"].get<std::string>() == "WSPR",
            "--test-tone must not persist tone mode into serialized config");

        json_to_config();
        require(
            config.mode == ModeType::WSPR,
            "persistent config reload must restore WSPR mode rather than tone mode");
    }

    {
        clear_direct_tone_startup_request();
        require(
            set_direct_tone_startup_request("730000"),
            "direct tone startup helper must accept valid test-tone frequency");

        ArgParserConfig tone_candidate;
        tone_candidate.mode = ModeType::TONE;
        tone_candidate.tx_pin = 4;
        tone_candidate.transmit = true;
        tone_candidate.callsign.clear();
        tone_candidate.grid_square.clear();
        tone_candidate.power_dbm = 0;
        tone_candidate.frequencies.clear();
        tone_candidate.wspr_dial_freq_set.clear();

        std::string validation_error;
        require(
            validate_config_candidate(tone_candidate, &validation_error),
            "tone validation must succeed without normal WSPR callsign/locator/power fields");

        ArgParserConfig valid_wspr_candidate;
        valid_wspr_candidate.mode = ModeType::WSPR;
        valid_wspr_candidate.tx_pin = 4;
        valid_wspr_candidate.transmit = true;
        valid_wspr_candidate.callsign = "AA0NT";
        valid_wspr_candidate.grid_square = "EM18";
        valid_wspr_candidate.power_dbm = 20;
        valid_wspr_candidate.frequencies = "20m";
        valid_wspr_candidate.wspr_dial_freq_set = {14095600.0};

        validation_error.clear();
        require(
            validate_config_candidate(valid_wspr_candidate, &validation_error),
            "normal WSPR validation must remain independent of prior tone usage");

        ArgParserConfig invalid_wspr_candidate;
        invalid_wspr_candidate.mode = ModeType::WSPR;
        invalid_wspr_candidate.tx_pin = 4;
        invalid_wspr_candidate.transmit = true;
        invalid_wspr_candidate.callsign.clear();
        invalid_wspr_candidate.grid_square = "EM18";
        invalid_wspr_candidate.power_dbm = 20;
        invalid_wspr_candidate.frequencies = "20m";
        invalid_wspr_candidate.wspr_dial_freq_set = {14095600.0};

        validation_error.clear();
        require(
            !validate_config_candidate(invalid_wspr_candidate, &validation_error),
            "normal WSPR validation must still reject missing callsign even after tone usage");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "<AA0NT>";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;

        require(
            !set_config(true),
            "one-shot startup planning failure must propagate as a fatal startup error");
        require(
            !config.transmit,
            "one-shot startup planning failure must disable transmission");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;

        require(
            set_config(true),
            "disabled WSPR configuration must not fail scheduler setup");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::DISABLED,
            "disabled WSPR configuration must not arm transmitter hardware");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_test.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);

        iniFile.setData(
            make_managed_ini_data("W1AW", "FN31", "80m", false));

        require(
            set_config(true),
            "valid managed reload candidate must commit cleanly");
        require(
            config.callsign == "W1AW" &&
                config.grid_square == "FN31" &&
                config.frequencies == "80m",
            "valid managed reload must replace committed live configuration");
        require(
            !config.transmit,
            "managed reload that disables TX must commit the disabled config");
        require(
            !managed_reload_tx_inhibited_for_test(),
            "valid managed reload must clear any runtime TX inhibit latch");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_invalid.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);

        iniFile.setData(
            make_managed_ini_data("<AA0NT>", "EM18", "20m", true));

        require(
            set_config(true),
            "invalid managed reload must remain recoverable");
        require(
            config.callsign == "AA0NT" &&
                config.grid_square == "EM18" &&
                config.frequencies == "20m",
            "invalid managed reload must preserve last-known-good live config");
        require(
            managed_reload_tx_inhibited_for_test(),
            "invalid managed reload must disable future transmissions until repaired");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_deferred.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);

        iniFile.setData(
            make_managed_ini_data("W1AW", "FN31", "80m", false));

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        callback_ini_changed();
        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "valid disable reload during TX must remain deferred until the current transmission completes");
        require(
            config.transmit,
            "valid disable reload during TX must not mutate live config mid-transmission");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "valid disable reload during TX must not interrupt the active transmission");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        require(
            set_config(false),
            "valid disable reload after TX completion must commit cleanly");
        require(
            !config.transmit,
            "valid disable reload after TX completion must commit disabled live config");
        require(
            !managed_reload_tx_inhibited_for_test(),
            "valid disable reload after TX completion must not set the invalid-reload inhibit latch");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_post_tx_disable.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);
        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", true));

        require(
            set_config(true),
            "initial managed enabled config must commit before post-TX reload regression");
        const double committed_frequency_before_reload =
            current_transmission_request_for_test().actual_rf_frequency_hz;

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        iniFile.setData(
            make_managed_ini_data("W1AW", "FN31", "80m", false));
        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "deferred disable reload must remain pending until COMPLETE callback");
        require(
            config.transmit,
            "deferred disable reload must not mutate live config before COMPLETE callback");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        transmitter_cb(
            WsprTransmitter::TransmissionCallbackEvent::COMPLETE,
            WsprTransmitter::LogLevel::INFO,
            "managed post-tx reload regression",
            110.6);

        require(
            !ini_reload_pending.load(std::memory_order_relaxed),
            "deferred reload must be consumed immediately by the COMPLETE handoff");
        require(
            !config.transmit,
            "deferred disable reload must commit disabled live config at COMPLETE handoff");
        require(
            config.callsign == "W1AW" &&
                config.grid_square == "FN31" &&
                config.frequencies == "80m",
            "deferred disable reload must commit the latest candidate before any next scheduling pass");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0,
            "deferred disable reload must not schedule a second transmission after COMPLETE handoff");
        require(
            committed_frequency_before_reload != current_transmission_request_for_test().actual_rf_frequency_hz,
            "deferred disable reload must replace the prior committed request state");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        const std::string ini_path = "/tmp/managed_reload_file_backed_post_tx.ini";
        write_managed_ini_file(
            ini_path,
            make_managed_ini_data("AA0NT", "EM18", "80m", true));
        iniFile.set_filename(ini_path);

        config.use_ini = true;
        config.ini_filename = ini_path;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "80m";
        config.tx_pin = 4;
        set_frequencies(config);

        require(
            set_config(true),
            "file-backed managed enabled config must commit before deferred reload regression");
        require(
            nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, 3570100.0),
            "file-backed managed enabled config must commit the initial 80m request");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        write_managed_ini_file(
            ini_path,
            make_managed_ini_data("W1AW", "FN31", "80m", false));
        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "file-backed deferred disable reload must remain pending until COMPLETE callback");
        require(
            config.transmit,
            "file-backed deferred disable reload must not mutate live config during TX");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "file-backed deferred reload regression",
            110.592);

        require(
            !ini_reload_pending.load(std::memory_order_relaxed),
            "file-backed deferred reload must be consumed immediately after COMPLETE");
        require(
            !config.transmit,
            "file-backed deferred disable reload must commit disabled config");
        require(
            config.callsign == "W1AW" &&
                config.grid_square == "FN31" &&
                config.frequencies == "80m",
            "file-backed deferred disable reload must use the latest on-disk INI as the source of truth");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0,
            "file-backed deferred disable reload must not schedule a second transmission");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_reclassify_disable.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);

        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", true));
        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "the first managed reload during TX must leave reload pending");
        require(
            config.transmit,
            "the first managed reload during TX must not change live transmit state");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "the first managed reload during TX must keep the current TX running");

        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", false));
        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "a later valid disable edit during TX must remain deferred while the current TX is active");
        require(
            config.transmit,
            "a later valid disable edit during TX must not mutate live config before TX completion");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "a later valid disable edit during TX must not interrupt the active transmission");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        require(
            set_config(false),
            "the latest deferred managed reload must apply after TX completion");
        require(
            !config.transmit,
            "multiple rapid INI changes must coalesce so the latest valid disable wins");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_invalid_during_tx.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        iniFile.setData(
            make_managed_ini_data("<AA0NT>", "EM18", "20m", true));

        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "invalid reload during TX must remain deferred until the current transmission completes");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "invalid reload during TX must not interrupt the active transmission");
        require(
            config.callsign == "AA0NT",
            "invalid reload during TX must preserve last-known-good live config");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        require(
            set_config(false),
            "invalid managed reload after TX completion must remain recoverable");
        require(
            managed_reload_tx_inhibited_for_test(),
            "invalid reload during TX must inhibit future transmissions after the current TX completes");
        require(
            config.callsign == "AA0NT",
            "invalid reload after TX completion must still preserve last-known-good live config");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_locator_error.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "80m";
        config.tx_pin = 4;
        set_frequencies(config);

        iniFile.setData(
            make_managed_ini_data("AA0NT", "BAD", "80m", true));

        require(
            set_config(true),
            "invalid managed locator/type reload must remain recoverable");
        require(
            config.callsign == "AA0NT" &&
                config.grid_square == "EM18" &&
                config.frequencies == "80m",
            "invalid managed locator/type reload must preserve last-known-good live config");
        require(
            managed_reload_tx_inhibited_for_test(),
            "invalid managed locator/type reload must disable future transmissions until repaired");
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_enabled_during_tx.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_frequencies(config);
        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", true));
        set_scheduler_execution_suppressed_for_test(true);
        set_config(true);

        const double original_frequency_hz =
            current_transmission_request_for_test().actual_rf_frequency_hz;

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        iniFile.setData(
            make_managed_ini_data("W1AW", "FN31", "80m", true));

        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "valid enabled reload during TX must be deferred");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "valid enabled reload during TX must not interrupt the active transmission");
        require(
            config.callsign == "AA0NT",
            "valid enabled reload during TX must not mutate live config immediately");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        require(
            set_config(false),
            "valid enabled reload after TX completion must commit cleanly");
        require(
            config.callsign == "W1AW" &&
                config.grid_square == "FN31" &&
                config.frequencies == "80m",
            "valid enabled reload after TX completion must replace live config");
        require(
            nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, 3570100.0),
            "valid enabled reload after TX completion must commit the next request from the new config");
        require(
            !nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, original_frequency_hz),
            "valid enabled reload after TX completion must replace the prior committed request");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        config.ppm = 12.5;
        set_frequencies(config);

        ppm_reload_pending.store(true, std::memory_order_relaxed);

        require(
            set_config(true),
            "pending runtime PPM update must be consumable during scheduler commit");
        require(
            nearly_equal(current_transmission_request_for_test().ppm, 0.0),
            "committed request must consume the current PPM manager value");
        require(
            nearly_equal(config.ppm, current_transmission_request_for_test().ppm),
            "live runtime config must retain the committed PPM for later requests");
        require(
            !ppm_reload_pending.load(std::memory_order_relaxed),
            "consumed runtime PPM update must clear the pending flag");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        PreparedConfigCandidate candidate;
        iniFile.setData(
            make_managed_ini_data("W1AW", "FN31", "80m", false));
        prepare_ini_config_candidate("/tmp/managed_candidate.ini", candidate);
        require(
            candidate.valid,
            "prepared INI candidate must succeed for valid managed configuration");
        require(
            candidate.normalized_config.callsign == "W1AW" &&
                candidate.normalized_config.grid_square == "FN31",
            "prepared INI candidate must carry normalized configuration without mutating live config");
    }

    {
        require_patch_accepts_and_runtime_plans(
            make_identity_patch("AA0NT", "EM18"),
            "Type1Single",
            "AA0NT",
            "EM18",
            "valid Type 1 config patch");
    }

    {
        require_patch_accepts_and_runtime_plans(
            make_identity_patch("AA0NT/12", "EM18"),
            "Type2Single",
            "AA0NT/12",
            "EM18",
            "valid compound Type 2 config patch");
    }

    {
        require_patch_accepts_and_runtime_plans(
            make_identity_patch("<AA0NT>", "EM18IG"),
            "Type3Single",
            "<AA0NT>",
            "EM18IG",
            "valid explicit Type 3 config patch");
    }

    {
        require_patch_accepts_and_runtime_plans(
            make_identity_patch("AA0NT/12", "EM18IG"),
            "Type2Type3Paired",
            "AA0NT/12",
            "EM18IG",
            "valid compound callsign with 6-character locator config patch",
            2U,
            1U,
            "AA0NT/12",
            "EM18");
    }

    {
        require_patch_rejects_with_plan_status(
            make_identity_patch("<AA0NT>", "EM18"),
            "Type3RequiresSixCharLocator",
            "explicit Type 3 config patch with only a 4-character locator");
    }

    {
        prime_valid_runtime_identity_config();
        patch_all_from_web(make_identity_patch("aa0nt/12", "em18"));
        require(
            config.callsign == "AA0NT/12" &&
                config.grid_square == "EM18",
            "lowercase callsign and locator must be normalized during config acceptance");

        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "lowercase identity patch must still succeed during scheduler planning");
        const WsprTransmissionRequest request = current_transmission_request_for_test();
        require(
            request.wspr_plan.plan_type == "Type2Single" &&
                request.wspr_plan.callsign == "AA0NT/12" &&
                request.wspr_plan.locator == "EM18",
            "runtime planning must agree with config-boundary normalization for lowercase identities");
        finish_runtime_planning_state_for_identity_test();
    }

    {
        prime_valid_runtime_identity_config();
        patch_all_from_web(make_identity_patch("  aa0nt  ", "  em18  "));
        require(
            config.callsign == "AA0NT" &&
                config.grid_square == "EM18",
            "whitespace-surrounded identities must be trimmed and normalized in durable config");

        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "whitespace-surrounded identities must still succeed during scheduler planning");
        const WsprTransmissionRequest request = current_transmission_request_for_test();
        require(
            request.wspr_plan.plan_type == "Type1Single",
            "runtime planning must still classify whitespace-surrounded identities as a valid Type 1 plan");
        require(
            request.wspr_plan.callsign == "AA0NT" &&
                request.wspr_plan.locator == "EM18",
            "committed single-frame requests must use the same normalized identities as accepted config and planner results");
        finish_runtime_planning_state_for_identity_test();
    }

    {
        require_patch_accepts_and_runtime_plans(
            make_identity_patch(
                "AA0NT/12",
                "EM18IG",
                WsprPlannerPreference::RequirePaired),
            "Type2Type3Paired",
            "AA0NT/12",
            "EM18IG",
            "RequirePaired config patch with a plannable compound identity",
            2U,
            1U,
            "AA0NT/12",
            "EM18");
    }

    {
        require_patch_rejects_with_plan_status(
            make_identity_patch(
                "AA0NT",
                "EM18",
                WsprPlannerPreference::RequirePaired),
            "PairedTransmissionRequiresExtendedIdentity",
            "RequirePaired config patch with a plain Type 1 identity");
    }

    {
        prime_valid_runtime_identity_config();
        require_patch_rejects_with_plan_status(
            make_identity_patch("<AA0NT>", "EM18"),
            "Type3RequiresSixCharLocator",
            "invalid explicit Type 3 config patch");

        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "rejecting an invalid config patch must leave the prior valid config runnable");
        const WsprTransmissionRequest request = current_transmission_request_for_test();
        require(
            request.wspr_plan.plan_type == "Type1Single" &&
                request.wspr_plan.callsign == "AA0NT" &&
                request.wspr_plan.locator == "EM18",
            "invalid config patches must fail before scheduling and preserve the prior valid runtime plan");
        finish_runtime_planning_state_for_identity_test();
    }

    {
        prime_valid_runtime_identity_config();
        patch_all_from_web(make_identity_patch(
            "AA0NT/12",
            "EM18IG",
            WsprPlannerPreference::RequirePaired));

        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "paired runtime snapshot regression must commit the first frame");
        {
            const WsprRuntimeStatusSnapshot snapshot =
                current_tx_runtime_status_snapshot();
            require(
                snapshot.plan_type == "Type2Type3Paired" &&
                    snapshot.frame_count == 2U &&
                    snapshot.current_frame == 1U,
                "paired runtime snapshot must expose first-frame progress");
            require(
                snapshot.callsign_normalized == "AA0NT/12" &&
                    snapshot.locator_normalized == "EM18IG" &&
                    snapshot.frame_callsign == "AA0NT/12" &&
                    snapshot.frame_locator == "EM18",
                "paired runtime snapshot must expose overall normalized identity and Type 2 frame identity");
        }

        transmitter_cb(
            WsprTransmitter::TransmissionCallbackEvent::COMPLETE,
            WsprTransmitter::LogLevel::INFO,
            "",
            110.6);

        {
            const WsprRuntimeStatusSnapshot snapshot =
                current_tx_runtime_status_snapshot();
            require(
                snapshot.plan_type == "Type2Type3Paired" &&
                    snapshot.frame_count == 2U &&
                    snapshot.current_frame == 2U,
                "paired runtime snapshot must advance to the second frame after completion");
            require(
                snapshot.callsign_normalized == "AA0NT/12" &&
                    snapshot.locator_normalized == "EM18IG" &&
                    snapshot.frame_callsign == "<AA0NT/12>" &&
                    snapshot.frame_locator == "EM18IG",
                "paired runtime snapshot must expose Type 3 frame identity for the second frame");
        }
        finish_runtime_planning_state_for_identity_test();
    }

    {
        PreparedConfigCandidate candidate;
        iniFile.setData(
            make_managed_ini_data(
                "AA0NT/12",
                "EM18IG",
                "20m",
                true,
                WsprPlannerPreference::RequirePaired));
        prepare_ini_config_candidate("/tmp/managed_candidate.ini", candidate);
        require(
            candidate.valid,
            "managed INI candidate must succeed for a RequirePaired identity that can be planned");
        require(
            candidate.normalized_config.wspr_planner_preference ==
                WsprPlannerPreference::RequirePaired,
            "managed INI candidate must preserve Planner Preference = require_paired");
    }

    {
        PreparedConfigCandidate candidate;
        iniFile.setData(
            make_managed_ini_data("<AA0NT>", "EM18", "20m", true));
        prepare_ini_config_candidate("/tmp/managed_candidate.ini", candidate);
        require(
            !candidate.valid,
            "managed INI candidate must reject invalid explicit Type 3 identities during candidate preparation");
        require(
            candidate.error_details.value("plan_status", "") == "Type3RequiresSixCharLocator",
            "managed INI candidate rejection must surface planner status details");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/planner_preference_persist.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        config.wspr_planner_preference = WsprPlannerPreference::Auto;
        config_to_json();

        patch_all_from_web(make_identity_patch(
            "AA0NT/12",
            "EM18IG",
            WsprPlannerPreference::PreferPaired));

        const auto persisted_ini = iniFile.getData();
        const auto meta_it = persisted_ini.find("Meta");
        require(
            meta_it != persisted_ini.end(),
            "json_to_ini must persist the Meta section");
        require(
            meta_it->second.at("Planner Preference") == "prefer_paired",
            "json_to_ini must persist planner preference in the Meta section");
        require(
            meta_it->second.find("Require Paired Plan") == meta_it->second.end(),
            "json_to_ini must not persist the removed Require Paired Plan compatibility field");
    }

    {
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(true, std::memory_order_relaxed);

        callback_ini_changed();

        require(
            !ini_reload_pending.load(std::memory_order_relaxed),
            "INI reload callback must not queue reload work after shutdown starts");

        exiting_wspr.store(false, std::memory_order_relaxed);
    }

    std::cout << "dial_frequency_semantics_test passed" << std::endl;
    return EXIT_SUCCESS;
}
