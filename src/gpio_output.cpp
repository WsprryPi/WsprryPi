/**
 * @file gpio_output.cpp
 * @brief Safe libgpiod-backed GPIO output helper implementation.
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

#include "gpio_output.hpp"
#include "logging.hpp"
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace
{
    std::mutex gpio_output_test_mtx;
    bool gpio_output_test_mode_enabled = false;
    std::unordered_map<int, bool> gpio_output_test_states;
    std::vector<GPIOOutput::TestEvent> gpio_output_test_events;

    void record_gpio_output_test_event(
        const std::string &action,
        int pin,
        bool active_high,
        bool logical_state)
    {
        std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
        if (!gpio_output_test_mode_enabled)
        {
            return;
        }

        gpio_output_test_events.push_back(GPIOOutput::TestEvent{
            action,
            pin,
            active_high,
            logical_state});

        if (action == "release")
        {
            gpio_output_test_states.erase(pin);
            return;
        }

        gpio_output_test_states[pin] = logical_state;
    }
}

// Global instances for optional transmit-related GPIO outputs.
GPIOOutput ledControl;
GPIOOutput ampControl;

/**
 * @brief Default constructor for GPIOOutput.
 * @details Initializes the GPIOOutput object with the following defaults:
 *          - pin_ is set to -1 (no pin configured)
 *          - active_high_ is set to true (active-high logic)
 *          - enabled_ is false (GPIO not yet configured)
 *          - libgpiod handles are empty until a resolved line is requested
 *
 * The object must be explicitly configured using enableGPIOPin() before use.
 */
GPIOOutput::GPIOOutput() : pin_(-1),
                           active_high_(true),
                           enabled_(false),
                           last_logical_state_(false),
                           last_error_(),
                           chip_(nullptr)
{
}

/**
 * @brief Default destructor for GPIOOutput.
 * @details Calls stop() and releases resources.
 */
GPIOOutput::~GPIOOutput()
{
    stop();
}

/**
 * @brief Enables a GPIO pin for output.
 * @details Resolves the requested BCM GPIO line at runtime and requests the
 * corresponding chip/offset through libgpiod. If the pin is already enabled,
 * it is first released and then re-requested.
 *
 * @param pin The BCM GPIO pin number to be enabled.
 * @param active_high If true, the pin is configured for active-high logic; if false,
 *                    for active-low (sink) logic.
 * @return True if the pin was successfully enabled.
 *
 * On failure the function returns false and records a human-readable message
 * in lastError().
 */
bool GPIOOutput::enableGPIOPin(int pin, bool active_high)
{
    last_error_.clear();

    if (enabled_)
    {
        stop();
    }
    pin_ = pin;
    active_high_ = active_high;
    last_logical_state_ = false;
    resolved_line_ = ResolvedGPIOLine{};

    bool use_test_mode = false;
    {
        std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
        use_test_mode = gpio_output_test_mode_enabled;
    }
    if (use_test_mode)
    {
        enabled_ = true;
        record_gpio_output_test_event("request", pin_, active_high_, false);
        return true;
    }

    try
    {
        std::string resolution_error;
        if (!resolve_gpio_line(pin_, resolved_line_, resolution_error))
        {
            last_error_ =
                "Error resolving GPIO pin " + std::to_string(pin_) + ": " +
                resolution_error;
            enabled_ = false;
            return false;
        }

        chip_ = std::make_unique<gpiod::chip>(resolved_line_.chip_path);

#if GPIOD_API_MAJOR >= 2
        // ---- libgpiod v2 path (Trixie) ----
        gpiod::line_settings ls;
        ls.set_direction(gpiod::line::direction::OUTPUT);

        // Let the kernel handle inversion.
        ls.set_active_low(!active_high_);

        auto builder = chip_->prepare_request();
        builder.set_consumer("GPIOOutput"); // separate call: no copy
        const GpioLineOffset off = resolved_line_.offset;
        builder.add_line_settings(off, ls);
        request_ = builder.do_request(); // move into optional

        // Initial logical state: inactive (false). Kernel inverts if needed.
        request_->set_value(off, gpiod::line::value::INACTIVE);
#else
        // ---- libgpiod v1 path (Bookworm) ----
        line_ = chip_->get_line(static_cast<unsigned int>(resolved_line_.offset));

        gpiod::line_request req;
        req.consumer = "GPIOOutput";
        req.request_type = gpiod::line_request::DIRECTION_OUTPUT;

        // Let the kernel handle inversion.
        if (!active_high_)
        {
            req.flags |= gpiod::line_request::FLAG_ACTIVE_LOW;
        }

        line_.request(req);

        // Initial logical state: inactive (false). Kernel inverts if needed.
        line_.set_value(/*logical*/ 0);
#endif

        enabled_ = true;
        last_logical_state_ = false;
        record_gpio_output_test_event("request", pin_, active_high_, false);
        llog.logS(
            DEBUG,
            "GPIOOutput: request success for ",
            describe_resolved_gpio_line(resolved_line_),
            ", active_high ",
            active_high_ ? "true" : "false",
            ".");
    }
    catch (const std::exception &e)
    {
        last_error_ =
            "Error enabling " + describe_resolved_gpio_line(resolved_line_) +
            ": " + e.what();
        enabled_ = false;
#if GPIOD_API_MAJOR >= 2
        request_.reset();
#endif
        chip_.reset();
        llog.logS(ERROR, "GPIOOutput: request failure. ", last_error_);
        return false;
    }
    return enabled_;
}

/**
 * @brief Disables the currently active GPIO pin.
 * @details Releases the previously resolved GPIO line and resets the internal
 *          libgpiod handles. After calling this function, the pin must be
 *          re-enabled via enableGPIOPin() before use.
 *
 * This function is safe to call even if no pin is currently enabled.
 */
void GPIOOutput::stop()
{
    if (!enabled_)
    {
        // Clear any stale handles just in case
#if GPIOD_API_MAJOR >= 2
        request_.reset();
#endif
        chip_.reset();
        resolved_line_ = ResolvedGPIOLine{};
        return;
    }

    // Try to put the output in inactive state before releasing
    (void)toggleGPIO(false);

#if GPIOD_API_MAJOR >= 2
    // v2: Destroys the handle, releasing the line
    if (request_)
    {
        // optional reset destroys the handle and releases the line
        request_.reset();
    }
#else
    // v1: release the line by value
    try
    {
        line_.release();
    }
    catch (...)
    {
        // Ignore; Releasing twice is harmless but may throw
    }
#endif

    enabled_ = false;
#if GPIOD_API_MAJOR >= 2
    request_.reset();
#endif
    chip_.reset();
    resolved_line_ = ResolvedGPIOLine{};
    record_gpio_output_test_event("release", pin_, active_high_, false);
    pin_ = -1;
    active_high_ = true;
    last_logical_state_ = false;
}

/**
 * @brief Sets the output state of the GPIO pin.
 * @details Converts the requested logical state into the corresponding physical
 *          voltage level based on the configured active-high or active-low mode.
 *          If the pin is not enabled, the function returns false.
 *
 * @param state Logical state to apply to the pin:
 *              - true: active (high if active-high, low if active-low)
 *              - false: inactive (low if active-high, high if active-low)
 *
 * @return True if the state was successfully applied; false if the pin was not
 *         enabled or an error occurred during the write operation.
 */
bool GPIOOutput::toggleGPIO(bool state)
{
    if (!enabled_)
    {
        return false;
    }

    bool use_test_mode = false;
    {
        std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
        use_test_mode = gpio_output_test_mode_enabled;
    }
    if (use_test_mode)
    {
        last_logical_state_ = state;
        record_gpio_output_test_event("write", pin_, active_high_, state);
        return true;
    }

    try
    {
        last_logical_state_ = state;
        int physical = compute_physical_state(state);

#if GPIOD_API_MAJOR >= 2
        const GpioLineOffset off = resolved_line_.offset;
        request_->set_value(
            off,
            physical ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE);
#else
        line_.set_value(physical);
#endif
        llog.logS(
            DEBUG,
            "GPIOOutput: write success for ",
            describe_resolved_gpio_line(resolved_line_),
            ", logical ",
            state ? "1" : "0",
            ", requested line value ",
            physical,
            ".");
        record_gpio_output_test_event("write", pin_, active_high_, state);
    }
    catch (const std::exception &e)
    {
        llog.logS(
            ERROR,
            "GPIOOutput: write failure for ",
            describe_resolved_gpio_line(resolved_line_),
            ": ",
            e.what());
        return false;
    }
    return true;
}

/**
 * @brief Converts logical state to the value written to the line.
 *
 * With kernel-level inversion enabled (active_low), the driver performs
 * the mapping. This function passes the logical value through.
 *
 * @param logical_state Desired logical output state (true = active).
 * @return 1 for logical true, 0 for logical false.
 */
int GPIOOutput::compute_physical_state(bool logical_state) const
{
    return logical_state ? 1 : 0;
}

void GPIOOutput::setTestMode(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
    gpio_output_test_mode_enabled = enabled;
    gpio_output_test_states.clear();
    gpio_output_test_events.clear();
}

bool GPIOOutput::testModeEnabled() noexcept
{
    std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
    return gpio_output_test_mode_enabled;
}

void GPIOOutput::clearTestEvents() noexcept
{
    std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
    gpio_output_test_events.clear();
}

std::vector<GPIOOutput::TestEvent> GPIOOutput::testEvents()
{
    std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
    return gpio_output_test_events;
}

std::optional<bool> GPIOOutput::testLogicalStateForPin(int pin) noexcept
{
    std::lock_guard<std::mutex> lk(gpio_output_test_mtx);
    const auto it = gpio_output_test_states.find(pin);
    if (it == gpio_output_test_states.end())
    {
        return std::nullopt;
    }

    return it->second;
}
