/**
 * @file band_gpio.cpp
 * @brief Implements amateur band to GPIO configuration mapping.
 *
 * This file provides the band-to-GPIO lookup backed by the global
 * runtime configuration. Each band is assigned a GPIO pin, an enable
 * flag, and a polarity (active high or active low).
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
#include "config_handler.hpp"

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

const BandGPIOConfig &gpio_config_for_band(HamBand band)
{
    const int band_index = ham_band_index(band);

    if (band_index < 0 || band_index >= HAM_BAND_COUNT)
    {
        return disabled_band_gpio_config();
    }

    return config.band_gpio[band_index];
}

bool is_band_gpio_enabled(HamBand band)
{
    const BandGPIOConfig &config = gpio_config_for_band(band);
    return config.enabled && config.gpio >= 0;
}
