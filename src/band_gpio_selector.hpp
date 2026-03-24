/**
 * @file band_gpio_selector.hpp
 * @brief Interface for selecting and controlling GPIO per amateur band.
 *
 * This class provides the runtime interface for selecting an amateur
 * band and controlling its associated GPIO output. It uses the band
 * configuration defined in band_gpio.* and supports selection by
 * HamBand or by frequency via WSPRBandLookup.
 *
 * This header represents the control interface of the band GPIO
 * subsystem.
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

#ifndef BAND_GPIO_SELECTOR_HPP
#define BAND_GPIO_SELECTOR_HPP

#include "band_gpio.hpp"
#include "gpio_output.hpp"
#include "wspr_band_lookup.hpp"

/**
 * @brief Controls a single GPIO output using amateur band selection.
 *
 * This class maps ham bands to configured GPIO outputs and uses GPIOOutput to
 * enable and drive the selected pin. GPIO polarity is taken from the selected
 * band's BandGPIOConfig.
 */
class BandGPIOSelector
{
public:
    /**
     * @brief Prepare a band and enable its configured GPIO pin.
     *
     * Any previously active GPIO pin is released before the new one is enabled.
     * The GPIO remains in its inactive state until setBandState(true) is called.
     *
     * @param band The ham band to prepare.
     * @return True on success, false on failure.
     */
    bool prepareBand(HamBand band);

    /**
     * @brief Prepare a band based on a frequency in Hz.
     *
     * The frequency is resolved through WSPRBandLookup directly to HamBand and
     * then the configured GPIO is enabled in its inactive state.
     *
     * @param frequency_hz The frequency in Hz.
     * @return True on success, false on failure.
     */
    bool prepareFrequency(double frequency_hz);

    /**
     * @brief Set the currently selected band GPIO enabled or disabled.
     *
     * The meaning of the logical state is translated using the selected band's
     * configured polarity.
     *
     * @param state True to enable the selected band's external hardware, false
     *              to disable it.
     * @return True on success, false on failure.
     */
    bool setBandState(bool state);

    /**
     * @brief Release the currently selected GPIO.
     */
    void stop();

    /**
     * @brief Return the currently selected band.
     *
     * @return Pointer to current band, or nullptr if none selected.
     */
    const HamBand *currentBand() const;

    /**
     * @brief Return the current band configuration.
     *
     * @return Pointer to the current configuration, or nullptr if none is
     *         selected.
     */
    const BandGPIOConfig *currentConfig() const;

private:
    GPIOOutput gpio_;
    WSPRBandLookup band_lookup_;
    bool has_band_ = false;
    HamBand current_band_ = HamBand::BAND_2200M;
    BandGPIOConfig current_config_{};
};

#endif // BAND_GPIO_SELECTOR_HPP
