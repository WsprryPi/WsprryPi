/**
 * @file band_lookup.cpp
 * @brief Implements band correlation and WSPR frequency conveniences.
 *
 * This file centralizes amateur band edge buckets used for feature
 * correlation, such as LPF GPIO selection, and default WSPR dial frequencies used
 * when a user selects a band name or alias. Band edge definitions in this file
 * are intended for feature correlation and convenience, not legal enforcement.
 *
 * This project is licensed under the MIT License. See LICENSE.md for more
 * information.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "band_lookup.hpp"
#include "Band-Lookup/include/amateur_band_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace
{
    struct CanonicalWsprBandDefinition
    {
        const char *name;
        double default_wspr_hz;
    };

    struct WSPRAliasDefinition
    {
        const char *alias;
        double frequency_hz;
    };

    struct LegacyActualWSPRAliasDefinition
    {
        const char *alias;
        double actual_frequency_hz;
    };

    struct QualifiedWsprPresetDefinition
    {
        const char *preset;
        const char *band;
        double dial_frequency_hz;
        bool existing_common;
    };

    // Built-in WSPR aliases are user-facing USB dial frequencies.
    // The scheduler converts dial frequency to actual RF exactly once before
    // configuring the RF-only transmitter/backend layer.
    constexpr double FREQ_2200M = 136000.0;
    constexpr double FREQ_630M = 474200.0;
    constexpr double FREQ_160M = 1836600.0;
    constexpr double FREQ_80M = 3568600.0;
    constexpr double FREQ_60M = 5287200.0;
    constexpr double FREQ_60M_WRC15 = 5364700.0;
    constexpr double FREQ_40M = 7038600.0;
    constexpr double FREQ_30M = 10138700.0;
    constexpr double FREQ_20M = 14095600.0;
    constexpr double FREQ_17M = 18104600.0;
    constexpr double FREQ_15M = 21094600.0;
    constexpr double FREQ_12M = 24924600.0;
    constexpr double FREQ_10M = 28124600.0;
    constexpr double FREQ_6M = 50293000.0;
    constexpr double FREQ_4M = 70091000.0;
    constexpr double FREQ_2M = 144489000.0;
    constexpr double FREQ_1_25M = 222100000.0;
    constexpr double FREQ_70CM = 432300000.0;

    // A canonical HamBand does not imply an authoritative WSPR dial-frequency
    // alias.  This table contains only the project's established WSPR defaults.
    constexpr std::array<CanonicalWsprBandDefinition, 17>
        CANONICAL_WSPR_BAND_DEFINITIONS = {{
            {"2200M", FREQ_2200M},
            {"630M", FREQ_630M},
            {"160M", FREQ_160M},
            {"80M", FREQ_80M},
            {"60M", FREQ_60M},
            {"40M", FREQ_40M},
            {"30M", FREQ_30M},
            {"20M", FREQ_20M},
            {"17M", FREQ_17M},
            {"15M", FREQ_15M},
            {"12M", FREQ_12M},
            {"10M", FREQ_10M},
            {"6M", FREQ_6M},
            {"4M", FREQ_4M},
            {"2M", FREQ_2M},
            {"1.25M", FREQ_1_25M},
            {"70CM", FREQ_70CM},
        }};

    constexpr std::array<WSPRAliasDefinition, 19> WSPR_ALIASES = {{
        {"lf", FREQ_2200M},
        {"2200m", FREQ_2200M},
        {"mf", FREQ_630M},
        {"630m", FREQ_630M},
        {"160m", FREQ_160M},
        {"80m", FREQ_80M},
        {"60m", FREQ_60M},
        {"40m", FREQ_40M},
        {"30m", FREQ_30M},
        {"20m", FREQ_20M},
        {"17m", FREQ_17M},
        {"15m", FREQ_15M},
        {"12m", FREQ_12M},
        {"10m", FREQ_10M},
        {"6m", FREQ_6M},
        {"4m", FREQ_4M},
        {"2m", FREQ_2M},
        {"1.25m", FREQ_1_25M},
        {"70cm", FREQ_70CM},
    }};

    // Qualified identities are WSPR conveniences, not new amateur bands or
    // assertions of operating authority. The bare 60m alias deliberately
    // retains the existing 5.2872 MHz behavior.
    constexpr std::array<QualifiedWsprPresetDefinition, 2>
        QUALIFIED_WSPR_PRESETS = {{
            {"60m:legacy", "60m", FREQ_60M, true},
            {"60m:wrc15", "60m", FREQ_60M_WRC15, false},
        }};

    constexpr std::array<LegacyActualWSPRAliasDefinition, 17> LEGACY_ACTUAL_WSPR_ALIASES = {{
        {"lf", 137500.0},
        {"2200m", 137500.0},
        {"mf", 475700.0},
        {"630m", 475700.0},
        {"160m", 1838100.0},
        {"80m", 3570100.0},
        {"60m", 5288700.0},
        {"40m", 7040100.0},
        {"30m", 10140200.0},
        {"20m", 14097100.0},
        {"17m", 18106100.0},
        {"15m", 21096100.0},
        {"12m", 24926100.0},
        {"10m", 28126100.0},
        {"6m", 50294500.0},
        {"4m", 70092500.0},
        {"2m", 144490500.0},
    }};
}

std::unordered_map<std::string, std::string> preset_only_band_preferences(
    const WsprBandPreferences &preferences)
{
    std::unordered_map<std::string, std::string> result;
    for (const auto &[band, value] : preferences)
        if (const auto *preset = std::get_if<std::string>(&value))
            result.emplace(band, *preset);
    return result;
}

BandLookup::BandLookup()
{
    wsprFrequencies.reserve(
        CANONICAL_WSPR_BAND_DEFINITIONS.size() + WSPR_ALIASES.size());

    for (const auto &band : CANONICAL_WSPR_BAND_DEFINITIONS)
    {
        wsprFrequencies.emplace(normalize_key(band.name), band.default_wspr_hz);
    }

    for (const auto &alias : WSPR_ALIASES)
    {
        wsprFrequencies[normalize_key(alias.alias)] = alias.frequency_hz;
    }

    for (const auto &preset : QUALIFIED_WSPR_PRESETS)
    {
        wsprFrequencies[normalize_key(preset.preset)] = preset.dial_frequency_hz;
    }
}

std::vector<WsprBandCatalogEntry>
BandLookup::canonical_wspr_band_catalog(
    std::string_view frequency_profile,
    const WsprBandPreferences &band_preferences) const
{
    std::vector<WsprBandCatalogEntry> catalog;
    catalog.reserve(wsprrypi::bands::catalog.size());
    for (const auto &band : wsprrypi::bands::catalog)
    {
        const auto resolution = resolve_wspr_band_frequency(
            band.canonical_name, frequency_profile, band_preferences);
        if (resolution)
            catalog.push_back({resolution->band,
                               resolution->dial_frequency_hz,
                               resolution->source,
                               resolution->preset});
    }

    return catalog;
}

std::vector<WsprPresetCatalogEntry>
BandLookup::complete_wspr_preset_catalog() const
{
    std::vector<WsprPresetCatalogEntry> catalog;
    catalog.reserve(
        CANONICAL_WSPR_BAND_DEFINITIONS.size() + QUALIFIED_WSPR_PRESETS.size());

    for (const auto &band : CANONICAL_WSPR_BAND_DEFINITIONS)
    {
        std::string canonical_band = normalize_key(band.name);
        catalog.push_back({
            canonical_band,
            canonical_band,
            static_cast<std::uint64_t>(band.default_wspr_hz),
            true});
    }

    for (const auto &preset : QUALIFIED_WSPR_PRESETS)
    {
        catalog.push_back({
            preset.preset,
            preset.band,
            static_cast<std::uint64_t>(preset.dial_frequency_hz),
            preset.existing_common});
    }

    return catalog;
}

std::optional<WsprBandFrequencyResolution>
BandLookup::resolve_wspr_band_frequency(
    std::string_view canonical_band,
    std::string_view frequency_profile,
    const WsprBandPreferences &band_preferences) const
{
    const std::string band = normalize_key(std::string(canonical_band));
    const auto preference = band_preferences.find(band);
    if (preference != band_preferences.end())
    {
        if (const auto *custom = std::get_if<std::uint64_t>(&preference->second))
        {
            const auto correlated = lookup_ham_band(static_cast<double>(*custom));
            if (!correlated || ham_band_to_string(*correlated) != band)
                throw std::invalid_argument(
                    "WSPR band preference for " + band +
                    " must resolve within that band.");
            return WsprBandFrequencyResolution{
                band, *custom, std::nullopt, true,
                WsprBandResolutionSource::BandPreferenceNumeric};
        }

        const std::string &preset = std::get<std::string>(preference->second);
        const double resolved = parse_string_to_frequency(preset, false);
        const auto correlated = lookup_ham_band(resolved);
        if (!correlated || ham_band_to_string(*correlated) != band)
            throw std::invalid_argument(
                "WSPR band preference for " + band +
                " must resolve within that band.");
        return WsprBandFrequencyResolution{
            band,
            static_cast<std::uint64_t>(resolved),
            normalize_key(preset),
            false,
            WsprBandResolutionSource::BandPreferencePreset};
    }

    std::string normalized_profile = normalize_key(std::string(frequency_profile));
    std::replace(normalized_profile.begin(), normalized_profile.end(), '-', '_');
    for (const auto &entry : CANONICAL_WSPR_BAND_DEFINITIONS)
        if (normalize_key(entry.name) == band)
        {
            const bool profile_preset =
                band == "60m" && normalized_profile == "wrc15";
            return WsprBandFrequencyResolution{
                band,
                static_cast<std::uint64_t>(
                    profile_preset ? FREQ_60M_WRC15 : entry.default_wspr_hz),
                profile_preset
                    ? std::optional<std::string>("60m:wrc15")
                    : std::optional<std::string>(band),
                false,
                profile_preset
                    ? WsprBandResolutionSource::ProfilePreset
                    : WsprBandResolutionSource::BuiltInPreset};
        }
    return std::nullopt;
}

long long BandLookup::parse_frequency_string(const std::string &freq_str) const
{
    const std::regex pattern(
        R"(^\s*([\d\.]+)\s*(GHz|MHz|kHz|Hz)?\s*$)",
        std::regex_constants::icase);
    std::smatch match;

    if (std::regex_match(freq_str, match, pattern))
    {
        const double value = std::stod(match[1].str());
        std::string unit = match[2].str();
        std::transform(
            unit.begin(),
            unit.end(),
            unit.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (unit == "ghz")
            return static_cast<long long>(value * 1e9);
        if (unit == "mhz")
            return static_cast<long long>(value * 1e6);
        if (unit == "khz")
            return static_cast<long long>(value * 1e3);
        if (unit == "hz")
            return static_cast<long long>(value);

        return static_cast<long long>(value);
    }

    throw std::invalid_argument("Invalid frequency format: " + freq_str);
}

std::string BandLookup::normalize_key(const std::string &key) const
{
    std::string lower_key = key;
    std::transform(
        lower_key.begin(),
        lower_key.end(),
        lower_key.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    return lower_key;
}

std::optional<HamBand> BandLookup::lookup_ham_band(long long frequency) const
{
    const auto index = wsprrypi::bands::find_index(static_cast<double>(frequency));
    return index ? std::optional<HamBand>(static_cast<HamBand>(*index)) : std::nullopt;
}

std::optional<HamBand> BandLookup::lookup_ham_band(double frequency) const
{
    return lookup_ham_band(static_cast<long long>(frequency));
}

std::string BandLookup::validate_frequency(long long frequency) const
{
    const auto ham_band = lookup_ham_band(frequency);
    if (ham_band.has_value())
    {
        return std::string(band_to_string(*ham_band));
    }

    return "Invalid Frequency";
}

std::string BandLookup::freq_display_string(long long frequency) const
{
    std::ostringstream ss;

    if (frequency >= 1000000000LL)
    {
        ss << std::fixed << std::setprecision(9)
           << (static_cast<double>(frequency) / 1e9) << " GHz";
    }
    else if (frequency >= 1000000LL)
    {
        ss << std::fixed << std::setprecision(6)
           << (static_cast<double>(frequency) / 1e6) << " MHz";
    }
    else if (frequency >= 1000LL)
    {
        ss << std::fixed << std::setprecision(3)
           << (static_cast<double>(frequency) / 1e3) << " kHz";
    }
    else
    {
        ss << frequency << " Hz";
    }

    return ss.str();
}

std::variant<double, std::string> BandLookup::lookup(
    const std::variant<std::string, double, int> &input) const
{
    if (std::holds_alternative<double>(input))
    {
        return validate_frequency(static_cast<long long>(std::get<double>(input)));
    }

    if (std::holds_alternative<int>(input))
    {
        return validate_frequency(static_cast<long long>(std::get<int>(input)));
    }

    if (std::holds_alternative<std::string>(input))
    {
        const std::string normalized_key = normalize_key(std::get<std::string>(input));
        const auto it = wsprFrequencies.find(normalized_key);
        if (it != wsprFrequencies.end())
        {
            return it->second;
        }
        if (normalized_key == "22m")
        {
            throw std::invalid_argument(
                "Unsupported band: 22m is not in the amateur correlation catalog");
        }
        throw std::invalid_argument("Key not found: " + normalized_key);
    }

    throw std::invalid_argument("Unsupported input type.");
}

double BandLookup::parse_string_to_frequency(
    std::string_view input,
    bool validate,
    std::string_view frequency_profile,
    const std::unordered_map<std::string, std::string> &band_preferences) const
{
    std::string input_str(input);

    auto first = input_str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
    {
        input_str.clear();
    }
    else
    {
        auto last = input_str.find_last_not_of(" \t\n\r");
        input_str = input_str.substr(first, last - first + 1);
    }

    std::string lower = input_str;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    double scale = 1.0;

    if (lower.size() >= 3 && lower.ends_with("ghz"))
    {
        scale = 1e9;
        input_str.erase(input_str.size() - 3);
    }
    else if (lower.size() >= 3 && lower.ends_with("mhz"))
    {
        scale = 1e6;
        input_str.erase(input_str.size() - 3);
    }
    else if (lower.size() >= 3 && lower.ends_with("khz"))
    {
        scale = 1e3;
        input_str.erase(input_str.size() - 3);
    }
    else if (lower.size() >= 2 && lower.ends_with("hz"))
    {
        scale = 1.0;
        input_str.erase(input_str.size() - 2);
    }

    if (input_str.find_first_not_of("0123456789.-") == std::string::npos)
    {
        try
        {
            const double raw_freq = std::stod(input_str) * scale;
            if (raw_freq == 0.0)
            {
                return 0.0;
            }

            if (validate && !lookup_ham_band(raw_freq).has_value())
            {
                const std::string band = validate_frequency(
                    static_cast<long long>(raw_freq));
                if (band == "Invalid Frequency")
                {
                    throw std::invalid_argument(
                        "Frequency does not match known bands: " + input_str);
                }
            }

            return raw_freq;
        }
        catch (const std::exception &)
        {
            throw std::invalid_argument(
                "Invalid frequency format: " + input_str);
        }
    }

    std::string preset_key = lower;
    const auto preference = band_preferences.find(preset_key);
    if (preference != band_preferences.end())
        preset_key = normalize_key(preference->second);
    std::string normalized_profile = normalize_key(std::string(frequency_profile));
    std::replace(normalized_profile.begin(), normalized_profile.end(), '-', '_');
    if (preset_key == "60m" && normalized_profile == "wrc15")
        preset_key = "60m:wrc15";

    const auto result = lookup(preset_key);
    if (std::holds_alternative<double>(result))
    {
        return std::get<double>(result);
    }

    throw std::invalid_argument("Invalid frequency format: " + input_str);
}

std::optional<std::string> BandLookup::legacy_actual_wspr_alias_for_frequency(
    double frequency) const
{
    for (const auto &alias : LEGACY_ACTUAL_WSPR_ALIASES)
    {
        if (std::fabs(alias.actual_frequency_hz - frequency) <= 0.5)
        {
            return std::string(alias.alias);
        }
    }

    return std::nullopt;
}

void BandLookup::print_wspr_frequencies() const
{
    for (const auto &entry : wsprFrequencies)
    {
        std::cout << entry.first << " -> "
                  << freq_display_string(static_cast<long long>(entry.second))
                  << std::endl;
    }
}

const char *ham_band_to_string(HamBand band)
{
    switch (band)
    {
    case HamBand::BAND_2200M:
        return "2200m";
    case HamBand::BAND_630M:
        return "630m";
    case HamBand::BAND_160M:
        return "160m";
    case HamBand::BAND_80M:
        return "80m";
    case HamBand::BAND_60M:
        return "60m";
    case HamBand::BAND_40M:
        return "40m";
    case HamBand::BAND_30M:
        return "30m";
    case HamBand::BAND_20M:
        return "20m";
    case HamBand::BAND_17M:
        return "17m";
    case HamBand::BAND_15M:
        return "15m";
    case HamBand::BAND_12M:
        return "12m";
    case HamBand::BAND_10M:
        return "10m";
    case HamBand::BAND_8M:
        return "8m";
    case HamBand::BAND_6M:
        return "6m";
    case HamBand::BAND_5M:
        return "5m";
    case HamBand::BAND_4M:
        return "4m";
    case HamBand::BAND_2M:
        return "2m";
    case HamBand::BAND_1_25M:
        return "1.25m";
    case HamBand::BAND_70CM:
        return "70cm";
    }

    return "unknown";
}
