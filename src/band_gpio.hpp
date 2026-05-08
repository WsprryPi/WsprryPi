/**
 * @file band_gpio.hpp
 * @brief Defines amateur band enumeration and GPIO configuration model.
 *
 * This file defines the HamBand enumeration and BandGPIOConfig structure
 * used to describe how each amateur band maps to a GPIO output. The
 * configuration includes GPIO number, enable state, and polarity.
 *
 * This header represents the data model for the band GPIO subsystem.
 * It does not perform any GPIO control directly.
 *
 * This project is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
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

#ifndef BAND_GPIO_HPP
#define BAND_GPIO_HPP

constexpr int HAM_BAND_COUNT = 16;

/**
 * @brief Supported amateur radio bands for GPIO band selection.
 */
enum class HamBand
{
    BAND_2200M,
    BAND_630M,
    BAND_160M,
    BAND_80M,
    BAND_60M,
    BAND_40M,
    BAND_30M,
    BAND_22M,
    BAND_20M,
    BAND_17M,
    BAND_15M,
    BAND_12M,
    BAND_10M,
    BAND_6M,
    BAND_4M,
    BAND_2M
};

constexpr int ham_band_index(HamBand band)
{
    return static_cast<int>(band);
}

/**
 * @brief GPIO configuration for a specific amateur radio band.
 *
 * The logical state passed to BandGPIOSelector::setBandState() is interpreted
 * as enabled or disabled. The active_high field controls how that logical
 * state is translated to the physical GPIO level.
 */
struct BandGPIOConfig
{
    int gpio = -1;
    bool enabled = false;
    bool active_high = false;
};

/**
 * @brief Return the GPIO configuration assigned to a band.
 *
 * A disabled configuration is returned for unknown or unsupported bands.
 *
 * @param band The band to look up.
 * @return The GPIO configuration for the band.
 */
const BandGPIOConfig &gpio_config_for_band(HamBand band);

/**
 * @brief Return the display name for a band.
 *
 * @param band The band to convert.
 * @return A null-terminated string describing the band.
 */
constexpr const char *band_to_string(HamBand band)
{
    switch (band)
    {
        case HamBand::BAND_2200M: return "2200m";
        case HamBand::BAND_630M:  return "630m";
        case HamBand::BAND_160M:  return "160m";
        case HamBand::BAND_80M:   return "80m";
        case HamBand::BAND_60M:   return "60m";
        case HamBand::BAND_40M:   return "40m";
        case HamBand::BAND_30M:   return "30m";
        case HamBand::BAND_22M:   return "22m";
        case HamBand::BAND_20M:   return "20m";
        case HamBand::BAND_17M:   return "17m";
        case HamBand::BAND_15M:   return "15m";
        case HamBand::BAND_12M:   return "12m";
        case HamBand::BAND_10M:   return "10m";
        case HamBand::BAND_6M:    return "6m";
        case HamBand::BAND_4M:    return "4m";
        case HamBand::BAND_2M:    return "2m";
    }

    return "unknown";
}

/**
 * @brief Check whether a band is configured and enabled.
 *
 * @param band The band to test.
 * @return True if the band is enabled and has a valid GPIO.
 */
bool is_band_gpio_enabled(HamBand band);

#endif // BAND_GPIO_HPP
