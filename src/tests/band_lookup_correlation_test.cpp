#include "../band_lookup.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    struct ExpectedBand
    {
        HamBand band;
        double lower_hz;
        double upper_hz;
    };

    constexpr std::array<ExpectedBand, 19> expected{{
        {HamBand::BAND_2200M, 130000, 190000},
        {HamBand::BAND_630M, 472000, 479000},
        {HamBand::BAND_160M, 1800000, 2000000},
        {HamBand::BAND_80M, 3500000, 4000000},
        {HamBand::BAND_60M, 5250000, 5450000},
        {HamBand::BAND_40M, 7000000, 7300000},
        {HamBand::BAND_30M, 10100000, 10150000},
        {HamBand::BAND_20M, 14000000, 14350000},
        {HamBand::BAND_17M, 18068000, 18168000},
        {HamBand::BAND_15M, 21000000, 21450000},
        {HamBand::BAND_12M, 24890000, 24990000},
        {HamBand::BAND_10M, 28000000, 29700000},
        {HamBand::BAND_8M, 40000000, 45000000},
        {HamBand::BAND_6M, 50000000, 54000000},
        {HamBand::BAND_5M, 54000001, 68000000},
        {HamBand::BAND_4M, 69900000, 70500000},
        {HamBand::BAND_2M, 144000000, 148000000},
        {HamBand::BAND_1_25M, 219000000, 225000000},
        {HamBand::BAND_70CM, 420000000, 450000000},
    }};

    BandLookup lookup;
    for (const auto &entry : expected)
    {
        require(lookup.lookup_ham_band(entry.lower_hz) == entry.band,
                "lower edge must correlate to its band");
        require(lookup.lookup_ham_band(entry.upper_hz) == entry.band,
                "upper edge must correlate to its band");
    }

    require(lookup.lookup_ham_band(54000000LL) == HamBand::BAND_6M,
            "the shared 54 MHz edge must correlate deterministically to 6m");
    require(lookup.lookup_ham_band(54000001LL) == HamBand::BAND_5M,
            "the first integral hertz above the shared edge must correlate to 5m");
    require(!lookup.lookup_ham_band(13560000LL).has_value(),
            "the former 22m frequency must remain outside the catalog");
    require(!lookup.lookup_ham_band(450000001LL).has_value(),
            "frequencies above 70cm must remain outside the catalog");

    const auto presets = lookup.complete_wspr_preset_catalog();
    require(presets.size() == 19,
            "complete WSPR preset catalog must contain 17 bare presets and two qualified 60m presets");
    require(lookup.parse_string_to_frequency("60m") == 5287200.0 &&
                lookup.parse_string_to_frequency("60m:legacy") == 5287200.0,
            "bare 60m and 60m:legacy must retain the existing convention");
    require(lookup.parse_string_to_frequency("60M:WRC15") == 5364700.0,
            "60m:wrc15 must resolve case-insensitively to 5,364,700 Hz");
    require(lookup.parse_string_to_frequency("60m", false, "wrc15") == 5364700.0,
            "the WRC-15 profile must change only the bare 60m convenience default");
    const std::unordered_map<std::string, std::string> preferences = {
        {"60m", "60m:legacy"}};
    require(lookup.parse_string_to_frequency("60m", false, "wrc15", preferences) == 5287200.0,
            "local preference must take precedence over the selected profile");
    require(lookup.parse_string_to_frequency("60m:wrc15", false, "existing_common", preferences) == 5364700.0,
            "explicit qualified presets must take precedence over local preferences");
    const WsprBandPreferences typed_preferences = {
        {"60m", std::string("60m:legacy")},
        {"20m", std::uint64_t{14095650}}};
    const auto custom_resolution = lookup.resolve_wspr_band_frequency(
        "20m", "existing_common", typed_preferences);
    require(custom_resolution && custom_resolution->band == "20m" &&
                custom_resolution->dial_frequency_hz == 14095650 &&
                custom_resolution->custom && !custom_resolution->preset,
            "typed numeric preference must resolve as a custom same-band dial frequency");
    const auto preset_resolution = lookup.resolve_wspr_band_frequency(
        "60m", "wrc15", typed_preferences);
    require(preset_resolution && preset_resolution->dial_frequency_hz == 5287200 &&
                !preset_resolution->custom &&
                preset_resolution->preset == std::optional<std::string>("60m:legacy"),
            "typed preset preference must precede the selected profile");
    const auto profile_resolution = lookup.resolve_wspr_band_frequency(
        "60m", "wrc15", {});
    require(profile_resolution &&
                profile_resolution->dial_frequency_hz == 5364700 &&
                profile_resolution->preset ==
                    std::optional<std::string>("60m:wrc15"),
            "shared resolver must retain the selected profile preset identity");
    const auto operational_preferences =
        preset_only_band_preferences(typed_preferences);
    require(operational_preferences.size() == 1 &&
                operational_preferences.at("60m") == "60m:legacy",
            "slice-one runtime projection must omit custom numeric preferences");
    bool rejected_cross_band_custom = false;
    try
    {
        (void)lookup.resolve_wspr_band_frequency(
            "20m", "existing_common",
            WsprBandPreferences{{"20m", std::uint64_t{7038600}}});
    }
    catch (const std::invalid_argument &)
    {
        rejected_cross_band_custom = true;
    }
    require(rejected_cross_band_custom,
            "typed custom preference must remain inside its canonical band");
    const WsprBandPreferences configured_only = {
        {"8m", std::uint64_t{40680000}},
        {"5m", std::uint64_t{60000000}}};
    const auto expanded_catalog = lookup.canonical_wspr_band_catalog(
        "existing_common", configured_only);
    require(expanded_catalog.size() == 19 &&
                expanded_catalog.at(12).band == "8m" &&
                expanded_catalog.at(12).dial_frequency_hz == 40680000 &&
                expanded_catalog.at(12).resolution_source ==
                    WsprBandResolutionSource::BandPreferenceNumeric &&
                expanded_catalog.at(14).band == "5m" &&
                expanded_catalog.at(14).dial_frequency_hz == 60000000,
            "configured-only 8m and 5m preferences must join the effective catalog");
    require(lookup.parse_string_to_frequency("60m:legacy", false, "wrc15") == 5287200.0 &&
                lookup.parse_string_to_frequency("5.2872MHz", false, "wrc15") == 5287200.0 &&
                lookup.parse_string_to_frequency("20m", false, "wrc15") == 14095600.0,
            "qualified, numeric, and unaffected-band inputs must ignore the selected profile");
    require(presets.at(18).preset == "60m:wrc15" &&
                presets.at(18).band == "60m" &&
                !presets.at(18).existing_common,
            "qualified preset identity must remain separate from correlated band identity");

    std::cout << "Band lookup correlation tests passed.\n";
    return 0;
}
