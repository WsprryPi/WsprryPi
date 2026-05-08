#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
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

    void reset_scheduler_test_state()
    {
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        reset_current_transmission_request_for_test();
        set_scheduler_execution_suppressed_for_test(true);
    }

    void finish_scheduler_test_state()
    {
        set_scheduler_execution_suppressed_for_test(false);
    }

    ArgParserConfig make_qrss_policy_config(
        const std::string &message,
        double dot_seconds,
        int repeat_minutes)
    {
        ArgParserConfig candidate;
        candidate.use_ini = false;
        candidate.mode = ModeType::QRSS;
        candidate.transmit = true;
        candidate.transmit_backend = TransmitBackendKind::GPIO;
        candidate.gpio_tx_pin = 4;
        candidate.gpio_power_level = 7;
        candidate.qrss.message = message;
        candidate.qrss.frequency_hz = 10140100.0;
        candidate.qrss.dot_seconds = dot_seconds;
        candidate.modulation_dot_seconds = dot_seconds;
        candidate.schedule_repeat_minutes = repeat_minutes;
        resolve_backend_specific_config(candidate);
        return candidate;
    }

    ArgParserConfig make_fskcw_policy_config(
        const std::string &message,
        double dot_seconds,
        int repeat_minutes)
    {
        ArgParserConfig candidate =
            make_qrss_policy_config(message, dot_seconds, repeat_minutes);
        candidate.mode = ModeType::FSKCW;
        candidate.fskcw.message = message;
        candidate.fskcw.mark_frequency_hz = 10140120.0;
        candidate.fskcw.space_frequency_hz = 10140100.0;
        candidate.fskcw.dot_seconds = dot_seconds;
        return candidate;
    }

    ArgParserConfig make_dfcw_policy_config(
        const std::string &message,
        double dot_seconds,
        int repeat_minutes)
    {
        ArgParserConfig candidate =
            make_qrss_policy_config(message, dot_seconds, repeat_minutes);
        candidate.mode = ModeType::DFCW;
        candidate.dfcw.message = message;
        candidate.dfcw.dot_frequency_hz = 10140100.0;
        candidate.dfcw.dash_frequency_hz = 10140120.0;
        candidate.dfcw.dot_seconds = dot_seconds;
        return candidate;
    }
} // namespace

int main()
{
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    set_si5351_detection_override_for_test(true);

    {
        std::chrono::nanoseconds qrss_duration{};
        std::string validation_error;

        ArgParserConfig qrss_short =
            make_qrss_policy_config("E", 59.0, 1);
        require(
            compute_non_wspr_message_duration(
                qrss_short,
                qrss_duration,
                &validation_error),
            "QRSS duration helper must succeed for a valid short message");
        require(
            qrss_duration == std::chrono::seconds(59),
            "QRSS duration helper must include the dot length for a one-symbol message");
        require(
            validate_non_wspr_repeat_interval_policy(
                qrss_short,
                &validation_error),
            "QRSS duration shorter than repeat_every must validate");

        ArgParserConfig qrss_equal =
            make_qrss_policy_config("E", 60.0, 1);
        require(
            compute_non_wspr_message_duration(
                qrss_equal,
                qrss_duration,
                &validation_error) &&
                qrss_duration == std::chrono::minutes(1),
            "QRSS duration helper must allow equality with repeat_every");
        require(
            validate_non_wspr_repeat_interval_policy(
                qrss_equal,
                &validation_error),
            "QRSS duration equal to repeat_every must validate");

        ArgParserConfig qrss_long =
            make_qrss_policy_config("E", 61.0, 1);
        require(
            !validate_non_wspr_repeat_interval_policy(
                qrss_long,
                &validation_error) &&
                validation_error.find(
                    "Configured QRSS message duration of 1m 01s exceeds repeat_every interval of 1m 00s.") !=
                    std::string::npos,
            "QRSS duration longer than repeat_every must be rejected with a clear error");

        ArgParserConfig fskcw_long =
            make_fskcw_policy_config("E", 61.0, 1);
        validation_error.clear();
        require(
            !validate_non_wspr_repeat_interval_policy(
                fskcw_long,
                &validation_error) &&
                validation_error.find("Configured FSKCW message duration") !=
                    std::string::npos,
            "FSKCW duration longer than repeat_every must be rejected");

        ArgParserConfig dfcw_long =
            make_dfcw_policy_config("E", 61.0, 1);
        validation_error.clear();
        require(
            !validate_non_wspr_repeat_interval_policy(
                dfcw_long,
                &validation_error) &&
                validation_error.find("Configured DFCW message duration") !=
                    std::string::npos,
            "DFCW duration longer than repeat_every must be rejected");

        std::chrono::nanoseconds with_word_gap{};
        std::chrono::nanoseconds without_word_gap{};
        validation_error.clear();
        require(
            compute_non_wspr_message_duration(
                make_qrss_policy_config("E E", 1.0, 10),
                with_word_gap,
                &validation_error) &&
                compute_non_wspr_message_duration(
                    make_qrss_policy_config("EE", 1.0, 10),
                    without_word_gap,
                    &validation_error) &&
                with_word_gap > without_word_gap,
            "QRSS duration helper must include inter-word spacing");

        std::chrono::nanoseconds dash_heavy{};
        std::chrono::nanoseconds dit_heavy{};
        require(
            compute_non_wspr_message_duration(
                make_qrss_policy_config("TTT", 1.0, 10),
                dash_heavy,
                &validation_error) &&
                compute_non_wspr_message_duration(
                    make_qrss_policy_config("EEE", 1.0, 10),
                    dit_heavy,
                    &validation_error) &&
                dash_heavy > dit_heavy,
            "QRSS duration helper must include dash timing");

        validation_error.clear();
        require(
            validate_non_wspr_repeat_interval_policy(
                make_qrss_policy_config("SOS", 2.0, 1),
                &validation_error) &&
                !validate_non_wspr_repeat_interval_policy(
                    make_qrss_policy_config("SOS", 5.0, 1),
                    &validation_error),
            "different unit lengths must change whether the same QRSS message passes the repeat_every policy");
    }

    {
        clear_qrss_startup_request();
        reset_scheduler_test_state();

        const ArgParserConfig candidate = make_qrss_policy_config("E", 61.0, 1);
        copy_runtime_config(candidate, config);

        require(
            !set_config(true),
            "scheduler must reject an overlong QRSS configuration before committing execution");
        require(
            current_transmission_request_for_test().actual_rf_frequency_hz == 0.0 &&
                current_transmission_request_for_test().payload.frames.empty(),
            "scheduler rejection must leave the committed execution request untouched");

        finish_scheduler_test_state();
    }

    {
        const ArgParserConfig candidate = make_qrss_policy_config("E", 61.0, 1);
        copy_runtime_config(candidate, config);

        std::string validation_error;
        require(
            !validate_config_data_for_test(&validation_error) &&
                validation_error.find("Configured QRSS message duration") !=
                    std::string::npos,
            "CLI validation must surface the repeat_every policy error");
    }

    {
        clear_qrss_startup_request();
        clear_fskcw_startup_request();
        clear_dfcw_startup_request();
        prime_valid_runtime_identity_config();
        const ModeType original_mode = config.mode;
        bool threw_patch_error = false;

        try
        {
            patch_all_from_web({
                {"Operation",
                 {{"Mode", "QRSS"},
                  {"Transmit", true}}},
                {"CW",
                 {{"Message", "E"},
                  {"Base Frequency", 10140100.0},
                  {"Dot Seconds", 61.0},
                  {"Repeat Minutes", 1}}}});
        }
        catch (const std::exception &e)
        {
            threw_patch_error = true;
            require(
                std::string(e.what()).find("Configured QRSS message duration") !=
                    std::string::npos,
                "web patch validation must expose the repeat_every policy error");
        }

        require(
            threw_patch_error,
            "web patch validation must reject an overlong QRSS configuration");
        require(
            config.mode == original_mode,
            "web patch rejection must not mutate the live mode");
    }

    std::cout << "Non-WSPR repeat policy regression tests passed." << std::endl;
    return EXIT_SUCCESS;
}
