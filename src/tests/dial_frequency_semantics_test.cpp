#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "frequency_semantics.hpp"
#include "scheduling.hpp"
#include "wspr_band_lookup.hpp"
#include "wspr_transmit_backend_si5351.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
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

    void write_text_file(const std::string &path, const std::string &contents)
    {
        std::ofstream out(path, std::ios::trunc);
        require(out.is_open(), "test helper must write " + path);
        out << contents;
    }

    std::string read_text_file(const std::string &path)
    {
        std::ifstream in(path);
        require(in.is_open(), "test helper must read " + path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
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

    std::string capture_print_usage_output(int exit_code)
    {
        int pipefd[2] = {-1, -1};
        require(pipe(pipefd) == 0, "help-output test must create a pipe");

        pid_t pid = fork();
        require(pid >= 0, "help-output test must fork a child process");

        if (pid == 0)
        {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            print_usage(exit_code);
            std::exit(EXIT_SUCCESS);
        }

        close(pipefd[1]);
        std::string output;
        char buffer[4096];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0)
        {
            output.append(buffer, static_cast<std::size_t>(bytes_read));
        }
        close(pipefd[0]);

        int status = 0;
        require(waitpid(pid, &status, 0) == pid, "help-output test must wait for the child process");
        require(
            WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS,
            "help-output test child must exit successfully");
        return output;
    }

    void require_cli_parse_rejected(
        std::vector<std::string> args,
        const std::string &message)
    {
        pid_t pid = fork();
        require(pid >= 0, message + " must be able to fork a child process");

        if (pid == 0)
        {
            reset_getopt_state();
            std::vector<char *> argv = argv_for(args);
            (void)parse_command_line(static_cast<int>(argv.size()), argv.data());
            std::exit(EXIT_SUCCESS);
        }

        int status = 0;
        require(waitpid(pid, &status, 0) == pid, message + " must wait for the child process");
        require(
            WIFEXITED(status) && WEXITSTATUS(status) != EXIT_SUCCESS,
            message + " must reject the CLI input");
    }

    std::map<std::string, std::unordered_map<std::string, std::string>>
    make_disabled_band_gpio_ini_data()
    {
        std::map<std::string, std::unordered_map<std::string, std::string>> data;
        for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
        {
            const std::string band_name = band_to_string(static_cast<HamBand>(band_index));
            data["Band GPIO"][band_name] = "";
            data["Band GPIO"][band_name + " Active High"] = "false";
        }
        return data;
    }

    void require_all_band_gpio_disabled(
        const std::array<BandGPIOConfig, HAM_BAND_COUNT> &band_gpio,
        const std::string &message)
    {
        for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
        {
            const BandGPIOConfig &band_config = band_gpio[band_index];
            require(
                band_config.gpio == -1 &&
                    !band_config.enabled &&
                    !band_config.active_high,
                message + " for " +
                    std::string(band_to_string(static_cast<HamBand>(band_index))));
        }
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
             {{"debug_logging", "false"}}},
            {"Operation",
             {{"Mode", "WSPR"},
              {"Transmit", transmit ? "true" : "false"},
              {"Transmit Backend", "gpio"},
              {"Use LED", "false"},
              {"LED Pin", "-1"},
              {"Web Port", "-1"},
              {"Socket Port", "-1"},
              {"Use Shutdown", "false"},
              {"Shutdown Button", "-1"}}},
            {"GPIO",
             {{"Transmit Pin", "4"},
              {"Power Level", "7"},
              {"Use NTP", "false"}}},
            {"Calibration",
             {{"PPM", "0"}}},
            {"Si5351",
             {{"I2C Bus", "1"},
              {"I2C Address", "96"},
              {"Reference Frequency", "27000000"},
              {"TX Output", "CLK0"},
              {"Power Level", "1"}}},
            {"WSPR",
             {{"Call Sign", callsign},
              {"Grid Square", grid_square},
              {"TX Power", "20"},
              {"Frequency", frequency},
              {"Planner Preference",
               wspr_planner_preference_to_string(planner_preference)},
             {"Use Random Offset", "false"}}},
            {"CW",
             {{"Message", ""},
              {"Base Frequency", "14096900.0"},
              {"Shift Hz", "5.0"},
              {"Dot Seconds", "3.0"},
              {"Intra Element Gap", "1.0"},
              {"Inter Character Gap", "3.0"},
              {"Inter Word Gap", "7.0"},
              {"Fade Shape", "none"},
              {"Fade In Ms", "0"},
              {"Fade Out Ms", "0"},
              {"Fade Slice Ms", "5"},
              {"Start Minute", "0"},
              {"Repeat Minutes", "10"}}}};
        const auto disabled_band_gpio = make_disabled_band_gpio_ini_data();
        data.insert(disabled_band_gpio.begin(), disabled_band_gpio.end());
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
        config.transmit_backend = TransmitBackendKind::GPIO;
        config.gpio_tx_pin = 4;
        config.gpio_power_level = 7;
        config.gpio_use_ntp = false;
        config.wspr_planner_preference = WsprPlannerPreference::Auto;
        config.wspr.callsign = config.callsign;
        config.wspr.grid_square = config.grid_square;
        config.wspr.power_dbm = config.power_dbm;
        config.wspr.frequencies = config.frequencies;
        config.wspr.audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
        config.wspr.planner_preference = config.wspr_planner_preference;
        resolve_backend_specific_config(config);
        set_frequencies(config);
        config_to_json();
    }

    nlohmann::json make_identity_patch(
        const std::string &callsign,
        const std::string &grid_square,
        WsprPlannerPreference planner_preference = WsprPlannerPreference::Auto)
    {
        return {
            {"WSPR",
             {{"Call Sign", callsign},
              {"Grid Square", grid_square},
              {"TX Power", 20},
              {"Frequency", "20m"},
              {"Planner Preference",
               wspr_planner_preference_to_string(planner_preference)},
              {"Use Random Offset", false}}},
            {"Operation", {{"Transmit", true}}}};
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

    void clear_pi_generation_override_for_scope() noexcept
    {
        clear_raspberry_pi_generation_override_for_test();
    }

    void clear_si5351_detection_override_for_scope() noexcept
    {
        set_si5351_detection_override_for_test(true);
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
        const TransmissionRequest request = current_transmission_request_for_test();
        require(
            request.mode == TransmissionMode::WSPR,
            message + " must commit a WSPR execution request");
        require(
            request.payload.plan_type == expected_plan_type,
            message + " must commit the expected plan type");
        require(
            request.payload.callsign == expected_callsign,
            message + " must commit the normalized callsign expected by runtime planning");
        require(
            request.payload.locator == expected_locator,
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
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    set_si5351_detection_override_for_test(true);

    WSPRBandLookup lookup;

    require(
        nearly_equal(lookup.parse_string_to_frequency("20m", false), 14095600.0),
        "20m must resolve to the WSPR dial frequency");
    require(
        nearly_equal(lookup.parse_string_to_frequency("2200m", false), 136000.0),
        "2200m must resolve to the WSPR dial frequency");
    require(
        nearly_equal(lookup.parse_string_to_frequency("lf", false), 136000.0),
        "lf must resolve to the 2200m WSPR dial frequency");
    require(
        nearly_equal(lookup.parse_string_to_frequency("630m", false), 474200.0),
        "630m must resolve to the WSPR dial frequency");
    require(
        nearly_equal(lookup.parse_string_to_frequency("mf", false), 474200.0),
        "mf must resolve to the 630m WSPR dial frequency");
    require(
        nearly_equal(lookup.parse_string_to_frequency("22m", false), 13551500.0),
        "22m must resolve to the WSPR dial frequency");
    require(
        lookup.lookup_ham_band(14095600.0).has_value(),
        "band lookup should still succeed on WSPR dial frequency values");

    {
        init_default_config();
        require_all_band_gpio_disabled(
            config.band_gpio,
            "init_default_config must leave Band GPIO defaults disabled");

        config_to_json();
        for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
        {
            const std::string band_name =
                band_to_string(static_cast<HamBand>(band_index));
            require(
                jConfig["Band GPIO"][band_name].value("GPIO", 0) == -1 &&
                    !jConfig["Band GPIO"][band_name].value("Enabled", true) &&
                    !jConfig["Band GPIO"][band_name].value("Active High", true),
                "config_to_json must persist disabled Band GPIO defaults for " + band_name);
        }
    }

    {
        init_config_json();
        json_to_config();
        require_all_band_gpio_disabled(
            config.band_gpio,
            "init_config_json/json_to_config must normalize Band GPIO defaults to disabled");

        jConfig.erase("Band GPIO");
        json_to_config();
        require_all_band_gpio_disabled(
            config.band_gpio,
            "missing Band GPIO section must normalize to disabled defaults");
    }

    {
        const std::string stock_ini =
            read_text_file("/home/pi/WsprryPi/config/wsprrypi.ini");
        require(
            stock_ini.find("2200m = \n2200m Active High = false") != std::string::npos &&
                stock_ini.find("20m = \n20m Active High = false") != std::string::npos &&
                stock_ini.find("2m =\n2m Active High = false") != std::string::npos &&
                stock_ini.find("Active High = true") == std::string::npos,
            "stock INI must declare explicit disabled Band GPIO defaults for every band");
    }

    for (const std::string &removed_alias : {
             std::string("lf-15"),
             std::string("2200m-15"),
             std::string("mf-15"),
             std::string("630m-15"),
             std::string("160m-15"),
         })
    {
        bool threw = false;
        try
        {
            (void)lookup.parse_string_to_frequency(removed_alias, false);
        }
        catch (const std::invalid_argument &)
        {
            threw = true;
        }

        require(
            threw,
            removed_alias + " must no longer resolve as a supported WSPR alias");
    }

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
        set_raspberry_pi_generation_override_for_test(4);
        prime_valid_runtime_identity_config();
        reset_runtime_planning_state_for_identity_test();

        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "GPIO backend must remain allowed on Raspberry Pi 4");
        require(
            set_config(true),
            "scheduler must allow GPIO execution requests on Raspberry Pi 4");
        require(
            current_transmission_request_for_test().mode == TransmissionMode::WSPR,
            "scheduler must commit a normal WSPR request for GPIO on Raspberry Pi 4");

        finish_runtime_planning_state_for_identity_test();
        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        prime_valid_runtime_identity_config();

        std::string validation_error;
        require(
            !validate_config_candidate(config, &validation_error),
            "GPIO backend must be rejected on Raspberry Pi 5");
        require(
            validation_error.find(
                "GPIO transmission mode is unsupported on Raspberry Pi 5 and newer.") !=
                std::string::npos,
            "GPIO backend rejection on Raspberry Pi 5 must explain the unsupported platform");

        PreparedConfigCandidate candidate;
        iniFile.setData(make_managed_ini_data("AA0NT", "EM18", "20m", true));
        prepare_ini_config_candidate("/tmp/gpio_pi5.ini", candidate);
        require(
            !candidate.valid,
            "managed GPIO configuration must be rejected on Raspberry Pi 5");
        require(
            candidate.error_reason.find(
                "GPIO transmission mode is unsupported on Raspberry Pi 5 and newer.") !=
                std::string::npos,
            "managed GPIO rejection on Raspberry Pi 5 must preserve the platform error");

        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        prime_valid_runtime_identity_config();
        config.transmit = false;

        std::string validation_error;
        require(
            !validate_config_candidate(config, &validation_error),
            "GPIO backend must remain invalid on Raspberry Pi 5 even when transmit is off");
        require(
            validation_error.find(
                "GPIO transmission mode is unsupported on Raspberry Pi 5 and newer.") !=
                std::string::npos,
            "GPIO backend invalidity on Raspberry Pi 5 must not depend on transmit state");

        clear_pi_generation_override_for_scope();
    }

    {
        config.enable_web = false;
        init_config_json();
        json_to_config();
        config.use_ini = false;
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--transmit-gpio",
            "4",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "CLI parsing without --no-web must succeed");
        require(
            config.enable_web,
            "CLI parsing without --no-web must leave the web runtime enabled by default");
        require(
            web_server_start_enabled(config),
            "startup gating must allow the HTTP server by default");
        require(
            websocket_server_start_enabled(config),
            "startup gating must allow the websocket server by default");
    }

    {
        init_config_json();
        json_to_config();
        config.use_ini = false;
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--no-web",
            "--transmit-gpio",
            "4",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "CLI parsing with --no-web must succeed");
        require(
            !config.enable_web,
            "CLI parsing with --no-web must disable the web runtime");
        require(
            !web_server_start_enabled(config),
            "startup gating must skip the HTTP server when --no-web is present");
        require(
            !websocket_server_start_enabled(config),
            "startup gating must skip the websocket server when --no-web is present");
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        reset_current_transmission_request_for_test();
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--backend",
            "gpio",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "CLI GPIO setup on Raspberry Pi 5 must still parse before validation");
        std::string validation_error;
        require(
            !validate_config_data_for_test(&validation_error),
            "CLI startup validation must fail for GPIO on Raspberry Pi 5");
        require(
            validation_error.find(
                "GPIO transmission mode is unsupported on Raspberry Pi 5 and newer.") !=
                std::string::npos,
            "CLI startup validation on Raspberry Pi 5 must report the GPIO platform error");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                current_transmission_request_for_test().payload.frames.empty(),
            "CLI startup failure on Raspberry Pi 5 must not commit a transmission request");

        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.si5351_i2c_bus = 1;
        config.si5351_i2c_address = 0x60;
        config.si5351_reference_hz = 27000000;
        config.si5351_tx_output = 0;
        config.si5351_power_level = 1;
        resolve_backend_specific_config(config);
        set_si5351_detection_override_for_test(true);

        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "non-GPIO backends must remain allowed on Raspberry Pi 5");

        clear_si5351_detection_override_for_scope();
        clear_pi_generation_override_for_scope();
    }

    {
        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.si5351_i2c_bus = 1;
        config.si5351_i2c_address = 0x60;
        config.si5351_reference_hz = 27000000;
        config.si5351_tx_output = 0;
        config.si5351_power_level = 1;
        resolve_backend_specific_config(config);
        set_si5351_detection_override_for_test(false);

        std::string validation_error;
        require(
            !validate_config_candidate(config, &validation_error),
            "Si5351 backend must reject transmit-enabled validation when no device is detected");
        require(
            validation_error.find(
                "Si5351 transmission is unavailable because no Si5351 device was detected on the I2C bus.") !=
                std::string::npos,
            "Si5351 missing-device validation must explain why transmission is unavailable");

        config.transmit = false;
        config.use_ini = true;
        validation_error.clear();
        require(
            validate_config_candidate(config, &validation_error),
            "Si5351 backend must remain config-valid for editing when no device is detected and transmit is off");

        PreparedConfigCandidate candidate;
        auto managed_ini = make_managed_ini_data("AA0NT", "EM18", "20m", true);
        managed_ini["Operation"]["Transmit Backend"] = "si5351";
        iniFile.setData(managed_ini);
        prepare_ini_config_candidate("/tmp/si5351_missing.ini", candidate);
        require(
            !candidate.valid,
            "managed Si5351 configuration must be rejected when transmit is enabled but no device is detected");
        require(
            candidate.error_reason.find(
                "Si5351 transmission is unavailable because no Si5351 device was detected on the I2C bus.") !=
                std::string::npos,
            "managed Si5351 rejection must preserve the missing-device error");

        clear_si5351_detection_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);

        WsprSi5351Backend::Config si5351_config;
        si5351_config.device.i2c_bus = 1;
        si5351_config.device.i2c_address = 0x60;
        si5351_config.device.reference_hz = 27000000;
        si5351_config.planner.reference_hz = 27000000;
        si5351_config.planner.tx_output = Si5351Device::Output::CLK0;
        si5351_config.power_level = 1;
        si5351_config.dry_run = true;

        WsprSi5351Backend backend(wsprTransmitter, si5351_config);

        wsprrypi::ExecutionPlan plan;
        plan.id.value = 1;
        plan.request_id.value = 1;
        plan.mode = wsprrypi::TransmissionMode::TONE;
        plan.backend = wsprrypi::BackendKind::SI5351;
        plan.reference_frequency_hz = 14097100.0;
        plan.events.push_back(wsprrypi::RfEvent{
            std::chrono::nanoseconds{0},
            std::chrono::milliseconds{1},
            wsprrypi::RfEventType::RF_ON,
            14097100.0,
            true,
            {}});
        plan.summary.total_duration = std::chrono::milliseconds{1};
        plan.summary.event_count = plan.events.size();
        plan.summary.min_frequency_hz = 14097100.0;
        plan.summary.max_frequency_hz = 14097100.0;

        wsprrypi::BackendExecutionInputs inputs;
        inputs.power_level = 1;

        const wsprrypi::BackendCompileResult compile_result =
            backend.configure(plan, inputs);
        require(
            compile_result.ok,
            "Si5351 backend configure path must remain allowed on Raspberry Pi 5");

        const wsprrypi::ExecutionResult execute_result = backend.execute(plan);
        require(
            execute_result.ok,
            "Si5351 backend execution path must remain allowed on Raspberry Pi 5");

        clear_pi_generation_override_for_scope();
    }

    {
        init_config_json();
        jConfig["WSPR"]["Frequency"] = "20m";
        jConfig["Meta"][std::string("Center ") + "Frequency Set"] =
            nlohmann::json::array({14095600.0});
        if (jConfig.contains("WSPR") && jConfig["WSPR"].is_object())
            jConfig["WSPR"].erase("WSPR Audio Offset Hz");
        json_to_config();

        require(
            config.wspr_dial_freq_set.empty(),
            "legacy center-frequency list must be ignored");
        require(
            nearly_equal(config.wspr_audio_offset_hz, 1500.0),
            "missing WSPR Audio Offset Hz must default to 1500.0");
    }

    {
        init_config_json();
        jConfig["WSPR"]["WSPR Dial Frequency Set"] =
            nlohmann::json::array({14095600.0});
        jConfig["WSPR"]["Frequency"] = "20m";
        json_to_config();

        require(
            config.wspr_dial_freq_set.empty(),
            "external WSPR.WSPR Dial Frequency Set input must be ignored");
        require(
            nearly_equal(config.wspr_audio_offset_hz, 1500.0),
            "WSPR audio offset must use the runtime constant");
        require(
            config.wspr.frequencies == "20m",
            "WSPR.Frequency must remain the authoritative external WSPR frequency input");
    }

    {
        init_config_json();
        if (jConfig.contains("WSPR") && jConfig["WSPR"].is_object())
        {
            jConfig["WSPR"].erase("WSPR Dial Frequency Set");
        }
        json_to_config();

        require(
            config.wspr_dial_freq_set.empty(),
            "missing WSPR frequency-set keys must load as an empty dial-frequency list");
    }

    {
        init_config_json();
        jConfig["WSPR"]["WSPR Dial Frequency Set"] = nlohmann::json::array();
        jConfig["Meta"]["WSPR Dial Frequency Set"] =
            nlohmann::json::array({10138700.0});
        json_to_config();

        require(
            config.wspr_dial_freq_set.empty(),
            "empty WSPR.WSPR Dial Frequency Set must not fall back to a legacy frequency list");
    }

    {
        init_config_json();
        config_to_json();

        require(
            jConfig.contains("WSPR") &&
                jConfig["WSPR"].is_object() &&
                !jConfig["WSPR"].contains("WSPR Dial Frequency Set"),
            "internal config JSON export must not expose WSPR Dial Frequency Set");

        const nlohmann::json public_config = get_public_config_json();
        require(
            public_config.contains("WSPR") &&
                public_config["WSPR"].is_object() &&
                !public_config["WSPR"].contains("WSPR Dial Frequency Set"),
            "public config JSON must not expose WSPR Dial Frequency Set");
    }

    {
        const std::optional<std::string> legacy_alias =
            lookup.legacy_actual_wspr_alias_for_frequency(14097100.0);
        require(
            legacy_alias.has_value() && *legacy_alias == "20m",
            "legacy actual RF alias detection must identify 20m");
    }

    {
        require(
            !lookup.legacy_actual_wspr_alias_for_frequency(137612.5).has_value() &&
                !lookup.legacy_actual_wspr_alias_for_frequency(475812.5).has_value() &&
                !lookup.legacy_actual_wspr_alias_for_frequency(1838212.5).has_value(),
            "removed -15 aliases must no longer be reported as legacy actual RF aliases");
    }

    {
        init_config_json();
        json_to_config();
        config.use_ini = true;
        config.transmit = true;
        config.frequencies = "80m@17H,40m@27L,20m,14.097100MHz@22";

        require(
            set_frequencies(config),
            "mixed frequency lists with optional @GPIO suffixes must parse");
        require(
            config.wspr_frequency_entries.size() == 4,
            "parsed frequency entries must preserve all configured tokens");
        require(
            config.wspr_frequency_entries[0].selector_gpio == 17 &&
                config.wspr_frequency_entries[0].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[0].dial_frequency_hz, 3568600.0),
            "80m@17H must retain the GPIO mapping, polarity, and dial frequency");
        require(
            config.wspr_frequency_entries[1].selector_gpio == 27 &&
                !config.wspr_frequency_entries[1].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[1].dial_frequency_hz, 7038600.0),
            "40m@27L must retain the GPIO mapping, polarity, and dial frequency");
        require(
            config.wspr_frequency_entries[2].selector_gpio == kSelectorGpioUnset &&
                !config.wspr_frequency_entries[2].selector_gpio_active_high &&
                !config.wspr_frequency_entries[2].allow_band_gpio_fallback &&
                nearly_equal(config.wspr_frequency_entries[2].dial_frequency_hz, 14095600.0),
            "plain frequency entries must remain unmapped, explicit-only, and default active low even on managed/INI paths");
        require(
            config.wspr_frequency_entries[3].selector_gpio == 22 &&
                !config.wspr_frequency_entries[3].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[3].dial_frequency_hz, 14097100.0),
            "unit-qualified frequencies must also support @GPIO with default active-low polarity");
    }

    {
        init_config_json();
        if (jConfig.contains("CW") && jConfig["CW"].is_object())
        {
            jConfig["CW"].erase("Base Frequency");
        }
        json_to_config();

        require(
            nearly_equal(config.qrss.frequency_hz, 14096900.0) &&
                nearly_equal(config.fskcw.space_frequency_hz, 14096900.0) &&
                nearly_equal(config.fskcw.mark_frequency_hz, 14096905.0) &&
                nearly_equal(config.dfcw.dot_frequency_hz, 14096900.0) &&
                nearly_equal(config.dfcw.dash_frequency_hz, 14096905.0),
            "missing CW.Base Frequency must normalize to the canonical 14096900 Hz default for all CW runtime modes");

        config_to_json();
        require(
            nearly_equal(jConfig["CW"].at("Base Frequency").get<double>(), 14096900.0),
            "canonical JSON defaults must persist the 14096900 Hz CW.Base Frequency default");
    }

    {
        init_default_config();
        config.mode = ModeType::WSPR;
        config.qrss.frequency_hz = 0.0;
        config_to_json();

        require(
            nearly_equal(jConfig["CW"].at("Base Frequency").get<double>(), 14096900.0),
            "config_to_json must preserve the canonical CW.Base Frequency default when runtime CW state is unset");
    }

    for (const auto &[raw_value, expected_hz] : std::vector<std::pair<std::string, double>>{
             {"14096900", 14096900.0},
             {"14096900.0", 14096900.0},
             {"14096900Hz", 14096900.0},
             {"14096.9kHz", 14096900.0},
             {"14.0969MHz", 14096900.0},
             {"0.0140969GHz", 14096900.0},
             {"  14.0969MHz  ", 14096900.0},
         })
    {
        init_config_json();
        jConfig["CW"]["Base Frequency"] = raw_value;
        json_to_config();

        require(
            nearly_equal(config.qrss.frequency_hz, expected_hz) &&
                nearly_equal(config.fskcw.space_frequency_hz, expected_hz) &&
                nearly_equal(config.dfcw.dot_frequency_hz, expected_hz),
            "CW.Base Frequency string forms must normalize to the expected Hz value");

        config_to_json();
        require(
            nearly_equal(jConfig["CW"].at("Base Frequency").get<double>(), expected_hz),
            "normalized CW.Base Frequency must round-trip back to canonical numeric Hz");
    }

    for (const std::string &raw_value : {
             std::string("14.0969"),
             std::string("14096900xb"),
             std::string("14.0969m"),
             std::string("14..0969MHz"),
         })
    {
        init_config_json();
        jConfig["CW"]["Base Frequency"] = raw_value;

        bool threw = false;
        try
        {
            json_to_config();
        }
        catch (const std::runtime_error &)
        {
            threw = true;
        }

        require(
            threw,
            "invalid CW.Base Frequency string forms must be rejected during backend normalization");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/cw_base_frequency_units.ini";
        write_text_file(
            config.ini_filename,
            "[Meta]\nUse INI=true\nDate Time Log=false\ndebug_logging=false\nLoop TX=false\nTX Iterations=0\n"
            "[Operation]\nMode=QRSS\nTransmit=false\nTransmit Backend=gpio\nUse LED=false\nLED Pin=-1\nWeb Port=31415\nSocket Port=31416\nUse Shutdown=false\nShutdown Button=-1\n"
            "[GPIO]\nTransmit Pin=4\nPower Level=7\nUse NTP=false\n"
            "[Calibration]\nPPM=0\n"
            "[Si5351]\nI2C Bus=1\nI2C Address=96\nReference Frequency=27000000\nTX Output=CLK0\nPower Level=1\n"
            "[WSPR]\nCall Sign=AA0NT\nGrid Square=EM18\nTX Power=20\nFrequency=20m\nPlanner Preference=auto\nUse Random Offset=false\n"
            "[CW]\nMessage=CQ\nBase Frequency= 14.0969MHz \nShift Hz=5\nDot Seconds=3.0\nIntra Element Gap=1.0\nInter Character Gap=3.0\nInter Word Gap=7.0\nFade Shape=none\nFade In Ms=0\nFade Out Ms=0\nFade Slice Ms=5\nStart Minute=0\nRepeat Minutes=10\n");
        iniFile.set_filename(config.ini_filename);

        std::string load_error;
        require(
            load_json(config.ini_filename, &load_error, nullptr),
            "load_json must accept unit-qualified CW.Base Frequency values from INI");
        require(
            nearly_equal(config.qrss.frequency_hz, 14096900.0),
            "INI-backed CW.Base Frequency values must normalize to numeric Hz");

        config_to_json();
        require(
            nearly_equal(jConfig["CW"].at("Base Frequency").get<double>(), 14096900.0),
            "INI-backed CW.Base Frequency values must round-trip through JSON as numeric Hz");
    }

    {
        prime_valid_runtime_identity_config();
        config.use_ini = true;
        config.enable_web = true;
        config.transmit = false;
        config.ini_filename = "/tmp/web_band_gpio_explicit_only.ini";
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);
        config_to_json();

        patch_all_from_web({
            {"WSPR", {{"Frequency", "80m,40m"}}},
            {"Band GPIO",
             {{"80m", {{"GPIO", 17}, {"Enabled", true}, {"Active High", true}}},
              {"40m", {{"GPIO", 27}, {"Enabled", true}, {"Active High", false}}}}}});

        require(
            config.wspr_frequency_entries.size() == 2U &&
                config.wspr_frequency_entries[0].selector_gpio == kSelectorGpioUnset &&
                !config.wspr_frequency_entries[0].allow_band_gpio_fallback &&
                config.wspr_frequency_entries[1].selector_gpio == kSelectorGpioUnset &&
                !config.wspr_frequency_entries[1].allow_band_gpio_fallback,
            "web patch normalization must keep plain frequency entries explicit-only even when Band GPIO config exists");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/wspr_dial_frequency_set_internal_only.ini";
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.transmit = false;
        config.frequencies = "20m";
        config_to_json();

        patch_all_from_web({
            {"WSPR",
             {{"Frequency", "20m"},
              {"WSPR Dial Frequency Set", nlohmann::json::array({10138700.0})}}}});

        require(
            config.wspr_dial_freq_set.size() == 1U &&
                nearly_equal(config.wspr_dial_freq_set.front(), 14095600.0),
            "web patch input must ignore WSPR Dial Frequency Set and derive the active list from WSPR.Frequency instead");
        require(
            jConfig.contains("WSPR") &&
                jConfig["WSPR"].is_object() &&
                !jConfig["WSPR"].contains("WSPR Dial Frequency Set"),
            "web patch persistence must strip WSPR Dial Frequency Set from internal JSON");
        require(
            !get_public_config_json()["WSPR"].contains("WSPR Dial Frequency Set"),
            "public config JSON must stay free of WSPR Dial Frequency Set after ignored patch input");
    }

    {
        init_config_json();
        json_to_config();
        config.transmit = true;
        config.frequencies = "0,2200m,630m,22m@17H,20m@27L,14.097100MHz@22";

        require(
            set_frequencies(config),
            "comma-separated WSPR lists must accept 0 skip windows, remaining band aliases, and optional @GPIO suffixes");
        require(
            config.wspr_frequency_entries.size() == 6,
            "comma-separated WSPR lists must preserve every supported entry");
        require(
            nearly_equal(config.wspr_frequency_entries[0].dial_frequency_hz, 0.0) &&
                config.wspr_frequency_entries[0].selector_gpio == kSelectorGpioUnset,
            "0 must remain a skip-window entry");
        require(
            nearly_equal(config.wspr_frequency_entries[1].dial_frequency_hz, 136000.0),
            "2200m must remain a supported WSPR alias");
        require(
            nearly_equal(config.wspr_frequency_entries[2].dial_frequency_hz, 474200.0),
            "630m must remain a supported WSPR alias");
        require(
            config.wspr_frequency_entries[3].selector_gpio == 17 &&
                config.wspr_frequency_entries[3].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[3].dial_frequency_hz, 13551500.0),
            "22m@17H must retain the GPIO mapping, polarity, and dial frequency");
        require(
            config.wspr_frequency_entries[4].selector_gpio == 27 &&
                !config.wspr_frequency_entries[4].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[4].dial_frequency_hz, 14095600.0),
            "20m@27L must retain the GPIO mapping, polarity, and dial frequency");
        require(
            config.wspr_frequency_entries[5].selector_gpio == 22 &&
                !config.wspr_frequency_entries[5].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[5].dial_frequency_hz, 14097100.0),
            "unit-qualified frequencies must remain compatible with comma-separated lists and @GPIO");
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

        TransmissionRequest committed_request;
        committed_request.mode = TransmissionMode::WSPR;
        committed_request.actual_rf_frequency_hz = 14097100.0;
        committed_request.ppm = 1.75;
        committed_request.power_level = 5;
        committed_request.tx_gpio = 20;
        committed_request.use_offset = true;
        committed_request.applied_offset_hz = 42.0;
        committed_request.frequency_entry_label = "20m@17";
        committed_request.payload.frames.resize(2);

        transmitter.current_request_ = committed_request;

        init_config_json();
        json_to_config();
        config.ppm = 99.0;
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);

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
        init_default_config();
        require(
            !config.debug_logging,
            "default configuration must disable debug logging");
        require(
            jConfig["Meta"].contains("debug_logging") &&
                !jConfig["Meta"]["debug_logging"].get<bool>(),
            "default JSON config must serialize debug_logging as false");
    }

    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::QRSS;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        wsprrypi::QrssPayload payload;
        payload.message = "AB";
        payload.frequency_hz = 7038600.0;
        payload.timing.dot = std::chrono::seconds(1);
        payload.timing.dash = std::chrono::seconds(3);
        payload.timing.intra_element_gap = std::chrono::seconds(1);
        payload.timing.inter_character_gap = std::chrono::seconds(3);
        payload.timing.inter_word_gap = std::chrono::seconds(7);
        request.payload = payload;

        const wsprrypi::ExecutionPlan plan =
            wsprrypi::ExecutionPlanCompiler{}.compile(request);

        bool saw_first_char = false;
        bool saw_second_char = false;
        bool saw_transition = false;
        int prior_index = -1;
        for (const auto &event : plan.events)
        {
            if (event.message_char_index == 0)
            {
                saw_first_char = true;
            }
            else if (event.message_char_index == 1)
            {
                saw_second_char = true;
            }

            if (prior_index != -1 && event.message_char_index != prior_index)
            {
                saw_transition = true;
            }
            prior_index = event.message_char_index;
        }

        require(
            saw_first_char && saw_second_char && saw_transition,
            "compiled CW execution plans must carry progressing message_char_index values across multiple characters");
    }

    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::QRSS;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        wsprrypi::QrssPayload payload;
        payload.message = "A A";
        payload.frequency_hz = 7038600.0;
        payload.timing.dot = std::chrono::seconds(1);
        payload.timing.dash = std::chrono::seconds(3);
        payload.timing.intra_element_gap = std::chrono::seconds(1);
        payload.timing.inter_character_gap = std::chrono::seconds(3);
        payload.timing.inter_word_gap = std::chrono::seconds(7);
        request.payload = payload;

        const wsprrypi::ExecutionPlan plan =
            wsprrypi::ExecutionPlanCompiler{}.compile(request);

        bool saw_first_char = false;
        bool saw_space_char = false;
        bool saw_second_char = false;
        for (const auto &event : plan.events)
        {
            if (event.message_char_index == 0)
            {
                saw_first_char = true;
            }
            else if (event.message_char_index == 1)
            {
                saw_space_char = true;
            }
            else if (event.message_char_index == 2)
            {
                saw_second_char = true;
            }
        }

        require(
            saw_first_char && saw_space_char && saw_second_char,
            "compiled CW execution plans must tag inter-word gaps with the space character index");
    }

    {
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--planner-preference",
            "auto",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "--planner-preference auto CLI parsing must succeed");
        require(
            config.wspr_planner_preference == WsprPlannerPreference::Auto &&
                config.wspr.planner_preference == WsprPlannerPreference::Auto,
            "--planner-preference auto must select the canonical auto planner preference");
    }

    {
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--planner-preference",
            "prefer_paired",
            "AA0NT/12",
            "EM18IG",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "--planner-preference prefer_paired CLI parsing must succeed");
        require(
            config.wspr_planner_preference == WsprPlannerPreference::PreferPaired &&
                config.wspr.planner_preference == WsprPlannerPreference::PreferPaired,
            "--planner-preference prefer_paired must select the canonical prefer_paired planner preference");
    }

    {
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--planner-preference",
            "require_paired",
            "AA0NT/12",
            "EM18IG",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "--planner-preference require_paired CLI parsing must succeed");
        require(
            config.wspr_planner_preference == WsprPlannerPreference::RequirePaired &&
                config.wspr.planner_preference == WsprPlannerPreference::RequirePaired,
            "--planner-preference require_paired must select the canonical require_paired planner preference");
    }

    {
        const std::string help_output = capture_print_usage_output(0);
        require(
            help_output.find("--use-ntp") != std::string::npos &&
                help_output.find("--repeat") != std::string::npos &&
                help_output.find("--offset") != std::string::npos &&
                help_output.find("--journald") != std::string::npos &&
                help_output.find("--date-time-log") != std::string::npos &&
                help_output.find("--terminate <count>") != std::string::npos &&
                help_output.find("--planner-preference <auto|prefer_paired|require_paired>") != std::string::npos &&
                help_output.find("--qrss-message <text>") != std::string::npos &&
                help_output.find("--fskcw-message <text>") != std::string::npos &&
                help_output.find("--dfcw-message <text>") != std::string::npos &&
                help_output.find("--require-paired") == std::string::npos,
            "CLI help output must enumerate the current public option surface and must not mention removed planner flags");
    }

    {
        require_cli_parse_rejected(
            {
                "wsprrypi",
                "--planner-preference",
                "prefer-paired",
                "AA0NT/12",
                "EM18IG",
                "20",
                "20m"},
            "--planner-preference prefer-paired");
    }

    {
        require_cli_parse_rejected(
            {
                "wsprrypi",
                "--planner-preference",
                "require-paired",
                "AA0NT/12",
                "EM18IG",
                "20",
                "20m"},
            "--planner-preference require-paired");
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
            jConfig["Operation"]["Mode"].get<std::string>() == "WSPR",
            "--test-tone must not persist tone mode into serialized config");

        json_to_config();
        require(
            config.mode == ModeType::WSPR,
            "persistent config reload must restore WSPR mode rather than tone mode");
    }

    {
        init_config_json();
        json_to_config();
        config.use_ini = false;
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "CLI parsing without --planner-preference must succeed");
        require(
            config.wspr_planner_preference == WsprPlannerPreference::Auto &&
                config.wspr.planner_preference == WsprPlannerPreference::Auto,
            "CLI planner preference must default to auto when unspecified");
    }

    {
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--debug-logging",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "--debug-logging CLI parsing must succeed");
        require(
            config.debug_logging,
            "--debug-logging must enable persisted debug logging");
        require(
            jConfig["Meta"].value("debug_logging", false),
            "--debug-logging must update serialized config state");
    }

    {
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--debug-logging",
            "--no-debug-logging",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "--no-debug-logging CLI parsing must succeed");
        require(
            !config.debug_logging,
            "--no-debug-logging must disable persisted debug logging");
        require(
            !jConfig["Meta"].value("debug_logging", true),
            "--no-debug-logging must update serialized config state");
    }

    {
        clear_direct_tone_startup_request();
        require(
            set_direct_tone_startup_request("730000"),
            "direct tone startup helper must accept valid test-tone frequency");

        ArgParserConfig tone_candidate;
        tone_candidate.mode = ModeType::TONE;
        tone_candidate.gpio_tx_pin = 4;
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
        valid_wspr_candidate.gpio_tx_pin = 4;
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
        invalid_wspr_candidate.gpio_tx_pin = 4;
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);

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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        set_scheduler_execution_suppressed_for_test(true);
        require(
            set_config(true),
            "disabled WSPR configuration must not fail scheduler setup");
        set_scheduler_execution_suppressed_for_test(false);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        config.ppm = 12.5;
        set_frequencies(config);

        ppm_reload_pending.store(true, std::memory_order_relaxed);
        const double expected_ppm = ppmManager.getCurrentPPM();

        require(
            set_config(true),
            "pending runtime PPM update must be consumable during scheduler commit");
        require(
            nearly_equal(current_transmission_request_for_test().ppm, expected_ppm),
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
        PreparedConfigCandidate candidate;
        auto data = make_managed_ini_data("W1AW", "FN31", "80m", false);
        data.erase("Operation");
        iniFile.setData(data);
        prepare_ini_config_candidate("/tmp/missing_operation.ini", candidate);
        require(
            !candidate.valid &&
                candidate.error_reason == "Missing [Operation] section.",
            "managed INI candidate must clearly reject a missing Operation section");
    }

    {
        PreparedConfigCandidate candidate;
        auto data = make_managed_ini_data("W1AW", "FN31", "80m", false);
        data["Operation"].erase("Mode");
        iniFile.setData(data);
        prepare_ini_config_candidate("/tmp/missing_operation_mode.ini", candidate);
        require(
            !candidate.valid &&
                candidate.error_reason == "Missing [Operation] Mode.",
            "managed INI candidate must clearly reject a missing Operation Mode");
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
        const TransmissionRequest request = current_transmission_request_for_test();
        require(
            request.payload.plan_type == "Type2Single" &&
                request.payload.callsign == "AA0NT/12" &&
                request.payload.locator == "EM18",
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
        const TransmissionRequest request = current_transmission_request_for_test();
        require(
            request.payload.plan_type == "Type1Single",
            "runtime planning must still classify whitespace-surrounded identities as a valid Type 1 plan");
        require(
            request.payload.callsign == "AA0NT" &&
                request.payload.locator == "EM18",
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
        const TransmissionRequest request = current_transmission_request_for_test();
        require(
            request.payload.plan_type == "Type1Single" &&
                request.payload.callsign == "AA0NT" &&
                request.payload.locator == "EM18",
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
        require(
            websocket_tx_state_for_message(
                "transmit",
                "starting",
                "enabled") == "transmitting",
            "websocket transmit start events must present a transmitting tx_state");
        require(
            websocket_tx_state_for_message(
                "transmit",
                "progress",
                "enabled") == "transmitting",
            "websocket transmit progress events must present a transmitting tx_state");
        require(
            websocket_tx_state_for_message(
                "transmit",
                "finished",
                "transmitting") == "complete",
            "websocket transmit finished events must present a non-transmitting terminal tx_state");
        require(
            websocket_tx_state_for_message(
                "transmit",
                "canceled",
                "transmitting") == "cancelled",
            "websocket transmit canceled events must present a cancelled tx_state");
        require(
            websocket_tx_state_for_message(
                "transmit",
                "stopped",
                "transmitting") == "disabled",
            "websocket transmit stopped events must present a disabled tx_state");
        require(
            websocket_tx_state_for_message(
                "transmit",
                "skipped",
                "transmitting") == "complete",
            "websocket transmit skipped events must present a non-transmitting terminal tx_state");
        require(
            websocket_tx_state_for_message(
                "configuration",
                "reload",
                "enabled") == "enabled",
            "non-transmit websocket messages must preserve the current runtime tx_state");
    }

    {
        init_default_config();
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        const WsprRuntimeStatusSnapshot snapshot =
            current_tx_runtime_status_snapshot();
        require(
            snapshot.tx_state == "transmitting",
            "runtime status snapshots must report transmitting when the transmitter state is TRANSMITTING");
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
    }

    {
        prime_valid_runtime_identity_config();
        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "stale idle runtime snapshot regression must plan an initial WSPR request");
        finish_runtime_planning_state_for_identity_test();

        require(
            current_transmission_request_for_test().mode == TransmissionMode::WSPR,
            "stale idle runtime snapshot regression must start from a committed WSPR request");

        wsprTransmitter.current_execution_mode_ = wsprrypi::TransmissionMode::WSPR;
        config.mode = ModeType::QRSS;
        config.transmit = false;
        config.schedule_start_minute = 0;
        config.schedule_repeat_minutes = 10;
        config.qrss.message = "A A";
        config.qrss.frequency_hz = 3572000.0;
        config.qrss.dot_seconds = 3.0;

        const WsprRuntimeStatusSnapshot snapshot =
            current_tx_runtime_status_snapshot();
        require(
            snapshot.runtime_mode == "QRSS",
            "idle runtime snapshots must report the committed scheduler mode instead of stale backend WSPR execution state");
        require(
            snapshot.plan_type.empty() &&
                snapshot.frame_count == 0U &&
                snapshot.current_frame == 0U,
            "idle CW runtime snapshots must not expose stale WSPR plan details after a mode change");
        require(
            snapshot.next_transmission_at.empty(),
            "idle CW runtime snapshots must not expose a next scheduled message time while transmissions are disabled");

        config.transmit = true;
        const WsprRuntimeStatusSnapshot enabled_snapshot =
            current_tx_runtime_status_snapshot();
        require(
            !enabled_snapshot.next_transmission_at.empty(),
            "idle CW runtime snapshots must expose the next scheduled message time once transmissions are enabled");
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
        init_default_config();
        config.enable_web = false;
        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", true));
        prepare_ini_config_candidate("/tmp/managed_candidate.ini", candidate);
        require(
            candidate.valid,
            "managed INI candidate must still validate when the CLI web override is disabled");
        require(
            !candidate.normalized_config.enable_web,
            "managed INI candidate preparation must preserve the CLI-only web override");

        commit_config_candidate(candidate);
        require(
            !config.enable_web,
            "committing a managed INI candidate must not re-enable the web runtime after --no-web");
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
        config.ini_filename = "/tmp/debug_logging.ini";
        write_text_file(config.ini_filename, "[Meta]\ndebug_logging=false\n");
        iniFile.set_filename(config.ini_filename);
        config.debug_logging = true;
        config_to_json();
        json_to_ini();

        const auto persisted_ini = iniFile.getData();
        const auto meta_it = persisted_ini.find("Meta");
        require(
            meta_it != persisted_ini.end(),
            "json_to_ini must persist the Meta section for debug logging");
        require(
            meta_it->second.at("debug_logging") == "true",
            "json_to_ini must persist debug_logging in the Meta section");

        init_config_json();
        ini_to_json("/tmp/debug_logging.ini");
        json_to_config();
        require(
            config.debug_logging,
            "INI plumbing must round-trip persisted debug logging");

        const nlohmann::json public_config = get_public_config_json();
        require(
            !public_config.contains("Meta"),
            "public config JSON must not expose Meta logging controls to the UI");

        init_default_config();
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/debug_logging_patch.ini";
        write_text_file(
            config.ini_filename,
            "[Meta]\ndebug_logging=false\n"
            "[Operation]\nMode=WSPR\nTransmit=false\nTransmit Backend=gpio\n"
            "Use LED=false\nLED Pin=-1\nWeb Port=31415\nSocket Port=31416\n"
            "Use Shutdown=false\nShutdown Button=-1\n"
            "[GPIO]\nTransmit Pin=4\nPower Level=7\nUse NTP=false\n"
            "[Calibration]\nPPM=0\n"
            "[Si5351]\nI2C Bus=1\nI2C Address=96\nReference Frequency=27000000\n"
            "TX Output=CLK0\nPower Level=1\n"
            "[WSPR]\nCall Sign=AA0NT\nGrid Square=EM18\nTX Power=20\n"
            "Frequency=20m\nPlanner Preference=auto\nUse Random Offset=false\n"
            "[CW]\nMessage=\nBase Frequency=14096900.0\nShift Hz=5.0\n"
            "Dot Seconds=3.0\nIntra Element Gap=1.0\nInter Character Gap=3.0\n"
            "Inter Word Gap=7.0\nFade Shape=none\nFade In Ms=0\nFade Out Ms=0\n"
            "Fade Slice Ms=5\nStart Minute=0\nRepeat Minutes=10\n");
        iniFile.set_filename(config.ini_filename);
        config_to_json();

        patch_all_from_web({{"Meta", {{"debug_logging", true}}}});

        require(
            config.debug_logging,
            "internal JSON patch path must apply Meta.debug_logging to live config");
        require(
            jConfig["Meta"].value("debug_logging", false),
            "internal JSON patch path must preserve Meta.debug_logging in internal JSON");
        require(
            iniFile.getData().at("Meta").at("debug_logging") == "true",
            "internal JSON patch path must persist Meta.debug_logging to INI");

        const nlohmann::json public_config = get_public_config_json();
        require(
            !public_config.contains("Meta"),
            "public config JSON must still hide Meta after internal debug logging patch");

        init_default_config();
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/no_web_persistence.ini";
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);
        config.enable_web = false;
        config_to_json();
        json_to_ini();

        const auto persisted_ini = iniFile.getData();
        const auto operation_it = persisted_ini.find("Operation");
        require(
            operation_it != persisted_ini.end(),
            "json_to_ini must persist the Operation section");
        require(
            operation_it->second.find("Enable Web") == operation_it->second.end(),
            "json_to_ini must not persist the CLI-only web override");
    }

    {
        init_default_config();
        set_si5351_detection_override_for_test(false);
        config.si5351_tx_output = 2;
        config_to_json();

        require(
            jConfig["Si5351"].contains("TX Output") &&
                jConfig["Si5351"]["TX Output"].get<std::string>() == "CLK2",
            "config_to_json must serialize Si5351 TX Output");

        const nlohmann::json public_config = get_public_config_json();
        require(
            public_config["Si5351"].contains("TX Output") &&
                public_config["Si5351"]["TX Output"].get<std::string>() == "CLK2",
            "public config JSON must include Si5351 TX Output");
        require(
            public_config["Platform"].contains("Si5351 Detected") &&
                public_config["Platform"]["Si5351 Detected"].get<bool>() == false,
            "public config JSON must surface Si5351 detection state");
        require(
            public_config["Platform"].contains("Si5351 Detection Error") &&
                public_config["Platform"]["Si5351 Detection Error"]
                        .get<std::string>()
                        .find("Si5351 transmission is unavailable because no Si5351 device was detected on the I2C bus.") !=
                    std::string::npos,
            "public config JSON must surface the Si5351 missing-device message");

        jConfig["Si5351"]["TX Output"] = "CLK1";
        json_to_config();
        require(
            config.si5351_tx_output == 1,
            "json_to_config must parse Si5351 TX Output");

        config.si5351_tx_output = 2;
        config_to_json();
        json_to_ini();
        const auto persisted_ini = iniFile.getData();
        const auto si5351_it = persisted_ini.find("Si5351");
        require(
            si5351_it != persisted_ini.end(),
            "json_to_ini must persist the Si5351 section");
        require(
            si5351_it->second.at("TX Output") == "CLK2",
            "json_to_ini must persist Si5351 TX Output");
        clear_si5351_detection_override_for_scope();
    }

    {
        init_default_config();
        PreparedConfigCandidate candidate;
        candidate.valid = true;
        candidate.normalized_config = config;
        candidate.normalized_config.use_led = true;
        candidate.normalized_config.led_pin = 18;
        candidate.normalized_config.web_port = 31555;
        candidate.normalized_config.use_shutdown = true;
        candidate.normalized_config.shutdown_pin = 19;
        candidate.normalized_json = jConfig;
        candidate.normalized_json["Operation"]["Use LED"] = true;
        candidate.normalized_json["Operation"]["LED Pin"] = 18;
        candidate.normalized_json["Operation"]["Web Port"] = 31555;
        candidate.normalized_json["Operation"]["Use Shutdown"] = true;
        candidate.normalized_json["Operation"]["Shutdown Button"] = 19;

        commit_config_candidate(candidate);

        require(
            config.use_led && config.led_pin == 18,
            "managed config candidate commit must update LED runtime settings");
        require(
            config.web_port == 31555,
            "managed config candidate commit must update web server port in live config");
        require(
            config.use_shutdown && config.shutdown_pin == 19,
            "managed config candidate commit must update shutdown GPIO settings");
    }

    {
        init_config_json();
        json_to_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/stop_request.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        require(
            set_config(true),
            "stop request regression must commit an initial transmit request");
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);

        const StopTransmissionResult stop_result =
            stop_transmission_by_user_request();

        require(
            stop_result.transmission_active,
            "user stop helper must detect an active transmission");
        require(
            stop_result.transmit_disabled,
            "user stop helper must disable runtime transmit");
        require(
            !config.transmit,
            "user stop helper must persist live transmit-disabled state");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                current_transmission_request_for_test().payload.frames.empty(),
            "user stop helper must clear the committed transmission request");
        require(
            wsprTransmitter.getState() != WsprTransmitter::State::TRANSMITTING,
            "user stop helper must leave the transmitter out of TRANSMITTING state");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ini_reload_generation.store(0, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/guarded_mode_change_single_persist.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        config_to_json();
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);
        json_to_ini();

        const std::string ini_before_guard = read_text_file(config.ini_filename);
        const std::uint64_t generation_before_guard =
            ini_reload_generation.load(std::memory_order_relaxed);

        const StopTransmissionResult guard_stop_result =
            stop_transmission_by_user_request(false);

        require(
            guard_stop_result.transmit_disabled && !guard_stop_result.persisted,
            "guarded mode-change stop must disable runtime transmit without persisting");
        require(
            read_text_file(config.ini_filename) == ini_before_guard,
            "guarded mode-change stop must not rewrite the INI before the final save");
        require(
            ini_reload_generation.load(std::memory_order_relaxed) == generation_before_guard,
            "guarded mode-change stop must not publish an extra persistence generation");

        patch_all_from_web({
            {"Operation", {{"Mode", "QRSS"}, {"Transmit", false}}},
            {"CW",
             {{"Message", "CQ"},
              {"Base Frequency", 14096900.0},
              {"Shift Hz", 5.0},
              {"Dot Seconds", 3.0},
              {"Intra Element Gap", 1.0},
              {"Inter Character Gap", 3.0},
              {"Inter Word Gap", 7.0},
              {"Start Minute", 0},
              {"Repeat Minutes", 10}}}
        });

        const std::string ini_after_guard = read_text_file(config.ini_filename);
        require(
            ini_after_guard != ini_before_guard,
            "guarded mode-change final save must persist the updated mode once");
        require(
            ini_reload_generation.load(std::memory_order_relaxed) ==
                generation_before_guard + 1U,
            "guarded mode change must produce exactly one persistence generation");
        require(
            iniFile.getData().at("Operation").at("Mode") == "QRSS" &&
                iniFile.getData().at("Operation").at("Transmit") == "false",
            "guarded mode-change final save must persist the final mode and disabled transmit state");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/transmit_toggle_patch.ini";
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.si5351_i2c_bus = 1;
        config.si5351_i2c_address = 0x60;
        config.si5351_reference_hz = 27000000;
        config.si5351_tx_output = 0;
        config.si5351_power_level = 1;
        resolve_backend_specific_config(config);
        config_to_json();
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);

        patch_all_from_web({{"Operation", {{"Transmit", true}}}});

        require(
            config.transmit,
            "web patch must update live Operation.Transmit immediately");
        require(
            jConfig["Operation"].value("Transmit", false),
            "web patch must persist canonical Operation.Transmit in JSON");
        const auto enabled_ini = iniFile.getData();
        require(
            enabled_ini.at("Operation").at("Transmit") == "true",
            "web patch must persist canonical Operation.Transmit in INI");

        PreparedConfigCandidate candidate;
        prepare_ini_config_candidate("/tmp/transmit_toggle_patch.ini", candidate);
        require(
            candidate.valid && candidate.normalized_config.transmit,
            "managed reload must preserve persisted Operation.Transmit");

        patch_all_from_web({{"Operation", {{"Transmit", false}}}});

        require(
            !config.transmit,
            "web patch must disable live Operation.Transmit immediately");
        require(
            !jConfig["Operation"].value("Transmit", true),
            "web patch must persist canonical Operation.Transmit=false in JSON");
        const auto disabled_ini = iniFile.getData();
        require(
            disabled_ini.at("Operation").at("Transmit") == "false",
            "web patch must persist canonical Operation.Transmit=false in INI");
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/mode_ntp_patch.ini";
        config.mode = ModeType::QRSS;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        config.gpio_use_ntp = false;
        config.transmit_backend = TransmitBackendKind::GPIO;
        resolve_backend_specific_config(config);
        config_to_json();

        patch_all_from_web(nlohmann::json{
            {"Operation", {{"Mode", "WSPR"}}},
            {"GPIO", {{"Use NTP", true}}}});

        require(
            config.mode == ModeType::WSPR,
            "web patch must update canonical Operation.Mode immediately");
        require(
            config.gpio_use_ntp && config.use_ntp,
            "web patch must update canonical GPIO.Use NTP immediately");
        require(
            jConfig["Operation"].value("Mode", std::string()) == "WSPR",
            "web patch must persist canonical Operation.Mode in JSON");
        require(
            jConfig["GPIO"].value("Use NTP", false),
            "web patch must persist canonical GPIO.Use NTP in JSON");

        const auto persisted_ini = iniFile.getData();
        require(
            persisted_ini.at("Operation").at("Mode") == "WSPR",
            "web patch must persist canonical Operation.Mode in INI");
        require(
            persisted_ini.at("GPIO").at("Use NTP") == "true",
            "web patch must persist canonical GPIO.Use NTP in INI");
    }

    {
        init_config_json();
        jConfig["Operation"]["Transmit Backend"] = "gpio";
        if (jConfig.contains("GPIO") && jConfig["GPIO"].is_object())
        {
            jConfig["GPIO"].erase("Use NTP");
        }
        json_to_config();

        require(
            config.gpio_use_ntp,
            "missing GPIO.Use NTP must default true in backend normalization");
        require(
            config.use_ntp,
            "missing GPIO.Use NTP must enable the active GPIO runtime NTP path");
    }

    {
        init_config_json();
        jConfig["Calibration"]["PPM"] = 275.5;
        json_to_config();

        require(
            nearly_equal(config.ppm, 200.0),
            "backend normalization must clamp manual Calibration.PPM to the shared upper bound");

        config_to_json();
        require(
            nearly_equal(jConfig["Calibration"].at("PPM").get<double>(), 200.0),
            "clamped manual Calibration.PPM must round-trip through canonical JSON");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/ppm_patch_clamp.ini";
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        config.gpio_use_ntp = false;
        config.transmit_backend = TransmitBackendKind::GPIO;
        resolve_backend_specific_config(config);
        config_to_json();

        patch_all_from_web({{"Calibration", {{"PPM", -275.5}}}});

        require(
            nearly_equal(config.ppm, -200.0),
            "web patch path must clamp manual Calibration.PPM to the shared lower bound");
        require(
            nearly_equal(jConfig["Calibration"].at("PPM").get<double>(), -200.0),
            "web patch path must persist the clamped manual Calibration.PPM in JSON");
        require(
            nearly_equal(
                std::stod(iniFile.getData().at("Calibration").at("PPM")),
                -200.0),
            "web patch path must persist the clamped manual Calibration.PPM in INI");
    }

    {
        init_config_json();
        jConfig["CW"]["Dot Seconds"] = 2.0;
        jConfig["CW"]["Intra Element Gap"] = 1.5;
        jConfig["CW"]["Inter Character Gap"] = 4.0;
        jConfig["CW"]["Inter Word Gap"] = 8.0;
        jConfig["CW"]["Fade Shape"] = "raised-cosine";
        jConfig["CW"]["Fade In Ms"] = 25;
        jConfig["CW"]["Fade Out Ms"] = 40;
        jConfig["CW"]["Fade Slice Ms"] = 2;
        json_to_config();

        require(
            nearly_equal(config.modulation_dot_seconds, 2.0) &&
                nearly_equal(config.cw_intra_element_gap, 1.5) &&
                nearly_equal(config.cw_inter_character_gap, 4.0) &&
                nearly_equal(config.cw_inter_word_gap, 8.0),
            "json_to_config must parse CW timing gap multipliers");
        require(
            config.qrss.dot_seconds == config.modulation_dot_seconds &&
                config.fskcw.dot_seconds == config.modulation_dot_seconds &&
                config.dfcw.dot_seconds == config.modulation_dot_seconds,
            "CW dot length must remain shared by QRSS, FSKCW, and DFCW configs");
        require(
            config.cw_fade_shape == "raised_cosine" &&
                config.cw_fade_in_ms == 25 &&
                config.cw_fade_out_ms == 40 &&
                config.cw_fade_slice_ms == 2,
            "json_to_config must parse CW fade settings");

        config_to_json();
        require(
            nearly_equal(jConfig["CW"]["Intra Element Gap"].get<double>(), 1.5) &&
                nearly_equal(jConfig["CW"]["Inter Character Gap"].get<double>(), 4.0) &&
                nearly_equal(jConfig["CW"]["Inter Word Gap"].get<double>(), 8.0) &&
                jConfig["CW"]["Fade Shape"].get<std::string>() == "raised_cosine" &&
                jConfig["CW"]["Fade Slice Ms"].get<int>() == 2,
            "config_to_json must serialize CW timing and fade settings");

        config.use_ini = true;
        json_to_ini();
        const auto persisted_ini = iniFile.getData();
        const auto cw_it = persisted_ini.find("CW");
        require(
            cw_it != persisted_ini.end(),
            "json_to_ini must persist the CW section");
        require(
            nearly_equal(std::stod(cw_it->second.at("Intra Element Gap")), 1.5) &&
                nearly_equal(std::stod(cw_it->second.at("Inter Character Gap")), 4.0) &&
                nearly_equal(std::stod(cw_it->second.at("Inter Word Gap")), 8.0) &&
                cw_it->second.at("Fade Shape") == "raised_cosine" &&
                cw_it->second.at("Fade In Ms") == "25" &&
                cw_it->second.at("Fade Out Ms") == "40" &&
                cw_it->second.at("Fade Slice Ms") == "2",
            "json_to_ini must persist CW timing and fade settings");
    }

    {
        init_config_json();
        jConfig["Operation"]["Mode"] = "FSKCW";
        jConfig["CW"]["Message"] = "CQ";
        jConfig["CW"]["Base Frequency"] = 7030000.0;
        jConfig["CW"]["Shift Hz"] = 25000.0;
        jConfig["CW"]["Dot Seconds"] = 90.5;
        jConfig["CW"]["Repeat Minutes"] = 1440;
        json_to_config();

        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "backend validation must allow large positive CW dot length, shift, and repeat values when runtime constraints are satisfied");
        require(
            nearly_equal(config.modulation_dot_seconds, 90.5) &&
                nearly_equal(config.modulation_fsk_offset_hz, 25000.0) &&
                config.schedule_repeat_minutes == 1440,
            "backend normalization must preserve large positive CW numeric values");
    }

    {
        init_config_json();
        jConfig["CW"]["Message"] = "  HELLO WORLD  ";
        json_to_config();

        require(
            config.qrss.message == "HELLO WORLD" &&
                config.fskcw.message == "HELLO WORLD" &&
                config.dfcw.message == "HELLO WORLD",
            "json_to_config must trim CW message edges for QRSS, FSKCW, and DFCW");

        config.mode = ModeType::DFCW;
        config_to_json();
        require(
            jConfig["CW"]["Message"].get<std::string>() == "HELLO WORLD",
            "config_to_json must serialize trimmed CW messages without altering internal spaces");

        config.use_ini = true;
        json_to_ini();
        const auto persisted_ini = iniFile.getData();
        const auto cw_it = persisted_ini.find("CW");
        require(
            cw_it != persisted_ini.end() &&
                cw_it->second.at("Message") == "HELLO WORLD",
            "json_to_ini must persist trimmed CW messages without leading or trailing spaces");
    }

    {
        init_config_json();
        json_to_config();
        config.use_ini = false;
        clear_qrss_startup_request();
        clear_fskcw_startup_request();
        clear_dfcw_startup_request();
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--qrss-message",
            "  AT AT  ",
            "--qrss-frequency",
            "7030000",
            "--qrss-dot-seconds",
            "3",
            "--transmit-gpio",
            "4"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "QRSS CLI parsing must succeed");
        require(
            config.qrss.message == "AT AT",
            "QRSS CLI parsing must trim CW message edges and preserve internal spaces");
    }

    {
        init_config_json();
        json_to_config();
        config.use_ini = false;
        clear_qrss_startup_request();
        clear_fskcw_startup_request();
        clear_dfcw_startup_request();
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--fskcw-message",
            "  CQ TEST  ",
            "--fskcw-mark-frequency",
            "7030100",
            "--fskcw-space-frequency",
            "7030000",
            "--fskcw-dot-seconds",
            "3",
            "--transmit-gpio",
            "4"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "FSKCW CLI parsing must succeed");
        require(
            config.fskcw.message == "CQ TEST",
            "FSKCW CLI parsing must trim CW message edges and preserve internal spaces");
    }

    {
        init_config_json();
        json_to_config();
        config.use_ini = false;
        clear_qrss_startup_request();
        clear_fskcw_startup_request();
        clear_dfcw_startup_request();
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--dfcw-message",
            "  CQ DX  ",
            "--dfcw-dot-frequency",
            "7030000",
            "--dfcw-dash-frequency",
            "7030100",
            "--dfcw-dot-seconds",
            "3",
            "--transmit-gpio",
            "4"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "DFCW CLI parsing must succeed");
        require(
            config.dfcw.message == "CQ DX",
            "DFCW CLI parsing must trim CW message edges and preserve internal spaces");
    }

    {
        ArgParserConfig invalid_gap_candidate;
        invalid_gap_candidate.cw_intra_element_gap = 0.0;
        std::string validation_error;
        require(
            !validate_config_candidate(invalid_gap_candidate, &validation_error) &&
                validation_error == "CW gap settings must be greater than 0.",
            "validation must reject non-positive CW gap settings");

        ArgParserConfig invalid_shift_candidate;
        invalid_shift_candidate.mode = ModeType::FSKCW;
        invalid_shift_candidate.modulation_fsk_offset_hz = 0.0;
        validation_error.clear();
        require(
            !validate_config_candidate(invalid_shift_candidate, &validation_error) &&
                validation_error == "CW shift_hz must be greater than 0 for FSKCW and DFCW.",
            "validation must reject non-positive CW shift values when the active mode requires a second tone");

        ArgParserConfig invalid_fade_candidate;
        invalid_fade_candidate.cw_fade_in_ms = -1;
        validation_error.clear();
        require(
            !validate_config_candidate(invalid_fade_candidate, &validation_error) &&
                validation_error == "CW fade durations must be 0 or greater.",
            "validation must reject negative CW fade durations");

        ArgParserConfig invalid_slice_candidate;
        invalid_slice_candidate.cw_fade_slice_ms = 0;
        validation_error.clear();
        require(
            !validate_config_candidate(invalid_slice_candidate, &validation_error) &&
                validation_error == "CW fade slice duration must be greater than 0.",
            "validation must reject non-positive CW fade slice durations");
    }

    {
        const auto prepare_si5351_address_candidate =
            [](const std::string &address)
        {
            PreparedConfigCandidate candidate;
            auto data = make_managed_ini_data("AA0NT", "EM18", "20m", true);
            data["Operation"]["Transmit Backend"] = "si5351";
            data["Si5351"]["I2C Address"] = address;
            iniFile.setData(data);
            prepare_ini_config_candidate("/tmp/si5351_i2c_address.ini", candidate);
            return candidate;
        };

        PreparedConfigCandidate hex_candidate =
            prepare_si5351_address_candidate("0x60");
        require(
            hex_candidate.valid &&
                hex_candidate.normalized_config.si5351_i2c_address == 0x60,
            "Si5351 I2C address must accept 0x-prefixed hex strings");

        PreparedConfigCandidate decimal_candidate =
            prepare_si5351_address_candidate("96");
        require(
            decimal_candidate.valid &&
                decimal_candidate.normalized_config.si5351_i2c_address == 96,
            "Si5351 I2C address must accept decimal strings");

        PreparedConfigCandidate out_of_range_candidate =
            prepare_si5351_address_candidate("0x80");
        require(
            !out_of_range_candidate.valid &&
                out_of_range_candidate.error_reason ==
                    "Invalid Si5351 I2C address. Expected 0x03 through 0x77.",
            "Si5351 I2C address must reject out-of-range hex values");

        PreparedConfigCandidate malformed_candidate =
            prepare_si5351_address_candidate("0xzz");
        require(
            !malformed_candidate.valid &&
                malformed_candidate.error_reason ==
                    "Si5351.I2C Address must be an integer.",
            "Si5351 I2C address must reject malformed strings cleanly");
    }

    {
        init_config_json();
        if (jConfig.contains("CW") && jConfig["CW"].is_object())
        {
            jConfig["CW"].erase("Shift Hz");
        }
        json_to_config();

        require(
            nearly_equal(config.modulation_fsk_offset_hz, 5.0),
            "missing CW.Shift Hz must default to 5 Hz in backend normalization");
        require(
            nearly_equal(config.fskcw.mark_frequency_hz, config.fskcw.space_frequency_hz + 5.0) &&
                nearly_equal(config.dfcw.dash_frequency_hz, config.dfcw.dot_frequency_hz + 5.0),
            "missing CW.Shift Hz must use the 5 Hz default for derived CW mode frequencies");
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
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        config.wspr_planner_preference = WsprPlannerPreference::Auto;
        config_to_json();

        patch_all_from_web(make_identity_patch(
            "AA0NT/12",
            "EM18IG",
            WsprPlannerPreference::PreferPaired));

        const auto persisted_ini = iniFile.getData();
        const auto wspr_it = persisted_ini.find("WSPR");
        require(
            wspr_it != persisted_ini.end(),
            "json_to_ini must persist the WSPR section");
        require(
            wspr_it->second.at("Planner Preference") == "prefer_paired",
            "json_to_ini must persist planner preference in the WSPR section");
        require(
            wspr_it->second.find("Require Paired Plan") == wspr_it->second.end(),
            "json_to_ini must not persist the removed Require Paired Plan compatibility field");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/cli_planner_preference_override.ini";
        iniFile.setData(
            make_managed_ini_data(
                "AA0NT/12",
                "EM18IG",
                "20m",
                true,
                WsprPlannerPreference::RequirePaired));
        write_managed_ini_file(
            config.ini_filename,
            make_managed_ini_data(
                "AA0NT/12",
                "EM18IG",
                "20m",
                true,
                WsprPlannerPreference::RequirePaired));
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--ini-file",
            "/tmp/cli_planner_preference_override.ini",
            "--planner-preference",
            "prefer_paired"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "CLI planner preference override over INI must parse");
        require(
            config.use_ini &&
                config.wspr_planner_preference == WsprPlannerPreference::PreferPaired &&
                config.wspr.planner_preference == WsprPlannerPreference::PreferPaired,
            "CLI --planner-preference must override the INI planner preference in canonical runtime config");

        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "CLI-overridden planner preference must remain valid through scheduler planning");
        const TransmissionRequest request = current_transmission_request_for_test();
        require(
            request.mode == TransmissionMode::WSPR &&
                request.payload.plan_type == "Type2Type3Paired",
            "CLI prefer_paired override must reach the runtime WSPR planning path");
        finish_runtime_planning_state_for_identity_test();
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
