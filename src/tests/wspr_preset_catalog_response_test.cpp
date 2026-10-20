#include "../band_lookup.hpp"
#include "../json.hpp"
#include "../wspr_band_catalog_response.hpp"

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
    const BandLookup lookup;
    const auto response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(lookup, 1500.0));

    require(response.at("bands").size() == 17,
            "compatibility band catalog must retain one effective preset per band");
    require(response.at("frequency_profile") == "existing_common",
            "catalog response must identify the effective default profile");
    require(response.at("presets").size() == 19,
            "complete preset catalog must contain the bare catalog and two qualified presets");

    const auto &legacy = response.at("presets").at(17);
    require(legacy.at("preset") == "60m:legacy" &&
                legacy.at("band") == "60m" &&
                legacy.at("dial_frequency_hz") == 5287200 &&
                legacy.at("existing_common") == true,
            "60m:legacy response must identify the retained convention");

    const auto &wrc15 = response.at("presets").at(18);
    require(wrc15.at("preset") == "60m:wrc15" &&
                wrc15.at("band") == "60m" &&
                wrc15.at("dial_frequency_hz") == 5364700 &&
                wrc15.at("existing_common") == false,
            "60m:wrc15 response must retain separate preset and band identities");

    const auto wrc15_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(lookup, 1500.0, "wrc15"));
    require(wrc15_response.at("frequency_profile") == "wrc15" &&
                wrc15_response.at("bands").at(4).at("band") == "60m" &&
                wrc15_response.at("bands").at(4).at("dial_frequency_hz") == 5364700 &&
                wrc15_response.at("bands").at(4).at("tone_frequency_hz") == 5366200,
            "WRC-15 profile must change the effective 60m compatibility row");

    const auto preferred_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(
            lookup, 1500.0, "wrc15", {{"60m", "60m:legacy"}}));
    require(preferred_response.at("band_preferences").at("60m") == "60m:legacy" &&
                preferred_response.at("bands").at(4).at("dial_frequency_hz") == 5287200 &&
                preferred_response.at("bands").at(4).at("resolution_source") ==
                    "band_preference_preset" &&
                preferred_response.at("bands").at(4).at("preset") == "60m:legacy",
            "local preference must be visible and override the effective profile row");

    const auto configured_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(
            lookup, 1500.0, "existing_common",
            {{"8m", std::uint64_t{40680000}},
             {"5m", std::uint64_t{60000000}}}));
    require(configured_response.at("bands").size() == 19 &&
                configured_response.at("bands").at(12).at("band") == "8m" &&
                configured_response.at("bands").at(12).at("resolution_source") ==
                    "band_preference_numeric" &&
                configured_response.at("bands").at(12).at("preset").is_null() &&
                configured_response.at("bands").at(14).at("band") == "5m",
            "effective API catalog must expose configured-only bands and source metadata");

    std::cout << "WSPR preset catalog response tests passed.\n";
    return 0;
}
