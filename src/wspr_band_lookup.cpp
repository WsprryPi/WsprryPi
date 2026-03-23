/**
 * @file wspr_band_lookup.cpp
 * @brief A C++ class designed to translate Hx to a higher order like Mhz,
 *        and back to Hz, validate frequencies, and translate terms like
 *        "20m" to a valid WSPR frequency.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
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
#include <sstream>
#include <iomanip>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <regex>

namespace
{
    constexpr double FREQ_2200M = 137500.0;
    constexpr double FREQ_2200M_15 = 137612.5;

    constexpr double FREQ_630M = 475700.0;
    constexpr double FREQ_630M_15 = 475812.5;
}

/**
 * @brief Constructs the WSPRBandLookup object and initializes frequency data.
 *
 * This constructor initializes:
 * - `validHamFrequencies`: A predefined set of ham radio frequency ranges,
 *   mapped to their respective band names.
 * - `wsprFrequencies`: A map of common WSPR band names to their corresponding
 *   transmission frequencies in Hz.
 *
 * Additionally, it normalizes all WSPR band names to lowercase to ensure
 * case-insensitive lookups.
 */
WSPRBandLookup::WSPRBandLookup()
    : validHamFrequencies({{135700, 137800, "2200M"},
                           {472000, 479000, "630M"},
                           {1800000, 2000000, "160M"},
                           {3500000, 4000000, "80M"},
                           {5332000, 5405000, "60M (Channelized)"},
                           {7000000, 7300000, "40M"},
                           {10100000, 10150000, "30M"},
                           {14000000, 14350000, "20M"},
                           {13000000, 13600000, "22M"},
                           {18068000, 18168000, "17M"},
                           {21000000, 21450000, "15M"},
                           {24890000, 24990000, "12M"},
                           {28000000, 29700000, "10M"},
                           {50000000, 54000000, "6M"},
                           {144000000, 148000000, "2M"},
                           {222000000, 225000000, "1.25M"},
                           {420000000, 450000000, "70CM"},
                           {902000000, 928000000, "33CM"},
                           {1240000000, 1300000000, "23CM"},
                           {2300000000, 2450000000, "13CM"},
                           {3300000000, 3500000000, "9CM"},
                           {5650000000, 5925000000, "6CM"},
                           {10000000000, 10500000000, "3CM"},
                           {24000000000, 24250000000, "1.2CM"},
                           {47000000000, 47200000000, "6MM"},
                           {75500000000, 81000000000, "4MM"},
                           {122250000000, 123000000000, "2.5MM"},
                           {134000000000, 141000000000, "2MM"},
                           {241000000000, 250000000000, "1MM"}})
{
    // Initialize WSPR band frequencies
    wsprFrequencies = {
        {"lf", FREQ_2200M},
        {"2200m", FREQ_2200M},
        {"lf-15", FREQ_2200M_15},
        {"2200m-15", FREQ_2200M_15},
        {"mf", FREQ_630M},
        {"630m", FREQ_630M},
        {"mf-15", FREQ_630M_15},
        {"630m-15", FREQ_630M_15},
        {"160m", 1838100},
        {"160m-15", 1838212.5},
        {"80m", 3570100},
        {"60m", 5288700},
        {"40m", 7040100},
        {"30m", 10140200},
        {"22m", 13553000},
        {"20m", 14097100},
        {"17m", 18106100},
        {"15m", 21096100},
        {"12m", 24926100},
        {"10m", 28126100},
        {"6m", 50294500},
        {"4m", 70092500},
        {"2m", 14449050}};

    // Normalize keys to lowercase for case-insensitive lookups
    std::unordered_map<std::string, double> normalized_table;
    for (const auto &entry : wsprFrequencies)
    {
        normalized_table[normalize_key(entry.first)] = entry.second;
    }
    wsprFrequencies = std::move(normalized_table);
}

/**
 * @brief Parses a frequency string and converts it to Hz.
 *
 * This function extracts the numeric value and unit (GHz, MHz, kHz, Hz) from a
 * frequency string and converts it into a `long long` value representing the
 * frequency in Hz.
 *
 * @param freq_str The frequency string (e.g., "7.040 MHz", "10 GHz", "475.812 kHz").
 *
 * @return The frequency value in Hz.
 *
 * @throws std::invalid_argument If the input format is incorrect or unrecognized.
 */
long long WSPRBandLookup::parse_frequency_string(const std::string &freq_str) const
{
    // Regular expression to extract a numeric value and an optional unit (GHz, MHz, kHz, Hz)
    std::regex pattern(R"(^\s*([\d\.]+)\s*(GHz|MHz|kHz|Hz)?\s*$)", std::regex_constants::icase);
    std::smatch match;

    // Check if the input string matches the expected format
    if (std::regex_match(freq_str, match, pattern))
    {
        // Convert the extracted numeric part to a double
        double value = std::stod(match[1].str());
        std::string unit = match[2].str();
        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower); // Convert unit to lowercase

        // Convert based on the unit
        if (unit == "ghz")
            return static_cast<long long>(value * 1e9);
        if (unit == "mhz")
            return static_cast<long long>(value * 1e6);
        if (unit == "khz")
            return static_cast<long long>(value * 1e3);
        if (unit == "hz")
            return static_cast<long long>(value);

        // If no unit is provided, assume Hz
        return static_cast<long long>(value);
    }

    // Throw an exception if the input format is invalid
    throw std::invalid_argument("Invalid frequency format: " + freq_str);
}

/**
 * @brief Normalizes a band name key by converting it to lowercase.
 *
 * This function ensures that all band names are case-insensitive by
 * transforming them into lowercase. It is primarily used for lookups
 * in the `wsprFrequencies` map.
 *
 * @param key The input string representing a WSPR band name.
 *
 * @return A lowercase version of the input string.
 */
std::string WSPRBandLookup::normalize_key(const std::string &key) const
{
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
    return lower_key;
}

/**
 * @brief Checks if a given frequency falls within a known ham radio band.
 *
 * This function iterates through the predefined `validHamFrequencies` set
 * and determines whether the provided frequency is within any of the
 * known amateur radio bands.
 *
 * @param frequency The frequency in Hz to be validated.
 *
 * @return The corresponding ham band name if the frequency is valid,
 *         otherwise returns "Invalid Frequency".
 */
std::string WSPRBandLookup::validate_frequency(long long frequency) const
{
    // Iterate through all valid ham frequency ranges
    for (const auto &range : validHamFrequencies)
    {
        // Check if the given frequency falls within a defined band range
        if (frequency >= std::get<0>(range) && frequency <= std::get<1>(range))
        {
            return std::get<2>(range); // Return the band name
        }
    }

    // Return "Invalid Frequency" if no matching band is found
    return "Invalid Frequency";
}

/**
 * @brief Converts a numeric frequency into a human-readable string representation.
 *
 * This function formats a given frequency in Hz into a string representation
 * using the appropriate unit (GHz, MHz, kHz, or Hz) based on its magnitude.
 * The decimal precision is adjusted accordingly:
 * - GHz: 9 decimal places
 * - MHz: 6 decimal places
 * - kHz: 3 decimal places
 * - Hz: No decimal places
 *
 * @param frequency The frequency in Hz to be formatted.
 *
 * @return A human-readable string representing the frequency with the appropriate unit.
 */
std::string WSPRBandLookup::freq_display_string(long long frequency) const
{
    std::ostringstream ss;

    if (frequency >= 1e9)
    {
        ss << std::fixed << std::setprecision(9) << (static_cast<double>(frequency) / 1e9) << " GHz";
    }
    else if (frequency >= 1e6)
    {
        ss << std::fixed << std::setprecision(6) << (static_cast<double>(frequency) / 1e6) << " MHz";
    }
    else if (frequency >= 1e3)
    {
        ss << std::fixed << std::setprecision(3) << (static_cast<double>(frequency) / 1e3) << " kHz";
    }
    else
    {
        ss << frequency << " Hz";
    }

    return ss.str();
}

/**
 * @brief Performs a lookup for a WSPR frequency or validates a numeric frequency.
 *
 * This function determines the type of input provided and performs the appropriate lookup:
 * - If given a **double** or **int**, it validates whether the frequency falls within a known ham band.
 * - If given a **string**, it checks for a matching WSPR band name and returns the associated frequency.
 *
 * @param input A `std::variant` that can be:
 *              - `std::string`: A WSPR band name (e.g., "40m").
 *              - `double`: A numeric frequency in Hz.
 *              - `int`: A numeric frequency in Hz.
 *
 * @return A `std::variant` that holds:
 *         - `double`: If the input was a WSPR band name, returns the corresponding frequency in Hz.
 *         - `std::string`: If the input was a frequency, returns the corresponding ham band name.
 *
 * @throws std::invalid_argument If the input is a string but does not match any known WSPR band.
 * @throws std::invalid_argument If the input type is not supported.
 */
std::variant<double, std::string> WSPRBandLookup::lookup(const std::variant<std::string, double, int> &input) const
{
    // If the input is a numeric frequency (double), validate its band
    if (std::holds_alternative<double>(input))
    {
        double value = std::get<double>(input);
        return validate_frequency(static_cast<long long>(value));
    }

    // If the input is a numeric frequency (int), validate its band
    if (std::holds_alternative<int>(input))
    {
        int value = std::get<int>(input);
        return validate_frequency(static_cast<long long>(value));
    }

    // If the input is a string, check for a WSPR band name lookup
    if (std::holds_alternative<std::string>(input))
    {
        std::string normalized_key = normalize_key(std::get<std::string>(input));
        auto it = wsprFrequencies.find(normalized_key);
        if (it != wsprFrequencies.end())
        {
            return it->second; // Return the WSPR frequency in Hz
        }
        throw std::invalid_argument("Key not found: " + normalized_key);
    }

    // If the input type is unsupported, throw an exception
    throw std::invalid_argument("Unsupported input type.");
}

/**
 * @brief Parses an input string as a frequency, with optional validation.
 *
 * This function first checks if the input is a known WSPR band name (e.g., "20m").
 * If not, it attempts to parse the input as a frequency string (e.g., "7.040 MHz").
 * If the input is a plain number (e.g., "7040100"), it can either validate it
 * against known ham bands or return it as-is based on the `validate` parameter.
 *
 * @param input The input string, which can be:
 *              - A WSPR band name (e.g., "20m")
 *              - A frequency with a unit (e.g., "7.040 MHz")
 *              - A raw numeric value (e.g., "7040100")
 * @param validate If `true`, checks raw numeric values against known ham bands.
 *                 If `false`, returns raw values as-is.
 *
 * @return A `double` representing the frequency in Hz.
 *
 * @throws std::invalid_argument If the input is invalid or unrecognized.
 */
double WSPRBandLookup::parse_string_to_frequency(std::string_view input, bool validate) const
{
    std::string input_str(input);

    // Trim spaces
    input_str.erase(0, input_str.find_first_not_of(" \t\n\r"));
    input_str.erase(input_str.find_last_not_of(" \t\n\r") + 1);

    // If the input contains only numbers (no letters), treat it as a raw numeric value
    if (input_str.find_first_not_of("0123456789.-") == std::string::npos)
    {
        try
        {
            double raw_freq = std::stod(input_str);
            // If it was exactly “0”, accept it without validating
            // This is a skip transmit window designator
            if (raw_freq == 0.0)
            {
                return 0.0;
            }
            // Validate if requested
            if (validate)
            {
                std::string band = validate_frequency(static_cast<long long>(raw_freq));
                if (band == "Invalid Frequency")
                {
                    throw std::invalid_argument("Frequency does not match known bands: " + input_str);
                }
            }

            return raw_freq;
        }
        catch (const std::exception &e)
        {
            throw std::invalid_argument("Invalid frequency format: " + input_str);
        }
    }

    // Check if input is a known WSPR band name
    auto result = lookup(input_str);
    if (std::holds_alternative<double>(result))
    {
        return std::get<double>(result);
    }

    throw std::invalid_argument("Invalid frequency format: " + input_str);
}

/**
 * @brief Prints all available WSPR band frequencies.
 *
 * This function iterates through the `wsprFrequencies` map and prints
 * each WSPR band name along with its corresponding frequency in a
 * human-readable format using `freq_display_string()`.
 *
 * The output is directed to `std::cout` and follows the format:
 * ```
 * 40m -> 7.040100 MHz
 * 30m -> 10.140200 MHz
 * 20m -> 14.097100 MHz
 * ```
 */
void WSPRBandLookup::print_wspr_frequencies() const
{
    for (const auto &entry : wsprFrequencies)
    {
        std::cout << entry.first << " -> " << freq_display_string(entry.second) << '\n';
    }
}
