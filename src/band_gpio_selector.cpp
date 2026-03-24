/**
 * @file band_gpio_selector.cpp
 * @brief Runtime control of GPIO selection for the active amateur band.
 *
 * This file implements the BandGPIOSelector class, which selects the
 * appropriate GPIO based on the active amateur band and controls its
 * state during transmission.
 *
 * It bridges frequency-based band lookup (WSPRBandLookup) with GPIO
 * control (GPIOOutput), using configuration provided by band_gpio.*.
 *
 * This file represents the runtime behavior of the band GPIO subsystem.
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

#include "band_gpio_selector.hpp"

bool BandGPIOSelector::prepareBand(HamBand band)
{
    const BandGPIOConfig &config = gpio_config_for_band(band);

    stop();

    if (!config.enabled || config.gpio < 0)
    {
        return false;
    }

    if (!gpio_.enableGPIOPin(config.gpio, config.active_high))
    {
        return false;
    }

    has_band_ = true;
    current_band_ = band;
    current_config_ = config;
    return true;
}

bool BandGPIOSelector::prepareFrequency(double frequency_hz)
{
    const auto band = band_lookup_.lookup_ham_band(frequency_hz);
    if (!band.has_value())
    {
        return false;
    }

    return prepareBand(*band);
}

bool BandGPIOSelector::setBandState(bool state)
{
    if (!has_band_ || !current_config_.enabled || current_config_.gpio < 0)
    {
        return false;
    }

    return gpio_.toggleGPIO(state);
}

void BandGPIOSelector::stop()
{
    gpio_.stop();
    has_band_ = false;
    current_config_ = BandGPIOConfig{};
}

const HamBand *BandGPIOSelector::currentBand() const
{
    if (!has_band_)
    {
        return nullptr;
    }

    return &current_band_;
}

const BandGPIOConfig *BandGPIOSelector::currentConfig() const
{
    if (!has_band_)
    {
        return nullptr;
    }

    return &current_config_;
}
