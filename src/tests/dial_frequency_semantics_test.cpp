#include "arg_parser.hpp"
#include "backend_capabilities.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "i2c_bus_inventory.hpp"
#include "execution_plan_compiler.hpp"
#include "frequency_semantics.hpp"
#include "gpio_band_policy.hpp"
#include "gpio_output.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "scheduling_rp1_test_support.hpp"
#include "system_clock_frequency_estimate.hpp"
#include "ppm_manager.hpp"
#include "band_lookup.hpp"
#include "wspr_transmit_backend_si5351.hpp"
#include "web_server_config_http.hpp"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <limits.h>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define private public
#include "wspr_transmit.hpp"
#undef private

namespace
{
    std::vector<std::string> &scoped_temp_cleanup_paths()
    {
        static std::vector<std::string> paths;
        return paths;
    }

    void cleanup_scoped_temp_files_at_exit()
    {
        for (const std::string &path : scoped_temp_cleanup_paths())
        {
            if (!path.empty())
            {
                unlink(path.c_str());
            }
        }
    }

    class ScopedTemporaryFile
    {
    public:
        explicit ScopedTemporaryFile(const std::string &name_template)
        {
            std::vector<char> path_buffer(name_template.begin(), name_template.end());
            path_buffer.push_back('\0');
            const int fd = mkstemp(path_buffer.data());
            if (fd < 0)
            {
                throw std::runtime_error("Failed to create scoped temporary file.");
            }
            close(fd);
            path_ = path_buffer.data();

            auto &cleanup_paths = scoped_temp_cleanup_paths();
            static const bool exit_cleanup_registered =
                std::atexit(cleanup_scoped_temp_files_at_exit) == 0;
            if (!exit_cleanup_registered)
            {
                unlink(path_.c_str());
                throw std::runtime_error("Failed to register temporary-file cleanup.");
            }
            cleanup_paths.push_back(path_);
        }

        ScopedTemporaryFile(const ScopedTemporaryFile &) = delete;
        ScopedTemporaryFile &operator=(const ScopedTemporaryFile &) = delete;

        ~ScopedTemporaryFile()
        {
            unlink(path_.c_str());
            for (std::string &registered_path : scoped_temp_cleanup_paths())
            {
                if (registered_path == path_)
                {
                    registered_path.clear();
                    break;
                }
            }
        }

        const std::string &path() const noexcept
        {
            return path_;
        }

    private:
        std::string path_;
    };

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
        const int saved_stdout = dup(STDOUT_FILENO);
        const int saved_stderr = dup(STDERR_FILENO);
        require(
            saved_stdout >= 0 && saved_stderr >= 0,
            "help-output test must duplicate stdout and stderr");

        require(
            dup2(pipefd[1], STDOUT_FILENO) >= 0 &&
                dup2(pipefd[1], STDERR_FILENO) >= 0,
            "help-output test must redirect stdout and stderr to the pipe");
        close(pipefd[1]);

        // `print_usage(0)` exits, but this test only needs the help text.
        // Use the non-exiting path so capture works safely in-process.
        print_usage(exit_code == 0 ? 3 : exit_code);
        std::cout.flush();
        std::cerr.flush();

        require(
            dup2(saved_stdout, STDOUT_FILENO) >= 0 &&
                dup2(saved_stderr, STDERR_FILENO) >= 0,
            "help-output test must restore stdout and stderr");
        close(saved_stdout);
        close(saved_stderr);

        std::string output;
        char buffer[4096];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0)
        {
            output.append(buffer, static_cast<std::size_t>(bytes_read));
        }
        close(pipefd[0]);
        return output;
    }

    void require_cli_parse_rejected(
        std::vector<std::string> args,
        const std::string &message)
    {
        char exe_path[PATH_MAX] = {0};
#if defined(__APPLE__)
        std::uint32_t exe_path_capacity = sizeof(exe_path);
        const bool resolved_executable_path =
            _NSGetExecutablePath(exe_path, &exe_path_capacity) == 0;
#else
        const ssize_t exe_path_length =
            readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        const bool resolved_executable_path = exe_path_length > 0;
        if (resolved_executable_path)
        {
            exe_path[exe_path_length] = '\0';
        }
#endif
        require(
            resolved_executable_path,
            message + " must resolve the current test binary path");

        pid_t pid = fork();
        require(pid >= 0, message + " must be able to fork a child process");

        if (pid == 0)
        {
            setenv("DIAL_FREQUENCY_SEMANTICS_CLI_CHECK", "1", 1);
            std::vector<char *> argv = argv_for(args);
            argv.insert(argv.begin(), exe_path);
            argv.push_back(nullptr);
            execv(exe_path, argv.data());
            _exit(127);
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
        WsprPlannerPreference planner_preference = WsprPlannerPreference::Auto,
        const std::string &enable_on_boot = "Never")
    {
        auto data = std::map<std::string, std::unordered_map<std::string, std::string>>{
            {"Meta",
             {{"debug_logging", "false"}}},
            {"Operation",
             {{"Mode", "WSPR"},
              {"Transmit", transmit ? "true" : "false"},
              {"Transmit Backend", "gpio"},
              {"Enable on Boot", enable_on_boot},
              {"Use LED", "false"},
              {"LED Pin", "-1"},
              {"Use Amp", "false"},
              {"Amp Pin", "-1"},
              {"Amp Pin Active High", "false"},
              {"Web Port", "-1"},
              {"Socket Port", "-1"},
              {"Use Shutdown", "false"},
              {"Shutdown Button", "-1"}}},
            {"GPIO",
             {{"Transmit Pin", "4"},
              {"Power Level", "7"},
              {"Use System Clock Frequency Estimate", "false"},
              {"Frequency Residual PPM", "0"},
              {"Manual PPM", "0"}}},
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
              {"DFCW Intra Element Gap", "0.333333"},
              {"DFCW Inter Character Gap", "1.0"},
              {"DFCW Inter Word Gap", "3.0"},
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
        config.frequencies = has_ancillary_gpio() ? "20m@17H" : "20m";
        config.transmit_backend = TransmitBackendKind::GPIO;
        config.gpio_tx_pin = 4;
        config.gpio_power_level = 7;
        config.gpio_use_system_clock_frequency_estimate = false;
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

#if WSPRRYPI_BACKEND_RP1_GPCLK
    void test_rp1_web_backend_persistence()
    {
        set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
        set_si5351_detection_override_for_test(true);
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        require(
            operator_exposes_rp1_gpio(),
            "RP1 web backend persistence regression requires the RP1 backend");

        ScopedTemporaryFile persistence_ini(
            "/tmp/wsprrypi-rp1-web-backend-persistence-XXXXXX");
        iniFile.set_filename(persistence_ini.path());
        prime_valid_runtime_identity_config();
        config.transmit = false;
        config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
        resolve_backend_specific_config(config);
        config_to_json();
        patch_all_from_web({
            {"Operation", {{"Transmit Backend", "gpio"}}},
            {"GPIO", {{"Transmit Pin", 20}}}});
        require(
            config.transmit_backend == TransmitBackendKind::RP1_GPCLK &&
                config.gpio_tx_pin == 20 &&
                jConfig["Operation"].value("Transmit Backend", std::string()) ==
                    "rp1-gpclk",
            "Pi 5 web GPIO changes must retain the canonical RP1 backend identity");

        config.transmit_backend = TransmitBackendKind::GPIO;
        resolve_backend_specific_config(config);
        config_to_json();
        std::string route_persistence_error;
        require(
            persist_rp1_gpclk_route_config(4, &route_persistence_error) &&
                config.transmit_backend == TransmitBackendKind::RP1_GPCLK &&
                config.gpio_tx_pin == 4 &&
                jConfig["Operation"].value("Transmit Backend", std::string()) ==
                    "rp1-gpclk",
            "Pi 5 route persistence must promote route-neutral GPIO to the canonical RP1 backend: " +
                route_persistence_error);

        clear_rp1_gpclk_provider_available_override_for_test();
        set_raspberry_pi_generation_override_for_test(4);
    }
#endif

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

    void cleanup_scheduler_regression_test_state()
    {
        stop_runtime_components_for_test();
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        set_band_gpio_selector_for_test(false, false);
        set_raspberry_pi_generation_override_for_test(4);
        set_scheduler_execution_suppressed_for_test(false);
        reset_managed_reload_runtime_for_test();
        clear_current_wspr_runtime_state_for_test();
        reset_current_transmission_request_for_test();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
    }

    void clear_pi_generation_override_for_scope() noexcept
    {
        set_raspberry_pi_generation_override_for_test(4);
    }

    void clear_si5351_detection_override_for_scope() noexcept
    {
        set_si5351_detection_override_for_test(true);
    }

    void sync_wspr_fields_for_test() noexcept
    {
        config.wspr.callsign = config.callsign;
        config.wspr.grid_square = config.grid_square;
        config.wspr.power_dbm = config.power_dbm;
        config.wspr.frequencies = config.frequencies;
        config.wspr.audio_offset_hz = config.wspr_audio_offset_hz;
        config.wspr.planner_preference = config.wspr_planner_preference;
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

    void require_patch_rejects_with_plan_status_and_message(
        const nlohmann::json &patch,
        const std::string &expected_plan_status,
        const std::string &expected_message,
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
                std::string(e.what()) == expected_message,
                message + " must report the expected planner validation message");
            require(
                e.details().is_object(),
                message + " must expose structured planner validation details");
            require(
                e.details().value("plan_status", "") == expected_plan_status,
                message + " must report the expected planner status");
            require(
                e.details().value("message", "") == expected_message,
                message + " must preserve the planner validation message in details");
        }

        require(threw, message + " must be rejected at config acceptance time");
        require(
            config.callsign == original_callsign &&
                config.grid_square == original_locator &&
                config.wspr_planner_preference == original_planner_preference,
            message + " must not mutate live config after rejection");
    }

void test_explicit_tone_experimental_policy()
{
    init_default_config();
    reset_managed_reload_runtime_for_test();
    reset_current_transmission_request_for_test();
    set_scheduler_execution_suppressed_for_test(true);
    config.transmit = false;
    config.transmit_backend = TransmitBackendKind::GPIO;
    config.mode = ModeType::QRSS;
    set_raspberry_pi_generation_override_for_test(4);
    for (const bool allowed : {false, true})
    {
        config.allow_unqualified_frequency = allowed;
        copy_runtime_config(config, config);
        for (const auto& request : {
                 TestToneRequest{TestToneFrequencySource::WsprBand, "12m", std::nullopt},
                 TestToneRequest{TestToneFrequencySource::CustomRf, "", 24926100U}})
        {
            const auto started = start_test_tone(request);
            const auto committed = current_transmission_request_for_test();
            const auto stopped = end_test_tone();
            require(started.started == allowed && !config.transmit,
                "explicit experimental tones must honor opt-in without enabling scheduling");
            if (allowed)
                require(stopped.stopped && committed.allow_unqualified_frequency,
                    "explicit experimental tones must preserve opt-in through selector preparation");
        }
    }
    set_scheduler_execution_suppressed_for_test(false);
    clear_pi_generation_override_for_scope();
}

} // namespace

void test_idle_startup_overrides()
{
    for (const char *flag : {"WSPRRYPI_ROUTE_RESTORE_IDLE", "WSPRRYPI_RP1_REBOOT_IDLE"})
    {
        setenv(flag, "offline-restoration", 1);
        for (auto policy : {EnableOnBootBehavior::Never,
                            EnableOnBootBehavior::Follow,
                            EnableOnBootBehavior::Always})
        {
            init_default_config();
            config.use_ini = true;
            config.ini_filename = "/tmp/route_restoration_startup.ini";
            write_text_file(config.ini_filename, "");
            iniFile.set_filename(config.ini_filename);
            config.enable_on_boot = policy;
            config.transmit = true;
            config_to_json();
            json_to_ini();
            apply_enable_on_boot_startup_policy();
            require(!config.transmit &&
                iniFile.getData().at("Operation").at("Transmit") == "false",
                "Idle startup must disable and persist transmission for every boot policy");
            require(config.enable_on_boot == policy,
                "Idle startup must preserve the saved boot preference");
        }
        unsetenv(flag);
    }
}

int main(int argc, char *argv[])
{
    if (argc == 2 && std::string(argv[1]) == "--wtp-transient-ini-only")
    {
        ScopedTemporaryFile ini("/tmp/wsprrypi-wtp-transient-XXXXXX");
        auto data = make_managed_ini_data(
            "AA0NT", "EM18", "20m", true, WsprPlannerPreference::Auto, "Follow");
        data["Operation"]["Transmit Backend"] = "wtp";
        data["Operation"]["Use LED"] = "false";
        data["Operation"]["Use Amp"] = "false";
        data["Operation"]["Use Shutdown"] = "false";
        data["Experimental"] = {
            {"Allow Unqualified Frequency", "true"},
            {"Allow Non-Amateur Frequency", "true"}};
        data["WTP"] = {
            {"Transport", "network"},
            {"Hostname", "wsprrypico-0a60df.local"},
            {"TCP Port", "18443"},
            {"TLS Server Identity", "wsprrypico-0a60df.local"},
            {"TLS CA File", "/tmp/ca.crt"},
            {"TLS Client Certificate", "/tmp/client.crt"},
            {"TLS Client Key", "/tmp/client.key"},
            {"Endpoint", ""},
            {"USB Serial", ""},
            {"USB Vendor ID", "0"},
            {"USB Product ID", "0"},
            {"Device ID", "fd6127d11d6aca42a9905fa3fb1bf1d5"},
            {"Allow Frequency Adjustment", "true"},
            {"Start Uncertainty ns", "500000000"}};
        write_managed_ini_file(ini.path(), data);
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi", "-i", ini.path(),
            "--dfcw-message", "ET E",
            "--dfcw-dot-frequency", "137505",
            "--dfcw-dash-frequency", "137500",
            "--dfcw-dot-seconds", "3"};
        std::vector<char *> parsed_argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(parsed_argv.size()), parsed_argv.data()),
            "managed WTP INI plus complete transient DFCW request must parse");
        std::string message;
        double dot_hz = 0.0;
        double dash_hz = 0.0;
        double dot_seconds = 0.0;
        require(
            config.use_ini && config.transmit_backend == TransmitBackendKind::WTP &&
                try_get_dfcw_startup_request(message, dot_hz, dash_hz, dot_seconds) &&
                message == "ET E" && nearly_equal(dot_hz, 137505.0) &&
                nearly_equal(dash_hz, 137500.0) && nearly_equal(dot_seconds, 3.0),
            "managed WTP transient DFCW must preserve explicit reverse-profile frequencies");
        std::cout << "Managed WTP transient INI parsing passed" << std::endl;
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string(argv[1]) == "--calibration-roundtrip-only")
    {
        init_config_json();
        json_to_config();
        for (const double value : {0.0, -0.0, 12.345678, -3.5})
        {
            auto data = make_managed_ini_data("AA0NT", "EM18", "20m", false);
            // WTP is compiled in the portable profile. Candidate preparation
            // validates these fixture identities without opening an endpoint.
            data["Operation"]["Transmit Backend"] = "wtp";
            data["WTP"] = {{"Endpoint", "/dev/calibration-test"},
                {"USB Serial", "calibration-test"}, {"USB Vendor ID", "1"},
                {"USB Product ID", "2"}, {"Device ID", std::string(32, 'a')}};
            const auto text = nlohmann::json(value).dump();
            data["Calibration"]["PPM"] = text;
            data["GPIO"]["Manual PPM"] = text;
            data["GPIO"]["Frequency Residual PPM"] = text;
            iniFile.setData(data);
            PreparedConfigCandidate candidate;
            prepare_ini_config_candidate("/tmp/calibration_roundtrip.ini", candidate);
            require(candidate.valid, "calibration INI candidate must parse: " + candidate.error_reason);
            require(candidate.normalized_config.si5351_ppm == value &&
                    candidate.normalized_config.gpio_manual_ppm == value &&
                    candidate.normalized_config.gpio_frequency_residual_ppm == value,
                    "calibration must preserve exact zero and custom double values");
            auto serialized = nlohmann::json::object();
            config_handler_serialization::serialize_runtime_config_to_json(
                candidate.normalized_config, serialized);
            require(serialized["Calibration"]["PPM"].get<double>() == value &&
                    serialized["GPIO"]["Manual PPM"].get<double>() == value &&
                    serialized["GPIO"]["Frequency Residual PPM"].get<double>() == value,
                    "calibration serialization must not introduce an exponent artifact");
        }
        ArgParserConfig defaults;
        require(defaults.si5351_ppm == 0.0 && defaults.gpio_manual_ppm == 0.0 &&
                defaults.gpio_frequency_residual_ppm == 0.0,
                "calibration defaults must be exact zero");
        std::cout << "Calibration exact-zero and custom-value round trips passed" << std::endl;
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string(argv[1]) == "--idle-startup-overrides-only")
    {
        test_idle_startup_overrides();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string(argv[1]) == "--i2c-bus-selection-only")
    {
        init_config_json();
        json_to_config();
        set_si5351_detection_override_for_test(true);
        set_si5351_address_inventory_override_for_test(1, {0x60, 0x6F});
        set_raspberry_pi_generation_override_for_test(4);
        const auto inventory = i2c_bus_inventory::discover();
        const auto public_config = get_public_config_json();
        const auto &platform = public_config.at("Platform");
        assert(platform.at("I2C Bus Discovery Error") == inventory.error);
        assert(platform.at("I2C Buses").size() == inventory.buses.size());
        for (std::size_t i = 0; i < inventory.buses.size(); ++i)
        {
            assert(platform.at("I2C Buses").at(i).at("Number") == inventory.buses[i].number);
            assert(platform.at("I2C Buses").at(i).at("Name") == inventory.buses[i].name);
        }
        assert(platform.at("Si5351 Address Bus") == 1);
        assert(platform.at("Si5351 Addresses") ==
            nlohmann::json::array({"0x60", "0x6F"}));
        assert(platform.at("Si5351 Address Discovery Error") == "");
        const auto address_response =
            web_server_routes::build_si5351_addresses_response("1");
        assert(address_response.status == 200);
        const auto address_body = nlohmann::json::parse(address_response.body);
        assert(address_body.at("I2C Bus") == 1);
        assert(address_body.at("Addresses") ==
            nlohmann::json::array({"0x60", "0x6F"}));
        assert(web_server_routes::build_si5351_addresses_response("1junk").status == 400);
        assert(!jConfig.contains("Platform"));
        const auto before = jConfig;
        const int saved_bus = config.si5351_i2c_bus;
        bool rejected = false;
        try
        {
            patch_all_from_web({
                {"Si5351", {{"I2C Bus", 2147483647}}},
                {"Platform", {{"I2C Buses", {{{"Number", 2147483647}, {"Name", "forged"}}}}}}});
        }
        catch (const std::exception &error)
        {
            rejected = std::string(error.what()).find("I2C bus") != std::string::npos;
        }
        assert(rejected);
        assert(jConfig == before);
        assert(config.si5351_i2c_bus == saved_bus);
        rejected = false;
        try
        {
            patch_all_from_web({{"Si5351", {{"I2C Address", "0x61"}}}});
        }
        catch (const std::exception &error)
        {
            rejected = std::string(error.what()).find("No register-compatible") !=
                std::string::npos;
        }
        assert(rejected);
        assert(jConfig == before);

        rejected = false;
        try
        {
            patch_all_from_web({{"Si5351", {{"I2C Address", "0x70"}}}});
        }
        catch (const std::exception &error)
        {
            rejected = std::string(error.what()).find("0x60 through 0x6F") !=
                std::string::npos;
        }
        assert(rejected);
        assert(jConfig == before);
        std::cout << "I2C web selection rejection preserves configuration" << std::endl;
        return EXIT_SUCCESS;
    }

    if (argc == 2 &&
        std::string(argv[1]) == "--rp1-web-backend-persistence-only")
    {
#if WSPRRYPI_BACKEND_RP1_GPCLK
        test_rp1_web_backend_persistence();
        std::cout << "RP1 web backend persistence tests passed" << std::endl;
#else
        std::cout <<
            "RP1 web backend persistence test skipped: RP1 backend unavailable" <<
            std::endl;
#endif
        return EXIT_SUCCESS;
    }

    if (const char *cli_check = std::getenv("DIAL_FREQUENCY_SEMANTICS_CLI_CHECK");
        cli_check != nullptr && std::string(cli_check) == "1")
    {
        init_config_json();
        json_to_config();
        reset_getopt_state();
        return parse_command_line(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    set_si5351_detection_override_for_test(true);
    set_raspberry_pi_generation_override_for_test(4);

    if (argc == 2 && std::string(argv[1]) == "--explicit-tone-policy-only")
    {
        test_explicit_tone_experimental_policy();
        std::cout << "Explicit tone experimental policy tests passed" << std::endl;
        return EXIT_SUCCESS;
    }

    init_default_config();
    require(
        config.rp1_gpio_drive_ma == 2,
        "RP1 GPIO drive must default to the minimum 2 mA profile");
    init_config_json();
    require(
        jConfig.at("GPIO").at("RP1 Drive mA").get<int>() == 2,
        "canonical default JSON must publish the minimum 2 mA RP1 drive");
    for (const int drive_ma : {2, 4, 8, 12})
    {
        config.rp1_gpio_drive_ma = drive_ma;
        config_to_json();
        config.rp1_gpio_drive_ma = 2;
        json_to_config();
        require(
            config.rp1_gpio_drive_ma == drive_ma,
            "each supported RP1 GPIO drive must survive JSON persistence");
    }
    jConfig["GPIO"]["RP1 Drive mA"] = 6;
    bool invalid_rp1_drive_rejected = false;
    try
    {
        json_to_config();
    }
    catch (const std::exception& error)
    {
        invalid_rp1_drive_rejected =
            std::string(error.what()).find("2, 4, 8, or 12") !=
            std::string::npos;
    }
    require(
        invalid_rp1_drive_rejected,
        "unsupported RP1 GPIO drive values must be rejected while loading JSON");
    init_default_config();
    config.use_ini = true;
    config.ini_filename = "/tmp/rp1_drive_persistence.ini";
    config.rp1_gpio_drive_ma = 12;
    config_to_json();
    write_text_file(config.ini_filename, "");
    iniFile.set_filename(config.ini_filename);
    json_to_ini();
    require(
        iniFile.getData().at("GPIO").at("RP1 Drive mA") == "12",
        "RP1 GPIO drive must be included in managed INI persistence");
    config.rp1_gpio_drive_ma = 2;
    ini_to_json(config.ini_filename);
    json_to_config();
    require(
        config.rp1_gpio_drive_ma == 12,
        "RP1 GPIO drive must survive managed INI reload");
    set_raspberry_pi_generation_override_for_test(5);
    config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
    for (const int drive_ma : {2, 4, 8, 12})
    {
        config.rp1_gpio_drive_ma = drive_ma;
        resolve_backend_specific_config(config);
        require(
            config.power_level == drive_ma,
            "explicit RP1 runtime resolution must consume each committed drive unchanged");
    }
    set_raspberry_pi_generation_override_for_test(4);
    config.transmit_backend = TransmitBackendKind::GPIO;
    config.gpio_power_level = 7;
    resolve_backend_specific_config(config);
    require(
        config.power_level == 7,
        "Pi 4 runtime resolution must preserve the legacy GPIO power level");
    init_default_config();

    {
#if WSPRRYPI_BACKEND_RP1_GPCLK
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
        config.frequencies = "20m";
        config.wspr.frequencies = config.frequencies;
        set_frequencies(config);
        config.gpio_tx_pin = 20;
        resolve_backend_specific_config(config);

        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "Pi 5 RP1 validation must accept GPIO20 when the provider is available");

        config_to_json();
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        json_to_config();
        require(
            config.gpio_tx_pin == 20 && config.tx_pin == 20,
            "Pi 5 GPIO20 must survive JSON parsing and runtime resolution");

        config.use_ini = true;
        config.ini_filename = "/tmp/rp1_gpio20_route_persistence.ini";
        config_to_json();
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);
        json_to_ini();
        require(
            iniFile.getData().at("GPIO").at("Transmit Pin") == "20",
            "Pi 5 GPIO20 must be written to managed INI persistence");
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        ini_to_json(config.ini_filename);
        json_to_config();
        require(
            config.gpio_tx_pin == 20 && config.tx_pin == 20,
            "Pi 5 GPIO20 must survive managed INI reload");

        config.use_ini = false;
        config.rp1_development_confirmation_json = R"({
            "enabled": true,
            "route": "GPIO20",
            "operation_id": "wspr-frame-operation-20",
            "physical_connection_confirmed": true,
            "attenuation_and_load_confirmed": true,
            "bounded_operation_confirmed": true,
            "non_radiating_topology_confirmed": true,
            "experimental_status_acknowledged": true
        })";
        set_rp1_development_reconcile_invoker_for_test(
            [](const std::string &route) {
                require(route == "GPIO20",
                    "GPIO20 WSPR planning must reconcile the confirmed route");
                set_rp1_route_transaction_inhibited(false);
                return nlohmann::json{
                    {"ok", true},
                    {"generation", 20},
                    {"persisted", "GPIO20"},
                    {"active", "GPIO20"},
                    {"reconciled", true},
                    {"journal", "none"},
                    {"contractIdentity", "rp1-gpclk-route-manager"},
                    {"endpointOpen", false},
                    {"endpointOwned", true},
                    {"state", "idle"},
                    {"outputInhibited", "Disabled"},
                    {"operationalReady", "Ready"}};
            });
        const ArgParserConfig copied_confirmation_config = config;
        require(
            copied_confirmation_config.rp1_development_confirmation_json ==
                config.rp1_development_confirmation_json,
            "WSPR runtime configuration copies must retain transient RP1 confirmation");
        config_to_json();
        require(
            jConfig.dump().find("wspr-frame-operation-20") == std::string::npos,
            "transient RP1 confirmation must never enter persisted JSON configuration");
        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "Pi 5 GPIO20 must enter the normal scheduler planning path");
        const TransmissionRequest gpio20_request =
            current_transmission_request_for_test();
        require(
            gpio20_request.tx_gpio == 20,
            "Pi 5 scheduling must snapshot GPIO20 into the committed request");
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        require(
            current_transmission_request_for_test().tx_gpio == 20,
            "a committed Pi 5 schedule must retain its GPIO20 snapshot after config changes");
        finish_runtime_planning_state_for_identity_test();
        reset_rp1_development_reconcile_invoker_for_test();

        clear_rp1_gpclk_provider_available_override_for_test();
        clear_pi_generation_override_for_scope();
#endif
    }

    BandLookup lookup;

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
        nearly_equal(
            lookup.parse_string_to_frequency("60m", false, "wrc15"),
            5364700.0) &&
            nearly_equal(
                lookup.parse_string_to_frequency("60m:legacy", false, "wrc15"),
                5287200.0),
        "WSPR profile resolution must affect only bare 60m");
    bool rejected_22m = false;
    try { (void)lookup.parse_string_to_frequency("22m", false); }
    catch (const std::invalid_argument &) { rejected_22m = true; }
    require(rejected_22m, "22m must no longer resolve as a WSPR alias");
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

#if WSPRRYPI_BACKEND_RPI_GPIO || WSPRRYPI_BACKEND_RP1_GPCLK || WSPRRYPI_BACKEND_SI5351
    {
        ScopedTemporaryFile conflict_ini("/tmp/wsprrypi-gpio-migration-conflict-XXXXXX");
        iniFile.set_filename(conflict_ini.path());
        auto conflict_data = make_managed_ini_data("AA0NT", "EM18", "20m", false);
#if !WSPRRYPI_BACKEND_RPI_GPIO && WSPRRYPI_BACKEND_SI5351
        conflict_data["Operation"]["Transmit Backend"] = "si5351";
#elif !WSPRRYPI_BACKEND_RPI_GPIO && WSPRRYPI_BACKEND_RP1_GPCLK
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
#endif
        conflict_data["GPIO"]["Use System Clock Frequency Estimate"] = "true";
        conflict_data["GPIO"]["Use NTP"] = "false";
        iniFile.setData(conflict_data);
        iniFile.save();

        PreparedConfigCandidate conflict_candidate;
        prepare_ini_config_candidate(conflict_ini.path(), conflict_candidate);
        const bool conflict_warning = std::any_of(
            conflict_candidate.warnings.begin(),
            conflict_candidate.warnings.end(),
            [](const std::string &warning)
            {
                return warning.find("conflicts with GPIO.Use System Clock Frequency Estimate") !=
                    std::string::npos;
            });
        require(conflict_candidate.valid,
                "GPIO estimate conflict migration must produce a valid configuration: " +
                    conflict_candidate.error_reason);
        require(conflict_candidate.migration_required,
                "GPIO estimate conflict must require migration");
        require(
            conflict_candidate.normalized_config.gpio_use_system_clock_frequency_estimate,
            "the canonical GPIO estimate value must win a legacy conflict");
        require(conflict_warning,
                "GPIO estimate conflict migration must produce a clear warning");
        commit_config_candidate(conflict_candidate);
        std::ifstream conflict_file(conflict_ini.path());
        const std::string conflict_text(
            (std::istreambuf_iterator<char>(conflict_file)),
            std::istreambuf_iterator<char>());
        require(
            conflict_text.find("Use NTP =") == std::string::npos,
            "conflict migration must remove the retired key after retaining the canonical value");
#if !WSPRRYPI_BACKEND_RPI_GPIO && !WSPRRYPI_BACKEND_SI5351 && WSPRRYPI_BACKEND_RP1_GPCLK
        clear_rp1_gpclk_provider_available_override_for_test();
        clear_pi_generation_override_for_scope();
#endif
    }
#endif

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
            read_text_file("../config/wsprrypi.ini");
        require(
            stock_ini.find("2200m = \n2200m Active High = false") != std::string::npos &&
                stock_ini.find("20m = \n20m Active High = false") != std::string::npos &&
                stock_ini.find("2m =\n2m Active High = false") != std::string::npos &&
                stock_ini.find("Active High = true") == std::string::npos,
            "stock INI must declare explicit disabled Band GPIO defaults for every band");
        require(
            stock_ini.find("[Experimental]") != std::string::npos &&
                stock_ini.find("Allow Unqualified Frequency = false") != std::string::npos &&
                stock_ini.find("Allow Non-Amateur Frequency = false") != std::string::npos,
            "stock INI must expose both experimental frequency controls as default deny");
        require(
            stock_ini.find("RP1 Drive mA = 2") != std::string::npos,
            "stock INI must publish the minimum RP1 GPIO drive default");
    }

    if (has_ancillary_gpio())
    {
        const auto configure_shared_band_gpio = [](ArgParserConfig &candidate,
                                                    bool active_high) {
            candidate.band_gpio[ham_band_index(HamBand::BAND_20M)] =
                BandGPIOConfig{.gpio = 5, .enabled = true, .active_high = active_high};
            candidate.band_gpio[ham_band_index(HamBand::BAND_40M)] =
                BandGPIOConfig{.gpio = 5, .enabled = true, .active_high = active_high};
        };

        prime_valid_runtime_identity_config();
        configure_shared_band_gpio(config, true);
        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "two enabled bands may share one GPIO when their polarity matches");

        config.band_gpio[ham_band_index(HamBand::BAND_80M)] =
            BandGPIOConfig{.gpio = 5, .enabled = true, .active_high = true};
        require(
            validate_config_candidate(config, &validation_error),
            "three enabled bands may share one GPIO when their polarity matches");

        config.band_gpio[ham_band_index(HamBand::BAND_80M)].active_high = false;
        require(
            !validate_config_candidate(config, &validation_error) &&
                validation_error ==
                    "Bands sharing GPIO5 must use the same Active High setting.",
            "bands sharing a GPIO with conflicting polarity must be rejected");

        prime_valid_runtime_identity_config();
        configure_shared_band_gpio(config, true);
        config_to_json();
        json_to_config();
        require(
            validate_config_candidate(config, &validation_error),
            "JSON configuration must retain valid shared Band GPIO assignments");

        for (const auto &[use_led, use_shutdown, use_amp, pin, label] :
             std::vector<std::tuple<bool, bool, bool, int, std::string>>{
                 {true, false, false, 5, "Transmit LED"},
                 {false, true, false, 5, "Shutdown Button"},
                 {false, false, true, 5, "Amp Control"}})
        {
            prime_valid_runtime_identity_config();
            configure_shared_band_gpio(config, true);
            config.use_led = use_led;
            config.led_pin = use_led ? pin : -1;
            config.use_shutdown = use_shutdown;
            config.shutdown_pin = use_shutdown ? pin : -1;
            config.use_amp = use_amp;
            config.amp_pin = use_amp ? pin : -1;
            require(
                !validate_config_candidate(config, &validation_error) &&
                    validation_error == "GPIO5 is reserved by " + label + ".",
                "Band GPIO must reject an enabled " + label + " assignment");
        }

        prime_valid_runtime_identity_config();
        configure_shared_band_gpio(config, true);
        config.use_led = false;
        config.led_pin = 5;
        config.use_shutdown = false;
        config.shutdown_pin = 5;
        config.use_amp = false;
        config.amp_pin = 5;
        require(
            validate_config_candidate(config, &validation_error),
            "disabled Pi I/O features must not reserve their retained GPIO values");

        enum class OrdinaryGpioRole
        {
            Band,
            Led,
            Shutdown,
            Amp
        };
        const auto assign_ordinary_gpio = [](ArgParserConfig &candidate,
                                             OrdinaryGpioRole role,
                                             int pin,
                                             bool enabled) {
            switch (role)
            {
                case OrdinaryGpioRole::Band:
                    candidate.band_gpio[ham_band_index(HamBand::BAND_20M)] =
                        BandGPIOConfig{.gpio = pin, .enabled = enabled, .active_high = true};
                    break;
                case OrdinaryGpioRole::Led:
                    candidate.use_led = enabled;
                    candidate.led_pin = pin;
                    break;
                case OrdinaryGpioRole::Shutdown:
                    candidate.use_shutdown = enabled;
                    candidate.shutdown_pin = pin;
                    break;
                case OrdinaryGpioRole::Amp:
                    candidate.use_amp = enabled;
                    candidate.amp_pin = pin;
                    break;
            }
        };

        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        for (const bool transmit : {false, true})
        {
            for (const int transmit_pin : {4, 20})
            {
                const int available_pin = transmit_pin == 4 ? 20 : 4;
                for (const OrdinaryGpioRole role : {
                         OrdinaryGpioRole::Band,
                         OrdinaryGpioRole::Led,
                         OrdinaryGpioRole::Shutdown,
                         OrdinaryGpioRole::Amp})
                {
                    prime_valid_runtime_identity_config();
                    config.transmit = transmit;
                    config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
                    config.gpio_tx_pin = transmit_pin;
                    resolve_backend_specific_config(config);
                    assign_ordinary_gpio(config, role, transmit_pin, true);
                    require(
                        !validate_config_candidate(config, &validation_error) &&
                            validation_error ==
                                "GPIO" + std::to_string(transmit_pin) +
                                    " is reserved by GPIO RF Output.",
                        "GPIO RF output must reject every enabled ordinary role on its selected pin regardless of transmit state");

                    prime_valid_runtime_identity_config();
                    config.transmit = transmit;
                    config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
                    config.gpio_tx_pin = transmit_pin;
                    resolve_backend_specific_config(config);
                    assign_ordinary_gpio(config, role, available_pin, true);
                    require(
                        validate_config_candidate(config, &validation_error),
                        "GPIO RF output must leave the other GPCLK0-capable pin available to ordinary roles");

                    prime_valid_runtime_identity_config();
                    config.transmit = transmit;
                    config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
                    config.gpio_tx_pin = transmit_pin;
                    resolve_backend_specific_config(config);
                    assign_ordinary_gpio(config, role, transmit_pin, false);
                    require(
                        validate_config_candidate(config, &validation_error),
                        "disabled ordinary roles may retain the selected GPIO RF output pin");
                }
            }
        }
        clear_rp1_gpclk_provider_available_override_for_test();
        clear_pi_generation_override_for_scope();

        for (const int retained_transmit_pin : {4, 20})
        {
            for (const int ordinary_pin : {4, 20})
            {
                for (const OrdinaryGpioRole role : {
                         OrdinaryGpioRole::Band,
                         OrdinaryGpioRole::Led,
                         OrdinaryGpioRole::Shutdown,
                         OrdinaryGpioRole::Amp})
                {
                    prime_valid_runtime_identity_config();
                    config.transmit_backend = TransmitBackendKind::SI5351;
                    config.gpio_tx_pin = retained_transmit_pin;
                    resolve_backend_specific_config(config);
                    assign_ordinary_gpio(config, role, ordinary_pin, true);
                    require(
                        validate_config_candidate(config, &validation_error),
                        "Si5351 backend must not reserve a retained GPIO transmit-pin value");
                }
            }
        }

        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::GPIO;
        config.gpio_tx_pin = 20;
        config.use_led = true;
        config.led_pin = 20;
        config_to_json();
        json_to_config();
        require(
            !validate_config_candidate(config, &validation_error) &&
                validation_error == "GPIO20 is reserved by GPIO RF Output.",
            "JSON round trips must preserve and reject a retained GPIO RF-output conflict");

        auto transmit_gpio_conflict_ini =
            make_managed_ini_data("AA0NT", "EM18", "20m", false);
        transmit_gpio_conflict_ini["GPIO"]["Transmit Pin"] = "20";
        transmit_gpio_conflict_ini["Operation"]["Use Shutdown"] = "true";
        transmit_gpio_conflict_ini["Operation"]["Shutdown Button"] = "20";
        iniFile.setData(transmit_gpio_conflict_ini);
        PreparedConfigCandidate transmit_gpio_ini_candidate;
        prepare_ini_config_candidate(
            "/tmp/transmit_gpio_conflict.ini",
            transmit_gpio_ini_candidate);
        require(
            !transmit_gpio_ini_candidate.valid &&
                transmit_gpio_ini_candidate.error_reason ==
                    "GPIO20 is reserved by GPIO RF Output.",
            "managed INI load and reload candidates must reject GPIO RF-output conflicts when transmit is off");

        prime_valid_runtime_identity_config();
        config_to_json();
        const nlohmann::json before_transmit_gpio_patch = jConfig;
        bool transmit_gpio_patch_rejected = false;
        try
        {
            patch_all_from_web({
                {"Operation", {{"Use Amp", true}, {"Amp Pin", 4}}}});
        }
        catch (const std::exception &e)
        {
            transmit_gpio_patch_rejected =
                std::string(e.what()).find(
                    "GPIO4 is reserved by GPIO RF Output.") != std::string::npos;
        }
        require(
            transmit_gpio_patch_rejected && jConfig == before_transmit_gpio_patch,
            "REST/web patches must reject GPIO RF-output conflicts without changing configuration");

        config.use_led = true;
        config.led_pin = 6;
        config.use_shutdown = true;
        config.shutdown_pin = 6;
        require(
            !validate_config_candidate(config, &validation_error) &&
                validation_error ==
                    "GPIO6 is assigned to both Transmit LED and Shutdown Button.",
            "enabled LED, shutdown, and amplifier roles must remain mutually exclusive");

        auto managed_ini = make_managed_ini_data("AA0NT", "EM18", "20m", true);
        managed_ini["Band GPIO"]["20m"] = "5";
        managed_ini["Band GPIO"]["20m Active High"] = "true";
        managed_ini["Band GPIO"]["40m"] = "5";
        managed_ini["Band GPIO"]["40m Active High"] = "true";
        iniFile.setData(managed_ini);
        PreparedConfigCandidate ini_candidate;
        prepare_ini_config_candidate("/tmp/shared_band_gpio.ini", ini_candidate);
        require(
            ini_candidate.valid,
            "INI configuration must accept matching shared Band GPIO assignments");

        managed_ini["Band GPIO"]["22m"] = "";
        managed_ini["Band GPIO"]["22m Active High"] = "false";
        iniFile.setData(managed_ini);
        prepare_ini_config_candidate("/tmp/retired_disabled_22m.ini", ini_candidate);
        require(
            ini_candidate.valid,
            "INI migration must ignore the retired 22m Band GPIO pair only when disabled");
        managed_ini["Band GPIO"]["22m"] = "5";
        iniFile.setData(managed_ini);
        prepare_ini_config_candidate("/tmp/retired_enabled_22m.ini", ini_candidate);
        require(
            !ini_candidate.valid &&
                ini_candidate.error_reason.find("Unknown key in [Band GPIO]") !=
                    std::string::npos &&
                ini_candidate.error_reason.find("22m") != std::string::npos,
            "INI migration must reject a configured retired 22m Band GPIO assignment");
        managed_ini["Band GPIO"].erase("22m");
        managed_ini["Band GPIO"].erase("22m Active High");

        managed_ini["Band GPIO"]["40m Active High"] = "false";
        iniFile.setData(managed_ini);
        prepare_ini_config_candidate("/tmp/conflicting_band_gpio.ini", ini_candidate);
        require(
            !ini_candidate.valid && ini_candidate.error_reason ==
                "Bands sharing GPIO5 must use the same Active High setting.",
            "INI configuration must reject conflicting shared Band GPIO polarity");

        prime_valid_runtime_identity_config();
        config.use_led = true;
        config.led_pin = 5;
        config_to_json();
        const nlohmann::json before_invalid_patch = jConfig;
        bool patch_rejected = false;
        try
        {
            patch_all_from_web({
                {"Band GPIO",
                 {{"20m", {{"GPIO", 5}, {"Enabled", true}, {"Active High", true}}}}}});
        }
        catch (const std::exception &)
        {
            patch_rejected = true;
        }
        require(
            patch_rejected && jConfig == before_invalid_patch,
            "web patches must reject reserved Band GPIO assignments without changing config");
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
        const auto provenance = current_tx_runtime_status_snapshot();
        require(
            provenance.gpio_correction_candidate.available &&
                provenance.gpio_correction_candidate.processor_profile == "BCM2711" &&
                provenance.gpio_correction_candidate.selected_parent == "PLLD" &&
                provenance.gpio_correction_candidate.nominal_rate_hz == 750000000.0 &&
                nearly_equal(
                    provenance.gpio_correction_candidate.intrinsic_ppm,
                    0.0,
                    1.0e-9) &&
                nearly_equal(
                    provenance.gpio_correction_candidate.final_ppm,
                    0.0,
                    1.0e-9),
            "candidate provenance must expose the exact planned BCM2711 parent and correction");
        require(
            provenance.gpio_correction_committed.available &&
                !provenance.gpio_correction_committed.active &&
                provenance.gpio_correction_committed.processor_profile == "BCM2711" &&
                provenance.gpio_correction_committed.selected_parent == "PLLD" &&
                provenance.gpio_correction_committed.nominal_rate_hz == 750000000.0 &&
                nearly_equal(
                    provenance.gpio_correction_committed.intrinsic_ppm,
                    0.0,
                    1.0e-9) &&
                nearly_equal(
                    provenance.gpio_correction_committed.final_ppm,
                    0.0,
                    1.0e-9) &&
                !provenance.gpio_correction_committed.execution_identity.empty(),
            "committed provenance must freeze exact plan identity without claiming idle work is active");

        finish_runtime_planning_state_for_identity_test();
        reset_current_transmission_request_for_test();
        require(
            !current_tx_runtime_status_snapshot().gpio_correction_committed.available,
            "cleared execution state must not expose stale committed provenance");
        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(4);
        prime_valid_runtime_identity_config();
        config.frequencies = "2200m";
        config.allow_unqualified_frequency = false;
        config.allow_non_amateur_frequency = false;
        sync_wspr_fields_for_test();
        require(
            set_frequencies(config),
            "BCM2711 2200 m WSPR regression must resolve its dial frequency");
        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "BCM2711 2200 m WSPR regression must plan a scheduler request");
        const auto bcm2711_legacy_request = current_transmission_request_for_test();
        const auto bcm2711_2200m_provenance = current_tx_runtime_status_snapshot();
        require(
            nearly_equal(bcm2711_legacy_request.ppm, 0.0) &&
                bcm2711_2200m_provenance.gpio_correction_candidate.available &&
                bcm2711_2200m_provenance.gpio_correction_candidate.selected_parent ==
                    "oscillator" &&
                bcm2711_2200m_provenance.gpio_correction_candidate.nominal_rate_hz ==
                    54000000.0 &&
                nearly_equal(
                    bcm2711_2200m_provenance.gpio_correction_candidate.intrinsic_ppm,
                    0.0) &&
                nearly_equal(
                    bcm2711_2200m_provenance.gpio_correction_candidate.final_ppm,
                    0.0),
            "BCM2711 2200 m planning must commit and publish the conservative oscillator zero baseline");
        auto bcm2711_request = controller_request_from_legacy_for_test(
            bcm2711_legacy_request,
            wsprrypi::TransmissionMode::WSPR);
        wsprrypi::WsprPayload bcm2711_payload;
        bcm2711_payload.prepared = bcm2711_legacy_request.payload;
        bcm2711_payload.base_frequency_hz =
            bcm2711_legacy_request.actual_rf_frequency_hz;
        bcm2711_request.payload = bcm2711_payload;
        require(
            !bcm2711_request.policy.allow_unqualified_frequency &&
                !bcm2711_request.policy.allow_non_amateur_frequency &&
                bcm2711_request.policy.hardware_profile ==
                    wsprrypi::HardwareProfile::BCM2711 &&
                wsprrypi::evaluate_gpio_band_policy(
                    wsprrypi::ExecutionPlanCompiler{}.compile(bcm2711_request)).allowed,
            "direct WSPR controller requests must preserve the qualified BCM2711 2200 m profile");
        finish_runtime_planning_state_for_identity_test();

        set_raspberry_pi_generation_override_for_test(3);
        prime_valid_runtime_identity_config();
        config.frequencies = "2200m";
        config.allow_unqualified_frequency = true;
        config.allow_non_amateur_frequency = false;
        sync_wspr_fields_for_test();
        require(
            set_frequencies(config),
            "legacy 2200 m WSPR override regression must resolve its dial frequency");
        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "legacy 2200 m WSPR override regression must plan a scheduler request");
        const auto legacy_request = current_transmission_request_for_test();
        auto legacy_override_request = controller_request_from_legacy_for_test(
            legacy_request,
            wsprrypi::TransmissionMode::WSPR);
        wsprrypi::WsprPayload legacy_payload;
        legacy_payload.prepared = legacy_request.payload;
        legacy_payload.base_frequency_hz = legacy_request.actual_rf_frequency_hz;
        legacy_override_request.payload = legacy_payload;
        require(
            legacy_override_request.policy.allow_unqualified_frequency &&
                !legacy_override_request.policy.allow_non_amateur_frequency &&
                legacy_override_request.policy.hardware_profile ==
                    wsprrypi::HardwareProfile::BCM2836_BCM2837 &&
                wsprrypi::evaluate_gpio_band_policy(
                    wsprrypi::ExecutionPlanCompiler{}.compile(legacy_override_request)).allowed,
            "direct WSPR controller requests must preserve the legacy unqualified-frequency override");
        finish_runtime_planning_state_for_identity_test();
        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::RP1_GPCLK;

        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "GPIO4 must be accepted on Raspberry Pi 5 when the RP1 provider is available");

        // This is a hardware-free planning fixture, including route confirmation.
        config.rp1_development_confirmation_json = R"({
            "enabled": true, "route": "GPIO4", "operation_id": "ppm-planning-fixture",
            "physical_connection_confirmed": true,
            "attenuation_and_load_confirmed": true,
            "bounded_operation_confirmed": true,
            "non_radiating_topology_confirmed": true,
            "experimental_status_acknowledged": true
        })";
        set_rp1_development_reconcile_invoker_for_test(
            [](const std::string &) {
                set_rp1_route_transaction_inhibited(false);
                return nlohmann::json{
                    {"ok", true}, {"generation", 20},
                    {"persisted", "GPIO4"}, {"active", "GPIO4"},
                    {"reconciled", true}, {"journal", "none"},
                    {"contractIdentity", "rp1-gpclk-route-manager"},
                    {"endpointOpen", false}, {"endpointOwned", true},
                    {"state", "idle"}, {"outputInhibited", "Disabled"},
                    {"operationalReady", "Ready"}};
            });
        config.gpio_use_system_clock_frequency_estimate = false;
        config.use_system_clock_frequency_estimate = false;
        config.gpio_manual_ppm = 12.5;
        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "RP1 scheduler must accept a manual GPIO correction");
        auto rp1_manual_request = current_transmission_request_for_test();
        auto rp1_manual_provenance = current_tx_runtime_status_snapshot();
        require(
            nearly_equal(rp1_manual_request.ppm, 12.5) &&
                nearly_equal(rp1_manual_provenance.additional_gpio_ppm, 12.5) &&
                nearly_equal(rp1_manual_provenance.gpio_correction_committed.additional_ppm, 12.5) &&
                rp1_manual_provenance.gpio_correction_committed.available &&
                rp1_manual_provenance.gpio_correction_committed.processor_profile ==
                    "RP1" &&
                rp1_manual_provenance.gpio_correction_committed.selected_parent ==
                    "PLL_SYS" &&
                nearly_equal(
                    rp1_manual_provenance.gpio_correction_committed.nominal_rate_hz,
                    200000000.0) &&
                nearly_equal(
                    rp1_manual_provenance.gpio_correction_committed.intrinsic_ppm,
                    -46.245) &&
                nearly_equal(
                    rp1_manual_provenance.gpio_correction_committed.final_ppm,
                    -33.745),
            "RP1 must freeze only additional PPM for the backend and report the shared intrinsic plus manual correction");
        config.gpio_manual_ppm = -200.0;
        require(set_config(true), "RP1 must retain the full additional correction range");
        require(nearly_equal(current_transmission_request_for_test().ppm, -200.0) &&
                nearly_equal(current_tx_runtime_status_snapshot().gpio_correction_committed.final_ppm, -246.245),
            "RP1 must not clamp or apply its intrinsic offset twice at the caller boundary");
        finish_runtime_planning_state_for_identity_test();

        SystemClockFrequencyEstimate rp1_estimate;
        rp1_estimate.provider_name = "chrony";
        rp1_estimate.qualification = FrequencyEstimateQualification::Qualified;
        rp1_estimate.frequency_ppm = 10.0;
        rp1_estimate.synchronized = true;
        rp1_estimate.selected_source = true;
        rp1_estimate.leap_normal = true;
        config.gpio_use_system_clock_frequency_estimate = true;
        config.use_system_clock_frequency_estimate = true;
        config.gpio_frequency_residual_ppm = 1.25;
        config.transmit = false;
        config.loop_tx = true;
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        set_current_frequency_estimate_for_test(rp1_estimate);
        reset_runtime_planning_state_for_identity_test();
        require(
            start_test_tone(14097100).started,
            "RP1 tone planning must accept the injected qualified system-clock estimate");
        const auto rp1_estimate_request = current_transmission_request_for_test();
        const auto rp1_estimate_provenance = current_tx_runtime_status_snapshot();
        require(
            nearly_equal(rp1_estimate_request.ppm, 11.25) &&
                nearly_equal(rp1_estimate_provenance.additional_gpio_ppm, 11.25) &&
                nearly_equal(rp1_estimate_provenance.gpio_correction_committed.additional_ppm, 11.25) &&
                nearly_equal(
                    rp1_estimate_provenance.gpio_correction_committed
                        .conducted_residual_ppm,
                    1.25) &&
                nearly_equal(
                    rp1_estimate_provenance.gpio_correction_committed.final_ppm,
                    -34.995),
            "RP1 must report its shared intrinsic plus qualified estimate and conducted residual exactly once");
        require(end_test_tone().stopped, "RP1 planning fixture must stop its suppressed tone");
        finish_runtime_planning_state_for_identity_test();
        reset_rp1_development_reconcile_invoker_for_test();
        set_current_frequency_estimate_for_test(SystemClockFrequencyEstimate{});

        PreparedConfigCandidate candidate;
        auto managed_rp1_ini =
            make_managed_ini_data("AA0NT", "EM18", "20m", true);
        managed_rp1_ini["Operation"]["Transmit Backend"] = "rp1-gpclk";
        iniFile.setData(managed_rp1_ini);
        prepare_ini_config_candidate("/tmp/gpio_pi5.ini", candidate);
        require(
            candidate.valid,
            "managed GPIO4 configuration must be accepted on Raspberry Pi 5");

        clear_rp1_gpclk_provider_available_override_for_test();
        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
        config.transmit = false;

        std::string validation_error;
        require(
            validate_config_candidate(config, &validation_error),
            "available Pi 5 RP1 GPIO backend must remain valid when transmit is off");

        clear_rp1_gpclk_provider_available_override_for_test();

        PreparedConfigCandidate inactive_candidate;
        auto inactive_managed_rp1_ini =
            make_managed_ini_data("AA0NT", "EM18", "20m", false);
        inactive_managed_rp1_ini["Operation"]["Transmit Backend"] = "rp1-gpclk";
        iniFile.setData(inactive_managed_rp1_ini);
        prepare_ini_config_candidate("/tmp/inactive_gpio_pi5.ini", inactive_candidate);
        require(
            inactive_candidate.valid,
            "inactive managed GPIO configuration must load on Raspberry Pi 5 without requiring the inactive provider: " +
                inactive_candidate.error_reason);
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
        init_config_json();
        json_to_config();
        config.use_ini = false;
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--no-http",
            "--socket-loopback-only",
            "--socket-loopback-family",
            "ipv4",
            "--transmit-gpio",
            "4",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "CLI parsing with --no-http must succeed");
        require(
            config.enable_web && !config.enable_http,
            "--no-http must disable HTTP without disabling WebSocket control");
        require(
            !web_server_start_enabled(config),
            "startup gating must skip HTTP when --no-http is present");
        require(
            websocket_server_start_enabled(config),
            "startup gating must retain WebSocket control when --no-http is present");
        require(
            config.socket_loopback_only,
            "loopback-only WebSocket control must remain selected");
        require(
            config.socket_loopback_family == WebSocketLoopbackFamily::IPv4,
            "explicit IPv4 loopback selection must remain command-line bound");
    }

    {
        require_cli_parse_rejected(
            {
                "wsprrypi",
                "--socket-loopback-family",
                "ipv4",
                "AA0NT",
                "EM18",
                "20",
                "20m"},
            "loopback family without loopback-only mode");
        require_cli_parse_rejected(
            {
                "wsprrypi",
                "--socket-loopback-only",
                "--socket-loopback-family",
                "localhost",
                "AA0NT",
                "EM18",
                "20",
                "20m"},
            "invalid loopback family");
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        reset_current_transmission_request_for_test();
        reset_getopt_state();

        std::vector<std::string> args = {
            "wsprrypi",
            "--backend",
            "rp1-gpclk",
            "AA0NT",
            "EM18",
            "20",
            "20m"};
        std::vector<char *> argv = argv_for(args);

        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "explicit CLI RP1 GPCLK setup on Raspberry Pi 5 must parse before validation");
        std::string validation_error;
        require(
            validate_config_data_for_test(&validation_error),
            "CLI startup validation must accept the explicit RP1 GPCLK backend on Raspberry Pi 5");
        require(
            config.transmit_backend == TransmitBackendKind::RP1_GPCLK &&
                std::string(transmit_backend_kind_to_string(config.transmit_backend)) ==
                    "rp1-gpclk",
            "explicit RP1 GPCLK CLI selection must retain its distinct runtime identity");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                current_transmission_request_for_test().payload.frames.empty(),
            "RP1 GPCLK CLI validation must not itself commit a transmission request");

        clear_rp1_gpclk_provider_available_override_for_test();
        clear_pi_generation_override_for_scope();
    }

    {
        set_raspberry_pi_generation_override_for_test(5);
        prime_valid_runtime_identity_config();
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.si5351_i2c_bus = 1;
        config.si5351_i2c_address = 0x60;
        config.si5351_reference_hz = 27000000;
        config.si5351_tx_output = 2;
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

#if WSPRRYPI_BACKEND_SI5351
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
            "Si5351 backend configure path must remain allowed on Raspberry Pi 5: " +
                compile_result.error);

        const wsprrypi::ExecutionResult execute_result = backend.execute(plan);
        require(
            execute_result.ok,
            "Si5351 backend execution path must remain allowed on Raspberry Pi 5");

        clear_pi_generation_override_for_scope();
    }
#endif

    {
        WsprTransmitter transmitter;
        transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED);

        wsprrypi::TransmissionRequest controller_request;
        controller_request.mode = wsprrypi::TransmissionMode::TONE;
        controller_request.output.backend = wsprrypi::BackendKind::RPI_CLOCK_GPIO;
        controller_request.output.output = wsprrypi::ClockSource::GPIO_CLK;
        controller_request.output.gpio = 4;
        controller_request.calibration.ppm = 0.0;
        controller_request.id.value = 1;

        wsprrypi::TonePayload tone_payload;
        tone_payload.frequency_hz = 14097100.0;
        controller_request.payload = tone_payload;

        TransmissionRequest legacy_request;
        legacy_request.mode = TransmissionMode::TONE;
        legacy_request.actual_rf_frequency_hz = 14097100.0;
        legacy_request.tx_gpio = 4;
        legacy_request.ppm = 0.0;
        legacy_request.power_level = 7;

        bool threw = false;
        std::string error_message;
        try
        {
            transmitter.configureExecution(controller_request, legacy_request);
        }
        catch (const std::invalid_argument &ex)
        {
            threw = true;
            error_message = ex.what();
        }

        require(
            threw,
            "legacy GPIO controller tone execution must be rejected before backend preparation");
        require(
            error_message.find(
                "Controller tone execution is supported only for SI5351 or RP1 GPCLK; received GPIO.") !=
                std::string::npos,
            "legacy GPIO controller tone execution must fail with the specific backend-routing diagnostic");
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
        jConfig["WSPR"]["Frequency Profile"] = "wrc15";
        jConfig["WSPR"]["Band Preferences"] = {{"60m", "60m:legacy"}};
        jConfig["WSPR"]["Frequency"] = "60m 60m:legacy 5.2872MHz";
        json_to_config();
        config.transmit = true;
        config.frequencies = config.wspr.frequencies;

        require(config.wspr.frequency_profile == "wrc15",
                "WSPR frequency profile must load through the configuration model");
        require(set_frequencies(config) && config.wspr_frequency_entries.size() == 3,
                "profile-aware WSPR frequency lists must parse");
        require(
            nearly_equal(config.wspr_frequency_entries[0].dial_frequency_hz, 5287200.0) &&
                nearly_equal(config.wspr_frequency_entries[1].dial_frequency_hz, 5287200.0) &&
                nearly_equal(config.wspr_frequency_entries[2].dial_frequency_hz, 5287200.0),
            "local preference must precede the profile while preserving explicit entries");
        config_to_json();
        require(jConfig["WSPR"].at("Frequency Profile") == "wrc15",
                "WSPR frequency profile must serialize through the configuration model");
        require(jConfig["WSPR"].at("Band Preferences").at("60m") == "60m:legacy",
                "WSPR band preferences must serialize through the configuration model");
    }

    {
        init_config_json();
        jConfig["WSPR"]["Band Preferences"] = {
            {"20m", 14095650},
            {"60m", "60m:wrc15"},
            {"8m", 40680000},
            {"5m", 60000000}};
        jConfig["WSPR"]["Frequency"] = "20m 60m 8m 5m";
        json_to_config();

        require(
            std::get<std::uint64_t>(config.wspr.band_preferences.at("20m")) ==
                    14095650 &&
                std::get<std::string>(config.wspr.band_preferences.at("60m")) ==
                    "60m:wrc15",
            "configuration must retain typed numeric and preset band preferences");
        require(set_frequencies(config) && config.wspr_frequency_entries.size() == 4 &&
                    nearly_equal(
                        config.wspr_frequency_entries[0].dial_frequency_hz,
                        14095650.0) &&
                    nearly_equal(
                        config.wspr_frequency_entries[1].dial_frequency_hz,
                        5364700.0) &&
                    nearly_equal(
                        config.wspr_frequency_entries[2].dial_frequency_hz,
                        40680000.0) &&
                    nearly_equal(
                        config.wspr_frequency_entries[3].dial_frequency_hz,
                        60000000.0),
            "numeric preferences, including configured-only 8m and 5m, must drive scheduler dial resolution");
        config_to_json();
        require(
            jConfig["WSPR"].at("Band Preferences").at("20m").is_number_unsigned() &&
                jConfig["WSPR"].at("Band Preferences").at("20m") == 14095650 &&
                jConfig["WSPR"].at("Band Preferences").at("60m") == "60m:wrc15",
            "typed band preferences must serialize without changing representation");
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
                config.wspr_frequency_entries[2].allow_band_gpio_fallback &&
                nearly_equal(config.wspr_frequency_entries[2].dial_frequency_hz, 14095600.0),
            "plain frequency entries must remain unmapped, eligible for Band GPIO fallback, and default active low even on managed/INI paths");
        require(
            config.wspr_frequency_entries[3].selector_gpio == 22 &&
                !config.wspr_frequency_entries[3].selector_gpio_active_high &&
                nearly_equal(config.wspr_frequency_entries[3].dial_frequency_hz, 14097100.0),
            "unit-qualified frequencies must also support @GPIO with default active-low polarity");
    }

    {
        init_config_json();
        json_to_config();
        config.transmit = false;
        config.frequencies = "223.5MHz 435000000";
        require(
            set_frequencies(config) &&
                config.wspr_frequency_entries.size() == 2U &&
                lookup.lookup_ham_band(
                    config.wspr_frequency_entries[0].dial_frequency_hz) ==
                    std::optional<HamBand>(HamBand::BAND_1_25M) &&
                lookup.lookup_ham_band(
                    config.wspr_frequency_entries[1].dial_frequency_hz) ==
                    std::optional<HamBand>(HamBand::BAND_70CM),
            "numeric WSPR entries in 1.25 m and 70 cm must resolve to canonical HamBands");

        config.frequencies = "1.25m 70cm";
        require(
            set_frequencies(config) &&
                config.wspr_frequency_entries.size() == 2U &&
                nearly_equal(
                    config.wspr_frequency_entries[0].dial_frequency_hz,
                    222100000.0) &&
                nearly_equal(
                    config.wspr_frequency_entries[1].dial_frequency_hz,
                    432300000.0),
            "authoritative 1.25 m and 70 cm WSPR aliases must parse to their dial frequencies");
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
            "[Operation]\nMode=QRSS\nTransmit=false\nTransmit Backend=si5351\nEnable on Boot=Never\nUse LED=false\nLED Pin=-1\nUse Amp=false\nAmp Pin=-1\nAmp Pin Active High=false\nWeb Port=31415\nSocket Port=31416\nUse Shutdown=false\nShutdown Button=-1\n"
            "[GPIO]\nTransmit Pin=4\nPower Level=7\nUse System Clock Frequency Estimate=false\nFrequency Residual PPM=0\nManual PPM=0\n"
            "[Calibration]\nPPM=0\n"
            "[Si5351]\nI2C Bus=1\nI2C Address=96\nReference Frequency=27000000\nTX Output=CLK0\nPower Level=1\n"
            "[WSPR]\nCall Sign=AA0NT\nGrid Square=EM18\nTX Power=20\nFrequency=20m\nPlanner Preference=auto\nUse Random Offset=false\n"
            "[CW]\nMessage=CQ\nBase Frequency= 14.0969MHz \nShift Hz=5\nDot Seconds=3.0\nIntra Element Gap=1.0\nInter Character Gap=3.0\nInter Word Gap=7.0\nDFCW Intra Element Gap=0.333333\nDFCW Inter Character Gap=1.0\nDFCW Inter Word Gap=3.0\nFade Shape=none\nFade In Ms=0\nFade Out Ms=0\nFade Slice Ms=5\nStart Minute=0\nRepeat Minutes=10\n");
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
                config.wspr_frequency_entries[0].allow_band_gpio_fallback &&
                config.wspr_frequency_entries[1].selector_gpio == kSelectorGpioUnset &&
                config.wspr_frequency_entries[1].allow_band_gpio_fallback,
            "web patch normalization must allow plain frequency entries to inherit Band GPIO config");
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
        config.frequencies = "0,2200m,630m,30m@17H,20m@27L,14.097100MHz@22";

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
                nearly_equal(config.wspr_frequency_entries[3].dial_frequency_hz, 10138700.0),
            "30m@17H must retain the GPIO mapping, polarity, and dial frequency");
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
        config.frequencies = "0.136,0.136Hz,14.0971001MHz@22,9223372036854775808";

        require(
            !set_frequencies(config) &&
                config.wspr_frequency_entries.empty() &&
                config.wspr_dial_freq_set.empty(),
            "WSPR frequency lists must reject values that do not resolve to whole-number Hz before scheduling or selector preparation");
    }

    {
        init_config_json();
        json_to_config();
        config.transmit = true;
        config.frequencies = "0,0.136MHz,136kHz,136000,2200m,0.136MHz@22";

        require(
            set_frequencies(config) &&
                config.wspr_frequency_entries.size() == 6U &&
                nearly_equal(config.wspr_frequency_entries[0].dial_frequency_hz, 0.0) &&
                nearly_equal(config.wspr_frequency_entries[1].dial_frequency_hz, 136000.0) &&
                nearly_equal(config.wspr_frequency_entries[2].dial_frequency_hz, 136000.0) &&
                nearly_equal(config.wspr_frequency_entries[3].dial_frequency_hz, 136000.0) &&
                nearly_equal(config.wspr_frequency_entries[4].dial_frequency_hz, 136000.0) &&
                nearly_equal(config.wspr_frequency_entries[5].dial_frequency_hz, 136000.0) &&
                config.wspr_frequency_entries[5].selector_gpio == 22,
            "WSPR frequency lists must retain zero skip, integral unit conversion, aliases, raw Hz, and selector suffixes");
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
        transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED);

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

        committed_request.payload.frames.clear();
        committed_request.dial_frequency_hz = 0.0;
        committed_request.actual_rf_frequency_hz = 0.0;

        bool saw_skipped_callback = false;
        transmitter.setTransmissionCallbacks(
            [&](WsprTransmitter::TransmissionCallbackEvent event,
                WsprTransmitter::LogLevel,
                const std::string &,
                double)
            {
                if (event == WsprTransmitter::TransmissionCallbackEvent::SKIPPED)
                {
                    saw_skipped_callback = true;
                }
            });
        transmitter.configureExecution(committed_request);
        transmitter.transmit();

        const auto skip_wait_deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (!saw_skipped_callback &&
               std::chrono::steady_clock::now() < skip_wait_deadline)
        {
            usleep(1000);
        }

        require(
            saw_skipped_callback,
            "explicit skip-window requests must emit the SKIPPED callback even without WSPR frames");
        require(
            transmitter.getState() == WsprTransmitter::State::COMPLETE,
            "skip-window execution must complete cleanly without backend RF setup");

        committed_request.skip_window = false;
        bool rejected_empty_wspr = false;
        try
        {
            transmitter.configureExecution(committed_request);
        }
        catch (const std::invalid_argument &ex)
        {
            rejected_empty_wspr =
                std::string(ex.what()) ==
                "WSPR transmission request contains no frames.";
        }
        require(
            rejected_empty_wspr,
            "ordinary WSPR requests with no frames must still be rejected");
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
        require(
            !config.allow_unqualified_frequency &&
                !config.allow_non_amateur_frequency &&
                !jConfig["Experimental"]["Allow Unqualified Frequency"].get<bool>() &&
                !jConfig["Experimental"]["Allow Non-Amateur Frequency"].get<bool>(),
            "experimental frequency policy must default deny");
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
        reset_getopt_state();
        set_si5351_detection_override_for_test(true);
        std::vector<std::string> args = {
            "wsprrypi",
            "--backend", "si5351",
            "--si5351-reference-source", "crystal",
            "--si5351-crystal-load-capacitance", "6",
            "AA0NT", "EM18", "20", "20m"};
        std::vector<char *> argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "Si5351 reference-source CLI settings must parse");
        require(
            config.si5351_reference_source == "crystal" &&
                config.si5351_crystal_load_capacitance_pf == 6,
            "Si5351 reference-source CLI settings must reach canonical runtime config");
        clear_si5351_detection_override_for_scope();
    }

    {
        const std::string help_output = capture_print_usage_output(0);
        require(
            help_output.find("--use-system-clock-frequency-estimate") != std::string::npos &&
                help_output.find("--no-system-clock-frequency-estimate") != std::string::npos &&
                help_output.find("--gpio-manual-ppm") != std::string::npos &&
                help_output.find("--gpio-frequency-residual-ppm") != std::string::npos &&
                help_output.find("--si5351-ppm") != std::string::npos &&
                help_output.find("--si5351-reference-source") != std::string::npos &&
                help_output.find("--si5351-crystal-load-capacitance") != std::string::npos &&
                help_output.find("--repeat") != std::string::npos &&
                help_output.find("--offset") != std::string::npos &&
                help_output.find("--journald") != std::string::npos &&
                help_output.find("--date-time-log") != std::string::npos &&
                help_output.find("--terminate <count>") != std::string::npos &&
                help_output.find("--socket-loopback-family <auto|ipv6|ipv4>") != std::string::npos &&
                help_output.find("--planner-preference <auto|prefer_paired|require_paired>") != std::string::npos &&
                help_output.find("--qrss-message/--qrss-frequency/--qrss-dot-seconds") != std::string::npos &&
                help_output.find("--fskcw-message/--fskcw-mark-frequency/--fskcw-space-frequency/--fskcw-dot-seconds") != std::string::npos &&
                help_output.find("--dfcw-message/--dfcw-dot-frequency/--dfcw-dash-frequency/--dfcw-dot-seconds") != std::string::npos &&
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
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi", "--allow-unqualified-frequency",
            "--allow-non-amateur-frequency",
            "--no-allow-non-amateur-frequency",
            "AA0NT", "EM18", "20", "20m"};
        std::vector<char *> argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "experimental frequency CLI flags must parse");
        require(
            config.allow_unqualified_frequency &&
                !config.allow_non_amateur_frequency,
            "experimental frequency CLI flags must use last-option precedence");
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
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(true, false);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "0,20m@17H";
        config.use_offset = false;
        config.gpio_tx_pin = 4;
        config.gpio_use_system_clock_frequency_estimate = false;
        resolve_backend_specific_config(config);

        require(
            set_frequencies(config),
            "runtime skip-window regression must accept explicit 0 and per-entry selector GPIO syntax");
        require(
            set_config(true),
            "runtime skip-window regression must commit an initial WSPR request");

        const TransmissionRequest skip_request =
            current_transmission_request_for_test();
        require(
            skip_request.isSkipWindow(),
            "normal runtime scheduling must commit 0 Hz WSPR periods as skip-window requests");
        require(
            nearly_equal(skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(skip_request.actual_rf_frequency_hz, 0.0),
            "normal runtime scheduling must keep 0 Hz skip windows at 0 Hz");
        require(
            !skip_request.hasSelectorGPIO(),
            "normal runtime scheduling must not prepare selector GPIO for 0 Hz skip windows");
        BandGPIOConfig active_selector_config;
        std::string active_selector_band;
        require(
            !current_band_gpio_selection_for_test(
                active_selector_config,
                active_selector_band),
            "normal runtime scheduling must not map 0 Hz skip windows to a ham band");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "runtime skip window regression",
            0.0);

        const TransmissionRequest next_request =
            current_transmission_request_for_test();
        require(
            !next_request.isSkipWindow() &&
                nearly_equal(next_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(next_request.actual_rf_frequency_hz, 14097100.0),
            "normal runtime scheduling must advance from a skip window to the next valid WSPR period");
        require(
            next_request.hasSelectorGPIO() &&
                next_request.selector_band == HamBand::BAND_20M &&
                next_request.selector_gpio_config.gpio == 17 &&
                next_request.selector_gpio_config.active_high,
            "normal runtime scheduling must preserve selector GPIO preparation for the next valid nonzero entry");

        cleanup_scheduler_regression_test_state();
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(true, false);
        reset_band_gpio_prepare_call_count_for_test();

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "30m@17H,0,0,0";
        config.use_offset = false;
        config.gpio_tx_pin = 4;
        config.gpio_use_system_clock_frequency_estimate = false;
        config.gpio_manual_ppm = 12.5;
        resolve_backend_specific_config(config);

        require(
            set_frequencies(config),
            "runtime repeated skip regression must accept a 30m slot followed by three explicit 0 Hz slots");
        require(
            set_config(true),
            "runtime repeated skip regression must commit the initial 30m request");

        const std::size_t prepare_calls_after_first_commit =
            band_gpio_prepare_call_count_for_test();
        const TransmissionRequest first_request =
            current_transmission_request_for_test();
        require(
            !first_request.isSkipWindow() &&
                nearly_equal(first_request.dial_frequency_hz, 10138700.0) &&
                nearly_equal(first_request.actual_rf_frequency_hz, 10140200.0),
            "runtime repeated skip regression must start on the valid 30m slot");
        require(
            nearly_equal(first_request.ppm, 12.5),
            "runtime repeated skip regression must snapshot PPM into the committed 30m request");
        require(
            first_request.hasSelectorGPIO() &&
                first_request.selector_band == HamBand::BAND_30M &&
                first_request.selector_gpio_config.gpio == 17 &&
                first_request.selector_gpio_config.active_high,
            "runtime repeated skip regression must prepare selector GPIO for the valid 30m slot");
        require(
            prepare_calls_after_first_commit == 1U,
            "runtime repeated skip regression must perform exactly one band-correlation pass for the initial valid slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime repeated skip first transmit regression",
            110.6);

        const std::size_t prepare_calls_after_first_skip =
            band_gpio_prepare_call_count_for_test();
        const TransmissionRequest first_skip_request =
            current_transmission_request_for_test();
        require(
            first_skip_request.isSkipWindow() &&
                nearly_equal(first_skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(first_skip_request.actual_rf_frequency_hz, 0.0),
            "runtime repeated skip regression must commit the first 0 Hz slot as a skip-window request");
        require(
            nearly_equal(first_skip_request.ppm, 12.5),
            "runtime repeated skip regression must snapshot PPM into committed skip requests");
        require(
            !first_skip_request.hasSelectorGPIO(),
            "runtime repeated skip regression must not commit selector GPIO for the first 0 Hz slot");
        require(
            band_gpio_prepare_call_count_for_test() == prepare_calls_after_first_skip,
            "runtime repeated skip regression must not call band correlation for the first 0 Hz slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "runtime repeated skip second slot regression",
            0.0);

        const TransmissionRequest second_skip_request =
            current_transmission_request_for_test();
        require(
            second_skip_request.isSkipWindow() &&
                nearly_equal(second_skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(second_skip_request.actual_rf_frequency_hz, 0.0),
            "runtime repeated skip regression must advance to the second 0 Hz slot");
        require(
            !second_skip_request.hasSelectorGPIO(),
            "runtime repeated skip regression must not commit selector GPIO for the second 0 Hz slot");
        require(
            band_gpio_prepare_call_count_for_test() == prepare_calls_after_first_skip,
            "runtime repeated skip regression must not call band correlation for the second 0 Hz slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "runtime repeated skip third slot regression",
            0.0);

        const TransmissionRequest third_skip_request =
            current_transmission_request_for_test();
        require(
            third_skip_request.isSkipWindow() &&
                nearly_equal(third_skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(third_skip_request.actual_rf_frequency_hz, 0.0),
            "runtime repeated skip regression must advance to the third 0 Hz slot");
        require(
            !third_skip_request.hasSelectorGPIO(),
            "runtime repeated skip regression must not commit selector GPIO for the third 0 Hz slot");
        require(
            band_gpio_prepare_call_count_for_test() == prepare_calls_after_first_skip,
            "runtime repeated skip regression must not call band correlation for the third 0 Hz slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "runtime repeated skip wrap regression",
            0.0);

        const TransmissionRequest wrapped_request =
            current_transmission_request_for_test();
        require(
            !wrapped_request.isSkipWindow() &&
                nearly_equal(wrapped_request.dial_frequency_hz, 10138700.0) &&
                nearly_equal(wrapped_request.actual_rf_frequency_hz, 10140200.0),
            "runtime repeated skip regression must wrap back to the valid 30m slot after three skips");
        require(
            nearly_equal(wrapped_request.ppm, 12.5),
            "runtime repeated skip regression must preserve the committed PPM snapshot when the valid slot resumes");
        require(
            wrapped_request.hasSelectorGPIO() &&
                wrapped_request.selector_band == HamBand::BAND_30M &&
                wrapped_request.selector_gpio_config.gpio == 17 &&
                wrapped_request.selector_gpio_config.active_high,
            "runtime repeated skip regression must restore selector GPIO when the valid 30m slot resumes");
        require(
            band_gpio_prepare_call_count_for_test() == prepare_calls_after_first_skip + 1U,
            "runtime repeated skip regression must call band correlation again only when the valid 30m slot resumes");

        cleanup_scheduler_regression_test_state();
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(false, false);
        reset_band_gpio_prepare_call_count_for_test();

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "30m,30m,20m";
        config.use_offset = false;
        config.gpio_tx_pin = 4;
        config.gpio_use_system_clock_frequency_estimate = false;
        resolve_backend_specific_config(config);

        require(
            set_frequencies(config),
            "runtime repeated valid-slot regression must accept repeated valid WSPR entries");
        require(
            set_config(true),
            "runtime repeated valid-slot regression must commit the first 30m slot");

        TransmissionRequest first_request =
            current_transmission_request_for_test();
        require(
            !first_request.isSkipWindow() &&
                nearly_equal(first_request.dial_frequency_hz, 10138700.0) &&
                nearly_equal(first_request.actual_rf_frequency_hz, 10140200.0),
            "runtime repeated valid-slot regression must start on the first 30m slot");
        require(
            band_gpio_prepare_call_count_for_test() == 1U,
            "runtime repeated valid-slot regression must correlate band GPIO once for the initial valid slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime repeated valid-slot first transmit regression",
            110.6);

        TransmissionRequest second_request =
            current_transmission_request_for_test();
        require(
            !second_request.isSkipWindow() &&
                nearly_equal(second_request.dial_frequency_hz, 10138700.0) &&
                nearly_equal(second_request.actual_rf_frequency_hz, 10140200.0),
            "runtime repeated valid-slot regression must commit the second 30m slot separately");
        require(
            band_gpio_prepare_call_count_for_test() == 2U,
            "runtime repeated valid-slot regression must produce a fresh scheduling decision for the repeated 30m slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime repeated valid-slot second transmit regression",
            110.6);

        TransmissionRequest third_request =
            current_transmission_request_for_test();
        require(
            !third_request.isSkipWindow() &&
                nearly_equal(third_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(third_request.actual_rf_frequency_hz, 14097100.0),
            "runtime repeated valid-slot regression must advance from the repeated 30m slots to 20m");
        require(
            band_gpio_prepare_call_count_for_test() == 3U,
            "runtime repeated valid-slot regression must re-run band correlation for the following 20m slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime repeated valid-slot wrap regression",
            110.6);

        TransmissionRequest wrapped_request =
            current_transmission_request_for_test();
        require(
            !wrapped_request.isSkipWindow() &&
                nearly_equal(wrapped_request.dial_frequency_hz, 10138700.0) &&
                nearly_equal(wrapped_request.actual_rf_frequency_hz, 10140200.0),
            "runtime repeated valid-slot regression must wrap from 20m back to the first 30m slot");

        cleanup_scheduler_regression_test_state();
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(false, false);
        reset_band_gpio_prepare_call_count_for_test();

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m,20m,0";
        config.use_offset = false;
        config.gpio_tx_pin = 4;
        config.gpio_use_system_clock_frequency_estimate = false;
        resolve_backend_specific_config(config);

        require(
            set_frequencies(config),
            "runtime valid-valid-skip regression must accept repeated valid entries followed by a skip");
        require(
            set_config(true),
            "runtime valid-valid-skip regression must commit the first 20m slot");

        TransmissionRequest first_request =
            current_transmission_request_for_test();
        require(
            !first_request.isSkipWindow() &&
                nearly_equal(first_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(first_request.actual_rf_frequency_hz, 14097100.0),
            "runtime valid-valid-skip regression must start on the first 20m slot");
        require(
            band_gpio_prepare_call_count_for_test() == 1U,
            "runtime valid-valid-skip regression must correlate band GPIO once for the initial valid slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime valid-valid-skip first transmit regression",
            110.6);

        TransmissionRequest second_request =
            current_transmission_request_for_test();
        require(
            !second_request.isSkipWindow() &&
                nearly_equal(second_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(second_request.actual_rf_frequency_hz, 14097100.0),
            "runtime valid-valid-skip regression must commit the second 20m slot separately");
        require(
            band_gpio_prepare_call_count_for_test() == 2U,
            "runtime valid-valid-skip regression must advance the schedule across repeated valid 20m slots");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime valid-valid-skip second transmit regression",
            110.6);

        TransmissionRequest skip_request =
            current_transmission_request_for_test();
        require(
            skip_request.isSkipWindow() &&
                nearly_equal(skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(skip_request.actual_rf_frequency_hz, 0.0),
            "runtime valid-valid-skip regression must commit the trailing 0 Hz slot as a skip-window request");
        require(
            band_gpio_prepare_call_count_for_test() == 2U,
            "runtime valid-valid-skip regression must not run band correlation for the 0 Hz slot");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "runtime valid-valid-skip wrap regression",
            0.0);

        TransmissionRequest wrapped_request =
            current_transmission_request_for_test();
        require(
            !wrapped_request.isSkipWindow() &&
                nearly_equal(wrapped_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(wrapped_request.actual_rf_frequency_hz, 14097100.0),
            "runtime valid-valid-skip regression must wrap from the skip slot back to 20m");
        require(
            band_gpio_prepare_call_count_for_test() == 3U,
            "runtime valid-valid-skip regression must resume normal band correlation after the skip slot");

        cleanup_scheduler_regression_test_state();
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(false, false);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m,30m,0";
        config.use_offset = false;
        config.gpio_tx_pin = 4;
        config.gpio_use_system_clock_frequency_estimate = false;
        resolve_backend_specific_config(config);

        require(
            set_frequencies(config),
            "runtime multi-slot skip regression must accept a trailing 0 Hz WSPR entry");
        require(
            set_config(true),
            "runtime multi-slot skip regression must commit the first WSPR request");

        TransmissionRequest first_request =
            current_transmission_request_for_test();
        require(
            !first_request.isSkipWindow() &&
                nearly_equal(first_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(first_request.actual_rf_frequency_hz, 14097100.0),
            "runtime multi-slot skip regression must start on the first valid 20m entry");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime multi-slot first transmit regression",
            110.6);

        TransmissionRequest second_request =
            current_transmission_request_for_test();
        require(
            !second_request.isSkipWindow() &&
                nearly_equal(second_request.dial_frequency_hz, 10138700.0) &&
                nearly_equal(second_request.actual_rf_frequency_hz, 10140200.0),
            "runtime multi-slot skip regression must advance to the second valid 30m entry");

        transmitter_cb(
            WsprTransmissionCallbackEvent::COMPLETE,
            WsprTransmitLogLevel::INFO,
            "runtime multi-slot second transmit regression",
            110.6);

        const TransmissionRequest skip_request =
            current_transmission_request_for_test();
        require(
            skip_request.isSkipWindow() &&
                nearly_equal(skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(skip_request.actual_rf_frequency_hz, 0.0),
            "runtime multi-slot skip regression must commit the trailing 0 Hz slot as a skip-window request");
        require(
            !skip_request.hasSelectorGPIO(),
            "runtime multi-slot skip regression must not prepare selector GPIO for the trailing skip window");
        const WsprRuntimeStatusSnapshot skip_snapshot =
            current_tx_runtime_status_snapshot();
        require(
            skip_snapshot.frequency_is_skip &&
                nearly_equal(skip_snapshot.frequency_hz, 0.0),
            "runtime snapshots must expose a committed 0 Hz WSPR slot as an explicit skip window");
        require(
            !skip_snapshot.selector_gpio_enabled &&
                skip_snapshot.selector_gpio == kSelectorGpioUnset &&
                !skip_snapshot.selector_gpio_active_high,
            "runtime snapshots must not expose a selector suffix for skip windows without committed selector GPIO");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "runtime multi-slot skip regression",
            0.0);

        const TransmissionRequest wrapped_request =
            current_transmission_request_for_test();
        require(
            !wrapped_request.isSkipWindow() &&
                nearly_equal(wrapped_request.dial_frequency_hz, 14095600.0) &&
                nearly_equal(wrapped_request.actual_rf_frequency_hz, 14097100.0),
            "runtime multi-slot skip regression must wrap cleanly to the next valid transmission after SKIPPED");
        const WsprRuntimeStatusSnapshot wrapped_snapshot =
            current_tx_runtime_status_snapshot();
        require(
            !wrapped_snapshot.frequency_is_skip &&
                nearly_equal(wrapped_snapshot.frequency_hz, 14095600.0),
            "runtime snapshots must return to the next committed WSPR dial frequency after a skip completes");

        cleanup_scheduler_regression_test_state();
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(true, false);

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_skip_window.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m@17H";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        auto managed_ini = make_managed_ini_data("AA0NT", "EM18", "0,20m@17H", true);
        iniFile.setData(managed_ini);

        require(
            set_config(true),
            "managed reload with a 0 Hz skip window must remain valid");

        const TransmissionRequest skip_request =
            current_transmission_request_for_test();
        require(
            skip_request.isSkipWindow(),
            "0 Hz WSPR periods must commit an explicit skip-window request");
        require(
            nearly_equal(skip_request.dial_frequency_hz, 0.0) &&
                nearly_equal(skip_request.actual_rf_frequency_hz, 0.0),
            "0 Hz skip windows must remain at 0 Hz in the committed request");
        require(
            !skip_request.hasSelectorGPIO(),
            "0 Hz skip windows must not commit any band-selector GPIO");
        BandGPIOConfig active_selector_config;
        std::string active_selector_band;
        require(
            !current_band_gpio_selection_for_test(
                active_selector_config,
                active_selector_band),
            "0 Hz skip windows must not prepare a band-selector GPIO");
        require(
            !managed_reload_tx_inhibited_for_test(),
            "0 Hz skip windows must not inhibit future transmissions");
        require(
            config.frequencies == "0,20m@17H",
            "valid managed 0 Hz skip reloads must replace the live WSPR frequency list");

        transmitter_cb(
            WsprTransmissionCallbackEvent::SKIPPED,
            WsprTransmitLogLevel::INFO,
            "managed skip window regression",
            0.0);

        const TransmissionRequest next_request =
            current_transmission_request_for_test();
        require(
            !next_request.isSkipWindow() &&
                nearly_equal(next_request.actual_rf_frequency_hz, 14097100.0),
            "skip-window completion must leave the scheduler ready for the next valid WSPR period");
        require(
            next_request.hasSelectorGPIO() &&
                next_request.selector_band == HamBand::BAND_20M &&
                next_request.selector_gpio_config.gpio == 17 &&
                next_request.selector_gpio_config.active_high,
            "the next valid WSPR period must still prepare the configured band-selector GPIO");

        cleanup_scheduler_regression_test_state();
    }

    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(true, false);

        config.use_ini = true;
        config.ini_filename = "/tmp/managed_reload_invalid_frequency.ini";
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m@17H";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        auto valid_managed_ini = make_managed_ini_data("AA0NT", "EM18", "20m@17H", true);
        iniFile.setData(valid_managed_ini);

        require(
            set_config(true),
            "managed invalid-frequency regression must start from a valid live configuration");

        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "1234567", true));

        require(
            set_config(true),
            "invalid nonzero managed reloads must remain recoverable");
        require(
            config.frequencies == "20m@17H",
            "invalid nonzero managed reloads must preserve the previous live frequency list");
        require(
            managed_reload_tx_inhibited_for_test(),
            "invalid nonzero managed reloads must still inhibit future transmissions");
        const TransmissionRequest preserved_request =
            current_transmission_request_for_test();
        require(
            !preserved_request.isSkipWindow() &&
                nearly_equal(preserved_request.actual_rf_frequency_hz, 14097100.0),
            "invalid nonzero managed reloads must preserve the previously committed valid WSPR request");

        cleanup_scheduler_regression_test_state();
    }


    {
        init_config_json();
        json_to_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        set_raspberry_pi_generation_override_for_test(4);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "<AA0NT>";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        sync_wspr_fields_for_test();

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
        set_raspberry_pi_generation_override_for_test(4);

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        sync_wspr_fields_for_test();
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
        config.gpio_use_system_clock_frequency_estimate = false;
        config.gpio_manual_ppm = 12.5;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        ppm_reload_pending.store(true, std::memory_order_relaxed);
        const double expected_ppm = 12.5;

        require(
            set_config(true),
            "pending runtime PPM update must be consumable during scheduler commit");
        require(
            nearly_equal(current_transmission_request_for_test().ppm, expected_ppm),
            "committed request must consume the selected GPIO correction value");
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
        require_patch_rejects_with_plan_status(
            make_identity_patch("AA0NT", "EM18IG"),
            "BareLongCallsignRequiresExplicitType3",
            "bare standard callsign with a 6-character locator under default planner policy");
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
        require_patch_rejects_with_plan_status_and_message(
            make_identity_patch(
                "<AA0NT>",
                "EM18IG",
                WsprPlannerPreference::RequirePaired),
            "PairedTransmissionUnavailable",
            "RequirePaired cannot be satisfied for explicit Type 3 callsigns.",
            "RequirePaired config patch with an explicit Type 3 standard identity");
    }

    {
        require_patch_rejects_with_plan_status_and_message(
            make_identity_patch(
                "<AA0NT/12>",
                "EM18IG",
                WsprPlannerPreference::RequirePaired),
            "PairedTransmissionUnavailable",
            "RequirePaired cannot be satisfied for explicit Type 3 callsigns.",
            "RequirePaired config patch with an explicit Type 3 compound identity");
    }

    {
        require_patch_rejects_with_plan_status(
            make_identity_patch(
                "AA0NT",
                "EM18",
                WsprPlannerPreference::RequirePaired),
            "PairedTransmissionUnavailable",
            "RequirePaired config patch with a plain Type 1 identity");
    }

    {
        require_patch_rejects_with_plan_status_and_message(
            make_identity_patch(
                "<AA0NT>>",
                "EM18IG",
                WsprPlannerPreference::RequirePaired),
            "PairedTransmissionUnavailable",
            "RequirePaired cannot be satisfied for explicit Type 3 callsigns.",
            "explicit Type 3 config patch with nested close marker under RequirePaired");
    }

    {
        require_patch_rejects_with_plan_status(
            make_identity_patch(
                "<AA0NT!>",
                "EM18IG",
                WsprPlannerPreference::RequirePaired),
            "InvalidCallsign",
            "invalid explicit Type 3 config patch with unsupported syntax");
    }

    {
        require_patch_rejects_with_plan_status(
            make_identity_patch(
                "<AA0NT>",
                "EM18I9",
                WsprPlannerPreference::RequirePaired),
            "InvalidLocator",
            "explicit Type 3 config patch with invalid 6-character locator");
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
            const TransmissionRequest request =
                current_transmission_request_for_test();
            require(
                snapshot.plan_type == "Type2Type3Paired" &&
                    snapshot.frame_count == 2U &&
                    snapshot.current_frame == 1U,
                "paired runtime snapshot must expose first-frame progress");
            require(
                request.payload.frames.size() == 1U &&
                    request.payload.current_frame == 1U &&
                    request.payload.total_frame_count == 1U,
                "paired first-slot request must carry a self-consistent slot-scoped payload");
            wsprrypi::TransmissionRequest controller_request;
            controller_request.mode = wsprrypi::TransmissionMode::WSPR;
            controller_request.output.backend = wsprrypi::BackendKind::SI5351;
            controller_request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
            controller_request.output.gpio = request.tx_gpio;
            controller_request.calibration.ppm = request.ppm;
            wsprrypi::WsprPayload first_payload;
            first_payload.prepared = request.payload;
            first_payload.base_frequency_hz = request.actual_rf_frequency_hz;
            controller_request.payload = first_payload;
            const wsprrypi::ExecutionPlan first_frame_plan =
                wsprrypi::ExecutionPlanCompiler{}.compile(controller_request);
            require(
                first_frame_plan.events.size() == WSPR_SYMBOL_COUNT,
                "paired first-slot request must compile into one WSPR frame");
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
            const TransmissionRequest request =
                current_transmission_request_for_test();
            require(
                snapshot.plan_type == "Type2Type3Paired" &&
                    snapshot.frame_count == 2U &&
                    snapshot.current_frame == 2U,
                "paired runtime snapshot must advance to the second frame after completion");
            require(
                request.payload.frames.size() == 1U &&
                    request.payload.current_frame == 1U &&
                    request.payload.total_frame_count == 1U,
                "paired second-slot request must remain slot-local while scheduler runtime status tracks overall paired progress");
            wsprrypi::TransmissionRequest controller_request;
            controller_request.mode = wsprrypi::TransmissionMode::WSPR;
            controller_request.output.backend = wsprrypi::BackendKind::SI5351;
            controller_request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
            controller_request.output.gpio = request.tx_gpio;
            controller_request.calibration.ppm = request.ppm;
            wsprrypi::WsprPayload second_payload;
            second_payload.prepared = request.payload;
            second_payload.base_frequency_hz = request.actual_rf_frequency_hz;
            controller_request.payload = second_payload;
            const wsprrypi::ExecutionPlan second_frame_plan =
                wsprrypi::ExecutionPlanCompiler{}.compile(controller_request);
            require(
                second_frame_plan.events.size() == WSPR_SYMBOL_COUNT,
                "paired second-slot request must compile into one WSPR frame without crashing");
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
                "transmitting") == "canceled",
            "websocket transmit canceled events must present a canceled tx_state");
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
        config.schedule_start_second = 17;
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
            nearly_equal(snapshot.frequency_hz, 3572000.0) &&
                snapshot.offset_hz == 0.0,
            "idle QRSS runtime snapshots must expose the active base frequency without an offset");
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
        require(
            enabled_snapshot.next_transmission_at.size() >= 19U &&
                enabled_snapshot.next_transmission_at.substr(17, 2) == "17",
            "runtime next_transmission_at must include the configured CW start second");

        config.mode = ModeType::FSKCW;
        config.fskcw.space_frequency_hz = 10140100.0;
        config.fskcw.mark_frequency_hz = 10140120.0;
        const WsprRuntimeStatusSnapshot fskcw_snapshot =
            current_tx_runtime_status_snapshot();
        require(
            nearly_equal(fskcw_snapshot.frequency_hz, 10140100.0) &&
                nearly_equal(fskcw_snapshot.offset_hz, 20.0),
            "runtime snapshots must expose FSKCW base frequency and offset");

        config.mode = ModeType::DFCW;
        config.dfcw.dot_frequency_hz = 10140100.0;
        config.dfcw.dash_frequency_hz = 10140120.0;
        const WsprRuntimeStatusSnapshot dfcw_snapshot =
            current_tx_runtime_status_snapshot();
        require(
            nearly_equal(dfcw_snapshot.frequency_hz, 10140100.0) &&
                nearly_equal(dfcw_snapshot.offset_hz, 20.0),
            "runtime snapshots must expose DFCW base frequency and offset");
    }

    {
        prime_valid_runtime_identity_config();
        reset_runtime_planning_state_for_identity_test();
        require(
            set_config(true),
            "WSPR frequency runtime snapshot regression must plan an initial WSPR request");
        const WsprRuntimeStatusSnapshot snapshot =
            current_tx_runtime_status_snapshot();
        require(
            snapshot.frequency_hz > 0.0,
            "runtime snapshots must expose the active WSPR dial frequency");
        require(
            snapshot.power_dbm == 20,
            "runtime snapshots must expose the committed WSPR transmit power in dBm");
        config.transmit = false;
        clear_current_wspr_runtime_state_for_test();
        const WsprRuntimeStatusSnapshot disabled_snapshot =
            current_tx_runtime_status_snapshot();
        require(
            disabled_snapshot.frequency_hz > 0.0,
            "idle disabled WSPR runtime snapshots must still expose the next configured dial frequency");
        require(
            disabled_snapshot.power_dbm == 20,
            "idle WSPR runtime snapshots must retain the configured WSPR transmit power in dBm");
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
        auto data = make_managed_ini_data("AA0NT", "EM18", "20m", true);
        data["Meta"]["Loop TX"] = "false";
        data["Meta"]["TX Iterations"] = "1";
        iniFile.setData(data);
        prepare_ini_config_candidate("/tmp/managed_finite_candidate.ini", candidate);
        require(
            candidate.valid &&
                !candidate.normalized_config.loop_tx &&
                candidate.normalized_config.tx_iterations.load() == 1,
            "managed INI candidate must preserve a finite WSPR iteration contract");
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
        init_config_json();
        iniFile.setData({
            {"Operation", {{"Mode", "WSPR"}}},
            {"Experimental", {
                {"Allow Unqualified Frequency", "true"},
                {"Allow Non-Amateur Frequency", "true"}}}});
        ini_to_json("/tmp/experimental_frequency.ini");
        json_to_config();
        require(
            config.allow_unqualified_frequency &&
                config.allow_non_amateur_frequency,
            "experimental frequency INI controls must load as booleans");
        config_to_json();
        const nlohmann::json public_config = get_public_config_json();
        require(
            !public_config.contains("Experimental"),
            "experimental frequency controls must remain hidden from UI JSON");
        config.use_ini = true;
        config.ini_filename = "/tmp/experimental_frequency.ini";
        write_text_file(config.ini_filename, "[Operation]\nMode=WSPR\n");
        iniFile.set_filename(config.ini_filename);
        json_to_ini();
        const auto persisted = iniFile.getData();
        require(
            persisted.at("Experimental").at("Allow Unqualified Frequency") == "true" &&
                persisted.at("Experimental").at("Allow Non-Amateur Frequency") == "true",
            "experimental frequency controls must persist in the INI section");
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
            "Enable on Boot=Never\n"
            "Use LED=false\nLED Pin=-1\nUse Amp=false\nAmp Pin=-1\nAmp Pin Active High=false\nWeb Port=31415\nSocket Port=31416\n"
            "Use Shutdown=false\nShutdown Button=-1\n"
            "[GPIO]\nTransmit Pin=4\nPower Level=7\nUse System Clock Frequency Estimate=false\nFrequency Residual PPM=0\nManual PPM=0\n"
            "[Calibration]\nPPM=0\n"
            "[Si5351]\nI2C Bus=1\nI2C Address=96\nReference Frequency=27000000\n"
            "TX Output=CLK0\nPower Level=1\n"
            "[WSPR]\nCall Sign=AA0NT\nGrid Square=EM18\nTX Power=20\n"
            "Frequency=20m\nPlanner Preference=auto\nUse Random Offset=false\n"
            "[CW]\nMessage=\nBase Frequency=14096900.0\nShift Hz=5.0\n"
            "Dot Seconds=3.0\nIntra Element Gap=1.0\nInter Character Gap=3.0\n"
            "Inter Word Gap=7.0\nDFCW Intra Element Gap=0.333333\n"
            "DFCW Inter Character Gap=1.0\nDFCW Inter Word Gap=3.0\n"
            "Fade Shape=none\nFade In Ms=0\nFade Out Ms=0\n"
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
        init_config_json();
        json_to_config();
        require(
            config.enable_on_boot == EnableOnBootBehavior::Never,
            "missing Operation.Enable on Boot must default to Never");

        jConfig["Operation"]["Enable on Boot"] = "Follow";
        json_to_config();
        require(
            config.enable_on_boot == EnableOnBootBehavior::Follow,
            "Operation.Enable on Boot must parse Follow");

        jConfig["Operation"]["Enable on Boot"] = "Always";
        json_to_config();
        require(
            config.enable_on_boot == EnableOnBootBehavior::Always,
            "Operation.Enable on Boot must preserve and parse Always spelling");

        config.use_ini = true;
        config.ini_filename = "/tmp/enable_on_boot_persistence.ini";
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);
        config_to_json();
        json_to_ini();
        require(
            iniFile.getData().at("Operation").at("Enable on Boot") == "Always",
            "json_to_ini must persist Operation.Enable on Boot using canonical Always spelling");

        patch_all_from_web({{"Operation", {{"Enable on Boot", "Never"}}}});
        require(
            config.enable_on_boot == EnableOnBootBehavior::Never &&
                jConfig["Operation"].value("Enable on Boot", std::string()) == "Never" &&
                iniFile.getData().at("Operation").at("Enable on Boot") == "Never",
            "web patch must persist canonical Operation.Enable on Boot in live config, JSON, and INI");

        jConfig["Operation"]["Enable on Boot"] = "Aways";
        bool rejected_invalid_enable_on_boot = false;
        try
        {
            json_to_config();
        }
        catch (const std::exception &e)
        {
            rejected_invalid_enable_on_boot =
                std::string(e.what()).find("Invalid Operation.Enable on Boot") !=
                std::string::npos;
        }
        require(
            rejected_invalid_enable_on_boot,
            "Operation.Enable on Boot must reject values other than Never, Follow, or Always");
    }

    {
        auto exercise_startup_policy =
            [](EnableOnBootBehavior behavior,
               bool initial_transmit,
               bool expected_transmit,
               const std::string &ini_filename,
               const char *message)
        {
            init_default_config();
            config.use_ini = true;
            config.ini_filename = ini_filename;
            write_text_file(config.ini_filename, "");
            iniFile.set_filename(config.ini_filename);
            config.enable_on_boot = behavior;
            config.transmit = initial_transmit;
            config_to_json();
            json_to_ini();

            apply_enable_on_boot_startup_policy();

            require(config.transmit == expected_transmit, message);
            require(
                iniFile.getData().at("Operation").at("Transmit") ==
                    std::string(expected_transmit ? "true" : "false"),
                message);
        };

        exercise_startup_policy(
            EnableOnBootBehavior::Never,
            true,
            false,
            "/tmp/enable_on_boot_startup_never.ini",
            "Enable on Boot Never must disable and persist Operation.Transmit on startup");
        exercise_startup_policy(
            EnableOnBootBehavior::Follow,
            true,
            true,
            "/tmp/enable_on_boot_startup_follow_true.ini",
            "Enable on Boot Follow must preserve a true Operation.Transmit on startup");
        exercise_startup_policy(
            EnableOnBootBehavior::Follow,
            false,
            false,
            "/tmp/enable_on_boot_startup_follow_false.ini",
            "Enable on Boot Follow must preserve a false Operation.Transmit on startup");
        exercise_startup_policy(
            EnableOnBootBehavior::Always,
            false,
            true,
            "/tmp/enable_on_boot_startup_always.ini",
            "Enable on Boot Always must enable and persist Operation.Transmit on startup");

        test_idle_startup_overrides();

        auto exercise_managed_startup_gate =
            [](EnableOnBootBehavior behavior,
               bool startup_config_handoff,
               bool use_ini,
               bool initial_transmit,
               bool expected_transmit,
               const std::string &ini_filename,
               const char *message)
        {
            init_default_config();
            config.use_ini = use_ini;
            config.ini_filename = ini_filename;
            write_text_file(config.ini_filename, "");
            iniFile.set_filename(config.ini_filename);
            config.enable_on_boot = behavior;
            config.transmit = initial_transmit;
            config_to_json();
            json_to_ini();

            apply_managed_startup_policy_if_requested(startup_config_handoff);

            require(config.transmit == expected_transmit, message);
            if (use_ini)
            {
                require(
                    iniFile.getData().at("Operation").at("Transmit") ==
                        std::string(expected_transmit ? "true" : "false"),
                    message);
            }
        };

        exercise_managed_startup_gate(
            EnableOnBootBehavior::Never,
            true,
            true,
            true,
            false,
            "/tmp/enable_on_boot_managed_gate_never.ini",
            "managed INI startup must apply Enable on Boot Never");
        exercise_managed_startup_gate(
            EnableOnBootBehavior::Follow,
            true,
            true,
            true,
            true,
            "/tmp/enable_on_boot_managed_gate_follow_true.ini",
            "managed INI startup must leave Operation.Transmit unchanged for Follow=true");
        exercise_managed_startup_gate(
            EnableOnBootBehavior::Follow,
            true,
            true,
            false,
            false,
            "/tmp/enable_on_boot_managed_gate_follow_false.ini",
            "managed INI startup must leave Operation.Transmit unchanged for Follow=false");
        exercise_managed_startup_gate(
            EnableOnBootBehavior::Always,
            true,
            true,
            false,
            true,
            "/tmp/enable_on_boot_managed_gate_always.ini",
            "managed INI startup must apply Enable on Boot Always");
        exercise_managed_startup_gate(
            EnableOnBootBehavior::Never,
            false,
            true,
            true,
            true,
            "/tmp/enable_on_boot_no_handoff.ini",
            "Enable on Boot policy must not apply without managed startup handoff");
        exercise_managed_startup_gate(
            EnableOnBootBehavior::Always,
            true,
            false,
            false,
            false,
            "/tmp/enable_on_boot_direct_cli.ini",
            "Enable on Boot policy must not apply outside INI-managed mode");

        auto exercise_direct_mode_no_startup_policy =
            [](ModeType mode, const char *message)
        {
            init_default_config();
            config.use_ini = false;
            config.mode = mode;
            config.enable_on_boot = EnableOnBootBehavior::Always;
            config.transmit = false;
            config_to_json();

            apply_managed_startup_policy_if_requested(true);

            require(!config.transmit, message);
        };

        exercise_direct_mode_no_startup_policy(
            ModeType::WSPR,
            "Enable on Boot policy must not apply to direct CLI WSPR mode");
        exercise_direct_mode_no_startup_policy(
            ModeType::TONE,
            "Enable on Boot policy must not apply to direct CLI test tone mode");
        exercise_direct_mode_no_startup_policy(
            ModeType::QRSS,
            "Enable on Boot policy must not apply to direct CLI QRSS mode");
        exercise_direct_mode_no_startup_policy(
            ModeType::FSKCW,
            "Enable on Boot policy must not apply to direct CLI FSKCW mode");
        exercise_direct_mode_no_startup_policy(
            ModeType::DFCW,
            "Enable on Boot policy must not apply to direct CLI DFCW mode");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/amp_config_persistence.ini";
        write_text_file(config.ini_filename, "");
        iniFile.set_filename(config.ini_filename);

        config.use_amp = false;
        config.amp_pin = -1;
        config.amp_pin_active_high = false;
        config_to_json();
        json_to_ini();

        auto persisted_ini = iniFile.getData();
        auto operation_it = persisted_ini.find("Operation");
        require(
            operation_it != persisted_ini.end(),
            "json_to_ini must persist the Operation section for Amp config");
        require(
            operation_it->second.at("Use Amp") == "false" &&
                operation_it->second.at("Amp Pin").empty() &&
                operation_it->second.at("Amp Pin Active High") == "false",
            "json_to_ini must persist disabled Amp config as Use Amp=false, blank Amp Pin, and inactive polarity");

        config.use_amp = true;
        config.amp_pin = 23;
        config.amp_pin_active_high = false;
        config_to_json();
        json_to_ini();

        persisted_ini = iniFile.getData();
        operation_it = persisted_ini.find("Operation");
        require(
            operation_it->second.at("Use Amp") == "true" &&
                operation_it->second.at("Amp Pin") == "23" &&
                operation_it->second.at("Amp Pin Active High") == "false",
            "json_to_ini must persist enabled Amp config as Use Amp=true, Amp Pin=23, and explicit polarity");

        config.use_amp = false;
        config.amp_pin = 23;
        config.amp_pin_active_high = false;
        config_to_json();
        json_to_ini();

        persisted_ini = iniFile.getData();
        operation_it = persisted_ini.find("Operation");
        require(
            jConfig["Operation"].value("Use Amp", true) == false &&
                jConfig["Operation"].value("Amp Pin", -1) == 23 &&
                operation_it->second.at("Use Amp") == "false" &&
                operation_it->second.at("Amp Pin") == "23",
            "disabled Amp config must retain Amp Pin as data while Use Amp remains the runtime enable flag");
    }

    {
        init_config_json();
        auto legacy_amp_ini = make_managed_ini_data("AA0NT", "EM18", "20m", true);
        legacy_amp_ini["Operation"].erase("Use Amp");
        legacy_amp_ini["Operation"]["Amp Pin"] = "23";
        legacy_amp_ini["Operation"]["Amp Pin Active High"] = "false";
        iniFile.setData(legacy_amp_ini);
        ini_to_json("/tmp/legacy_amp_enabled.ini");
        json_to_config();
        require(
            config.use_amp && config.amp_pin == 23 && !config.amp_pin_active_high,
            "legacy INI without Use Amp must infer enabled Amp control from a valid Amp Pin");

        init_config_json();
        legacy_amp_ini["Operation"]["Amp Pin"] = "";
        iniFile.setData(legacy_amp_ini);
        ini_to_json("/tmp/legacy_amp_disabled.ini");
        json_to_config();
        require(
            !config.use_amp && config.amp_pin == -1,
            "legacy INI without Use Amp must infer disabled Amp control from a blank Amp Pin");
    }

    {
        init_default_config();
        set_raspberry_pi_generation_override_for_test(5);
        set_rp1_gpclk_provider_available_override_for_test(true);
        config.rp1_gpio_drive_ma = 12;
        config_to_json();
        require(
            platform_supports_gpio_clock_transmission(),
            "operator visibility must not disable the explicit Pi 5 RP1 runtime path");
        const nlohmann::json ready_rp1_public_config = get_public_config_json();
        require(
            ready_rp1_public_config["Platform"]
                 ["GPIO Clock Transmission Supported"].get<bool>() &&
                ready_rp1_public_config["Platform"]
                  ["RP1 GPIO Operator Visible"].get<bool>(),
            "public capability JSON must expose a ready Pi 5 RP1 GPIO path");
        require(
            ready_rp1_public_config["Platform"]
                ["GPIO Clock Transmission Error"].get<std::string>().empty(),
            "ready Pi 5 RP1 GPIO must not report a platform error");
        require(
            ready_rp1_public_config["GPIO"]["RP1 Drive mA"].get<int>() == 12,
            "operator visibility must preserve retained RP1 configuration");

        set_rp1_gpclk_provider_available_override_for_test(false);
        const nlohmann::json neutral_rp1_public_config = get_public_config_json();
        require(
            !neutral_rp1_public_config["Platform"]
                 ["GPIO Clock Transmission Supported"].get<bool>() &&
                neutral_rp1_public_config["Platform"]
                  ["RP1 GPIO Operator Visible"].get<bool>(),
            "route-neutral Pi 5 must expose administration without claiming transmission readiness");
        const std::string neutral_gpio_error =
            neutral_rp1_public_config["Platform"]
                ["GPIO Clock Transmission Error"].get<std::string>();
        require(
            neutral_gpio_error.find(
                "canonical RP1 GPCLK provider is unavailable") !=
                    std::string::npos &&
                neutral_gpio_error.find(
                    "Review the RP1 clock route status below") !=
                    std::string::npos,
            "route-neutral Pi 5 must report the explicit next operator action");
        std::string neutral_backend_error;
        require(
            !backend_ready_for_transmission(config, &neutral_backend_error) &&
                neutral_backend_error.find("/dev/rp1-gpclk") != std::string::npos,
            "route-neutral configuration must not weaken live GPIO transmission readiness");
        set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
        patch_all_from_web({
            {"Meta", {{"debug_logging", true}}},
            {"GPIO", {
                {"Transmit Pin", 20},
                {"RP1 Drive mA", 8}}}});
        require(
            jConfig["Meta"]["debug_logging"].get<bool>(),
            "route-neutral RP1 state must not block unrelated web patches");
        require(
            jConfig["GPIO"]["Transmit Pin"].get<int>() == 20 &&
                jConfig["GPIO"]["RP1 Drive mA"].get<int>() == 8,
            "operator-visible Pi 5 RP1 web patches must preserve valid route settings");
        clear_rp1_gpclk_provider_available_override_for_test();
        clear_raspberry_pi_generation_override_for_test();

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

        require(
            jConfig["Si5351"].value("Reference Source", "") == "external_tcxo" &&
                jConfig["Si5351"].value("Crystal Load Capacitance", 0) == 10,
            "legacy missing Si5351 reference settings must use TCXO and 10 pF defaults");

        jConfig["Si5351"]["Reference Source"] = "crystal";
        jConfig["Si5351"]["Crystal Load Capacitance"] = 8;
        json_to_config();
        require(
            config.si5351_reference_source == "crystal" &&
                config.si5351_crystal_load_capacitance_pf == 8,
            "JSON parsing must carry crystal reference settings into runtime config");
        config_to_json();
        json_to_ini();
        const auto crystal_ini = iniFile.getData().at("Si5351");
        require(
            crystal_ini.at("Reference Source") == "crystal" &&
                crystal_ini.at("Crystal Load Capacitance") == "8",
            "crystal reference settings must round-trip through canonical JSON and INI");

        for (const int invalid : {0, 7, 9, 12})
        {
            ArgParserConfig invalid_config = config;
            invalid_config.transmit_backend = TransmitBackendKind::SI5351;
            invalid_config.transmit = false;
            invalid_config.use_ini = true;
            invalid_config.si5351_crystal_load_capacitance_pf = invalid;
            std::string validation_error;
            require(
                !validate_config_candidate(invalid_config, &validation_error) &&
                    validation_error.find("crystal load capacitance") != std::string::npos,
                "invalid Si5351 crystal load capacitance must be rejected");
        }
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
        GPIOOutput::setTestMode(true);
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();

        config.use_ini = true;
        config.mode = ModeType::WSPR;
        config.transmit = true;

        transmitter_cb(
            WsprTransmissionCallbackEvent::FAILED,
            WsprTransmitLogLevel::ERROR,
            "injected managed backend failure",
            0.0);

        require(
            managed_reload_tx_inhibited_for_test(),
            "managed backend failure must inhibit future transmissions");
        require(
            !exiting_wspr.load(std::memory_order_relaxed),
            "managed backend failure must not request process shutdown");

        cleanup_scheduler_regression_test_state();
        GPIOOutput::setTestMode(false);
    }

    {
        GPIOOutput::setTestMode(true);
        init_default_config();
        config.use_led = false;
        config.led_pin = -1;
        reset_tx_led_request_counts_for_test();
        GPIOOutput::clearTestEvents();

        transmitter_cb(
            WsprTransmissionCallbackEvent::STARTING,
            WsprTransmitLogLevel::INFO,
            std::string(),
            14097100.0);
        transmitter_cb(
            WsprTransmissionCallbackEvent::CANCELLED,
            WsprTransmitLogLevel::INFO,
            std::string(),
            1.0);

        require(
            tx_led_assert_request_count_for_test() == 0 &&
                tx_led_deassert_request_count_for_test() == 0 &&
                tx_led_failure_count_for_test() == 0,
            "disabled TX LED configuration must not issue assert or deassert requests");
        require(
            GPIOOutput::testEvents().empty(),
            "disabled TX LED configuration must not write any GPIO LED events");

        GPIOOutput::setTestMode(false);
    }

    {
        GPIOOutput::setTestMode(true);
        init_default_config();
        config.use_led = true;
        config.led_pin = 18;
        require(
            ledControl.enableGPIOPin(config.led_pin, true),
            "TX LED lifecycle regression must enable the LED test GPIO");
        reset_tx_led_request_counts_for_test();
        GPIOOutput::clearTestEvents();

        transmitter_cb(
            WsprTransmissionCallbackEvent::STARTING,
            WsprTransmitLogLevel::INFO,
            std::string(),
            14097100.0);
        transmitter_cb(
            WsprTransmissionCallbackEvent::CANCELLED,
            WsprTransmitLogLevel::INFO,
            std::string(),
            1.0);

        const auto led_events = GPIOOutput::testEvents();
        std::size_t led_write_events = 0;
        for (const auto &event : led_events)
        {
            if (event.action == "write" && event.pin == 18)
            {
                ++led_write_events;
            }
        }

        require(
            tx_led_assert_request_count_for_test() == 1 &&
                tx_led_deassert_request_count_for_test() == 1 &&
                tx_led_failure_count_for_test() == 0,
            "enabled TX LED lifecycle must issue exactly one assert and one deassert without failures");
        require(
            led_write_events == 2,
            "enabled TX LED lifecycle must produce exactly one GPIO write for assert and one for deassert");

        ledControl.stop();
        GPIOOutput::setTestMode(false);
    }

    {
        GPIOOutput::setTestMode(true);
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_current_transmission_request_for_test();
        set_band_gpio_selector_for_test(true, true);

        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.use_led = true;
        config.led_pin = 18;
        config.use_amp = true;
        config.amp_pin = 23;
        config.amp_pin_active_high = false;

        require(
            ledControl.enableGPIOPin(config.led_pin, true),
            "central transmit GPIO lifecycle regression must enable LED test GPIO");
        require(
            ampControl.enableGPIOPin(config.amp_pin, config.amp_pin_active_high),
            "central transmit GPIO lifecycle regression must enable Amp Control test GPIO");
        TransmissionRequest request;
        request.mode = TransmissionMode::WSPR;
        request.dial_frequency_hz = 14095600.0;
        request.actual_rf_frequency_hz = 14097100.0;
        request.selector_gpio_enabled = true;
        request.selector_band = HamBand::BAND_20M;
        request.selector_gpio_config.gpio = 21;
        request.selector_gpio_config.enabled = true;
        request.selector_gpio_config.active_high = true;
        set_current_transmission_request_for_test(request);
        {
            const WsprRuntimeStatusSnapshot snapshot =
                current_tx_runtime_status_snapshot();
            require(
                snapshot.selector_gpio_enabled &&
                    snapshot.selector_gpio == 21 &&
                    snapshot.selector_gpio_active_high,
                "runtime snapshots must expose committed selector GPIO and polarity");
        }

        reset_tx_led_request_counts_for_test();
        GPIOOutput::clearTestEvents();

        transmitter_cb(
            WsprTransmissionCallbackEvent::STARTING,
            WsprTransmitLogLevel::INFO,
            std::string(),
            14097100.0);
        transmitter_cb(
            WsprTransmissionCallbackEvent::CANCELLED,
            WsprTransmitLogLevel::INFO,
            std::string(),
            1.0);

        std::vector<GPIOOutput::TestEvent> writes;
        for (const auto &event : GPIOOutput::testEvents())
        {
            if (event.action == "write")
            {
                writes.push_back(event);
            }
        }

        require(
            writes.size() >= 6,
            "central transmit GPIO lifecycle must write selector, amp, and LED on assert and deassert");
        require(
            writes[0].pin == 21 && writes[0].logical_state &&
                writes[1].pin == 23 && writes[1].logical_state &&
                !writes[1].active_high &&
                writes[2].pin == 18 && writes[2].logical_state,
            "central transmit GPIO assert order must be LPF/Band GPIO, Amp Control, then TX LED");
        require(
            writes[3].pin == 23 && !writes[3].logical_state &&
                !writes[3].active_high &&
                writes[4].pin == 18 && !writes[4].logical_state &&
                writes[5].pin == 21 && !writes[5].logical_state,
            "central transmit GPIO deassert order must be Amp Control, TX LED, then LPF/Band GPIO");

        require(
            GPIOOutput::testLogicalStateForPin(23).has_value() &&
                GPIOOutput::testLogicalStateForPin(23).value() == false,
            "Amp Control must be inactive after terminal transmit cleanup");
        require(
            tx_led_assert_request_count_for_test() == 1 &&
                tx_led_deassert_request_count_for_test() == 1 &&
                tx_led_failure_count_for_test() == 0,
            "central transmit GPIO lifecycle must keep LED accounting on the shared path");

        ampControl.stop();
        ledControl.stop();
        set_band_gpio_selector_for_test(false, false);
        reset_current_transmission_request_for_test();
        GPIOOutput::setTestMode(false);
    }

    {
        GPIOOutput::setTestMode(true);
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_current_transmission_request_for_test();

        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.use_led = false;
        config.led_pin = -1;
        config.use_amp = false;
        config.amp_pin = 23;
        config.amp_pin_active_high = false;

        require(
            ampControl.enableGPIOPin(config.amp_pin, config.amp_pin_active_high),
            "disabled Amp runtime gate regression must initialize the Amp test GPIO");
        GPIOOutput::clearTestEvents();

        transmitter_cb(
            WsprTransmissionCallbackEvent::STARTING,
            WsprTransmitLogLevel::INFO,
            std::string(),
            14097100.0);
        transmitter_cb(
            WsprTransmissionCallbackEvent::CANCELLED,
            WsprTransmitLogLevel::INFO,
            std::string(),
            1.0);

        bool amp_write_seen = false;
        for (const auto &event : GPIOOutput::testEvents())
        {
            if (event.action == "write" && event.pin == 23)
            {
                amp_write_seen = true;
                break;
            }
        }

        require(
            !amp_write_seen,
            "Use Amp=false with a retained Amp Pin must not issue Amp GPIO writes");

        ampControl.stop();
        reset_current_transmission_request_for_test();
        GPIOOutput::setTestMode(false);
    }

    {
        GPIOOutput::setTestMode(true);
        init_default_config();
        config.use_led = true;
        config.led_pin = 18;
        require(
            ledControl.enableGPIOPin(config.led_pin, true),
            "TX LED shutdown fallback regression must enable the LED test GPIO");
        reset_tx_led_request_counts_for_test();
        GPIOOutput::clearTestEvents();

        transmitter_cb(
            WsprTransmissionCallbackEvent::STARTING,
            WsprTransmitLogLevel::INFO,
            std::string(),
            14097100.0);

        require(
            tx_led_active_for_test(),
            "TX LED must remain marked active after a start without a terminal callback");

        const bool first_fallback =
            reconcile_tx_led_after_transmitter_stop_for_test(
                "test shutdown fallback");
        const bool second_fallback =
            reconcile_tx_led_after_transmitter_stop_for_test(
                "test shutdown fallback duplicate");

        const auto led_events = GPIOOutput::testEvents();
        std::size_t led_write_events = 0;
        for (const auto &event : led_events)
        {
            if (event.action == "write" && event.pin == 18)
            {
                ++led_write_events;
            }
        }

        require(
            first_fallback && !second_fallback,
            "TX LED shutdown fallback must deassert exactly once when no terminal callback arrived");
        require(
            tx_led_assert_request_count_for_test() == 1 &&
                tx_led_deassert_request_count_for_test() == 1 &&
                tx_led_failure_count_for_test() == 0,
            "TX LED shutdown fallback must add exactly one deassert without duplicate requests");
        require(
            !tx_led_active_for_test(),
            "TX LED shutdown fallback must clear the active LED state");
        require(
            led_write_events == 2,
            "TX LED shutdown fallback must leave exactly one assert and one deassert GPIO write");

        ledControl.stop();
        GPIOOutput::setTestMode(false);
    }

    {
        GPIOOutput::setTestMode(true);
        init_default_config();
        config.use_led = false;
        config.led_pin = -1;
        reset_tx_led_request_counts_for_test();
        GPIOOutput::clearTestEvents();

        const bool fallback_used =
            reconcile_tx_led_after_transmitter_stop_for_test(
                "disabled shutdown fallback");

        require(
            !fallback_used,
            "disabled TX LED configuration must skip shutdown fallback handling");
        require(
            tx_led_assert_request_count_for_test() == 0 &&
                tx_led_deassert_request_count_for_test() == 0 &&
                tx_led_failure_count_for_test() == 0,
            "disabled TX LED shutdown fallback must not issue LED requests");
        require(
            GPIOOutput::testEvents().empty(),
            "disabled TX LED shutdown fallback must not touch GPIO test events");

        GPIOOutput::setTestMode(false);
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

        set_patch_all_from_web_runtime_apply_suppressed_for_test(false);
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
              {"DFCW Intra Element Gap", 0.333333},
              {"DFCW Inter Character Gap", 1.0},
              {"DFCW Inter Word Gap", 3.0},
              {"Start Minute", 0},
              {"Repeat Minutes", 10}}}
        });
        set_patch_all_from_web_runtime_apply_suppressed_for_test(true);

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
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);

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
        const TestToneStartResult blocked_start = start_test_tone();

        require(
            !blocked_start.started && blocked_start.blocked_by_active_transmission,
            "test tone start must reject an active scheduler-managed transmission");
        require(
            current_transmission_request_for_test().mode != TransmissionMode::TONE,
            "rejected test tone start must not commit a tone request");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "rejected test tone start must not disturb the active transmission");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::ENABLED);
        const TestToneStartResult blocked_start = start_test_tone();

        require(
            !blocked_start.started &&
                blocked_start.blocked_by_enabled_transmission &&
                !blocked_start.blocked_by_active_transmission,
            "test tone start must reject scheduled transmissions that remain enabled even when idle");
        require(
            current_transmission_request_for_test().mode != TransmissionMode::TONE,
            "enabled-idle test tone rejection must not commit a tone request");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::ENABLED,
            "enabled-idle test tone rejection must not disturb the idle scheduler state");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        constexpr std::uint64_t override_frequency_hz = 14097123;
        const TestToneStartResult tone_start_result =
            start_test_tone(override_frequency_hz);
        require(
            tone_start_result.started,
            "test tone start must accept an explicit web frequency override");

        const TransmissionRequest tone_request =
            current_transmission_request_for_test();
        require(
            tone_request.mode == TransmissionMode::TONE &&
                nearly_equal(
                    tone_request.actual_rf_frequency_hz,
                    static_cast<double>(override_frequency_hz)),
            "web test tone override must become the exact committed RF frequency");
        require(
            nearly_equal(tone_request.dial_frequency_hz, 14095600.0),
            "web test tone override must preserve the configured scheduler dial frequency metadata");
        require(
            !nearly_equal(
                tone_request.actual_rf_frequency_hz,
                resolve_actual_rf_frequency_hz(
                    tone_request.dial_frequency_hz,
                    config.wspr.audio_offset_hz,
                    FrequencyPath::WsprDial)),
            "web test tone override must not receive the WSPR 1500 Hz dial-frequency offset again");

        const TestToneStopResult tone_stop_result = end_test_tone();
        require(
            tone_stop_result.stopped,
            "test tone override regression must stop the transient tone cleanly");

        SystemClockFrequencyEstimate qualified_estimate;
        qualified_estimate.provider_name = "chrony";
        qualified_estimate.qualification = FrequencyEstimateQualification::Qualified;
        qualified_estimate.frequency_ppm = 10.763;
        qualified_estimate.synchronized = true;
        qualified_estimate.selected_source = true;
        qualified_estimate.leap_normal = true;
        config.gpio_use_system_clock_frequency_estimate = true;
        config.use_system_clock_frequency_estimate = true;
        config.gpio_frequency_residual_ppm = 1.310;
        config.gpio_manual_ppm = 0.0;
        config.ppm = 10.763;
        set_current_frequency_estimate_for_test(qualified_estimate);

        const TestToneStartResult residual_tone_start =
            start_test_tone(override_frequency_hz);
        require(
            residual_tone_start.started,
            "GPIO test tone must start with a qualified system-clock estimate and residual");
        const TransmissionRequest residual_tone_request =
            current_transmission_request_for_test();
        require(
            nearly_equal(residual_tone_request.ppm, 12.073) &&
                nearly_equal(config.ppm, 12.073),
            "GPIO test tone commit must select the current estimate plus residual instead of a stale cached PPM");
        require(
            end_test_tone().stopped,
            "GPIO estimate-plus-residual test tone must stop cleanly");
        set_current_frequency_estimate_for_test(SystemClockFrequencyEstimate{});
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ini_reload_generation.store(0, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_si5351_detection_override_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/test_tone_disabled_runtime.ini";
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.si5351_i2c_bus = 1;
        config.si5351_i2c_address = 0x60;
        config.si5351_reference_hz = 27000000;
        config.si5351_tx_output = 2;
        config.si5351_power_level = 1;
        resolve_backend_specific_config(config);
        set_frequencies(config);
        auto managed_ini =
            make_managed_ini_data("AA0NT", "EM18", "20m", false);
        managed_ini["Operation"]["Transmit Backend"] = "si5351";
        managed_ini["Si5351"]["TX Output"] = "CLK2";
        managed_ini["Calibration"]["PPM"] = "2.409358";
        iniFile.setData(managed_ini);
        require(
            set_config(true),
            "disabled scheduler tone regression must load a stable WSPR runtime config");
        require(
            current_test_tone_planning_config_snapshot().transmit_backend ==
                TransmitBackendKind::SI5351,
            "disabled scheduler configuration must publish the accepted Si5351 backend for explicit Test Tone planning");
        reset_tx_led_request_counts_for_test();

        const TestToneStartResult tone_start_result = start_test_tone();
        require(
            tone_start_result.started,
            "test tone start must succeed when scheduler transmit is disabled and no managed TX is active");
        const TransmissionRequest tone_request =
            current_transmission_request_for_test();
        require(
            tone_request.mode == TransmissionMode::TONE &&
                tone_request.actual_rf_frequency_hz > 0.0,
            "disabled-scheduler test tone start must still commit a runnable tone request");
        const auto controller_request = current_controller_request_for_test();
        require(
            controller_request.has_value(),
            "disabled-scheduler test tone start must commit controller metadata for backend-selected execution");
        require(
            controller_request->mode == wsprrypi::TransmissionMode::TONE &&
                controller_request->output.backend == wsprrypi::BackendKind::SI5351 &&
                controller_request->output.output == wsprrypi::ClockSource::SI5351_CLK2,
            "disabled-scheduler Si5351 test tone start must carry SI5351 tone output metadata");
        require(
            committed_execution_route_for_test() ==
                CommittedExecutionRouteForTest::CONTROLLER_TONE,
            "disabled-scheduler Si5351 test tone start must commit through the controller tone route");
        require(
            tx_led_assert_request_count_for_test() == 0 &&
                tx_led_deassert_request_count_for_test() == 0,
            "test tone wrappers must not manipulate the TX LED directly");
        const auto *tone_payload =
            std::get_if<wsprrypi::TonePayload>(&controller_request->payload);
        require(
            tone_payload != nullptr &&
                nearly_equal(tone_payload->frequency_hz, tone_request.actual_rf_frequency_hz),
            "disabled-scheduler Si5351 test tone start must expose the committed tone frequency through the controller request");

#if WSPRRYPI_BACKEND_SI5351
        set_raspberry_pi_generation_override_for_test(5);
        wsprrypi::ExecutionPlan plan =
            wsprrypi::ExecutionPlanCompiler{}.compile(*controller_request);
        require(
            plan.mode == wsprrypi::TransmissionMode::TONE &&
                plan.backend == wsprrypi::BackendKind::SI5351,
            "disabled-scheduler Si5351 test tone controller request must compile into a Si5351 tone execution plan");
        WsprSi5351Backend::Config si5351_config;
        si5351_config.device.i2c_bus = 1;
        si5351_config.device.i2c_address = 0x60;
        si5351_config.device.reference_hz = 27000000;
        si5351_config.planner.reference_hz = 27000000;
        si5351_config.planner.tx_output = Si5351Device::Output::CLK2;
        si5351_config.power_level = 1;
        si5351_config.dry_run = true;
        WsprSi5351Backend backend(wsprTransmitter, si5351_config);
        wsprrypi::BackendExecutionInputs inputs;
        inputs.power_level = 1;
        const wsprrypi::BackendCompileResult compile_result =
            backend.configure(plan, inputs);
        require(
            compile_result.ok,
            "disabled-scheduler Si5351 test tone must use the backend-selected execution path instead of the legacy GPIO-only tone path");
        const wsprrypi::ExecutionResult execute_result = backend.execute(plan);
        require(
            execute_result.ok,
            "disabled-scheduler Si5351 test tone dry-run execution must not fail through the canonical backend path");
        clear_pi_generation_override_for_scope();
#endif

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        const TestToneStopResult tone_stop_result = end_test_tone();
        require(
            tone_stop_result.stopped && tone_stop_result.scheduler_restored,
            "test tone stop must still restore scheduler ownership when transmit stays disabled");
        require(
            !config.transmit,
            "test tone stop must preserve disabled scheduler state");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0,
            "test tone stop with transmit disabled must leave the committed scheduler request disabled");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::DISABLED,
            "test tone stop with transmit disabled must not leave the transmitter busy");
        require(
            current_test_tone_planning_config_snapshot().transmit_backend ==
                TransmitBackendKind::SI5351,
            "test tone stop must preserve the accepted Si5351 backend for the next explicit Test Tone plan");

        constexpr std::uint64_t calibrated_2m_hz = 144490498U;
        const TestToneStartResult calibrated_start = start_test_tone(
            TestToneRequest{
                TestToneFrequencySource::CustomRf,
                "",
                calibrated_2m_hz});
        require(
            calibrated_start.started,
            "calibrated custom 2 m Test Tone must start through the normal "
            "disabled-scheduler path");
        const TransmissionRequest calibrated_tone_request =
            current_transmission_request_for_test();
        const auto calibrated_controller_request =
            current_controller_request_for_test();
        require(
            calibrated_tone_request.mode == TransmissionMode::TONE &&
                nearly_equal(
                    calibrated_tone_request.actual_rf_frequency_hz,
                    static_cast<double>(calibrated_2m_hz)) &&
                nearly_equal(calibrated_tone_request.ppm, 2.409358),
            "custom 2 m Test Tone must preserve requested RF and committed "
            "Calibration.PPM in the legacy request");
        require(
            calibrated_controller_request.has_value() &&
                calibrated_controller_request->mode ==
                    wsprrypi::TransmissionMode::TONE &&
                calibrated_controller_request->output.backend ==
                    wsprrypi::BackendKind::SI5351 &&
                nearly_equal(
                    calibrated_controller_request->calibration.ppm,
                    2.409358),
            "custom 2 m Test Tone must carry committed Calibration.PPM into "
            "the controller request");
        const auto *calibrated_payload =
            calibrated_controller_request.has_value()
                ? std::get_if<wsprrypi::TonePayload>(
                      &calibrated_controller_request->payload)
                : nullptr;
        require(
            calibrated_payload != nullptr &&
                nearly_equal(
                    calibrated_payload->frequency_hz,
                    static_cast<double>(calibrated_2m_hz)),
            "custom 2 m Test Tone must preserve requested RF in the "
            "controller payload");

#if WSPRRYPI_BACKEND_SI5351
        set_raspberry_pi_generation_override_for_test(5);
        const wsprrypi::ExecutionPlan calibrated_plan =
            wsprrypi::ExecutionPlanCompiler{}.compile(
                *calibrated_controller_request);
        require(
            calibrated_plan.mode == wsprrypi::TransmissionMode::TONE &&
                calibrated_plan.backend == wsprrypi::BackendKind::SI5351 &&
                nearly_equal(calibrated_plan.calibration.ppm, 2.409358) &&
                calibrated_plan.events.size() == 2 &&
                nearly_equal(
                    calibrated_plan.events.front().frequency_hz,
                    static_cast<double>(calibrated_2m_hz)),
            "custom 2 m Test Tone must retain requested RF and calibration "
            "through execution-plan compilation");
        WsprSi5351Backend calibrated_backend(
            wsprTransmitter,
            si5351_config);
        const wsprrypi::BackendCompileResult calibrated_compile =
            calibrated_backend.configure(calibrated_plan, inputs);
        require(
            calibrated_compile.ok,
            "calibrated custom 2 m Test Tone must configure through the "
            "canonical dry-run Si5351 backend");
        const wsprrypi::ExecutionResult calibrated_execute =
            calibrated_backend.execute(calibrated_plan);
        require(
            calibrated_execute.ok,
            "calibrated custom 2 m Test Tone must execute through the "
            "canonical dry-run Si5351 backend");
        clear_pi_generation_override_for_scope();
#endif

        wsprTransmitter.backendSetStateValue(
            WsprTransmitter::State::TRANSMITTING);
        const TestToneStopResult calibrated_stop = end_test_tone();
        require(
            calibrated_stop.stopped && calibrated_stop.scheduler_restored &&
                !config.transmit &&
                wsprTransmitter.getState() ==
                    WsprTransmitter::State::DISABLED,
            "calibrated custom 2 m Test Tone stop must restore the disabled "
            "scheduler and transmitter state");

        clear_si5351_detection_override_for_scope();
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ini_reload_generation.store(0, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/test_tone_disabled_gpio_runtime.ini";
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);
        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", false));
        require(
            set_config(true),
            "disabled GPIO scheduler tone regression must load a stable WSPR runtime config");
        reset_tx_led_request_counts_for_test();

        const TestToneStartResult tone_start_result = start_test_tone();
        require(
            tone_start_result.started,
            "GPIO test tone start must succeed when scheduler transmit is disabled and no managed TX is active");
        const TransmissionRequest tone_request =
            current_transmission_request_for_test();
        require(
            tone_request.mode == TransmissionMode::TONE &&
                tone_request.actual_rf_frequency_hz > 0.0,
            "disabled-scheduler GPIO test tone start must still commit a runnable tone request");
        require(
            !current_controller_request_for_test().has_value(),
            "disabled-scheduler GPIO test tone start must remain on the legacy GPIO tone execution path");
        require(
            committed_execution_route_for_test() ==
                CommittedExecutionRouteForTest::LEGACY,
            "disabled-scheduler GPIO test tone start must commit through the legacy execution route");
        require(
            tx_led_assert_request_count_for_test() == 0 &&
                tx_led_deassert_request_count_for_test() == 0,
            "GPIO test tone wrappers must not manipulate the TX LED directly");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        const TestToneStopResult tone_stop_result = end_test_tone();
        require(
            tone_stop_result.stopped && tone_stop_result.scheduler_restored,
            "GPIO test tone stop must still restore scheduler ownership when transmit stays disabled");
        require(
            !config.transmit,
            "GPIO test tone stop must preserve disabled scheduler state");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0,
            "GPIO test tone stop with transmit disabled must leave the committed scheduler request disabled");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::DISABLED,
            "GPIO test tone stop with transmit disabled must not leave the transmitter busy");

        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", true));
        callback_ini_changed();
        require(
            !ini_reload_pending.load(std::memory_order_relaxed),
            "GPIO test tone stop must not leave managed reload deferred after a later INI change");
        require(
            config.transmit,
            "GPIO test tone stop must allow a later INI reload to apply immediately");
        require(
            current_transmission_request_for_test().mode == TransmissionMode::WSPR &&
                current_transmission_request_for_test().actual_rf_frequency_hz > 0.0,
            "GPIO test tone stop must allow a later INI reload to recommit the scheduler request immediately");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ini_reload_generation.store(0, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/test_tone_idle_runtime.ini";
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
            "idle WSPR scheduler regression must commit the normal waiting request");

        const TestToneStartResult tone_start_result = start_test_tone();
        require(
            !tone_start_result.started &&
                tone_start_result.blocked_by_enabled_transmission,
            "test tone start must reject idle WSPR wait periods while scheduled transmissions remain enabled");
        require(
            current_transmission_request_for_test().mode == TransmissionMode::WSPR,
            "idle WSPR test tone rejection must preserve the committed waiting request");
        require(
            !current_controller_request_for_test().has_value(),
            "idle GPIO WSPR test tone rejection must not commit a controller execution request");
        require(
            committed_execution_route_for_test() !=
                CommittedExecutionRouteForTest::LEGACY,
            "idle GPIO WSPR test tone rejection must not commit through the tone execution route");

        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ini_reload_generation.store(0, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);

        config.use_ini = true;
        config.ini_filename = "/tmp/test_tone_reload_recovery.ini";
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        resolve_backend_specific_config(config);
        set_frequencies(config);

        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", false));
        require(
            set_config(true),
            "test tone recovery regression must commit an initial disabled scheduler request");

        const TestToneStartResult tone_start_result = start_test_tone();
        require(
            tone_start_result.started,
            "test tone recovery regression must start the transient tone");
        require(
            current_transmission_request_for_test().mode == TransmissionMode::TONE,
            "test tone start must commit a tone request through the scheduler");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);
        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", true));
        callback_ini_changed();

        require(
            ini_reload_pending.load(std::memory_order_relaxed),
            "test tone reload regression must defer managed reload while the tone is active");

        const TestToneStopResult tone_stop_result = end_test_tone();
        require(
            tone_stop_result.stopped && tone_stop_result.scheduler_restored,
            "test tone stop must restore scheduler ownership");
        require(
            tone_stop_result.deferred_reload_reconciled,
            "test tone stop must reconcile deferred reload state");
        require(
            !ini_reload_pending.load(std::memory_order_relaxed),
            "test tone stop must consume deferred reload work");
        require(
            config.transmit,
            "deferred managed reload after test tone stop must commit the latest enabled config");
        require(
            current_transmission_request_for_test().mode == TransmissionMode::WSPR &&
                current_transmission_request_for_test().actual_rf_frequency_hz > 0.0,
            "deferred managed reload after test tone stop must restore the committed WSPR request");
        require(
            wsprTransmitter.getState() == WsprTransmitter::State::DISABLED,
            "test tone stop must clear the transmitter busy state before the scheduler is re-enabled");

        iniFile.setData(
            make_managed_ini_data("AA0NT", "EM18", "20m", false));
        callback_ini_changed();

        require(
            set_config(true),
            "scheduler must remain able to disable normal beaconing again after tone recovery");
        require(
            !config.transmit,
            "scheduler resume after tone recovery must restore disabled live config");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0,
            "scheduler resume after tone recovery must be able to clear the committed WSPR request again");

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
        config.ini_filename = "/tmp/mode_clock_estimate_patch.ini";
        config.mode = ModeType::QRSS;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.gpio_tx_pin = 4;
        config.gpio_use_system_clock_frequency_estimate = false;
        config.transmit_backend = TransmitBackendKind::GPIO;
        resolve_backend_specific_config(config);
        config_to_json();

        patch_all_from_web(nlohmann::json{
            {"Operation", {{"Mode", "WSPR"}}},
            {"GPIO", {{"Use System Clock Frequency Estimate", true},
                      {"Frequency Residual PPM", -0.125},
                      {"Manual PPM", 3.5}}}});

        require(
            config.mode == ModeType::WSPR,
            "web patch must update canonical Operation.Mode immediately");
        require(
            config.gpio_use_system_clock_frequency_estimate && config.use_system_clock_frequency_estimate,
            "web patch must update canonical GPIO system-clock estimate setting immediately");
        require(
            nearly_equal(config.gpio_frequency_residual_ppm, -0.125) &&
                nearly_equal(config.gpio_manual_ppm, 3.5),
            "web patch must update canonical GPIO residual and manual PPM values");
        require(
            jConfig["Operation"].value("Mode", std::string()) == "WSPR",
            "web patch must persist canonical Operation.Mode in JSON");
        require(
            jConfig["GPIO"].value("Use System Clock Frequency Estimate", false),
            "web patch must persist canonical GPIO estimate setting in JSON");

        const auto persisted_ini = iniFile.getData();
        require(
            persisted_ini.at("Operation").at("Mode") == "WSPR",
            "web patch must persist canonical Operation.Mode in INI");
        require(
            persisted_ini.at("GPIO").at("Use System Clock Frequency Estimate") == "true" &&
                nearly_equal(std::stod(persisted_ini.at("GPIO").at("Frequency Residual PPM")), -0.125) &&
                nearly_equal(std::stod(persisted_ini.at("GPIO").at("Manual PPM")), 3.5),
            "web patch must persist all canonical GPIO correction settings in INI");
    }

    {
        init_config_json();
        jConfig["Operation"]["Transmit Backend"] = "gpio";
        if (jConfig.contains("GPIO") && jConfig["GPIO"].is_object())
        {
            jConfig["GPIO"].erase("Use System Clock Frequency Estimate");
        }
        json_to_config();

        require(
            config.gpio_use_system_clock_frequency_estimate,
            "missing GPIO estimate setting must default true in backend normalization");
        require(
            config.use_system_clock_frequency_estimate,
            "missing GPIO estimate setting must enable the active GPIO provider path");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.ini_filename = "/tmp/retired_gpio_key_patch.ini";
        config_to_json();
        const nlohmann::json before_retired_patch = jConfig;
        bool retired_patch_rejected = false;
        try
        {
            patch_all_from_web({{"GPIO", {{"Use NTP", false}}}});
        }
        catch (const std::exception &e)
        {
            retired_patch_rejected =
                std::string(e.what()).find("accepted only during INI migration") !=
                std::string::npos;
        }
        require(
            retired_patch_rejected && jConfig == before_retired_patch,
            "public patches must reject the retired GPIO.Use NTP key without changing configuration");
    }

    {
        ScopedTemporaryFile migrated_ini("/tmp/wsprrypi-gpio-migration-XXXXXX");
        iniFile.set_filename(migrated_ini.path());
        auto legacy_ini = make_managed_ini_data("AA0NT", "EM18", "20m", false);
        legacy_ini["GPIO"].erase("Use System Clock Frequency Estimate");
        legacy_ini["GPIO"].erase("Frequency Residual PPM");
        legacy_ini["GPIO"].erase("Manual PPM");
        legacy_ini["GPIO"]["Use NTP"] = "false";
        legacy_ini["Calibration"]["PPM"] = "4.25";
        iniFile.setData(legacy_ini);
        iniFile.save();
        iniFile.set_filename(migrated_ini.path());

        PreparedConfigCandidate migrated_candidate;
        prepare_ini_config_candidate(migrated_ini.path(), migrated_candidate);
        require(
            migrated_candidate.valid && migrated_candidate.migration_required,
            "legacy GPIO.Use NTP must produce a valid migration candidate");
        require(
            !migrated_candidate.normalized_config.gpio_use_system_clock_frequency_estimate &&
                nearly_equal(migrated_candidate.normalized_config.gpio_manual_ppm, 4.25) &&
                nearly_equal(migrated_candidate.normalized_config.gpio_frequency_residual_ppm, 0.0),
            "legacy fixed GPIO correction must migrate to canonical manual PPM with zero residual");

        commit_config_candidate(migrated_candidate);
        std::ifstream migrated_file(migrated_ini.path());
        const std::string migrated_text(
            (std::istreambuf_iterator<char>(migrated_file)),
            std::istreambuf_iterator<char>());
        require(
            migrated_text.find("Use NTP =") == std::string::npos &&
                migrated_text.find("Use System Clock Frequency Estimate = false") != std::string::npos &&
                migrated_text.find("Frequency Residual PPM = 0.0") != std::string::npos &&
                migrated_text.find("Manual PPM = 4.25") != std::string::npos,
            "migration persistence must remove the retired key and write all canonical GPIO keys");

        PreparedConfigCandidate second_candidate;
        prepare_ini_config_candidate(migrated_ini.path(), second_candidate);
        require(
            second_candidate.valid && !second_candidate.migration_required,
            "loading an already migrated GPIO configuration must be idempotent");
    }

    {
        init_config_json();
        jConfig["Calibration"]["PPM"] = 275.5;
        json_to_config();

        require(
            nearly_equal(config.si5351_ppm, 200.0) && nearly_equal(config.ppm, 0.0),
            "backend normalization must clamp Si5351 Calibration.PPM without changing active GPIO correction");

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
        config.gpio_use_system_clock_frequency_estimate = false;
        config.transmit_backend = TransmitBackendKind::GPIO;
        resolve_backend_specific_config(config);
        config_to_json();

        patch_all_from_web({{"Calibration", {{"PPM", -275.5}}}});

        require(
            nearly_equal(config.si5351_ppm, -200.0) && nearly_equal(config.ppm, 0.0),
            "web patch path must clamp Si5351 Calibration.PPM without changing active GPIO correction");
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
        jConfig["CW"]["DFCW Intra Element Gap"] = 0.25;
        jConfig["CW"]["DFCW Inter Character Gap"] = 1.25;
        jConfig["CW"]["DFCW Inter Word Gap"] = 3.5;
        jConfig["CW"]["Fade Shape"] = "raised-cosine";
        jConfig["CW"]["Fade In Ms"] = 25;
        jConfig["CW"]["Fade Out Ms"] = 40;
        jConfig["CW"]["Fade Slice Ms"] = 2;
        json_to_config();

        require(
            nearly_equal(config.modulation_dot_seconds, 2.0) &&
                nearly_equal(config.cw_intra_element_gap, 1.5) &&
                nearly_equal(config.cw_inter_character_gap, 4.0) &&
                nearly_equal(config.cw_inter_word_gap, 8.0) &&
                nearly_equal(config.dfcw_intra_element_gap, 0.25) &&
                nearly_equal(config.dfcw_inter_character_gap, 1.25) &&
                nearly_equal(config.dfcw_inter_word_gap, 3.5),
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
                nearly_equal(jConfig["CW"]["DFCW Intra Element Gap"].get<double>(), 0.25) &&
                nearly_equal(jConfig["CW"]["DFCW Inter Character Gap"].get<double>(), 1.25) &&
                nearly_equal(jConfig["CW"]["DFCW Inter Word Gap"].get<double>(), 3.5) &&
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
                nearly_equal(std::stod(cw_it->second.at("DFCW Intra Element Gap")), 0.25) &&
                nearly_equal(std::stod(cw_it->second.at("DFCW Inter Character Gap")), 1.25) &&
                nearly_equal(std::stod(cw_it->second.at("DFCW Inter Word Gap")), 3.5) &&
                cw_it->second.at("Fade Shape") == "raised_cosine" &&
                cw_it->second.at("Fade In Ms") == "25" &&
                cw_it->second.at("Fade Out Ms") == "40" &&
                cw_it->second.at("Fade Slice Ms") == "2",
            "json_to_ini must persist CW timing and fade settings");
    }

    {
        init_config_json();
        jConfig["CW"]["Intra Element Gap"] = 9.0;
        jConfig["CW"]["Inter Character Gap"] = 9.0;
        jConfig["CW"]["Inter Word Gap"] = 9.0;
        jConfig["CW"].erase("DFCW Intra Element Gap");
        jConfig["CW"].erase("DFCW Inter Character Gap");
        jConfig["CW"].erase("DFCW Inter Word Gap");
        json_to_config();

        require(
            nearly_equal(config.dfcw_intra_element_gap, kDefaultDfcwIntraElementGap) &&
                nearly_equal(config.dfcw_inter_character_gap, kDefaultDfcwInterCharacterGap) &&
                nearly_equal(config.dfcw_inter_word_gap, kDefaultDfcwInterWordGap),
            "missing DFCW gaps must use DFCW defaults instead of shared CW gaps");

        config_to_json();
        require(
            nearly_equal(
                jConfig["CW"]["DFCW Intra Element Gap"].get<double>(),
                kDefaultDfcwIntraElementGap) &&
                nearly_equal(
                    jConfig["CW"]["DFCW Inter Character Gap"].get<double>(),
                    kDefaultDfcwInterCharacterGap) &&
                nearly_equal(
                    jConfig["CW"]["DFCW Inter Word Gap"].get<double>(),
                    kDefaultDfcwInterWordGap),
            "config_to_json must materialize missing DFCW gap defaults");
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
        jConfig["CW"]["Message"] = "73";
        json_to_config();

        require(
            config.qrss.message == "73" &&
                config.fskcw.message == "73" &&
                config.dfcw.message == "73",
            "json_to_config must accept numeric-only CW message strings");

        jConfig["CW"]["Message"] = "599";
        json_to_config();
        require(
            config.qrss.message == "599",
            "json_to_config must accept report-like numeric CW message strings");

        jConfig["CW"]["Message"] = "5NN";
        json_to_config();
        require(
            config.qrss.message == "5NN",
            "json_to_config must preserve mixed alphanumeric CW message strings");

        jConfig["CW"]["Message"] = "CQ AA0NT 73";
        json_to_config();
        require(
            config.qrss.message == "CQ AA0NT 73",
            "json_to_config must preserve existing valid CW messages with spaces and digits");
    }

    {
        init_config_json();
        jConfig["CW"]["Message"] = 73;
        json_to_config();

        require(
            config.qrss.message == "73" &&
                config.fskcw.message == "73" &&
                config.dfcw.message == "73",
            "json_to_config must normalize legacy integer CW.Message values to text");

        config_to_json();
        require(
            jConfig["CW"]["Message"].is_string() &&
                jConfig["CW"]["Message"].get<std::string>() == "73",
            "config_to_json must reserialize normalized integer CW.Message values as strings");
    }

    {
        auto data = make_managed_ini_data("AA0NT", "EM18", "20m", false);
        data["CW"]["Message"] = "73";
        iniFile.setData(data);

        PreparedConfigCandidate candidate;
        prepare_ini_config_candidate("/tmp/cw_numeric_message.ini", candidate);
        require(
            candidate.valid &&
                candidate.normalized_config.qrss.message == "73" &&
                candidate.normalized_config.fskcw.message == "73" &&
                candidate.normalized_config.dfcw.message == "73",
            "managed INI candidate preparation must normalize unquoted numeric CW.Message values to text");
        require(
            candidate.normalized_json["CW"]["Message"].is_string() &&
                candidate.normalized_json["CW"]["Message"].get<std::string>() == "73",
            "managed INI candidate preparation must reserialize normalized numeric CW.Message values as strings");
    }

    {
        const auto assert_invalid_cw_message_type =
            [](const nlohmann::json &value, const std::string &description)
        {
            init_config_json();
            jConfig["CW"]["Message"] = value;

            bool threw = false;
            std::string error_message;
            try
            {
                json_to_config();
            }
            catch (const std::exception &e)
            {
                threw = true;
                error_message = e.what();
            }

            require(
                threw &&
                    error_message ==
                        "Invalid CW.Message. Expected a string or integer number.",
                "json_to_config must reject " + description + " CW.Message values with a field-specific error");
        };

        assert_invalid_cw_message_type(73.5, "non-integer numeric");
        assert_invalid_cw_message_type(true, "boolean");
        assert_invalid_cw_message_type(nullptr, "null");
        assert_invalid_cw_message_type(nlohmann::json::array({73}), "array");
        assert_invalid_cw_message_type(
            nlohmann::json::object({{"Message", 73}}),
            "object");
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
        ScopedTemporaryFile ini("/tmp/wsprrypi-wtp-transient-XXXXXX");
        auto data = make_managed_ini_data(
            "AA0NT", "EM18", "20m", true, WsprPlannerPreference::Auto, "Follow");
        data["Operation"]["Transmit Backend"] = "wtp";
        data["Operation"]["Use LED"] = "false";
        data["Operation"]["Use Amp"] = "false";
        data["Operation"]["Use Shutdown"] = "false";
        data["Experimental"] = {
            {"Allow Unqualified Frequency", "true"},
            {"Allow Non-Amateur Frequency", "true"}};
        data["WTP"] = {
            {"Transport", "network"},
            {"Hostname", "wsprrypico-0a60df.local"},
            {"TCP Port", "18443"},
            {"TLS Server Identity", "wsprrypico-0a60df.local"},
            {"TLS CA File", "/tmp/ca.crt"},
            {"TLS Client Certificate", "/tmp/client.crt"},
            {"TLS Client Key", "/tmp/client.key"},
            {"Endpoint", ""},
            {"USB Serial", ""},
            {"USB Vendor ID", "0"},
            {"USB Product ID", "0"},
            {"Device ID", "fd6127d11d6aca42a9905fa3fb1bf1d5"},
            {"Allow Frequency Adjustment", "true"},
            {"Start Uncertainty ns", "500000000"}};
        write_managed_ini_file(ini.path(), data);
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi", "-i", ini.path(),
            "--dfcw-message", "ET E",
            "--dfcw-dot-frequency", "137505",
            "--dfcw-dash-frequency", "137500",
            "--dfcw-dot-seconds", "3"};
        std::vector<char *> argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "managed WTP INI plus complete transient DFCW request must parse");
        std::string message;
        double dot_hz = 0.0;
        double dash_hz = 0.0;
        double dot_seconds = 0.0;
        require(
            config.use_ini && config.transmit_backend == TransmitBackendKind::WTP &&
                try_get_dfcw_startup_request(message, dot_hz, dash_hz, dot_seconds) &&
                message == "ET E" && nearly_equal(dot_hz, 137505.0) &&
                nearly_equal(dash_hz, 137500.0) && nearly_equal(dot_seconds, 3.0),
            "managed WTP transient DFCW must preserve explicit reverse-profile frequencies");
    }

    {
        ArgParserConfig invalid_gap_candidate;
        invalid_gap_candidate.cw_intra_element_gap = 0.0;
        std::string validation_error;
        require(
            !validate_config_candidate(invalid_gap_candidate, &validation_error) &&
                validation_error == "CW gap settings must be finite and greater than 0.",
            "validation must reject non-positive CW gap settings");

        ArgParserConfig invalid_dfcw_gap_candidate;
        invalid_dfcw_gap_candidate.dfcw_inter_word_gap = 0.0;
        validation_error.clear();
        require(
            !validate_config_candidate(invalid_dfcw_gap_candidate, &validation_error) &&
                validation_error == "DFCW gap settings must be finite and greater than 0.",
            "validation must reject non-positive DFCW gap settings");

        ArgParserConfig non_finite_dot_candidate;
        non_finite_dot_candidate.modulation_dot_seconds =
            std::numeric_limits<double>::infinity();
        validation_error.clear();
        require(
            !validate_config_candidate(non_finite_dot_candidate, &validation_error) &&
                validation_error == "Modulation dot_seconds must be finite and greater than 0.",
            "validation must reject non-finite shared CW dot timing");

        ArgParserConfig non_finite_cw_gap_candidate;
        non_finite_cw_gap_candidate.cw_inter_character_gap =
            std::numeric_limits<double>::quiet_NaN();
        validation_error.clear();
        require(
            !validate_config_candidate(non_finite_cw_gap_candidate, &validation_error) &&
                validation_error == "CW gap settings must be finite and greater than 0.",
            "validation must reject non-finite conventional CW gaps");

        ArgParserConfig non_finite_dfcw_gap_candidate;
        non_finite_dfcw_gap_candidate.dfcw_intra_element_gap =
            std::numeric_limits<double>::infinity();
        validation_error.clear();
        require(
            !validate_config_candidate(non_finite_dfcw_gap_candidate, &validation_error) &&
                validation_error == "DFCW gap settings must be finite and greater than 0.",
            "validation must reject non-finite DFCW gaps");

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

        PreparedConfigCandidate upper_boundary_candidate =
            prepare_si5351_address_candidate("0x6F");
        require(
            upper_boundary_candidate.valid &&
                upper_boundary_candidate.normalized_config.si5351_i2c_address == 0x6F,
            "Si5351 I2C address must accept the inclusive upper boundary");

        PreparedConfigCandidate below_range_candidate =
            prepare_si5351_address_candidate("0x5F");
        require(
            !below_range_candidate.valid &&
                below_range_candidate.error_reason ==
                    "Invalid Si5351 I2C address. Expected 0x60 through 0x6F.",
            "Si5351 I2C address must reject values below 0x60");

        PreparedConfigCandidate out_of_range_candidate =
            prepare_si5351_address_candidate("0x80");
        require(
            !out_of_range_candidate.valid &&
                out_of_range_candidate.error_reason ==
                    "Invalid Si5351 I2C address. Expected 0x60 through 0x6F.",
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
            config.transmit &&
                iniFile.getData().at("Operation").at("Transmit") == "true",
            "CLI planner preference override must not receive Enable on Boot startup policy during parsing");
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

    std::string cw_start_second_persistence_path;
    {
        init_config_json();
        jConfig["CW"].erase("Start Second");
        json_to_config();
        require(
            config.schedule_start_second == 5,
            "missing CW.Start Second must default to 5");

        for (const int value : {0, 5, 59})
        {
            jConfig["CW"]["Start Second"] = value;
            json_to_config();
            require(
                config.schedule_start_second == value,
                "json_to_config must preserve valid CW.Start Second values");
            config_to_json();
            require(
                jConfig.at("CW").at("Start Second") == value &&
                    get_public_config_json().at("CW").at("Start Second") == value,
                "config and public JSON must round-trip CW.Start Second");
        }

        for (const auto &invalid : {
                 nlohmann::json(-1),
                 nlohmann::json(60),
                 nlohmann::json(5.5),
                 nlohmann::json("5"),
                 nlohmann::json("invalid")})
        {
            init_config_json();
            jConfig["CW"]["Start Second"] = invalid;
            bool rejected = false;
            try
            {
                json_to_config();
                std::string validation_error;
                rejected = !validate_config_candidate(config, &validation_error);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            require(rejected, "invalid CW.Start Second JSON must be rejected");
        }

        init_config_json();
        json_to_config();
        config.transmit = false;
        config_to_json();
        patch_all_from_web({{"CW", {{"Start Second", 59}}}});
        require(
            config.schedule_start_second == 59 &&
                jConfig.at("CW").at("Start Second") == 59,
            "web PATCH must preserve CW.Start Second");
        const nlohmann::json before_rejected_patch = jConfig;
        bool patch_rejected = false;
        try
        {
            patch_all_from_web({{"CW", {{"Start Second", 60}}}});
        }
        catch (const std::exception &)
        {
            patch_rejected = true;
        }
        require(
            patch_rejected && config.schedule_start_second == 59 &&
                jConfig == before_rejected_patch,
            "rejected web CW.Start Second updates must not mutate live or persisted config");

        ScopedTemporaryFile persistence_file(
            "/tmp/wsprrypi_cw_start_second_persist.XXXXXX");
        cw_start_second_persistence_path = persistence_file.path();
        config.use_ini = true;
        config.ini_filename = cw_start_second_persistence_path;
        iniFile.set_filename(config.ini_filename);
        config_to_json();
        json_to_ini();
        iniFile.load();
        require(
            iniFile.getData().at("CW").at("Start Second") == "59",
            "INI persistence must preserve CW.Start Second");
        config.use_ini = false;
        config.ini_filename.clear();
    }
    require(
        access(cw_start_second_persistence_path.c_str(), F_OK) != 0,
        "scoped CW.Start Second persistence file must be removed after the test block");

    {
        auto local_time = [](int year, int month, int day, int hour, int minute, int second)
        {
            std::tm value{};
            value.tm_year = year - 1900;
            value.tm_mon = month - 1;
            value.tm_mday = day;
            value.tm_hour = hour;
            value.tm_min = minute;
            value.tm_sec = second;
            value.tm_isdst = -1;
            return std::chrono::system_clock::from_time_t(std::mktime(&value));
        };
        auto require_launch = [&](int now_hour,
                                  int now_minute,
                                  int now_second,
                                  int start_second,
                                  int expected_day,
                                  int expected_hour,
                                  int expected_minute,
                                  int expected_second)
        {
            ArgParserConfig candidate;
            candidate.schedule_start_minute = 0;
            candidate.schedule_start_second = start_second;
            candidate.schedule_repeat_minutes = 10;
            const auto launch = next_non_wspr_schedule_time_for_test(
                candidate,
                local_time(2026, 7, 27, now_hour, now_minute, now_second));
            const std::time_t launch_time = std::chrono::system_clock::to_time_t(launch);
            std::tm actual{};
            localtime_r(&launch_time, &actual);
            require(
                actual.tm_mday == expected_day &&
                    actual.tm_hour == expected_hour &&
                    actual.tm_min == expected_minute &&
                    actual.tm_sec == expected_second,
                "deterministic CW scheduler must select the expected strict-future launch");
        };

        require_launch(12, 0, 4, 5, 27, 12, 0, 5);
        require_launch(12, 0, 5, 5, 27, 12, 10, 5);
        require_launch(12, 0, 6, 5, 27, 12, 10, 5);
        require_launch(12, 59, 59, 5, 27, 13, 0, 5);
        require_launch(23, 59, 59, 5, 28, 0, 0, 5);
        require_launch(12, 0, 0, 0, 27, 12, 10, 0);
        require_launch(12, 0, 58, 59, 27, 12, 0, 59);

        ArgParserConfig shared;
        shared.schedule_start_minute = 20;
        shared.schedule_start_second = 5;
        shared.schedule_repeat_minutes = 10;
        const auto now = local_time(2026, 7, 27, 12, 20, 4);
        std::optional<std::chrono::system_clock::time_point> common;
        for (const ModeType mode : {ModeType::QRSS, ModeType::FSKCW, ModeType::DFCW})
        {
            shared.mode = mode;
            const auto launch = next_non_wspr_schedule_time_for_test(shared, now);
            require(!common.has_value() || *common == launch,
                    "QRSS, FSKCW, and DFCW must share the CW schedule calculation");
            common = launch;
        }

        init_default_config();
        config.use_ini = false;
        config.mode = ModeType::QRSS;
        config.transmit = true;
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.si5351_i2c_bus = 1;
        config.si5351_i2c_address = 0x60;
        config.si5351_reference_hz = 27000000;
        config.si5351_tx_output = 0;
        config.si5351_power_level = 1;
        resolve_backend_specific_config(config);
        config.qrss.message = "TEST";
        config.qrss.frequency_hz = 14096900.0;
        config.qrss.dot_seconds = 1.0;
        set_scheduler_execution_suppressed_for_test(true);
        const auto generation = non_wspr_schedule_generation_for_test();
        require(set_config(true),
                "CW configuration change must apply through the production scheduler path");
        require(
            non_wspr_schedule_generation_for_test() > generation,
            "schedule changes must invalidate the previous sleeping generation");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_config_json();
        json_to_config();
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi", "--mode", "QRSS", "--cw-message", "TEST",
            "--cw-base-frequency", "14096900", "--cw-dot-seconds", "1",
            "--cw-start-second", "0"};
        std::vector<char *> argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()) &&
                config.schedule_start_second == 0,
            "CLI --cw-start-second must accept and preserve zero");

        init_config_json();
        json_to_config();
        reset_getopt_state();
        args = {
            "wsprrypi", "--mode", "QRSS", "--cw-message", "TEST",
            "--cw-base-frequency", "14096900", "--cw-dot-seconds", "1",
            "--cw-start-second", "59"};
        argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()) &&
                config.schedule_start_second == 59,
            "CLI --cw-start-second must accept 59");

        require_cli_parse_rejected(
            {"--mode", "QRSS", "--cw-message", "TEST", "--cw-base-frequency", "14096900",
             "--cw-dot-seconds", "1", "--cw-start-second", "60"},
            "CLI --cw-start-second above 59");
        require_cli_parse_rejected(
            {"--mode", "QRSS", "--cw-message", "TEST", "--cw-base-frequency", "14096900",
             "--cw-dot-seconds", "1", "--cw-start-second=-1"},
            "negative CLI --cw-start-second");
        require_cli_parse_rejected(
            {"--mode", "QRSS", "--cw-message", "TEST", "--cw-base-frequency", "14096900",
             "--cw-dot-seconds", "1", "--cw-start-second", "5.5"},
            "fractional CLI --cw-start-second");
        require_cli_parse_rejected(
            {"--mode", "QRSS", "--cw-message", "TEST", "--cw-base-frequency", "14096900",
             "--cw-dot-seconds", "1", "--cw-start-second", "invalid"},
            "nonnumeric CLI --cw-start-second");
        require_cli_parse_rejected(
            {"--qrss-message", "TEST", "--qrss-frequency", "14096900",
             "--qrss-dot-seconds", "1", "--cw-start-second", "5"},
            "scheduled seconds with transient QRSS startup");
    }

    {
        init_default_config();
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "40m@5";
        config.wspr.audio_offset_hz = 1500.0;
        set_frequencies(config);
        copy_runtime_config(config, config);
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        const TransmissionRequest request = current_transmission_request_for_test();
        require(started.started && nearly_equal(request.dial_frequency_hz, 14095600.0) &&
                    nearly_equal(request.actual_rf_frequency_hz, 14097100.0),
                "explicit 20m tone must commit canonical dial plus offset once");
        require(!request.selector_gpio_enabled,
                "different-band first-entry selector must not be inherited");
        const TestToneStopResult stopped = end_test_tone();
        require(stopped.tone_was_active && stopped.stopped,
                "explicit 20m Test Tone End must succeed");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.transmit_backend = TransmitBackendKind::GPIO;
        config.mode = ModeType::QRSS;
        copy_runtime_config(config, config);
        TransmissionRequest sentinel;
        sentinel.actual_rf_frequency_hz = 7654321.0;
        set_current_transmission_request_for_test(sentinel);
        reset_band_gpio_prepare_call_count_for_test();

        const TestToneStartResult band_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "12m", std::nullopt});
        const TestToneStartResult four_meter_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "4m", std::nullopt});
        const TestToneStartResult two_meter_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "2m", std::nullopt});
        const TestToneStartResult one_twenty_five_meter_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "1.25m", std::nullopt});
        const TestToneStartResult seventy_centimeter_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "70cm", std::nullopt});
        const TestToneStartResult custom_125m_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::CustomRf, "", 223500000U});
        const TestToneStartResult custom_70cm_rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::CustomRf, "", 435000000U});

        require(
            !band_rejected.started && !four_meter_rejected.started &&
                !two_meter_rejected.started &&
                !one_twenty_five_meter_rejected.started &&
                !seventy_centimeter_rejected.started &&
                !custom_125m_rejected.started &&
                !custom_70cm_rejected.started &&
                band_rejected.message.find("12 m") !=
                    std::string::npos &&
                four_meter_rejected.message.find("4 m") != std::string::npos &&
                two_meter_rejected.message.find("2 m") != std::string::npos &&
                one_twenty_five_meter_rejected.message.find("1.25 m") !=
                    std::string::npos &&
                seventy_centimeter_rejected.message.find("70 cm") !=
                    std::string::npos &&
                custom_125m_rejected.message.find("1.25 m") != std::string::npos &&
                custom_70cm_rejected.message.find("70 cm") != std::string::npos &&
                band_rejected.message.find("experimental risk") !=
                    std::string::npos &&
                config.mode == ModeType::QRSS &&
                nearly_equal(
                    current_transmission_request_for_test().actual_rf_frequency_hz,
                    7654321.0) &&
                band_gpio_prepare_call_count_for_test() == 0U,
            "named and custom disqualified GPIO Test Tones must reject before runtime or selector mutation");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.transmit_backend = TransmitBackendKind::GPIO;
        config.mode = ModeType::QRSS;
        copy_runtime_config(config, config);
        set_raspberry_pi_generation_override_for_test(4);

        const TestToneStartResult six_meter_started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "6m", std::nullopt});
        const TestToneStopResult six_meter_stopped = end_test_tone();
        const TestToneStartResult custom_six_meter_started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::CustomRf, "", 51000000U});
        const TestToneStopResult custom_six_meter_stopped = end_test_tone();
        require(
            six_meter_started.started && six_meter_stopped.tone_was_active &&
                six_meter_stopped.stopped && custom_six_meter_started.started &&
                custom_six_meter_stopped.tone_was_active &&
                custom_six_meter_stopped.stopped,
            "named and custom BCM2711 6 m TONE must start and stop after qualification");
        set_scheduler_execution_suppressed_for_test(false);
        clear_pi_generation_override_for_scope();
    }

    test_explicit_tone_experimental_policy();

    {
        init_config_json();
        json_to_config();
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--test-tone",
            "435000000",
            "--backend",
            "gpio",
            "--transmit-gpio",
            "4"};
        std::vector<char *> argv = argv_for(args);
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()),
            "direct CLI 70 cm test-tone parsing must succeed before band policy evaluation");

        WsprFrequencyEntry entry;
        double actual_rf_frequency_hz = 0.0;
        require(
            try_get_direct_tone_startup_request(entry, actual_rf_frequency_hz) &&
                nearly_equal(actual_rf_frequency_hz, 435000000.0),
            "direct CLI 70 cm test tone must retain its transient numeric request");

        set_scheduler_execution_suppressed_for_test(true);
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        reset_band_gpio_prepare_call_count_for_test();
        int start_async_calls = 0;
        set_direct_tone_start_invoker_for_test(
            [&start_async_calls] { ++start_async_calls; });
        std::string startup_error;
        const bool started = start_direct_tone_execution_for_test(
            config,
            entry,
            actual_rf_frequency_hz,
            &startup_error);

        require(
            !started && startup_error.find("70 cm") != std::string::npos &&
                start_async_calls == 0 &&
                band_gpio_prepare_call_count_for_test() == 0U &&
                current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                !current_controller_request_for_test().has_value() &&
                committed_execution_route_for_test() ==
                    CommittedExecutionRouteForTest::NONE,
            "direct CLI 70 cm GPIO tone must fail before selector preparation, request commit, or startAsync");

        reset_direct_tone_start_invoker_for_test();
        clear_direct_tone_startup_request();
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        config.transmit_backend = TransmitBackendKind::GPIO;
        set_scheduler_execution_suppressed_for_test(true);
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        reset_band_gpio_prepare_call_count_for_test();
        int start_async_calls = 0;
        set_direct_tone_start_invoker_for_test(
            [&start_async_calls] { ++start_async_calls; });

        WsprFrequencyEntry unknown_band_entry;
        unknown_band_entry.token = "150000000";
        unknown_band_entry.dial_frequency_hz = 150000000.0;
        std::string lookup_error;
        const bool unknown_band_started = start_direct_tone_execution_for_test(
            config,
            unknown_band_entry,
            unknown_band_entry.dial_frequency_hz,
            &lookup_error);
        BandGPIOConfig inactive_selector_config;
        std::string inactive_selector_band;
        require(
            !unknown_band_started && !lookup_error.empty() &&
                start_async_calls == 0 &&
                band_gpio_prepare_call_count_for_test() == 0U &&
                current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                !current_controller_request_for_test().has_value() &&
                committed_execution_route_for_test() ==
                    CommittedExecutionRouteForTest::NONE &&
                !current_band_gpio_selection_for_test(
                    inactive_selector_config,
                    inactive_selector_band),
            "outside-band direct CLI policy rejection must precede selector preparation, request commit, and startAsync");

        reset_direct_tone_start_invoker_for_test();
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        const std::string confirmation_json = R"({
            "enabled": true,
            "route": "GPIO4",
            "operation_id": "wspr-frame-operation-43",
            "physical_connection_confirmed": true,
            "attenuation_and_load_confirmed": true,
            "bounded_operation_confirmed": true,
            "non_radiating_topology_confirmed": true,
            "experimental_status_acknowledged": true
        })";
        reset_getopt_state();
        std::vector<std::string> args = {
            "wsprrypi",
            "--backend", "rp1-gpclk",
            "--transmit-gpio", "4",
            "--rp1-development-confirmation-json", confirmation_json,
            "-x", "3",
            "AA0NT", "EM18", "20", "20m"};
        std::vector<char *> argv = argv_for(args);
        std::string validation_error;
        require(
            parse_command_line(static_cast<int>(argv.size()), argv.data()) &&
                validate_config_data_for_test(&validation_error),
            "bounded positional RP1 WSPR CLI must parse and validate: " +
                validation_error);
        set_rp1_route_transaction_inhibited(true);
        int reconcile_calls = 0;
        set_rp1_development_reconcile_invoker_for_test(
            [&reconcile_calls](const std::string &route) {
                ++reconcile_calls;
                require(route == "GPIO4",
                    "positional RP1 WSPR must reconcile the confirmed route");
                set_rp1_route_transaction_inhibited(false);
                return nlohmann::json{
                    {"ok", true},
                    {"generation", 43},
                    {"persisted", "GPIO4"},
                    {"active", "GPIO4"},
                    {"reconciled", true},
                    {"journal", "none"},
                    {"contractIdentity", "rp1-gpclk-route-manager"},
                    {"endpointOpen", false},
                    {"endpointOwned", true},
                    {"state", "idle"},
                    {"outputInhibited", "Disabled"},
                    {"operationalReady", "Ready"}};
            });

        const bool configured = set_config(true);
        const TransmissionRequest committed =
            current_transmission_request_for_test();
        require(
            configured && reconcile_calls == 1 &&
                committed.mode == TransmissionMode::WSPR &&
                committed.rp1_development.enabled &&
                committed.rp1_development.operation_id ==
                    "wspr-frame-operation-43" &&
                committed.rp1_development.route_transaction_generation == 43 &&
                committed.rp1_development.confirmation_gpio == 4 &&
                !config.transmit && !config.loop_tx &&
                config.tx_iterations.load() == 3,
            "bounded positional RP1 WSPR must reconcile before its runtime gate, bind and commit the frame request, and preserve transient iteration semantics");

        reset_rp1_development_reconcile_invoker_for_test();
        set_rp1_route_transaction_inhibited(false);
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        config.use_ini = false;
        config.mode = ModeType::TONE;
        config.transmit = false;
        config.transmit_backend = TransmitBackendKind::RP1_GPCLK;
        config.tx_pin = 4;
        config.gpio_tx_pin = 4;
        config.rp1_development_confirmation_json = R"({
            "enabled": true,
            "route": "GPIO4",
            "operation_id": "tone-operation-42",
            "physical_connection_confirmed": true,
            "attenuation_and_load_confirmed": true,
            "bounded_operation_confirmed": true,
            "non_radiating_topology_confirmed": true,
            "experimental_status_acknowledged": true
        })";
        const nlohmann::json valid_confirmation = nlohmann::json::parse(
            config.rp1_development_confirmation_json);
        set_scheduler_execution_suppressed_for_test(true);
        const bool persistent_transmit_before = config.transmit;
        set_rp1_route_transaction_inhibited(true);
        set_rp1_development_reconcile_invoker_for_test(
            [](const std::string &route) {
                require(route == "GPIO4",
                    "direct RP1 tone must reconcile the confirmed route");
                set_rp1_route_transaction_inhibited(false);
                return nlohmann::json{
                    {"ok", true},
                    {"generation", 42},
                    {"persisted", "GPIO4"},
                    {"active", "GPIO4"},
                    {"reconciled", true},
                    {"journal", "none"},
                    {"contractIdentity", "rp1-gpclk-route-manager"},
                    {"endpointOpen", false},
                    {"endpointOwned", true},
                    {"state", "idle"},
                    {"outputInhibited", "Disabled"},
                    {"operationalReady", "Ready"}};
            });
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        int start_async_calls = 0;
        set_direct_tone_start_invoker_for_test(
            [&start_async_calls] { ++start_async_calls; });

        WsprFrequencyEntry entry;
        entry.token = "20m";
        entry.dial_frequency_hz = 14097100.0;
        std::string error;
        const bool started = start_direct_tone_execution_for_test(
            config, entry, entry.dial_frequency_hz, &error);
        const TransmissionRequest committed =
            current_transmission_request_for_test();
        require(
            started && error.empty() && start_async_calls == 1 &&
                committed.rp1_development.enabled &&
                committed.rp1_development.operation_id == "tone-operation-42" &&
                committed.rp1_development.route_transaction_generation == 42 &&
                committed.rp1_development.confirmation_gpio == 4 &&
                config.transmit == persistent_transmit_before,
            "valid direct RP1 tone confirmation must reconcile before gating, bind the committed request, start once, and not mutate persistent transmit");

        set_rp1_route_transaction_inhibited(true);
        error.clear();
        const bool replay_started = start_direct_tone_execution_for_test(
            config, entry, entry.dial_frequency_hz, &error);
        require(
            !replay_started && error.find("replay") != std::string::npos &&
                start_async_calls == 1 && config.transmit == persistent_transmit_before,
            "direct RP1 tone confirmation must not be reusable after its first bounded request");

        std::vector<std::string> rejected_confirmations = {"not-json", "{}"};
        for (const auto& [field, value] : std::vector<std::pair<std::string, nlohmann::json>>{
                 {"enabled", false},
                 {"route", "GPIO20"},
                 {"operation_id", "short"},
                 {"physical_connection_confirmed", false},
                 {"attenuation_and_load_confirmed", false},
                 {"bounded_operation_confirmed", false},
                 {"non_radiating_topology_confirmed", false},
                 {"experimental_status_acknowledged", false}})
        {
            nlohmann::json rejected = valid_confirmation;
            rejected[field] = value;
            rejected_confirmations.push_back(rejected.dump());
        }
        for (const std::string& rejected : rejected_confirmations)
        {
            config.rp1_development_confirmation_json = rejected;
            set_rp1_route_transaction_inhibited(true);
            error.clear();
            const bool malformed_started = start_direct_tone_execution_for_test(
                config, entry, entry.dial_frequency_hz, &error);
            require(
                !malformed_started &&
                    error.find("confirmation rejected") != std::string::npos &&
                    start_async_calls == 1 &&
                    config.transmit == persistent_transmit_before,
                "malformed or mismatched direct RP1 confirmation must fail before commit or start without changing persistent transmit");
        }

        reset_rp1_development_reconcile_invoker_for_test();
        reset_direct_tone_start_invoker_for_test();
        set_rp1_route_transaction_inhibited(false);
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        config.transmit_backend = TransmitBackendKind::GPIO;
        config.allow_unqualified_frequency = true;
        config.allow_non_amateur_frequency = true;
        config.use_ini = false;
        set_scheduler_execution_suppressed_for_test(true);
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        reset_band_gpio_prepare_call_count_for_test();
        int start_async_calls = 0;
        set_direct_tone_start_invoker_for_test(
            [&start_async_calls] { ++start_async_calls; });

        WsprFrequencyEntry experimental_entry;
        experimental_entry.token = "150000000@17H";
        experimental_entry.dial_frequency_hz = 150000000.0;
        experimental_entry.selector_gpio = 17;
        experimental_entry.selector_gpio_active_high = true;
        std::string error;
        const bool started = start_direct_tone_execution_for_test(
            config, experimental_entry,
            experimental_entry.dial_frequency_hz, &error);
        const TransmissionRequest committed =
            current_transmission_request_for_test();
        require(
            started && error.empty() && start_async_calls == 1 &&
                band_gpio_prepare_call_count_for_test() == 1U &&
                committed.selector_gpio_enabled &&
                committed.selector_gpio_config.gpio == 17 &&
                committed.selector_gpio_config.active_high,
            "dual outside-band override must retain an explicit CLI selector without band inference: " + error);

        stop_active_transmission_selectors_for_test();
        reset_direct_tone_start_invoker_for_test();
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.mode = ModeType::QRSS;
        copy_runtime_config(config, config);

        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "2m", std::nullopt});
        const TestToneStopResult stopped = end_test_tone();

        require(
            started.started && stopped.tone_was_active && stopped.stopped,
            "the GPIO band restriction must not block a Si5351 2 m Test Tone");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "20m";
        config.wspr.audio_offset_hz = 1500.0;
        set_frequencies(config);
        copy_runtime_config(config, config);
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "22m", std::nullopt});
        require(!started.started && started.message.find("22m") != std::string::npos,
                "removed 22m Test Tone preset must fail with an explicit diagnostic");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "20m";
        config.wspr.audio_offset_hz = 2750.0;
        set_frequencies(config);
        copy_runtime_config(config, config);
        // Simulate a later uncommitted mutable write. Explicit planning must
        // consume the one published snapshot for both RF and result metadata.
        config.wspr.audio_offset_hz = 1500.0;
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        const TransmissionRequest request = current_transmission_request_for_test();
        require(started.started && nearly_equal(request.actual_rf_frequency_hz, 14098350.0) &&
                    nearly_equal(request.applied_offset_hz, 2750.0) &&
                    started.audio_offset_hz == 2750U,
                "one published snapshot must supply explicit RF and committed offset metadata");
        const TestToneStopResult stopped = end_test_tone();
        require(stopped.tone_was_active && stopped.stopped,
                "snapshot-backed explicit Test Tone End must succeed");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "40m";
        config.wspr.audio_offset_hz = 1500.0;
        set_frequencies(config);
        copy_runtime_config(config, config);
        constexpr std::uint64_t custom_rf_hz = 14097123;
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::CustomRf, "", custom_rf_hz});
        const TransmissionRequest request = current_transmission_request_for_test();
        require(started.started && nearly_equal(request.actual_rf_frequency_hz, static_cast<double>(custom_rf_hz)) &&
                    nearly_equal(request.dial_frequency_hz, static_cast<double>(custom_rf_hz)) &&
                    started.band == "20m",
                "explicit custom RF must remain exact and resolve 20m without a WSPR offset");
        const TestToneStopResult stopped = end_test_tone();
        require(stopped.tone_was_active && stopped.stopped,
                "explicit custom-RF Test Tone End must succeed");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "40m@5 20m@17";
        set_frequencies(config);
        copy_runtime_config(config, config);
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        const TransmissionRequest request = current_transmission_request_for_test();
        require(started.started && request.selector_gpio_enabled &&
                    request.selector_gpio_config.gpio == 17 &&
                    !request.selector_gpio_config.active_high &&
                    !started.selector_gpio_active_high &&
                    request.selector_band == HamBand::BAND_20M,
                "matching same-band active-low selector metadata must match the committed RF-band request");
        const TestToneStopResult stopped = end_test_tone();
        require(stopped.tone_was_active && stopped.stopped,
                "selector-backed explicit Test Tone End must succeed");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "40m@5";
        config.band_gpio[ham_band_index(HamBand::BAND_20M)] = {19, true, true};
        config.band_gpio[ham_band_index(HamBand::BAND_40M)] = {6, true, false};
        set_frequencies(config);
        copy_runtime_config(config, config);
        config.band_gpio[ham_band_index(HamBand::BAND_20M)] = {20, true, false};
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        const TransmissionRequest request = current_transmission_request_for_test();
        require(started.started && request.selector_gpio_enabled &&
                    request.selector_gpio_config.gpio == 19 &&
                    request.selector_gpio_config.active_high &&
                    started.selector_gpio_active_high &&
                    request.selector_band == HamBand::BAND_20M,
                "active-high metadata must come from the committed selected-band fallback, not a different-band selector");
        const TestToneStopResult stopped = end_test_tone();
        require(stopped.tone_was_active && stopped.stopped,
                "fallback-selector explicit Test Tone End must succeed");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.frequencies = "40m@5";
        set_frequencies(config);
        copy_runtime_config(config, config);
        const TestToneStartResult started = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        const TransmissionRequest request = current_transmission_request_for_test();
        require(started.started && !request.selector_gpio_enabled &&
                    !started.selector_gpio_active_high,
                "selector-disabled Test Tone metadata must remain neutral");
        const TestToneStopResult stopped = end_test_tone();
        require(stopped.tone_was_active && stopped.stopped,
                "selector-disabled explicit Test Tone End must succeed");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.mode = ModeType::QRSS;
        TransmissionRequest sentinel;
        sentinel.mode = TransmissionMode::WSPR;
        sentinel.actual_rf_frequency_hz = 1234567.0;
        set_current_transmission_request_for_test(sentinel);
        reset_band_gpio_prepare_call_count_for_test();
        const TestToneStartResult rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "not-a-band", std::nullopt});
        require(!rejected.started && config.mode == ModeType::QRSS &&
                    nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, 1234567.0) &&
                    band_gpio_prepare_call_count_for_test() == 0U,
                "frequency planning failure must leave tone, mode, request, and selector preparation unchanged");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        set_scheduler_execution_suppressed_for_test(true);
        config.transmit = false;
        config.mode = ModeType::FSKCW;
        config.frequencies = "20m@17 20m@18";
        set_frequencies(config);
        copy_runtime_config(config, config);
        TransmissionRequest sentinel;
        sentinel.mode = TransmissionMode::WSPR;
        sentinel.actual_rf_frequency_hz = 7654321.0;
        set_current_transmission_request_for_test(sentinel);
        reset_band_gpio_prepare_call_count_for_test();
        const TestToneStartResult rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        require(!rejected.started && !rejected.message.empty() && config.mode == ModeType::FSKCW &&
                    nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, 7654321.0) &&
                    band_gpio_prepare_call_count_for_test() == 0U,
                "selector conflict must reject before tone, mode, request, or selector preparation mutation");
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.frequencies = "20m";
        set_frequencies(config);
        TransmissionRequest sentinel;
        sentinel.mode = TransmissionMode::WSPR;
        sentinel.actual_rf_frequency_hz = 1111111.0;
        set_current_transmission_request_for_test(sentinel);
        reset_band_gpio_prepare_call_count_for_test();
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);

        const TestToneStartResult rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        const TestToneStopResult inactive_stop = end_test_tone();
        require(!rejected.started && rejected.blocked_by_active_transmission &&
                    !rejected.blocked_by_enabled_transmission &&
                    !inactive_stop.tone_was_active &&
                    nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, 1111111.0) &&
                    band_gpio_prepare_call_count_for_test() == 0U &&
                    wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING,
                "explicit WSPR-band request must reject before planning, selector preparation, Test Tone activation, or request mutation while TX is active");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.frequencies = "20m";
        set_frequencies(config);
        TransmissionRequest sentinel;
        sentinel.mode = TransmissionMode::WSPR;
        sentinel.actual_rf_frequency_hz = 2222222.0;
        set_current_transmission_request_for_test(sentinel);
        reset_band_gpio_prepare_call_count_for_test();
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::ENABLED);

        const TestToneStartResult rejected = start_test_tone(
            TestToneRequest{TestToneFrequencySource::CustomRf, "", 14097123});
        const TestToneStopResult inactive_stop = end_test_tone();
        require(!rejected.started && !rejected.blocked_by_active_transmission &&
                    rejected.blocked_by_enabled_transmission && !inactive_stop.tone_was_active &&
                    nearly_equal(current_transmission_request_for_test().actual_rf_frequency_hz, 2222222.0) &&
                    band_gpio_prepare_call_count_for_test() == 0U &&
                    wsprTransmitter.getState() == WsprTransmitter::State::ENABLED,
                "explicit custom-RF request must reject before planning, selector preparation, Test Tone activation, or request mutation while schedule is enabled");

        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_committed_execution_route_for_test();
        reset_tx_led_request_counts_for_test();
        set_scheduler_execution_suppressed_for_test(false);
        config.mode = ModeType::QRSS;
        config.transmit = false;
        config.transmit_backend = TransmitBackendKind::SI5351;
        config.frequencies = "20m";
        set_frequencies(config);
        copy_runtime_config(config, config);
        wsprTransmitter.backendSetStateValue(WsprTransmitter::State::DISABLED);
        set_test_tone_commit_invoker_for_test([] {
            throw std::runtime_error(
                "Si5351 planner produced unusable frequency data for tone 0.");
        });

        const TestToneStartResult rejected = start_test_tone(
            TestToneRequest{
                TestToneFrequencySource::CustomRf,
                "",
                144490500U});
        reset_test_tone_commit_invoker_for_test();
        const TestToneStopResult inactive_stop = end_test_tone();

        require(
            !rejected.started &&
                rejected.message.find("Si5351 planner produced unusable") !=
                    std::string::npos &&
                config.mode == ModeType::QRSS &&
                !inactive_stop.tone_was_active &&
                current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                !current_controller_request_for_test().has_value() &&
                committed_execution_route_for_test() ==
                    CommittedExecutionRouteForTest::NONE &&
                wsprTransmitter.getState() == WsprTransmitter::State::DISABLED &&
                !tx_led_active_for_test(),
            "synchronous test tone configuration failure must reject with its diagnostic and restore inactive runtime state");
    }

    // Managed INI CW modes are idle owners, never implicit CLI direct tones.
    const std::array<std::pair<ModeType, TestToneRequest>, 5> managed_idle_cases{{
        {ModeType::FSKCW, {TestToneFrequencySource::WsprBand, "20m", std::nullopt}},
        {ModeType::FSKCW, {TestToneFrequencySource::CustomRf, "", 14097123U}},
        {ModeType::QRSS, {TestToneFrequencySource::WsprBand, "20m", std::nullopt}},
        {ModeType::DFCW, {TestToneFrequencySource::WsprBand, "20m", std::nullopt}},
        {ModeType::FSKCW, {TestToneFrequencySource::LegacyExactRf, "", 14097123U}},
    }};
    for (const auto &[prior_mode, request] : managed_idle_cases)
    {
        init_default_config();
        clear_direct_tone_startup_request();
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        reset_band_gpio_prepare_call_count_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.use_ini = true;
        config.transmit = false;
        config.mode = prior_mode;
        config.frequencies = "20m";
        config.wspr.audio_offset_hz = 1500.0;
        set_frequencies(config);
        copy_runtime_config(config, config);
        const WsprTransmitter::State state_before_start =
            wsprTransmitter.getState();
        const TestToneStartResult start = start_test_tone(request);
        const WsprTransmitter::State state_after_suppressed_start =
            wsprTransmitter.getState();
        const TransmissionRequest committed = current_transmission_request_for_test();
        const TestToneStopResult stop = end_test_tone();
        const TransmissionRequest cleared = current_transmission_request_for_test();
        const std::size_t prepares = band_gpio_prepare_call_count_for_test();
        BandGPIOConfig selector_config;
        std::string selector_band;
        const bool selector_active = current_band_gpio_selection_for_test(
            selector_config,
            selector_band);
        const ModeType mode_after_first_end = config.mode;
        const WsprTransmitter::State state_after_first_end =
            wsprTransmitter.getState();
        const bool direct_tone_after_first_end =
            has_direct_tone_startup_request();
        const TestToneStopResult repeated = end_test_tone();
        require(start.started && committed.mode == TransmissionMode::TONE,
                "managed idle non-WSPR Test Tone must start and commit a tone request");
        require(state_before_start == WsprTransmitter::State::DISABLED &&
                    state_after_suppressed_start == WsprTransmitter::State::DISABLED,
                "suppressed managed idle Test Tone setup must leave the transmitter disabled before End cleanup");
        require(stop.tone_was_active && stop.stopped,
                "managed idle non-WSPR Test Tone End must report a successful active stop");
        require(config.mode == prior_mode && !config.transmit,
                "managed idle non-WSPR Test Tone End must restore the inactive managed mode");
        require(state_after_first_end == WsprTransmitter::State::DISABLED,
                "managed idle non-WSPR Test Tone End must leave the transmitter disabled");
        require(cleared.actual_rf_frequency_hz == 0.0,
                "managed idle non-WSPR Test Tone End must clear committed execution state");
        require(!selector_active && prepares <= 1U,
                "managed idle non-WSPR Test Tone must leave the disabled selector neutral without a restart");
        require(!direct_tone_after_first_end,
                "managed idle non-WSPR Test Tone must not create or consume a direct-tone startup request");
        require(!repeated.tone_was_active && !repeated.stopped &&
                    config.mode == mode_after_first_end &&
                    wsprTransmitter.getState() == state_after_first_end &&
                    current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                    band_gpio_prepare_call_count_for_test() == prepares &&
                    has_direct_tone_startup_request() == direct_tone_after_first_end,
                "managed idle non-WSPR Test Tone repeated End must remain inert");
        if (request.source == TestToneFrequencySource::WsprBand)
            require(nearly_equal(committed.dial_frequency_hz, 14095600.0) &&
                        nearly_equal(committed.actual_rf_frequency_hz, 14097100.0),
                    "managed explicit 20m Test Tone must use dial plus 1500 Hz once");
        if (request.source == TestToneFrequencySource::CustomRf ||
            request.source == TestToneFrequencySource::LegacyExactRf)
            require(nearly_equal(committed.actual_rf_frequency_hz, 14097123.0),
                    "managed exact-RF Test Tone must not apply WSPR offset");
        clear_direct_tone_startup_request();
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        init_default_config();
        clear_direct_tone_startup_request();
        reset_current_transmission_request_for_test();
        reset_committed_execution_route_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        config.use_ini = false;
        config.transmit = true;
        config.mode = ModeType::TONE;
        require(set_direct_tone_startup_request("14097123"),
                "CLI direct-tone restoration setup must create the production startup request");
        set_startup_quiesce_invoker_for_test(
            []()
            {
                return wsprrypi::StartupQuiesceResult{
                    false,
                    "direct-tone restoration regression inhibition"};
            });
        require(!run_startup_quiesce_gate_for_test(config) &&
                    startup_quiesce_inhibited_for_test(),
                "CLI direct-tone restoration setup must use the production startup inhibition path");
        copy_runtime_config(config, config);

        const TestToneStartResult start = start_test_tone(
            TestToneRequest{TestToneFrequencySource::WsprBand, "20m", std::nullopt});
        require(start.started,
                "CLI direct-tone restoration regression must begin the web Test Tone");
        reset_startup_quiesce_for_test();
        const TestToneStopResult stop = end_test_tone();
        const TransmissionRequest restored = current_transmission_request_for_test();
        WsprFrequencyEntry startup_entry;
        double startup_rf_hz = 0.0;
        const bool startup_request_preserved =
            try_get_direct_tone_startup_request(startup_entry, startup_rf_hz);

        require(start.started && stop.tone_was_active && stop.stopped &&
                    config.mode == ModeType::TONE &&
                    startup_request_preserved && nearly_equal(startup_rf_hz, 14097123.0) &&
                    restored.mode == TransmissionMode::TONE &&
                    nearly_equal(restored.actual_rf_frequency_hz, 14097123.0),
                "a genuine CLI direct-tone startup request must restore through the direct-tone path");
        clear_direct_tone_startup_request();
        reset_startup_quiesce_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(false);
    }

    {
        clear_direct_tone_startup_request();
        for (const std::string token : {"0.136", "0.136Hz", "14.0971001MHz@22"})
        {
            std::string error;
            require(
                !set_direct_tone_startup_request(token, &error) &&
                    error.find("whole-number frequency in Hz") != std::string::npos &&
                    !has_direct_tone_startup_request(),
                "direct CLI test tone must reject fractional-Hz values before creating startup state");
        }

        {
            std::string error;
            require(
                !set_direct_tone_startup_request("9223372036854775808", &error) &&
                    error.find("outside the supported RF range") != std::string::npos &&
                    !has_direct_tone_startup_request(),
                "direct CLI test tone must reject values outside the signed integral-Hz correlation range");
        }

        for (const std::string token : {"0.136MHz", "136kHz", "136000", "2200m", "0.136MHz@22"})
        {
            std::string error;
            require(
                set_direct_tone_startup_request(token, &error),
                "direct CLI test tone must accept inputs that resolve to integral Hz");
            WsprFrequencyEntry entry;
            double actual_rf_hz = 0.0;
            require(
                try_get_direct_tone_startup_request(entry, actual_rf_hz) &&
                    nearly_equal(actual_rf_hz, 136000.0),
                "accepted direct CLI test-tone forms must retain 136000 Hz exactly");
            if (token == "0.136MHz@22")
            {
                require(
                    entry.selector_gpio == 22,
                    "integral unit-qualified direct test-tone input must retain its selector suffix");
            }
            clear_direct_tone_startup_request();
        }
    }

    {
        FrequencyEstimateQualifier qualifier;
        SystemClockFrequencyEstimate sample;
        sample.provider_name = "chrony";
        sample.frequency_ppm = 1.25;
        sample.synchronized = true;
        sample.age_seconds = 2.0;
        sample.residual_frequency_ppm = 0.05;
        sample.skew_ppm = 0.2;
        sample.selected_source = true;
        sample.leap_normal = true;
        sample.source_provenance = "Network NTP";
        sample.source_signature = "ntp.example";
        sample.retained_source_samples = 8;
        sample.source_stability_span_seconds = 512.0;
        require(
            qualifier.evaluate(sample).qualification == FrequencyEstimateQualification::Converging &&
                qualifier.evaluate(sample).qualification == FrequencyEstimateQualification::Converging,
            "a provider estimate must remain converging until its stability window is complete");
        const SystemClockFrequencyEstimate qualified = qualifier.evaluate(sample);
        require(
            qualified.qualification == FrequencyEstimateQualification::Qualified,
            "a stable synchronized provider estimate within quality limits must qualify");
        const GpioFrequencyCorrection combined = select_gpio_frequency_correction(
            true, -0.20, 8.0, qualified);
        require(
            combined.mode == GpioCorrectionMode::QualifiedEstimate &&
                nearly_equal(combined.effective_ppm, 1.05),
            "effective GPIO PPM must equal the qualified provider estimate plus RF residual with the same sign convention");

        sample.source_signature = "PPS0,ntp.example";
        sample.source_provenance = "Mixed";
        const SystemClockFrequencyEstimate changed_source = qualifier.evaluate(sample);
        const GpioFrequencyCorrection stale_fallback = select_gpio_frequency_correction(
            true, -0.20, 8.0, changed_source);
        require(
            changed_source.qualification == FrequencyEstimateQualification::Converging &&
                stale_fallback.mode == GpioCorrectionMode::StaleEstimate &&
                nearly_equal(stale_fallback.effective_ppm, 1.05),
            "a source-composition change must reset convergence while retaining a visible last-qualified fallback");

        SystemClockFrequencyEstimate unavailable;
        unavailable.qualification = FrequencyEstimateQualification::Unavailable;
        unavailable.reason = "provider unavailable";
        const GpioFrequencyCorrection manual = select_gpio_frequency_correction(
            true, 0.3, -2.5, unavailable);
        require(
            manual.mode == GpioCorrectionMode::FixedManual &&
                nearly_equal(manual.effective_ppm, -2.5),
            "an unavailable estimate without a stale value must fall back to fixed manual GPIO correction");
        const GpioFrequencyCorrection uncalibrated = select_gpio_frequency_correction(
            true, 0.3, 0.0, unavailable);
        require(
            uncalibrated.mode == GpioCorrectionMode::Uncalibrated &&
                nearly_equal(uncalibrated.effective_ppm, 0.0),
            "an unavailable estimate with no configured fallback must use zero PPM and report uncalibrated");
    }

    {
        const std::string tracking =
            "REF,2,0,0,0,0,0,1.25,0.05,0.20,0,0,16,Normal\n";
        const PPMProviderSnapshot network = PPMManager::parseChronyReports(
            tracking,
            "^,*,ntp.example,2,6,377,4,0,0,0\n",
            "ntp.example,8,5,512,1.25,0.20,0,0\n");
        const PPMProviderSnapshot local = PPMManager::parseChronyReports(
            tracking,
            "#,*,PPS0,0,4,377,1,0,0,0\n",
            "PPS0,8,5,64,1.25,0.20,0,0\n");
        const PPMProviderSnapshot mixed = PPMManager::parseChronyReports(
            tracking,
            "#,*,PPS0,0,4,377,1,0,0,0\n^,+,ntp.example,2,6,377,4,0,0,0\n",
            "PPS0,8,5,64,1.25,0.20,0,0\nntp.example,8,5,512,1.24,0.25,0,0\n");
        const PPMProviderSnapshot reselected = PPMManager::parseChronyReports(
            tracking,
            "#,+,PPS0,0,4,377,1,0,0,0\n^,*,ntp.example,2,6,377,4,0,0,0\n",
            "PPS0,8,5,64,1.25,0.20,0,0\nntp.example,8,5,512,1.24,0.25,0,0\n");
        require(
            network.synchronized && network.source_provenance == "Network NTP" &&
                local.synchronized && local.source_provenance.find("local reference") != std::string::npos &&
                mixed.synchronized && mixed.combined_sources && mixed.source_provenance == "Mixed" &&
                network.source_signature != mixed.source_signature &&
                mixed.source_signature != reselected.source_signature,
            "fake chrony reports must distinguish network, local-reference, mixed composition, and selected-source changes");
    }

    {
        init_default_config();
        config.use_ini = true;
        config.loop_tx = false;
        config.tx_iterations.store(1, std::memory_order_release);
        shutdown_after_current_transmission.store(false, std::memory_order_release);
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();

        consume_tx_iteration_if_needed();

        require(
            config.tx_iterations.load(std::memory_order_acquire) == 0 &&
                shutdown_after_current_transmission.load(std::memory_order_acquire) &&
                !shutdown_after_wspr_plan.load(std::memory_order_acquire),
            "a finite managed-INI WSPR run must request shutdown after its final frame");

        shutdown_after_current_transmission.store(false, std::memory_order_release);
        config.tx_iterations.store(0, std::memory_order_release);
        consume_tx_iteration_if_needed();
        require(
            config.tx_iterations.load(std::memory_order_acquire) == 0 &&
                !shutdown_after_current_transmission.load(std::memory_order_acquire) &&
                !shutdown_after_wspr_plan.load(std::memory_order_acquire),
            "managed-INI TX Iterations=0 must retain unlimited scheduling semantics");

        config.loop_tx = true;
        config.tx_iterations.store(1, std::memory_order_release);
        consume_tx_iteration_if_needed();
        require(
            config.tx_iterations.load(std::memory_order_acquire) == 1 &&
                !shutdown_after_current_transmission.load(std::memory_order_acquire) &&
                !shutdown_after_wspr_plan.load(std::memory_order_acquire),
            "managed-INI Loop TX must override a finite iteration value");

        config.loop_tx = false;
        config.tx_iterations.store(1, std::memory_order_release);
        active_wspr_plan_in_progress = true;
        active_wspr_frame_index = 0;
        active_wspr_plan.frames.resize(2);
        consume_tx_iteration_if_needed();
        require(
            config.tx_iterations.load(std::memory_order_acquire) == 0 &&
                !shutdown_after_current_transmission.load(std::memory_order_acquire) &&
                shutdown_after_wspr_plan.load(std::memory_order_acquire),
            "a finite managed-INI paired WSPR run must defer shutdown until the complete plan finishes");

        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();
    }

    std::cout << "dial_frequency_semantics_test passed" << std::endl;
    return EXIT_SUCCESS;
}
