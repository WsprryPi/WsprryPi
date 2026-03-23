#ifndef BAND_GPIO_SELECTOR_HPP
#define BAND_GPIO_SELECTOR_HPP

/**
 * @file band_gpio_selector.hpp
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

#include <optional>
#include <string>

#include "band_gpio.hpp"
#include "gpio_output.hpp"
#include "wspr_band_lookup.hpp"

/**
 * @class BandGPIOSelector
 * @brief Selects the GPIO assigned to the current amateur band.
 *
 * This class bridges WSPRBandLookup and GPIOOutput. It can select a band
 * directly from a HamBand value or derive the band from a transmit frequency.
 *
 * The selected GPIO is configured but left inactive until setBandState(true)
 * is called. Selecting a new band automatically releases the previously
 * selected GPIO because GPIOOutput::enableGPIOPin() handles that transition.
 */
class BandGPIOSelector
{
public:
    /**
     * @brief Select a GPIO line for the specified band.
     *
     * @param band Amateur band to select.
     * @param active_high True for active-high operation.
     * @return True if the band's GPIO was selected successfully.
     */
    bool selectBand(HamBand band, bool active_high = true);

    /**
     * @brief Select a GPIO line based on a transmit frequency.
     *
     * The frequency is resolved through WSPRBandLookup and then mapped to a
     * HamBand value supported by band_gpio.hpp.
     *
     * @param frequency_hz Frequency in Hz.
     * @param active_high True for active-high operation.
     * @return True if a supported band was found and selected.
     */
    bool selectFrequency(double frequency_hz, bool active_high = true);

    /**
     * @brief Set the active state of the currently selected band GPIO.
     *
     * @param state True to assert the band GPIO, false to deassert it.
     * @return True on success, false if no GPIO is currently selected.
     */
    bool setBandState(bool state);

    /**
     * @brief Release the currently selected GPIO, if any.
     */
    void stop();

    /**
     * @brief Return the currently selected band.
     *
     * @return Pointer to the current band, or nullptr if none is selected.
     */
    const HamBand *currentBand() const;

private:
    /**
     * @brief Convert a band name to a HamBand value.
     *
     * @param band_name Band name such as "40M" or "22m".
     * @return Matching HamBand on success, or std::nullopt if unsupported.
     */
    std::optional<HamBand> hamBandFromString(const std::string &band_name) const;

    GPIOOutput gpio_;
    WSPRBandLookup band_lookup_;
    bool has_band_ = false;
    HamBand current_band_ = HamBand::BAND_2200M;
};

#endif // BAND_GPIO_SELECTOR_HPP
