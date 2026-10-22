/**
 * @file wspr_band_lookup.cpp
 * @brief Implements WSPR frequency lookup and band correlation.
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

#include "wspr_band_lookup.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace
{
    struct BandDefinition
    {
        const char *name;
        HamBand ham_band;
        long long lower_hz;
        long long upper_hz;
        double default_wspr_hz;
    };

    struct GenericBandDefinition
    {
        const char *name;
        long long lower_hz;
        long long upper_hz;
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

    // Built-in WSPR aliases are user-facing USB dial frequencies.
    // The scheduler converts dial frequency to actual RF exactly once before
    // configuring the RF-only transmitter/backend layer.
    constexpr double FREQ_2200M = 136000.0;
    constexpr double FREQ_630M = 474200.0;
    constexpr double FREQ_160M = 1836600.0;
    constexpr double FREQ_80M = 3568600.0;
    constexpr double FREQ_60M = 5287200.0;
    constexpr double FREQ_40M = 7038600.0;
    constexpr double FREQ_30M = 10138700.0;
    constexpr double FREQ_22M = 13551500.0;
    constexpr double FREQ_20M = 14095600.0;
    constexpr double FREQ_17M = 18104600.0;
    constexpr double FREQ_15M = 21094600.0;
    constexpr double FREQ_12M = 24924600.0;
    constexpr double FREQ_10M = 28124600.0;
    constexpr double FREQ_6M = 50293000.0;
    constexpr double FREQ_4M = 70091000.0;
    constexpr double FREQ_2M = 144489000.0;

    constexpr std::array<BandDefinition, 16> BAND_DEFINITIONS = {{
        {"2200M", HamBand::BAND_2200M, 135700LL, 137800LL, FREQ_2200M},
        {"630M", HamBand::BAND_630M, 472000LL, 479000LL, FREQ_630M},
        {"160M", HamBand::BAND_160M, 1800000LL, 2000000LL, FREQ_160M},
        {"80M", HamBand::BAND_80M, 3500000LL, 3800000LL, FREQ_80M},
        {"60M", HamBand::BAND_60M, 5250000LL, 5450000LL, FREQ_60M},
        {"40M", HamBand::BAND_40M, 7000000LL, 7200000LL, FREQ_40M},
        {"30M", HamBand::BAND_30M, 10100000LL, 10150000LL, FREQ_30M},
        {"22M", HamBand::BAND_22M, 13000000LL, 13600000LL, FREQ_22M},
        {"20M", HamBand::BAND_20M, 14000000LL, 14350000LL, FREQ_20M},
        {"17M", HamBand::BAND_17M, 18068000LL, 18168000LL, FREQ_17M},
        {"15M", HamBand::BAND_15M, 21000000LL, 21450000LL, FREQ_15M},
        {"12M", HamBand::BAND_12M, 24890000LL, 24990000LL, FREQ_12M},
        {"10M", HamBand::BAND_10M, 28000000LL, 29700000LL, FREQ_10M},
        {"6M", HamBand::BAND_6M, 50000000LL, 52000000LL, FREQ_6M},
        {"4M", HamBand::BAND_4M, 70000000LL, 71000000LL, FREQ_4M},
        {"2M", HamBand::BAND_2M, 144000000LL, 148000000LL, FREQ_2M},
    }};

    constexpr std::array<GenericBandDefinition, 10> GENERIC_BAND_DEFINITIONS =
        {{
            {"1.25M", 222000000LL, 225000000LL},
            {"70CM", 420000000LL, 450000000LL},
            {"33CM", 902000000LL, 928000000LL},
            {"23CM", 1240000000LL, 1300000000LL},
            {"13CM", 2300000000LL, 2450000000LL},
            {"9CM", 3300000000LL, 3500000000LL},
            {"6CM", 5650000000LL, 5925000000LL},
            {"3CM", 10000000000LL, 10500000000LL},
            {"1.25CM", 24000000000LL, 24250000000LL},
            {"1MM", 241000000000LL, 250000000000LL},
        }};

    constexpr std::array<WSPRAliasDefinition, 18> WSPR_ALIASES = {{
        {"lf", FREQ_2200M},
        {"2200m", FREQ_2200M},
        {"mf", FREQ_630M},
        {"630m", FREQ_630M},
        {"160m", FREQ_160M},
        {"80m", FREQ_80M},
        {"60m", FREQ_60M},
        {"40m", FREQ_40M},
        {"30m", FREQ_30M},
        {"22m", FREQ_22M},
        {"20m", FREQ_20M},
        {"17m", FREQ_17M},
        {"15m", FREQ_15M},
        {"12m", FREQ_12M},
        {"10m", FREQ_10M},
        {"6m", FREQ_6M},
        {"4m", FREQ_4M},
        {"2m", FREQ_2M},
    }};

    constexpr std::array<LegacyActualWSPRAliasDefinition, 18> LEGACY_ACTUAL_WSPR_ALIASES = {{
        {"lf", 137500.0},
        {"2200m", 137500.0},
        {"mf", 475700.0},
        {"630m", 475700.0},
        {"160m", 1838100.0},
        {"80m", 3570100.0},
        {"60m", 5288700.0},
        {"40m", 7040100.0},
        {"30m", 10140200.0},
        {"22m", 13553000.0},
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

WSPRBandLookup::WSPRBandLookup()
{
    validHamFrequencies.reserve(
        BAND_DEFINITIONS.size() + GENERIC_BAND_DEFINITIONS.size());

    for (const auto &band : BAND_DEFINITIONS)
    {
        validHamFrequencies.emplace_back(
            band.lower_hz,
            band.upper_hz,
            band.name);
    }

    for (const auto &band : GENERIC_BAND_DEFINITIONS)
    {
        validHamFrequencies.emplace_back(
            band.lower_hz,
            band.upper_hz,
            band.name);
    }

    wsprFrequencies.reserve(BAND_DEFINITIONS.size() + WSPR_ALIASES.size());

    for (const auto &band : BAND_DEFINITIONS)
    {
        wsprFrequencies.emplace(normalize_key(band.name), band.default_wspr_hz);
    }

    for (const auto &alias : WSPR_ALIASES)
    {
        wsprFrequencies[normalize_key(alias.alias)] = alias.frequency_hz;
    }
}

long long WSPRBandLookup::parse_frequency_string(const std::string &freq_str) const
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

std::string WSPRBandLookup::normalize_key(const std::string &key) const
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

std::optional<HamBand> WSPRBandLookup::lookup_ham_band(long long frequency) const
{
    for (const auto &band : BAND_DEFINITIONS)
    {
        if (frequency >= band.lower_hz && frequency <= band.upper_hz)
        {
            return band.ham_band;
        }
    }

    return std::nullopt;
}

std::optional<HamBand> WSPRBandLookup::lookup_ham_band(double frequency) const
{
    return lookup_ham_band(static_cast<long long>(frequency));
}

std::string WSPRBandLookup::validate_frequency(long long frequency) const
{
    const auto ham_band = lookup_ham_band(frequency);
    if (ham_band.has_value())
    {
        return std::string(band_to_string(*ham_band));
    }

    for (const auto &range : validHamFrequencies)
    {
        if (frequency >= std::get<0>(range) &&
            frequency <= std::get<1>(range))
        {
            return std::get<2>(range);
        }
    }

    return "Invalid Frequency";
}

std::string WSPRBandLookup::freq_display_string(long long frequency) const
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

std::variant<double, std::string> WSPRBandLookup::lookup(
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
        throw std::invalid_argument("Key not found: " + normalized_key);
    }

    throw std::invalid_argument("Unsupported input type.");
}

double WSPRBandLookup::parse_string_to_frequency(
    std::string_view input,
    bool validate) const
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

    const auto result = lookup(input_str);
    if (std::holds_alternative<double>(result))
    {
        return std::get<double>(result);
    }

    throw std::invalid_argument("Invalid frequency format: " + input_str);
}

std::optional<std::string> WSPRBandLookup::legacy_actual_wspr_alias_for_frequency(
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

void WSPRBandLookup::print_wspr_frequencies() const
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
    case HamBand::BAND_22M:
        return "22m";
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
    case HamBand::BAND_6M:
        return "6m";
    case HamBand::BAND_4M:
        return "4m";
    case HamBand::BAND_2M:
        return "2m";
    }

    return "unknown";
}
