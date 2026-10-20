/**
 * @file band_lookup.hpp
 * @brief Provides general band correlation and WSPR frequency conveniences.
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

#ifndef BAND_LOOKUP_HPP
#define BAND_LOOKUP_HPP

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "band_gpio.hpp"

enum class WsprBandResolutionSource
{
    BuiltInPreset,
    ProfilePreset,
    BandPreferencePreset,
    BandPreferenceNumeric,
};

inline constexpr const char *wspr_band_resolution_source_name(
    WsprBandResolutionSource source) noexcept
{
    switch (source)
    {
    case WsprBandResolutionSource::BuiltInPreset: return "built_in_preset";
    case WsprBandResolutionSource::ProfilePreset: return "profile_preset";
    case WsprBandResolutionSource::BandPreferencePreset:
        return "band_preference_preset";
    case WsprBandResolutionSource::BandPreferenceNumeric:
        return "band_preference_numeric";
    }
    return "built_in_preset";
}

struct WsprBandCatalogEntry
{
    std::string band;
    std::uint64_t dial_frequency_hz;
    WsprBandResolutionSource resolution_source;
    std::optional<std::string> preset;
};

struct WsprPresetCatalogEntry
{
    std::string preset;
    std::string band;
    std::uint64_t dial_frequency_hz;
    bool existing_common;
};

using WsprBandPreferenceValue = std::variant<std::string, std::uint64_t>;
using WsprBandPreferences =
    std::unordered_map<std::string, WsprBandPreferenceValue>;

struct WsprBandFrequencyResolution
{
    std::string band;
    std::uint64_t dial_frequency_hz;
    std::optional<std::string> preset;
    bool custom;
    WsprBandResolutionSource source;
};

/** Return only built-in preset preferences for legacy runtime consumers. */
std::unordered_map<std::string, std::string> preset_only_band_preferences(
    const WsprBandPreferences &preferences);

/**
 * @class BandLookup
 * @brief Provides band correlation plus separate WSPR preset conveniences.
 */
class BandLookup
{
private:
    /**
     * @brief Stores default WSPR dial frequencies by normalized band name or alias.
     */
    std::unordered_map<std::string, double> wsprFrequencies;

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
    BandLookup();

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
     * @param frequency_profile Explicit WSPR convenience profile. The default
     *        preserves the existing/common bare-alias meanings.
     * @return Frequency in Hz.
     * @throws std::invalid_argument If the input is invalid.
     */
    double parse_string_to_frequency(
        std::string_view input,
        bool validate = true,
        std::string_view frequency_profile = "existing_common",
        const std::unordered_map<std::string, std::string> &band_preferences = {}) const;

    /**
     * @brief Return canonical WSPR display bands in stable order.
     *
     * @return Display names and integral USB dial frequencies.
     * @throws std::logic_error if an authoritative definition cannot be
     *         represented as an integral external Hz value.
     */
    std::vector<WsprBandCatalogEntry> canonical_wspr_band_catalog(
        std::string_view frequency_profile = "existing_common",
        const WsprBandPreferences &band_preferences = {}) const;

    /**
     * @brief Return every built-in qualified WSPR preset in stable order.
     *
     * @details This is separate from the one-effective-preset-per-band catalog
     * used by compatibility consumers. Multiple presets may correlate to the
     * same canonical band.
     */
    std::vector<WsprPresetCatalogEntry> complete_wspr_preset_catalog() const;

    /**
     * @brief Resolve one canonical WSPR band through a typed local preference.
     *
     * @details This contract accepts built-in preset identities or integral
     * custom USB dial frequencies. Runtime consumers are migrated separately.
     */
    std::optional<WsprBandFrequencyResolution> resolve_wspr_band_frequency(
        std::string_view canonical_band,
        std::string_view frequency_profile,
        const WsprBandPreferences &band_preferences) const;

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

#endif // BAND_LOOKUP_HPP
