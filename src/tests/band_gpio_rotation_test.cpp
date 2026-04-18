#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <set>
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
        for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
        {
            config.band_gpio[band_index] = BandGPIOConfig{};
        }

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
            request.hasSelectorGPIO(),
            message + ": committed request must carry selector GPIO snapshot");
        require(
            std::string(band_to_string(request.selector_band)) == expected_band,
            message + ": committed request selector band must match the selected entry");
        require(
            request.selector_gpio_config.gpio == expected_gpio,
            message + ": committed request selector GPIO must match the selected band");
        require(
            request.selector_gpio_config.active_high == expected_active_high,
            message + ": committed request selector polarity must match the selected band");
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

    void require_selector_rehydrates_from_committed_request(
        const std::string &expected_band,
        int expected_gpio,
        bool expected_active_high,
        const std::string &message)
    {
        BandGPIOConfig selector_config;
        std::string selector_band;

        stop_active_transmission_selectors_for_test();
        require(
            !current_band_gpio_selection_for_test(selector_config, selector_band),
            message + ": selector teardown precondition must clear live selector state");

        require(
            restore_committed_band_gpio_selection_for_test(true),
            message + ": committed request snapshot must restore selector state");
        require(
            current_band_gpio_selection_for_test(selector_config, selector_band),
            message + ": execution restore must recover selector from committed request snapshot");
        require(
            selector_band == expected_band,
            message + ": restored selector band must match committed request");
        require(
            selector_config.gpio == expected_gpio,
            message + ": restored selector GPIO must match committed request");
        require(
            selector_config.active_high == expected_active_high,
            message + ": restored selector polarity must match committed request");
    }

    void require_initialized_selector_set(
        const std::set<std::pair<int, bool>> &expected,
        const std::string &message)
    {
        std::set<std::pair<int, bool>> actual;
        for (const BandGPIOConfig &config_entry : initialized_selector_gpios_for_test())
        {
            actual.emplace(config_entry.gpio, config_entry.active_high);
        }

        BandGPIOConfig active_config;
        std::string active_band;
        if (current_band_gpio_selection_for_test(active_config, active_band))
        {
            actual.emplace(active_config.gpio, active_config.active_high);
        }

        require(
            actual == expected,
            message + ": initialized selector GPIO set must match expected configured GPIOs");
    }

    void require_shutdown_cleanup_targets(
        const std::set<std::pair<int, bool>> &expected,
        const std::string &message)
    {
        std::set<std::pair<int, bool>> actual;
        for (const BandGPIOConfig &config : selector_shutdown_cleanup_targets_for_test())
        {
            actual.emplace(config.gpio, config.active_high);
        }

        require(
            actual == expected,
            message + ": shutdown cleanup must cover the full configured selector GPIO set");
    }

    void require_all_selector_gpios_released(const std::string &message)
    {
        BandGPIOConfig active_config;
        std::string active_band;
        require(
            initialized_selector_gpios_for_test().empty(),
            message + ": idle selector GPIO reservations must be empty");
        require(
            !current_band_gpio_selection_for_test(active_config, active_band),
            message + ": no active selector GPIO may remain prepared");
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
    require_initialized_selector_set(
        {{17, true}, {27, false}, {22, true}},
        "first UI rotation entry");
    require_current_selector(
        "80m",
        "80m",
        17,
        true,
        "first UI rotation entry");
    require_selector_rehydrates_from_committed_request(
        "80m",
        17,
        true,
        "first UI rotation entry");

    require(set_config(false), "scheduler must commit second UI band GPIO entry");
    require_initialized_selector_set(
        {{17, true}, {27, false}, {22, true}},
        "second UI rotation entry");
    require_current_selector(
        "40m",
        "40m",
        27,
        false,
        "second UI rotation entry");
    require_selector_rehydrates_from_committed_request(
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
    require_selector_rehydrates_from_committed_request(
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
    require_selector_rehydrates_from_committed_request(
        "80m",
        17,
        true,
        "wrapped UI rotation entry");

    patch_all_from_web({
        {"WSPR", {{"Frequency", "30m,20m"}}},
        {"Band GPIO",
         {{"30m", {{"GPIO", 5}, {"Enabled", true}, {"Active High", false}}},
          {"20m", {{"GPIO", 12}, {"Enabled", true}, {"Active High", true}}}}}});

    require(set_config(true), "scheduler must commit first reloaded UI band GPIO entry");
    require_initialized_selector_set(
        {{5, false}, {12, true}},
        "first reloaded UI rotation entry");
    require_current_selector(
        "30m",
        "30m",
        5,
        false,
        "first reloaded UI rotation entry");
    require_selector_rehydrates_from_committed_request(
        "30m",
        5,
        false,
        "first reloaded UI rotation entry");

    require(set_config(false), "scheduler must commit second reloaded UI band GPIO entry");
    require_current_selector(
        "20m",
        "20m",
        12,
        true,
        "second reloaded UI rotation entry");
    require_selector_rehydrates_from_committed_request(
        "20m",
        12,
        true,
        "second reloaded UI rotation entry");

    patch_all_from_web({{"WSPR", {{"Frequency", "20m@16H,30m@20H,40m@21H"}}}});

    require(set_config(true), "scheduler must commit first explicit selector entry");
    require_initialized_selector_set(
        {{16, true}, {20, true}, {21, true}},
        "first explicit selector rotation entry");
    require_current_selector(
        "20m",
        "20m",
        16,
        true,
        "first explicit selector rotation entry");
    require_selector_rehydrates_from_committed_request(
        "20m",
        16,
        true,
        "first explicit selector rotation entry");

    require(set_config(false), "scheduler must commit second explicit selector entry");
    require_initialized_selector_set(
        {{16, true}, {20, true}, {21, true}},
        "second explicit selector rotation entry");
    require_current_selector(
        "30m",
        "30m",
        20,
        true,
        "second explicit selector rotation entry");
    require_selector_rehydrates_from_committed_request(
        "30m",
        20,
        true,
        "second explicit selector rotation entry");

    require(set_config(false), "scheduler must commit third explicit selector entry");
    require_initialized_selector_set(
        {{16, true}, {20, true}, {21, true}},
        "third explicit selector rotation entry");
    require_current_selector(
        "40m",
        "40m",
        21,
        true,
        "third explicit selector rotation entry");
    require_selector_rehydrates_from_committed_request(
        "40m",
        21,
        true,
        "third explicit selector rotation entry");

    patch_all_from_web({{"Operation", {{"Transmit", false}}}});
    require(
        set_config(true),
        "scheduler must accept disabling transmit while selector GPIOs are configured");
    require_all_selector_gpios_released(
        "disabled transmit");

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

    prime_valid_ui_config();
    patch_all_from_web({
        {"WSPR", {{"Frequency", "20m@16H,30m@20H,40m@21H"}}},
        {"Band GPIO",
         {{"20m", {{"GPIO", 5}, {"Enabled", true}, {"Active High", true}}},
          {"30m", {{"GPIO", 6}, {"Enabled", true}, {"Active High", true}}},
          {"40m", {{"GPIO", 13}, {"Enabled", true}, {"Active High", false}}}}}});
    reset_scheduler_test_state();

    require(set_config(true), "scheduler must commit first explicit selector entry");
    require_current_selector("20m@16H", "20m", 16, true, "first explicit selector entry");

    require(set_config(false), "scheduler must commit second explicit selector entry");
    require_current_selector("30m@20H", "30m", 20, true, "second explicit selector entry");
    run_final_selector_gpio_shutdown_cleanup_for_test();
    require_shutdown_cleanup_targets(
        {{16, true}, {20, true}, {21, true}, {5, true}, {6, true}, {13, false}},
        "final shutdown cleanup with explicit and band-config selectors");
    require_all_selector_gpios_released(
        "final shutdown cleanup with explicit and band-config selectors");

    patch_all_from_web({
        {"WSPR", {{"Frequency", "20m,30m"}}},
        {"Band GPIO",
         {{"20m", {{"GPIO", 17}, {"Enabled", true}, {"Active High", true}}},
          {"30m", {{"GPIO", 18}, {"Enabled", true}, {"Active High", false}}}}}});
    require(set_config(true), "scheduler must reload band GPIO selector set");
    require_current_selector("20m", "20m", 17, true, "reloaded band GPIO selector set");
    run_final_selector_gpio_shutdown_cleanup_for_test();
    require_shutdown_cleanup_targets(
        {{17, true}, {18, false}},
        "final shutdown cleanup with band-config-only selectors");
    require_all_selector_gpios_released(
        "final shutdown cleanup with band-config-only selectors");

    patch_all_from_web({
        {"WSPR", {{"Frequency", "20m@17H,30m@17L"}}},
        {"Band GPIO",
         {{"20m", {{"GPIO", -1}, {"Enabled", false}, {"Active High", true}}},
          {"30m", {{"GPIO", -1}, {"Enabled", false}, {"Active High", true}}}}}});
    require(!set_config(true), "conflicting selector polarity on the same GPIO must be rejected");

    finish_scheduler_test_state();

    std::cout << "Band GPIO rotation regression tests passed." << std::endl;
    return EXIT_SUCCESS;
}
