/**
 * @file wspr_band_lookup.hpp
 * @brief Provides WSPR frequency lookup, formatting, and band correlation.
 *
 * This class translates frequencies to band names, band names to default WSPR
 * dial frequencies, and frequency strings to Hz. Band edge definitions are used to
 * correlate a manually entered frequency to a band for feature selection, such
 * as LPF GPIO control. These band edge definitions are not intended to enforce
 * legal operating privileges.
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

#ifndef WSPR_BAND_LOOKUP_HPP
#define WSPR_BAND_LOOKUP_HPP

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include "band_gpio.hpp"

/**
 * @class WSPRBandLookup
 * @brief Provides methods for WSPR frequency lookup, validation, and display.
 */
class WSPRBandLookup
{
private:
    /**
     * @brief Defines a frequency range tuple of lower edge, upper edge, and
     *        display name.
     */
    using FrequencyRange = std::tuple<long long, long long, std::string>;

    /**
     * @brief Stores default WSPR dial frequencies by normalized band name or alias.
     */
    std::unordered_map<std::string, double> wsprFrequencies;

    /**
     * @brief Stores band ranges used to correlate a frequency to a band.
     */
    std::vector<FrequencyRange> validHamFrequencies;

protected:
    /**
     * @brief Normalizes a lookup key to lowercase.
     *
     * @param key Input band name or alias.
     * @return Lowercase copy of the input key.
     */
    std::string normalize_key(const std::string &key) const;

    /**
     * @brief Correlates a frequency to a known band bucket.
     *
     * @param frequency Frequency in Hz.
     * @return Matching band name, or "Invalid Frequency" if no band matches.
     */
    std::string validate_frequency(long long frequency) const;

public:
    /**
     * @brief Constructs the lookup object and initializes band tables.
     */
    WSPRBandLookup();

    /**
     * @brief Return the correlated HamBand for a numeric frequency.
     *
     * @param frequency Frequency in Hz.
     * @return Matching HamBand, or std::nullopt if no band matches.
     */
    std::optional<HamBand> lookup_ham_band(long long frequency) const;

    /**
     * @brief Return the correlated HamBand for a numeric frequency.
     *
     * @param frequency Frequency in Hz.
     * @return Matching HamBand, or std::nullopt if no band matches.
     */
    std::optional<HamBand> lookup_ham_band(double frequency) const;

    /**
     * @brief Looks up a default WSPR dial frequency or validates a numeric value.
     *
     * @param input Band name, alias, or numeric frequency in Hz.
     * @return Default WSPR frequency for string input or a band name for
     *         numeric input.
     */
    std::variant<double, std::string> lookup(
        const std::variant<std::string, double, int> &input) const;

    /**
     * @brief Formats a frequency into a human-readable string.
     *
     * @param frequency Frequency in Hz.
     * @return String formatted in GHz, MHz, kHz, or Hz.
     */
    std::string freq_display_string(long long frequency) const;

    /**
     * @brief Parses a frequency string and converts it to Hz.
     *
     * @param freq_str Frequency string such as "7.040100 MHz".
     * @return Frequency value in Hz.
     * @throws std::invalid_argument If the format is invalid.
     */
    long long parse_frequency_string(const std::string &freq_str) const;

    /**
     * @brief Parses an input string as a band alias or numeric frequency.
     *
     * @param input Band alias, unit-qualified frequency, or raw numeric value.
     * @param validate If true, validates numeric values against known bands.
     * @return Frequency in Hz.
     * @throws std::invalid_argument If the input is invalid.
     */
    double parse_string_to_frequency(
        std::string_view input,
        bool validate = true) const;

    /**
     * @brief Detect whether a numeric frequency exactly matches a legacy
     *        built-in actual-RF WSPR alias value.
     *
     * @details
     * This is intended only for conservative compatibility warnings. It does
     * not reinterpret user input automatically.
     *
     * @param frequency Frequency in Hz.
     * @return Matching WSPR alias such as "20m", or std::nullopt if no exact
     *         legacy built-in actual frequency matches.
     */
    std::optional<std::string> legacy_actual_wspr_alias_for_frequency(
        double frequency) const;

    /**
     * @brief Prints all predefined WSPR frequencies to standard output.
     */
    void print_wspr_frequencies() const;
};

/**
 * @brief Convert a HamBand enum value to its string representation.
 *
 * This is used for logging, debugging, and user-facing messages.
 *
 * @param band The HamBand enum value.
 * @return Null-terminated string representing the band.
 */
const char *ham_band_to_string(HamBand band);

#endif // WSPR_BAND_LOOKUP_HPP
