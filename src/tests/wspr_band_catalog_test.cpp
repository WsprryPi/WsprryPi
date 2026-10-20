#include "../wspr_band_catalog_response.hpp"
#include "../band_lookup.hpp"
#include "../config_handler.hpp"
#include "../version.hpp"

#include <cmath>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "../json.hpp"

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

    struct ExpectedBand
    {
        const char *name;
        std::int64_t dial_frequency_hz;
    };

    const std::vector<ExpectedBand> expected_bands = {
        {"2200m", 136000}, {"630m", 474200}, {"160m", 1836600},
        {"80m", 3568600}, {"60m", 5287200}, {"40m", 7038600},
        {"30m", 10138700}, {"20m", 14095600},
        {"17m", 18104600}, {"15m", 21094600}, {"12m", 24924600},
        {"10m", 28124600}, {"6m", 50293000}, {"4m", 70091000},
        {"2m", 144489000}, {"1.25m", 222100000}, {"70cm", 432300000},
    };

    void require_catalog_response(
        const nlohmann::json &response,
        std::int64_t expected_offset_hz)
    {
        require(response.at("command") == "wspr_band_catalog",
                "catalog response must identify wspr_band_catalog");
        require(response.at("status") == "ok",
                "catalog response must report success");
        require(response.at("audio_offset_hz").is_number_integer(),
                "audio_offset_hz must be integral JSON");
        require(response.at("audio_offset_hz") == expected_offset_hz,
                "catalog response must report the requested audio offset");
        require(response.at("bands").is_array(),
                "catalog response must contain a bands array");
        require(response.at("bands").size() == expected_bands.size(),
                "catalog response must contain exactly the canonical display bands");

        for (std::size_t index = 0; index < expected_bands.size(); ++index)
        {
            const auto &actual = response.at("bands").at(index);
            const auto &expected = expected_bands.at(index);
            require(actual.at("band") == expected.name,
                    "catalog response order must use canonical display bands");
            require(actual.at("dial_frequency_hz").is_number_integer() &&
                        actual.at("tone_frequency_hz").is_number_integer(),
                    "catalog response frequencies must be integral JSON");
            require(actual.at("dial_frequency_hz") == expected.dial_frequency_hz,
                    std::string(expected.name) +
                        " dial frequency must match the backend definition");
            require(actual.at("tone_frequency_hz") ==
                        expected.dial_frequency_hz + expected_offset_hz,
                    std::string(expected.name) +
                        " tone frequency must apply the offset exactly once");
        }
    }

    void require_rejected_offset(
        const BandLookup &lookup,
        double offset_hz,
        const std::string &description)
    {
        bool rejected = false;
        try
        {
            (void)build_wspr_band_catalog_response_json(lookup, offset_hz);
        }
        catch (const std::runtime_error &)
        {
            rejected = true;
        }
        require(rejected, "catalog response must reject " + description);
    }
}

int main()
{
    BandLookup lookup;
    const auto catalog = lookup.canonical_wspr_band_catalog();
    require(catalog.size() == expected_bands.size(),
            "lookup catalog must expose exactly 17 canonical WSPR display bands");
    for (std::size_t index = 0; index < expected_bands.size(); ++index)
    {
        require(catalog.at(index).band == expected_bands.at(index).name,
                "lookup catalog must preserve canonical display order");
        require(catalog.at(index).dial_frequency_hz ==
                    static_cast<std::uint64_t>(expected_bands.at(index).dial_frequency_hz),
                "lookup catalog dial frequency must match the authoritative definition");
    }
    for (const auto &entry : catalog)
    {
        require(entry.band != "lf" && entry.band != "mf",
                "lookup catalog must expose only authoritative WSPR display rows");
    }

    const auto presets = lookup.complete_wspr_preset_catalog();
    require(presets.size() == expected_bands.size() + 2,
            "complete preset catalog must add two qualified 60m identities");
    require(
        presets.at(expected_bands.size()).preset == "60m:legacy" &&
            presets.at(expected_bands.size()).band == "60m" &&
            presets.at(expected_bands.size()).dial_frequency_hz == 5287200 &&
            presets.at(expected_bands.size()).existing_common,
        "60m:legacy must explicitly identify the retained bare-alias convention");
    require(
        presets.at(expected_bands.size() + 1).preset == "60m:wrc15" &&
            presets.at(expected_bands.size() + 1).band == "60m" &&
            presets.at(expected_bands.size() + 1).dial_frequency_hz == 5364700 &&
            !presets.at(expected_bands.size() + 1).existing_common,
        "60m:wrc15 must expose the alternate sourced WSPR convention");
    require(
        lookup.parse_string_to_frequency("60m") == 5287200.0 &&
            lookup.parse_string_to_frequency("60M:LEGACY") == 5287200.0 &&
            lookup.parse_string_to_frequency("60m:wrc15") == 5364700.0,
        "bare and qualified 60m presets must resolve case-insensitively without changing compatibility");

    require(
        lookup.lookup_ham_band(223500000.0) ==
            std::optional<HamBand>(HamBand::BAND_1_25M) &&
            lookup.parse_string_to_frequency("223.5MHz") == 223500000.0 &&
            std::get<std::string>(lookup.lookup(223500000.0)) == "1.25m",
        "numeric 1.25 m WSPR input must resolve to the canonical HamBand");
    require(
        lookup.lookup_ham_band(219000000.0) ==
                std::optional<HamBand>(HamBand::BAND_1_25M) &&
            lookup.lookup_ham_band(225000000.0) ==
                std::optional<HamBand>(HamBand::BAND_1_25M) &&
            !lookup.lookup_ham_band(218999999.0).has_value() &&
            !lookup.lookup_ham_band(225000001.0).has_value(),
        "1.25 m lookup must use the worldwide 219-225 MHz correlation envelope");
    require(
        lookup.lookup_ham_band(435000000.0) ==
            std::optional<HamBand>(HamBand::BAND_70CM) &&
            lookup.parse_string_to_frequency("435000000") == 435000000.0 &&
            std::get<std::string>(lookup.lookup(435000000.0)) == "70cm",
        "numeric 70 cm WSPR input must resolve to the canonical HamBand");
    require(
        lookup.lookup_ham_band(420000000.0) ==
                std::optional<HamBand>(HamBand::BAND_70CM) &&
            lookup.lookup_ham_band(450000000.0) ==
                std::optional<HamBand>(HamBand::BAND_70CM) &&
            !lookup.lookup_ham_band(419999999.0).has_value() &&
            !lookup.lookup_ham_band(450000001.0).has_value(),
        "70 cm lookup must cover the inclusive 420-450 MHz allocation");

    require(
        lookup.parse_string_to_frequency("1.25m") == 222100000.0 &&
            lookup.parse_string_to_frequency("70cm") == 432300000.0,
        "authoritative 1.25 m and 70 cm WSPR aliases must resolve to their dial frequencies");

    require(std::get<double>(lookup.lookup(std::string("lf"))) == 136000.0,
            "lf lookup alias must remain accepted");
    require(std::get<double>(lookup.lookup(std::string("mf"))) == 474200.0,
            "mf lookup alias must remain accepted");
    bool rejected_22m = false;
    try { (void)lookup.parse_string_to_frequency("22m"); }
    catch (const std::invalid_argument &) { rejected_22m = true; }
    require(rejected_22m, "22m must not remain a canonical WSPR alias");

    struct CorrelationEdge
    {
        HamBand band;
        long long lower_hz;
        long long upper_hz;
    };
    constexpr std::array<CorrelationEdge, HAM_BAND_COUNT> edges{{
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
    for (const auto &edge : edges)
    {
        require(lookup.lookup_ham_band(edge.lower_hz) == edge.band,
                std::string(band_to_string(edge.band)) + " lower edge must be inclusive");
        require(lookup.lookup_ham_band(edge.upper_hz) == edge.band,
                std::string(band_to_string(edge.band)) + " upper edge must be inclusive");
    }
    require(!lookup.lookup_ham_band(13551500LL).has_value(),
            "former 22m frequency must be outside the catalog");
    require(!lookup.lookup_ham_band(902000000LL).has_value(),
            "bands above 70cm must be outside the catalog");

    const auto default_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(lookup, 1500.0));
    require_catalog_response(default_response, 1500);
    require(default_response.at("bands").at(7).at("tone_frequency_hz") == 14097100,
            "20m default catalog tone frequency must be 14,097,100 Hz");
    require(default_response.at("presets").size() == expected_bands.size() + 2,
            "catalog response must expose the separate complete preset catalog");
    require(
        default_response.at("presets").at(expected_bands.size() + 1).at("preset") ==
                "60m:wrc15" &&
            default_response.at("presets").at(expected_bands.size() + 1).at("band") ==
                "60m" &&
            default_response.at("presets").at(expected_bands.size() + 1).at("dial_frequency_hz") ==
                5364700,
        "catalog response must keep preset identity separate from correlated band identity");

    const auto zero_offset_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(lookup, 0.0));
    require_catalog_response(zero_offset_response, 0);

    const auto non_default_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(lookup, 2750.0));
    require_catalog_response(non_default_response, 2750);
    require(non_default_response.at("bands").at(7).at("tone_frequency_hz") == 14098350,
            "non-default offset must be reflected exactly once in the catalog tone frequency");

    require_rejected_offset(lookup, -1500.0, "negative integral offsets");
    require_rejected_offset(lookup, -1500.5, "negative fractional offsets");
    require_rejected_offset(lookup, 1500.5, "positive fractional offsets");
    require_rejected_offset(
        lookup,
        std::numeric_limits<double>::quiet_NaN(),
        "NaN offsets");
    require_rejected_offset(
        lookup,
        std::numeric_limits<double>::infinity(),
        "positive infinite offsets");
    require_rejected_offset(
        lookup,
        -std::numeric_limits<double>::infinity(),
        "negative infinite offsets");
    require_rejected_offset(
        lookup,
        static_cast<double>(std::numeric_limits<std::int64_t>::max()),
        "offsets outside the safely representable integral range");
    require_rejected_offset(
        lookup,
        std::nextafter(
            static_cast<double>(std::numeric_limits<std::int64_t>::max()),
            0.0),
        "offsets that overflow derived tone frequencies");

    const std::int64_t highest_dial_frequency_hz =
        static_cast<std::int64_t>(catalog.back().dial_frequency_hz);
    const std::int64_t mathematical_safe_offset_limit =
        std::numeric_limits<std::int64_t>::max() - highest_dial_frequency_hz;
    const auto offset_is_accepted = [&lookup](double offset_hz) {
        try
        {
            (void)build_wspr_band_catalog_response_json(lookup, offset_hz);
            return true;
        }
        catch (const std::runtime_error &)
        {
            return false;
        }
    };

    // The integer boundary may round either direction when represented as a
    // double.  Move toward zero only if the initial representation is rejected,
    // then advance to the greatest adjacent representable accepted value.
    constexpr int kMaximumBoundarySearchSteps = 4;
    double nearby_safe_offset =
        static_cast<double>(mathematical_safe_offset_limit);
    int downward_steps = 0;
    while (!offset_is_accepted(nearby_safe_offset) &&
           downward_steps < kMaximumBoundarySearchSteps)
    {
        nearby_safe_offset = std::nextafter(nearby_safe_offset, 0.0);
        ++downward_steps;
    }
    require(offset_is_accepted(nearby_safe_offset),
            "boundary search must find an accepted representable catalog offset");

    int upward_steps = 0;
    for (;;)
    {
        const double next_offset = std::nextafter(
            nearby_safe_offset,
            std::numeric_limits<double>::infinity());
        if (!offset_is_accepted(next_offset))
        {
            break;
        }
        require(upward_steps < kMaximumBoundarySearchSteps,
                "boundary search must reach the greatest accepted offset within four ULP steps");
        nearby_safe_offset = next_offset;
        ++upward_steps;
    }

    require(std::isfinite(nearby_safe_offset) &&
                std::trunc(nearby_safe_offset) == nearby_safe_offset &&
                nearby_safe_offset >= 0.0 &&
                nearby_safe_offset <
                    static_cast<double>(std::numeric_limits<std::int64_t>::max()),
            "nearby safe offset must satisfy the production integral conversion contract");
    const std::int64_t nearby_safe_offset_hz =
        static_cast<std::int64_t>(nearby_safe_offset);
    require(nearby_safe_offset_hz <= mathematical_safe_offset_limit,
            "nearby safe offset must not overflow the highest catalog dial frequency");
    const auto nearby_safe_response = nlohmann::json::parse(
        build_wspr_band_catalog_response_json(lookup, nearby_safe_offset));
    require_catalog_response(nearby_safe_response, nearby_safe_offset_hz);
    const auto &highest_band = nearby_safe_response.at("bands").back();
    require(highest_band.at("tone_frequency_hz") <=
                std::numeric_limits<std::int64_t>::max() &&
                highest_band.at("tone_frequency_hz") ==
                    highest_dial_frequency_hz + nearby_safe_offset_hz,
            "nearby safe offset must produce an exact in-range tone for the highest band");
    const double unsafe_offset = std::nextafter(
        nearby_safe_offset,
        std::numeric_limits<double>::infinity());
    require(unsafe_offset > nearby_safe_offset &&
                std::nextafter(
                    nearby_safe_offset,
                    std::numeric_limits<double>::infinity()) == unsafe_offset,
            "unsafe offset must be the immediate representable neighbor above the safe offset");
    require(std::isfinite(unsafe_offset) && unsafe_offset >= 0.0 &&
                std::trunc(unsafe_offset) == unsafe_offset &&
                unsafe_offset <
                    static_cast<double>(std::numeric_limits<std::int64_t>::max()),
            "adjacent unsafe offset must reach tone addition rather than another validation failure");
    require_rejected_offset(
        lookup,
        unsafe_offset,
        "the immediate representable offset above the tone-addition limit");

    const ArgParserConfig original_config = config;
    const nlohmann::json original_json = jConfig;
    const auto same_band_gpio = [](const auto &left, const auto &right)
    {
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (left[index].gpio != right[index].gpio ||
                left[index].enabled != right[index].enabled ||
                left[index].active_high != right[index].active_high)
            {
                return false;
            }
        }
        return true;
    };
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    set_raspberry_pi_generation_override_for_test(4);
    init_default_config();
    config.use_ini = false;
    config_to_json();
    const TestTonePlanningConfigSnapshot default_snapshot =
        current_test_tone_planning_config_snapshot();
    require(current_wspr_audio_offset_hz() == WSPR_AUDIO_OFFSET_HZ,
            "default configuration must publish the default catalog offset");
    require(default_snapshot.transmit_backend == config.transmit_backend &&
                default_snapshot.wspr_audio_offset_hz == WSPR_AUDIO_OFFSET_HZ &&
                default_snapshot.wspr_frequency_entries.size() ==
                    config.wspr_frequency_entries.size() &&
                same_band_gpio(default_snapshot.band_gpio, config.band_gpio),
            "default configuration must publish a coherent Test Tone planning snapshot");

    PreparedConfigCandidate reloaded_candidate;
    reloaded_candidate.valid = true;
    reloaded_candidate.normalized_config = config;
    reloaded_candidate.normalized_config.wspr_audio_offset_hz = 2750.0;
    reloaded_candidate.normalized_config.wspr.audio_offset_hz = 2750.0;
    reloaded_candidate.normalized_config.wspr_frequency_entries = {
        {"20m", 14095600.0, 17, true, false}};
    reloaded_candidate.normalized_config.band_gpio[
        ham_band_index(HamBand::BAND_20M)] = {19, true, true};
    reloaded_candidate.normalized_json = jConfig;
    commit_config_candidate(reloaded_candidate);
    const TestTonePlanningConfigSnapshot reloaded_snapshot =
        current_test_tone_planning_config_snapshot();
    require(current_wspr_audio_offset_hz() == 2750.0,
            "accepted reload configuration must publish its catalog offset");
    require(reloaded_snapshot.transmit_backend ==
                    reloaded_candidate.normalized_config.transmit_backend &&
                reloaded_snapshot.wspr_audio_offset_hz == 2750.0 &&
                reloaded_snapshot.wspr_frequency_entries.size() == 1U &&
                reloaded_snapshot.wspr_frequency_entries.front().selector_gpio == 17 &&
                reloaded_snapshot.band_gpio[ham_band_index(HamBand::BAND_20M)].gpio == 19 &&
                reloaded_snapshot.band_gpio[ham_band_index(HamBand::BAND_20M)].active_high &&
                current_wspr_audio_offset_hz() ==
                    reloaded_snapshot.wspr_audio_offset_hz,
            "accepted reload must publish offset, entries, and Band GPIO together");
    require_catalog_response(
        nlohmann::json::parse(build_wspr_band_catalog_response_json(
            lookup,
            current_wspr_audio_offset_hz())),
        2750);

    ArgParserConfig runtime_copy_source = config;
    runtime_copy_source.wspr_audio_offset_hz = 3000.0;
    runtime_copy_source.wspr.audio_offset_hz = 3000.0;
    runtime_copy_source.wspr_frequency_entries = {
        {"40m", 7038600.0, 5, false, false}};
    runtime_copy_source.band_gpio[ham_band_index(HamBand::BAND_40M)] =
        {6, true, false};
    ArgParserConfig local_copy_target = config;
    copy_runtime_config(runtime_copy_source, local_copy_target);
    require(local_copy_target.wspr.audio_offset_hz == 3000.0 &&
                current_wspr_audio_offset_hz() == 2750.0,
            "non-global runtime copies must not publish the catalog offset snapshot");
    require(current_test_tone_planning_config_snapshot().wspr_frequency_entries.front().token == "20m",
            "non-global runtime copies must not publish Test Tone planning entries");
    copy_runtime_config(runtime_copy_source, config);
    TestTonePlanningConfigSnapshot runtime_snapshot =
        current_test_tone_planning_config_snapshot();
    require(config.wspr.audio_offset_hz == 3000.0 &&
                current_wspr_audio_offset_hz() == 3000.0,
            "global runtime copies must publish the catalog offset snapshot");
    require(runtime_snapshot.transmit_backend == runtime_copy_source.transmit_backend &&
                runtime_snapshot.wspr_audio_offset_hz == 3000.0 &&
                runtime_snapshot.wspr_frequency_entries.size() == 1U &&
                runtime_snapshot.wspr_frequency_entries.front().token == "40m" &&
                runtime_snapshot.band_gpio[ham_band_index(HamBand::BAND_40M)].gpio == 6,
            "global runtime copies must publish one coherent Test Tone planning snapshot");
    runtime_snapshot.wspr_frequency_entries.front().token = "mutated-copy";
    runtime_snapshot.band_gpio[ham_band_index(HamBand::BAND_40M)].gpio = 27;
    const TestTonePlanningConfigSnapshot independent_snapshot =
        current_test_tone_planning_config_snapshot();
    require(independent_snapshot.wspr_frequency_entries.front().token == "40m" &&
                independent_snapshot.band_gpio[ham_band_index(HamBand::BAND_40M)].gpio == 6,
            "Test Tone planning snapshots must be independent value copies");

    const nlohmann::json before_rejected_patch = jConfig;
    const TestTonePlanningConfigSnapshot before_rejected_snapshot =
        current_test_tone_planning_config_snapshot();
    bool rejected_web_update = false;
    try
    {
        patch_all_from_web({{"CW", {{"Start Second", 60}}}});
    }
    catch (const std::exception &)
    {
        rejected_web_update = true;
    }
    require(rejected_web_update,
            "invalid CW start seconds must be rejected before web configuration commit");
    require(config.wspr.audio_offset_hz == 3000.0 &&
                current_wspr_audio_offset_hz() == 3000.0 &&
                jConfig == before_rejected_patch,
            "rejected web updates must leave the published catalog offset unchanged");
    const TestTonePlanningConfigSnapshot after_rejected_snapshot =
        current_test_tone_planning_config_snapshot();
    require(after_rejected_snapshot.transmit_backend ==
                    before_rejected_snapshot.transmit_backend &&
                after_rejected_snapshot.wspr_audio_offset_hz ==
                    before_rejected_snapshot.wspr_audio_offset_hz &&
                after_rejected_snapshot.wspr_frequency_entries.front().token ==
                    before_rejected_snapshot.wspr_frequency_entries.front().token &&
                after_rejected_snapshot.band_gpio[ham_band_index(HamBand::BAND_40M)].gpio ==
                    before_rejected_snapshot.band_gpio[ham_band_index(HamBand::BAND_40M)].gpio,
            "rejected web updates must leave the entire Test Tone planning snapshot unchanged");

    patch_all_from_web({{"WSPR", {{"Frequency", "20m"}}}});
    const TestTonePlanningConfigSnapshot web_snapshot =
        current_test_tone_planning_config_snapshot();
    require(current_wspr_audio_offset_hz() == WSPR_AUDIO_OFFSET_HZ &&
                web_snapshot.wspr_audio_offset_hz == WSPR_AUDIO_OFFSET_HZ,
            "accepted web configuration update must republish the catalog offset");
    copy_runtime_config(original_config, config);
    jConfig = original_json;
    require(current_wspr_audio_offset_hz() == original_config.wspr.audio_offset_hz,
            "test cleanup must restore the published catalog offset snapshot");
    clear_raspberry_pi_generation_override_for_test();
    set_patch_all_from_web_runtime_apply_suppressed_for_test(false);

    std::cout << "wspr band catalog test passed" << std::endl;
    return EXIT_SUCCESS;
}
