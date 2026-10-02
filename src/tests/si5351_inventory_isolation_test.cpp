#include "config_handler.hpp"
#include "arg_parser.hpp"
#include "si5351_inventory_process.hpp"
#include "web_server_config_http.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--inventory-fixture") {
        std::ofstream(argv[2]) << "started";
        std::this_thread::sleep_for(400ms);
        if (std::string(argv[3]) == "bad") std::cout << "malformed";
        else std::cout << R"({"bus":1,"addresses":[96,97],"error":""})";
        return 0;
    }
    if (const auto exit = si5351_inventory_process::helper_main(argc, argv)) return *exit;
    init_default_config();
    config.callsign = "AA0NT";
    config.grid_square = "EM18";
    config.use_ini = false;
    // Runtime simulation permits this application transaction test on portable builds.
    config.simulated_backend_override = true;
    set_raspberry_pi_generation_override_for_test(4);
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config_to_json();
    const auto [before, revision] = get_public_config_snapshot();
    assert(!before.at("Platform").at("Si5351 Inventory Checked").get<bool>());
    assert(!before.at("Platform").contains("Si5351 Detected"));
    assert(before.at("Si5351").at("I2C Address") == "0x60");
    // Invalid metadata must remain an explicit error without affecting the saved value.
    assert(web_server_routes::build_si5351_addresses_response("1junk").status == 400);
#if defined(__linux__)
    char marker[] = "/tmp/wsprrypi-inventory-test-XXXXXX";
    const int fd = mkstemp(marker);
    assert(fd >= 0);
    close(fd);
    const auto prepare = [&](const std::string& mode) {
        unlink(marker);
        set_si5351_inventory_command_override_for_test({
            "/proc/self/exe", "--inventory-fixture", marker, mode});
    };
    const auto started = [&] {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (access(marker, F_OK) != 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        assert(access(marker, F_OK) == 0);
    };
    prepare("good");
    std::string conflict;
    std::thread update([&] {
        try { patch_all_from_web_revision({{"Si5351", {{"I2C Address", "0x61"}}}}, revision); }
        catch (const std::exception& error) { conflict = error.what(); }
    });
    started();
    const auto read_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) assert(get_public_config_snapshot().second == revision);
    assert(std::chrono::steady_clock::now() - read_start < 200ms);
    const auto newer_revision = patch_all_from_web_revision({{"Meta", {{"debug_logging", true}}}}, revision);
    update.join();
    assert(conflict == "revision_conflict");
    const auto [after, after_revision] = get_public_config_snapshot();
    assert(after_revision == newer_revision && after_revision != revision);
    assert(after.at("Si5351").at("I2C Address") == "0x60");
    assert(after.at("Platform").at("Si5351 Inventory Checked").get<bool>());
    assert(after.at("Platform").at("Si5351 Detected").get<bool>());
    assert(!cached_si5351_addresses(2, config.si5351_reference_hz).checked);
    assert(!cached_si5351_addresses(1, config.si5351_reference_hz + 1).checked);
    // Cached presence cannot authorize a new selection: a fresh bad reply must reject it.
    prepare("bad");
    bool rejected = false;
    try { patch_all_from_web_revision({{"Si5351", {{"I2C Address", "0x61"}}}}, newer_revision); }
    catch (const std::exception& error) {
        rejected = std::string(error.what()).find("Invalid Si5351 discovery reply") != std::string::npos;
    }
    assert(rejected && get_public_config_snapshot().second == newer_revision);
    assert(!get_public_config_json().at("Platform").at("Si5351 Detected").get<bool>());
    clear_si5351_inventory_command_override_for_test();
    unlink(marker);
#endif
    // A prepared INI candidate must not overwrite a newer web transaction.
    char ini_path[] = "/tmp/wsprrypi-inventory-ini-XXXXXX";
    const int ini_fd = mkstemp(ini_path);
    assert(ini_fd >= 0);
    close(ini_fd);
    config.use_ini = true;
    config.ini_filename = ini_path;
    iniFile.set_filename(ini_path);
    for (const bool initial_debug_logging : {false, true}) {
        config.debug_logging = initial_debug_logging;
        config_to_json();
        json_to_ini();
        PreparedConfigCandidate candidate;
        prepare_ini_config_candidate(ini_path, candidate);
        assert(candidate.valid);
        const auto committed = patch_all_from_web_revision(
            {{"Meta", {{"debug_logging", !candidate.normalized_config.debug_logging}}}}, {});
        bool ini_conflict = false;
        try { commit_config_candidate(candidate); }
        catch (const std::exception& error) { ini_conflict = std::string(error.what()) == "revision_conflict"; }
        assert(ini_conflict && get_public_config_snapshot().second == committed);
    }
    unlink(ini_path);
    std::cout << "Si5351 snapshot isolation, fresh validation and revision conflict tests passed\n";
}
