/**
 * @file gpio_output.hpp
 * @brief Handles LED output.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
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

#ifndef GPIO_OUTPUT_HPP
#define GPIO_OUTPUT_HPP

#include <memory>
#include <optional>
#include <string>

#include "gpio_include.hpp"

/**
 * @class GPIOOutput
 * @brief Simple GPIO output controller using libgpiod.
 *
 * This class configures a specified GPIO pin as an output. The pin can be
 * configured as active high (default) or as an active low sink. Methods are provided
 * to enable (configure) the pin, disable (release) it, and toggle its output state.
 *
 * Example:
 * @code
 * GPIOOutput gpio;
 * gpio.enableGPIOPin(17, true); // enable pin 17 as active high output
 * gpio.toggleGPIO(true);         // set pin high (active)
 * gpio.toggleGPIO(false);        // set pin low (inactive)
 * gpio.stop_gpio_pin();        // release the pin
 * @endcode
 */
class GPIOOutput
{
public:
    /**
     * @brief Default constructor.
     *
     * Constructs an inactive GPIOOutput object.
     */
    GPIOOutput();

    /**
     * @brief Destructor.
     *
     * Releases the GPIO pin if it has been enabled.
     */
    ~GPIOOutput();

    /**
     * @brief Configures and enables a GPIO pin for output.
     *
     * Opens the default GPIO chip (/dev/gpiochip0), obtains the specified pin,
     * and requests it as an output. The pin can be configured as active high or
     * active low (sink).
     *
     * @param pin The GPIO pin number (BCM numbering).
     * @param active_high True for active-high operation (default), false for sink.
     * @return True if the pin was successfully configured; false otherwise.
     */
    bool enableGPIOPin(int pin, bool active_high = true);

    /**
     * @brief Disables the GPIO pin.
     *
     * Releases the line resource if it is currently in use.
     */
    void stop();

    /**
     * @brief Sets the GPIO output state.
     *
     * For an active-high pin, passing true sets the pin high; for an active-low
     * (sink) pin, the logic is inverted (i.e. passing true sets the pin low).
     *
     * @param state Desired logical state (true for active, false for inactive).
     * @return True if the state was successfully set; false otherwise.
     */
    bool toggleGPIO(bool state);

    /**
     * @brief Returns the most recent GPIO setup error, if any.
     */
    const std::string &lastError() const noexcept { return last_error_; }

private:
    int pin_;
    bool active_high_;
    bool enabled_;
    std::string last_error_;

    // Using unique_ptr to manage the libgpiod chip.
    std::unique_ptr<gpiod::chip> chip_;
#if GPIOD_API_MAJOR >= 2
    // v2: request handle (no default ctor) — wrap in optional
    std::optional<gpiod::line_request> request_;
#else
    // v1: store the line by value.
    gpiod::line line_;
#endif

    // Helper to compute the physical state to write based on active configuration.
    int compute_physical_state(bool logical_state) const;
};

// Global instance
extern GPIOOutput ledControl;

#endif // GPIO_OUTPUT_HPP
