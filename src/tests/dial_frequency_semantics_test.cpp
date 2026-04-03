#include "config_handler.hpp"
#include "frequency_semantics.hpp"
#include "wspr_band_lookup.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
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

    bool nearly_equal(double lhs, double rhs, double epsilon = 0.01)
    {
        return std::fabs(lhs - rhs) <= epsilon;
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

    std::cout << "dial_frequency_semantics_test passed" << std::endl;
    return EXIT_SUCCESS;
}
