#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "gpio_output.hpp"
#include "scheduling.hpp"
#include "version.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <utility>

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

    std::set<std::pair<int, bool>> selector_set(
        const std::vector<BandGPIOConfig> &configs)
    {
        std::set<std::pair<int, bool>> result;
        for (const BandGPIOConfig &config : configs)
        {
            result.emplace(config.gpio, config.active_high);
        }
        return result;
    }

    void require_no_active_or_idle_selectors(const std::string &message)
    {
        BandGPIOConfig active_config;
        std::string active_band;
        require(
            initialized_selector_gpios_for_test().empty(),
            message + ": idle selector reservations must be empty");
        require(
            !current_band_gpio_selection_for_test(active_config, active_band),
            message + ": active selector must be cleared");
    }

    void prime_base_config()
    {
        init_config_json();
        json_to_config();

        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = false;
        config.callsign = "AA0NT";
        config.grid_square = "EM18";
        config.power_dbm = 20;
        config.frequencies = "20m";
        config.tx_pin = 4;
        set_raspberry_pi_generation_override_for_test(4);
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
        GPIOOutput::setTestMode(true);
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(false, false);
    }
}

int main()
{
    prime_base_config();
    patch_all_from_web({
        {"WSPR", {{"Frequency", "20m@21H,30m"}}},
        {"Band GPIO",
         {{"20m", {{"GPIO", -1}, {"Enabled", false}, {"Active High", true}}},
          {"30m", {{"GPIO", 22}, {"Enabled", true}, {"Active High", false}}}}}});

    set_band_gpio_selector_for_test(true, false);
    seed_selector_shutdown_state_for_test(
        BandGPIOConfig{.gpio = 21, .enabled = true, .active_high = true},
        std::vector<BandGPIOConfig>{
            BandGPIOConfig{.gpio = 22, .enabled = true, .active_high = false}});
    require(
        park_active_transmission_selectors_for_test(),
        "runtime selector teardown must park the active GPIO back into the idle pool");
    require(
        selector_set(initialized_selector_gpios_for_test()) ==
            std::set<std::pair<int, bool>>{{21, true}, {22, false}},
        "runtime selector teardown must keep every configured selector GPIO actively tracked");
    {
        BandGPIOConfig active_config;
        std::string active_band;
    require(
        !current_band_gpio_selection_for_test(active_config, active_band),
            "runtime selector teardown must clear the active selector after parking");
    }
    set_band_gpio_selector_for_test(false, false);

    prime_base_config();
    patch_all_from_web({
        {"WSPR", {{"Frequency", "20m@16H,30m@20H,40m"}}},
        {"Band GPIO",
         {{"20m", {{"GPIO", 5}, {"Enabled", true}, {"Active High", true}}},
          {"30m", {{"GPIO", 6}, {"Enabled", true}, {"Active High", true}}},
          {"40m", {{"GPIO", 13}, {"Enabled", true}, {"Active High", false}}}}}});

    seed_selector_shutdown_state_for_test(
        BandGPIOConfig{.gpio = 21, .enabled = true, .active_high = true},
        std::vector<BandGPIOConfig>{
            BandGPIOConfig{.gpio = 22, .enabled = true, .active_high = false}});
    run_final_selector_gpio_shutdown_cleanup_for_test();

    require(
        selector_set(selector_shutdown_cleanup_targets_for_test()) ==
            std::set<std::pair<int, bool>>{
                {5, true},
                {6, true},
                {13, false},
                {16, true},
                {20, true},
                {21, true},
                {22, false}},
        "shutdown cleanup must cover band-config selectors, explicit @GPIO selectors, active selector, and idle selectors");
    require_no_active_or_idle_selectors(
        "shutdown cleanup after mixed selector sources");

    prime_base_config();
    patch_all_from_web({
        {"WSPR", {{"Frequency", "20m,30m"}}},
        {"Band GPIO",
         {{"20m", {{"GPIO", 17}, {"Enabled", true}, {"Active High", true}}},
          {"30m", {{"GPIO", 18}, {"Enabled", true}, {"Active High", false}}}}}});

    seed_selector_shutdown_state_for_test(BandGPIOConfig{}, {});
    run_final_selector_gpio_shutdown_cleanup_for_test();

    require(
        selector_set(selector_shutdown_cleanup_targets_for_test()) ==
            std::set<std::pair<int, bool>>{{17, true}, {18, false}},
        "shutdown cleanup must cover band-config-only selectors");
    require_no_active_or_idle_selectors(
        "shutdown cleanup after band-config-only selectors");

    std::cout << "Selector shutdown cleanup regression tests passed." << std::endl;
    set_scheduler_execution_suppressed_for_test(false);
    GPIOOutput::setTestMode(false);
    clear_raspberry_pi_generation_override_for_test();
    return EXIT_SUCCESS;
}
