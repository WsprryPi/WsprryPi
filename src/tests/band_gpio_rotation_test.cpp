#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"

#include <atomic>
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

    void prime_valid_ui_config()
    {
        init_config_json();
        json_to_config();

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        config.ppm = 0.0;
        config.use_ntp = false;
        config.use_offset = false;
        config.wspr.callsign = config.callsign;
        config.wspr.grid_square = config.grid_square;
        config.wspr.power_dbm = config.power_dbm;
        config.wspr.frequencies = config.frequencies;
        config.wspr.audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
        config.wspr.planner_preference = WsprPlannerPreference::Auto;
        config.wspr_planner_preference = WsprPlannerPreference::Auto;

        config_to_json();
    }

    void reset_scheduler_test_state()
    {
        ini_reload_pending.store(false, std::memory_order_relaxed);
        ppm_reload_pending.store(false, std::memory_order_relaxed);
        exiting_wspr.store(false, std::memory_order_relaxed);
        reset_managed_reload_runtime_for_test();
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(true, false);
    }

    void finish_scheduler_test_state()
    {
        set_band_gpio_selector_for_test(false, false);
        set_scheduler_execution_suppressed_for_test(false);
    }

    void require_current_selector(
        const std::string &expected_token,
        const std::string &expected_band,
        int expected_gpio,
        bool expected_active_high,
        const std::string &message)
    {
        const TransmissionRequest request = current_transmission_request_for_test();
        BandGPIOConfig selector_config;
        std::string selector_band;

        require(
            request.frequency_entry_label == expected_token,
            message + ": committed request token must match the selected entry");
        require(
            current_band_gpio_selection_for_test(selector_config, selector_band),
            message + ": scheduler must prepare a band GPIO selector");
        require(
            selector_band == expected_band,
            message + ": selector band must match the selected entry");
        require(
            selector_config.enabled,
            message + ": selector GPIO must be enabled");
        require(
            selector_config.gpio == expected_gpio,
            message + ": selector GPIO number must match the selected band");
        require(
            selector_config.active_high == expected_active_high,
            message + ": selector polarity must match the selected band");
    }
}

int main()
{
    prime_valid_ui_config();
    patch_all_from_web({
        {"WSPR", {{"Frequency", "80m,40m,30m"}}},
        {"Band GPIO",
         {{"80m", {{"GPIO", 17}, {"Enabled", true}, {"Active High", true}}},
          {"40m", {{"GPIO", 27}, {"Enabled", true}, {"Active High", false}}},
          {"30m", {{"GPIO", 22}, {"Enabled", true}, {"Active High", true}}}}}});

    require(
        !config.use_ini &&
            config.wspr_frequency_entries.size() == 3U &&
            config.wspr_frequency_entries[0].allow_band_gpio_fallback &&
            config.wspr_frequency_entries[1].allow_band_gpio_fallback &&
            config.wspr_frequency_entries[2].allow_band_gpio_fallback,
        "UI Band GPIO patches must enable band-config fallback for every parsed frequency entry");

    patch_all_from_web({{"WSPR", {{"Frequency", "80m,40m,30m"}}}});
    require(
        !config.use_ini &&
            config.wspr_frequency_entries.size() == 3U &&
            config.wspr_frequency_entries[0].allow_band_gpio_fallback &&
            config.wspr_frequency_entries[1].allow_band_gpio_fallback &&
            config.wspr_frequency_entries[2].allow_band_gpio_fallback,
        "frequency-only UI patches must preserve band-config fallback for rebuilt frequency entries");

    reset_scheduler_test_state();

    require(set_config(true), "scheduler must commit first UI band GPIO entry");
    require_current_selector(
        "80m",
        "80m",
        17,
        true,
        "first UI rotation entry");

    require(set_config(false), "scheduler must commit second UI band GPIO entry");
    require_current_selector(
        "40m",
        "40m",
        27,
        false,
        "second UI rotation entry");

    require(set_config(false), "scheduler must commit third UI band GPIO entry");
    require_current_selector(
        "30m",
        "30m",
        22,
        true,
        "third UI rotation entry");

    require(set_config(false), "scheduler must wrap back to first UI band GPIO entry");
    require_current_selector(
        "80m",
        "80m",
        17,
        true,
        "wrapped UI rotation entry");

    finish_scheduler_test_state();

    init_config_json();
    json_to_config();
    config.use_ini = true;
    config.transmit = true;
    config.frequencies = "80m,40m";
    require(
        set_frequencies(config),
        "INI frequency entries must parse before fallback checks");
    require(
        config.wspr_frequency_entries.size() == 2U &&
            config.wspr_frequency_entries[0].allow_band_gpio_fallback &&
            config.wspr_frequency_entries[1].allow_band_gpio_fallback,
        "INI entries without @GPIO must keep band-config GPIO fallback enabled");

    config.use_ini = false;
    config.frequencies = "80m@17H,40m";
    require(
        set_frequencies(config),
        "CLI mixed selector entries must parse before precedence checks");
    require(
        config.wspr_frequency_entries.size() == 2U &&
            config.wspr_frequency_entries[0].selector_gpio == 17 &&
            config.wspr_frequency_entries[0].selector_gpio_active_high &&
            !config.wspr_frequency_entries[0].allow_band_gpio_fallback &&
            config.wspr_frequency_entries[1].selector_gpio == kSelectorGpioUnset &&
            !config.wspr_frequency_entries[1].allow_band_gpio_fallback,
        "CLI @GPIO selectors must stay explicit and must not enable band-config fallback");

    std::cout << "Band GPIO rotation regression tests passed." << std::endl;
    return EXIT_SUCCESS;
}
