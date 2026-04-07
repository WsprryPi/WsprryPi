#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "frequency_semantics.hpp"
#include "scheduling.hpp"
#include "wspr_band_lookup.hpp"

#include <cmath>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <optional>
#include <string>
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

    std::cout << "dial_frequency_semantics_test passed" << std::endl;
    return EXIT_SUCCESS;
}
