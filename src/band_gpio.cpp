/**
 * @file band_gpio.cpp
 * @brief Implements amateur band to GPIO configuration mapping.
 *
 * This file defines the static mapping between HamBand values and their
 * associated GPIO configuration. Each band is assigned a GPIO pin, an
 * enable flag, and a polarity (active high or active low).
 *
 * This file provides the configuration layer of the band GPIO subsystem.
 * It is intentionally separated from runtime control logic.
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

#include "band_gpio.hpp"

/**
 * @brief Return a shared disabled GPIO configuration.
 *
 * @return A disabled configuration with an invalid GPIO.
 */
static const BandGPIOConfig &disabled_band_gpio_config()
{
    static const BandGPIOConfig disabled_config{-1, false, false};
    return disabled_config;
}

// TODO:  Put this in config object
const BandGPIOConfig &gpio_config_for_band(HamBand band)
{
    switch (band)
    {
        case HamBand::BAND_2200M:
        {
            static const BandGPIOConfig config{17, true, false};
            return config;
        }
        case HamBand::BAND_630M:
        {
            static const BandGPIOConfig config{27, true, false};
            return config;
        }
        case HamBand::BAND_160M:
        {
            static const BandGPIOConfig config{22, true, false};
            return config;
        }
        case HamBand::BAND_80M:
        {
            static const BandGPIOConfig config{23, true, false};
            return config;
        }
        case HamBand::BAND_60M:
        {
            static const BandGPIOConfig config{24, true, false};
            return config;
        }
        case HamBand::BAND_40M:
        {
            static const BandGPIOConfig config{25, true, false};
            return config;
        }
        case HamBand::BAND_30M:
        {
            static const BandGPIOConfig config{5, true, false};
            return config;
        }
        case HamBand::BAND_22M:
        {
            static const BandGPIOConfig config{6, true, false};
            return config;
        }
        case HamBand::BAND_20M:
        {
            static const BandGPIOConfig config{12, true, false};
            return config;
        }
        case HamBand::BAND_17M:
        {
            static const BandGPIOConfig config{13, true, false};
            return config;
        }
        case HamBand::BAND_15M:
        {
            static const BandGPIOConfig config{16, true, false};
            return config;
        }
        case HamBand::BAND_12M:
        {
            static const BandGPIOConfig config{26, true, false};
            return config;
        }
        case HamBand::BAND_10M:
        {
            static const BandGPIOConfig config{20, true, false};
            return config;
        }
        case HamBand::BAND_6M:
        {
            static const BandGPIOConfig config{21, true, false};
            return config;
        }
        case HamBand::BAND_4M:
        {
            static const BandGPIOConfig config{-1, false, false};
            return config;
        }
        case HamBand::BAND_2M:
        {
            static const BandGPIOConfig config{-1, false, false};
            return config;
        }
    }

    return disabled_band_gpio_config();
}

bool is_band_gpio_enabled(HamBand band)
{
    const BandGPIOConfig &config = gpio_config_for_band(band);
    return config.enabled && config.gpio >= 0;
}
