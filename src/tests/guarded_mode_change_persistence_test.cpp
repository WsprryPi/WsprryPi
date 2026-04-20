#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"

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
        require(in.is_open(), "test helper must read " + path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    void write_text_file(const std::string &path, const std::string &contents)
    {
        std::ofstream out(path, std::ios::trunc);
        require(out.is_open(), "test helper must write " + path);
        out << contents;
    }
} // namespace

int main()
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
        "guarded mode-change final save must persist the updated mode");
    require(
        ini_reload_generation.load(std::memory_order_relaxed) ==
            generation_before_guard + 1U,
        "guarded mode change must produce exactly one persistence generation");
    require(
        iniFile.getData().at("Operation").at("Mode") == "QRSS" &&
            iniFile.getData().at("Operation").at("Transmit") == "false",
        "guarded mode-change final save must persist the final mode and disabled transmit state");

    set_scheduler_execution_suppressed_for_test(false);
    std::cout << "guarded_mode_change_persistence_test passed" << std::endl;
    return EXIT_SUCCESS;
}
