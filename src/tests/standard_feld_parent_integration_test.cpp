#include "config_handler.hpp"
#include "scheduling.hpp"
#include "standard_feld.hpp"
#include "standard_feld_asset.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string read_source(const std::string &path)
{
    std::ifstream input(path);
    require(input.is_open(), "source-boundary test must read " + path);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

ArgParserConfig candidate(std::string message = "A")
{
    ArgParserConfig cfg;
    cfg.mode = ModeType::STANDARD_FELD;
    cfg.transmit = true;
    cfg.use_ini = false;
    cfg.transmit_backend = TransmitBackendKind::GPIO;
    cfg.gpio_tx_pin = 4;
    cfg.gpio_power_level = 7;
    cfg.ppm = 1.25;
    cfg.standard_feld.message = std::move(message);
    cfg.standard_feld.frequency_hz = 10140100.0;
    cfg.schedule_start_minute = 12;
    cfg.schedule_start_second = 34;
    cfg.schedule_repeat_minutes = 10;
    resolve_backend_specific_config(cfg);
    return cfg;
}
} // namespace

int main()
{
    set_scheduler_execution_suppressed_for_test(true);
    reset_current_controller_request_for_test();
    reset_current_transmission_request_for_test();
    reset_band_gpio_prepare_call_count_for_test();
    reset_tx_led_request_counts_for_test();
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);

    const std::string config_source = read_source("config_handler.cpp");
    const std::string arg_source = read_source("arg_parser.cpp");
    const std::string websocket_source = read_source("web_socket.cpp");
    const std::string rpi_backend_source =
        read_source("WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp");
    const std::string si5351_backend_source =
        read_source("WSPR-Transmitter/src/wspr_transmit_backend_si5351.cpp");
    require(config_source.find("if (upper == \"STANDARD_FELD\")") == std::string::npos,
            "JSON, INI, and web mode parsing must not recognize STANDARD_FELD");
    require(arg_source.find("if (value == \"STANDARD_FELD\")") == std::string::npos,
            "CLI mode parsing must not recognize STANDARD_FELD");
    require(websocket_source.find("STANDARD_FELD") == std::string::npos &&
                websocket_source.find("standard_feld_") == std::string::npos,
            "WebSocket serialization must not expose Standard Feld fields");
    require(rpi_backend_source.find("TransmissionMode::STANDARD_FELD") == std::string::npos &&
                rpi_backend_source.find(
                    "Only WSPR, QRSS, FSKCW, and DFCW execution plans are currently supported.") !=
                    std::string::npos,
            "Raspberry Pi backend must retain its mode rejection guard");
    const std::size_t si5351_rejection = si5351_backend_source.find(
        "case wsprrypi::TransmissionMode::STANDARD_FELD:");
    require(si5351_rejection != std::string::npos &&
                si5351_backend_source.find("return false;", si5351_rejection) !=
                    std::string::npos,
            "Si5351 planner mapping must explicitly reject STANDARD_FELD");

    init_default_config();
    config_to_json();
    const ArgParserConfig selection_config_before = config;
    const ModeType selection_mode_before = config.mode;
    const nlohmann::json selection_json_before = jConfig;
    bool web_selection_rejected = false;
    try
    {
        patch_all_from_web({{"Operation", {{"Mode", "STANDARD_FELD"}}}});
    }
    catch (const std::exception &)
    {
        web_selection_rejected = true;
    }
    require(web_selection_rejected && config.mode == selection_mode_before &&
                jConfig == selection_json_before,
            "web/JSON selection must reject STANDARD_FELD atomically");

    copy_runtime_config(candidate("A"), config);
    config_to_json();
    require(jConfig.at("Operation").at("Mode") != "STANDARD_FELD",
            "JSON serialization must not persist STANDARD_FELD as a selectable mode");
    copy_runtime_config(selection_config_before, config);
    jConfig = selection_json_before;

    const std::string ini_path = "/tmp/standard_feld_parent_selection.ini";
    {
        std::ofstream ini(ini_path, std::ios::trunc);
        require(ini.is_open(), "INI boundary test must create its temporary input");
        ini << "[Operation]\nMode=STANDARD_FELD\n";
    }
    bool ini_selection_rejected = false;
    try
    {
        ini_to_json(ini_path);
        json_to_config();
    }
    catch (const std::exception &)
    {
        ini_selection_rejected = true;
    }
    require(ini_selection_rejected && config.mode != ModeType::STANDARD_FELD,
            "INI selection must not activate STANDARD_FELD");
    copy_runtime_config(selection_config_before, config);
    jConfig = selection_json_before;

    const ArgParserConfig defaults;
    require(defaults.standard_feld.message.empty(), "Standard Feld message default must be empty");
    require(defaults.standard_feld.frequency_hz == 0.0, "Standard Feld carrier default must be inactive");
    require(defaults.standard_feld.profile_id == wsprrypi::standard_feld::kProfileId,
            "Standard Feld profile default must be fixed v1 identity");

    ArgParserConfig original = candidate("a  b ");
    ArgParserConfig copied(original);
    ArgParserConfig assigned;
    assigned = original;
    require(copied.mode == ModeType::STANDARD_FELD && assigned.standard_feld.message == "a  b ",
            "copy and assignment must preserve the distinct Standard Feld model");

    std::chrono::nanoseconds duration{};
    std::string error;
    require(compute_non_wspr_message_duration(candidate("A"), duration, &error) &&
                duration == std::chrono::milliseconds(1200),
            "single-character compiled duration must be exactly 1.2 seconds");
    require(compute_non_wspr_message_duration(
                candidate("HELL TEST 0123456789 DE WSPRY WSPRY 73"), duration, &error) &&
                duration == std::chrono::seconds(16),
            "interoperability corpus compiled duration must be exactly 16 seconds");
    require(compute_non_wspr_message_duration(candidate(" A  B "), duration, &error) &&
                duration == std::chrono::milliseconds(3200),
            "leading, trailing, and repeated spaces must be preserved as cells");

    std::string repertoire;
    for (unsigned char c = 0x20; c <= 0x5f; ++c)
        repertoire.push_back(static_cast<char>(c));
    require(validate_standard_feld_candidate(candidate(repertoire), &error),
            "the complete frozen repertoire must validate");
    require(validate_standard_feld_candidate(candidate("abcdefghijklmnopqrstuvwxyz"), &error),
            "ASCII lowercase must validate through compiler normalization");
    ArgParserConfig parent_candidate = candidate("A");
    require(validate_config_candidate(parent_candidate, &error),
            "parent candidate validation must accept the internal mode without Morse or backend requirements");

    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()})
    {
        ArgParserConfig bad = candidate();
        bad.standard_feld.frequency_hz = invalid;
        require(!validate_standard_feld_candidate(bad, &error) &&
                    error.find("Standard Feld") != std::string::npos,
                "invalid and non-finite carriers must be rejected clearly");
    }
    for (const std::string &bad_message : {std::string{}, std::string("`"),
                                            std::string("\x1f", 1), std::string("\xc3\xa9", 2)})
    {
        require(!validate_standard_feld_candidate(candidate(bad_message), &error),
                "empty and unsupported input classes must be rejected without substitution");
    }
    for (unsigned int byte = 0; byte <= 0xff; ++byte)
    {
        if ((byte >= 0x20 && byte <= 0x5f) ||
            (byte >= static_cast<unsigned int>('a') &&
             byte <= static_cast<unsigned int>('z')))
            continue;
        const std::string unsupported(1, static_cast<char>(byte));
        require(!validate_standard_feld_candidate(candidate(unsupported), &error),
                "every unsupported byte class must be rejected atomically");
    }
    ArgParserConfig bad_profile = candidate();
    bad_profile.standard_feld.profile_id = "other";
    require(!validate_standard_feld_candidate(bad_profile, &error),
            "unsupported profile identity must be rejected");

    ArgParserConfig exact = candidate(std::string(148, 'A'));
    exact.schedule_repeat_minutes = 1;
    require(validate_standard_feld_candidate(exact, &error),
            "compiled duration equal to repeat interval must be accepted");
    exact.standard_feld.message.push_back('A');
    require(!validate_standard_feld_candidate(exact, &error) &&
                error.find("Shorten the message or increase repeat_every") != std::string::npos &&
                error.find("unit length") == std::string::npos,
            "repeat overflow must use Standard Feld-specific corrective advice");

    const auto now = std::chrono::system_clock::from_time_t(0);
    ArgParserConfig scheduled = candidate();
    scheduled.schedule_start_minute = 1;
    scheduled.schedule_start_second = 2;
    scheduled.schedule_repeat_minutes = 10;
    const auto next = next_non_wspr_schedule_time_for_test(scheduled, now);
    require(std::chrono::system_clock::to_time_t(next) == 62,
            "Standard Feld must reuse absolute start-minute/start-second scheduling");

    const std::uint64_t generation_before_rejection = non_wspr_schedule_generation_for_test();
    const ArgParserConfig config_before_rejection = config;
    const nlohmann::json json_before_rejection = jConfig;
    require(!commit_standard_feld_request_for_test(exact, &error) &&
                non_wspr_schedule_generation_for_test() == generation_before_rejection,
            "invalid candidates must not commit or advance scheduler generation");
    require(!current_controller_request_for_test().has_value() &&
                band_gpio_prepare_call_count_for_test() == 0,
            "invalid candidates must not prepare selector GPIO or commit requests");
    require(config.mode == config_before_rejection.mode &&
                config.standard_feld.message == config_before_rejection.standard_feld.message &&
                jConfig == json_before_rejection,
            "failed Standard Feld validation must not mutate or persist active configuration");

    ArgParserConfig committed_source = candidate("a  b ");
    copy_runtime_config(committed_source, config);
    const WsprRuntimeStatusSnapshot unavailable_before_commit =
        current_tx_runtime_status_snapshot();
    require(!unavailable_before_commit.standard_feld_available &&
                !unavailable_before_commit.standard_feld_total_physical_positions.has_value(),
            "Standard Feld plan and progress status must be unavailable before commitment");
    const std::uint64_t generation_before_commit = non_wspr_schedule_generation_for_test();
    require(commit_standard_feld_request_for_test(committed_source, &error),
            "valid Standard Feld request must commit under execution suppression");
    require(non_wspr_schedule_generation_for_test() == generation_before_commit + 1,
            "valid suppressed commitment must advance scheduler generation once");
    const auto request = current_controller_request_for_test();
    require(request.has_value() && request->mode == wsprrypi::TransmissionMode::STANDARD_FELD &&
                committed_execution_route_for_test() ==
                    CommittedExecutionRouteForTest::CONTROLLER_STANDARD_FELD,
            "committed request must retain first-class Standard Feld identity");
    const auto payload = std::get<wsprrypi::StandardFeldPayload>(request->payload);
    require(payload.message == "a  b " && payload.frequency_hz == 10140100.0 &&
                payload.profile_id == wsprrypi::standard_feld::kProfileId &&
                request->output.gpio == 4 && request->calibration.ppm == 1.25 &&
                request->slot.start_time > std::chrono::system_clock::now() &&
                request->policy.allow_truncation_on_stop &&
                !request->policy.allow_quantization &&
                !request->policy.allow_backend_approximation &&
                request->metadata.label == "standard-feld" &&
                request->metadata.origin == "internal-standard-feld-scheduler",
            "request must snapshot payload, output, calibration, policy, and metadata");

    config.standard_feld.message = "MUTATED";
    config.standard_feld.frequency_hz = 1.0;
    config.ppm = 99.0;
    const auto stable = current_controller_request_for_test();
    require(std::get<wsprrypi::StandardFeldPayload>(stable->payload).message == "a  b " &&
                stable->calibration.ppm == 1.25,
            "committed request must not read mutable source configuration");

    const WsprRuntimeStatusSnapshot status = current_tx_runtime_status_snapshot();
    require(status.standard_feld_available &&
                status.standard_feld_profile_id == wsprrypi::standard_feld::kProfileId &&
                status.standard_feld_asset_id == wsprrypi::standard_feld::kAssetId &&
                status.standard_feld_raw_character_count == 5 &&
                status.standard_feld_total_physical_positions == 686 &&
                status.standard_feld_total_duration == std::chrono::milliseconds(2800) &&
                !status.standard_feld_current_absolute_position.has_value(),
            "status must expose immutable plan identity/totals and explicit unavailable progress");

    ArgParserConfig invalid_after_commit = committed_source;
    invalid_after_commit.standard_feld.message = "`";
    const std::uint64_t generation_before_atomic_rejection =
        non_wspr_schedule_generation_for_test();
    require(!commit_standard_feld_request_for_test(invalid_after_commit, &error) &&
                non_wspr_schedule_generation_for_test() ==
                    generation_before_atomic_rejection &&
                current_controller_request_for_test().has_value() &&
                std::get<wsprrypi::StandardFeldPayload>(
                    current_controller_request_for_test()->payload).message == "a  b " &&
                current_tx_runtime_status_snapshot().standard_feld_total_physical_positions == 686,
            "failed replacement validation must preserve the immutable committed request and status");
    require(band_gpio_prepare_call_count_for_test() == 0 &&
                tx_led_assert_request_count_for_test() == 0 &&
                tx_led_deassert_request_count_for_test() == 0,
            "suppressed commitment must not touch selector, LED, amplifier, or backend paths");

    const nlohmann::json persisted_before_stop = jConfig;
    config.use_ini = true;
    const StopTransmissionResult stopped = stop_transmission_by_user_request();
    require(stopped.transmission_active && stopped.stop_performed && !stopped.persisted &&
                !current_controller_request_for_test().has_value(),
            "suppressed Standard Feld request must cancel without persistence");
    require(jConfig == persisted_before_stop,
            "suppressed cancellation must not alter persisted configuration state");
    const WsprRuntimeStatusSnapshot cancelled = current_tx_runtime_status_snapshot();
    require(!cancelled.standard_feld_available &&
                cancelled.standard_feld_terminal_reason == "cancelled" &&
                cancelled.frequency_hz == 0.0 &&
                cancelled.standard_feld_profile_id.empty() &&
                !cancelled.standard_feld_total_physical_positions.has_value(),
            "cancelled status must be terminal without fabricating physical latency");

    reset_current_controller_request_for_test();
    const WsprRuntimeStatusSnapshot reset_status = current_tx_runtime_status_snapshot();
    require(reset_status.standard_feld_terminal_reason.empty() &&
                !reset_status.standard_feld_available &&
                !reset_status.standard_feld_total_duration.has_value(),
            "explicit reset must clear terminal and compiled Standard Feld source status");
    set_scheduler_execution_suppressed_for_test(false);
    require(!commit_standard_feld_request_for_test(candidate(), &error),
            "Standard Feld commitment must remain unreachable without execution suppression");
    set_patch_all_from_web_runtime_apply_suppressed_for_test(false);

    std::cout << "Standard Feld parent integration tests passed.\n";
    return EXIT_SUCCESS;
}
