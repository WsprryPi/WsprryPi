/**
 * @file band_gpio_selector.cpp
 * @brief Selects and controls a GPIO line for the active amateur band.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
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

#include <algorithm>
#include <cctype>
#include <variant>

std::optional<HamBand> BandGPIOSelector::hamBandFromString(
    const std::string &band_name) const
{
    std::string normalized = band_name;
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

    if (normalized == "2200m")
        return HamBand::BAND_2200M;
    if (normalized == "630m")
        return HamBand::BAND_630M;
    if (normalized == "160m")
        return HamBand::BAND_160M;
    if (normalized == "80m")
        return HamBand::BAND_80M;
    if (normalized == "60m" || normalized == "60m (channelized)")
        return HamBand::BAND_60M;
    if (normalized == "40m")
        return HamBand::BAND_40M;
    if (normalized == "30m")
        return HamBand::BAND_30M;
    if (normalized == "22m")
        return HamBand::BAND_22M;
    if (normalized == "20m")
        return HamBand::BAND_20M;
    if (normalized == "17m")
        return HamBand::BAND_17M;
    if (normalized == "15m")
        return HamBand::BAND_15M;
    if (normalized == "12m")
        return HamBand::BAND_12M;
    if (normalized == "10m")
        return HamBand::BAND_10M;
    if (normalized == "6m")
        return HamBand::BAND_6M;

    return std::nullopt;
}

bool BandGPIOSelector::selectBand(HamBand band, bool active_high)
{
    const int gpio = gpio_for_band(band);
    if (gpio < 0)
    {
        return false;
    }

    if (!gpio_.enableGPIOPin(gpio, active_high))
    {
        return false;
    }

    has_band_ = true;
    current_band_ = band;
    return true;
}

bool BandGPIOSelector::selectFrequency(double frequency_hz, bool active_high)
{
    auto lookup_result = band_lookup_.lookup(frequency_hz);
    if (!std::holds_alternative<std::string>(lookup_result))
    {
        return false;
    }

    std::optional<HamBand> band =
        hamBandFromString(std::get<std::string>(lookup_result));
    if (!band.has_value())
    {
        return false;
    }

    return selectBand(*band, active_high);
}

bool BandGPIOSelector::setBandState(bool state)
{
    if (!has_band_)
    {
        return false;
    }

    return gpio_.toggleGPIO(state);
}

void BandGPIOSelector::stop()
{
    gpio_.stop();
    has_band_ = false;
}

const HamBand *BandGPIOSelector::currentBand() const
{
    if (!has_band_)
    {
        return nullptr;
    }

    return &current_band_;
}
