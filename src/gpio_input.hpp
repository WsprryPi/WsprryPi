/**
 * @file gpio_input.hpp
 * @brief Handles shutdown button sensing.
 *
 * This project is licensed under the MIT License. See LICENSE.md for more
 * information.
 *
 * Copyright © 2023 - 2026 Lee C. Bussy (@LBussy).
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

#ifndef GPIO_INPUT_HPP
#define GPIO_INPUT_HPP

#include "gpio_include.hpp"
#include "gpio_line_resolver.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

/**
 * @class GPIOInput
 * @brief Monitors a BCM GPIO pin using libgpiod with thread-based event handling.
 *
 * This class allows for edge-triggered monitoring of a GPIO pin using the
 * libgpiod C++ API. It supports edge detection (rising or falling), optional
 * internal pull-up or pull-down configuration, CPU priority control, debounce
 * management, and thread-safe lifecycle operations.
 *
 * Designed for Raspberry Pi BCM numbering, the class resolves the runtime
 * chip/offset mapping internally and is suitable for global instantiation.
 *
 * Dependencies: libgpiod >= 1.6.
 *
 * Thread-safe: Yes.
 * Reentrant: No.
 */
class GPIOInput
{
public:
    /**
     * @brief GPIO internal pull configuration.
     */
    enum class PullMode
    {
        None,    ///< No pull resistor.
        PullUp,  ///< Enable internal pull-up resistor.
        PullDown ///< Enable internal pull-down resistor.
    };

    /**
     * @brief Operational state of the GPIO monitor.
     */
    enum class Status
    {
        NotConfigured, ///< GPIO is not configured yet.
        Running,       ///< Monitoring is currently active.
        Stopped,       ///< Monitoring was active but has been stopped.
        Error          ///< An error occurred during setup or runtime.
    };

    GPIOInput();
    ~GPIOInput();

    GPIOInput(const GPIOInput&) = delete;
    GPIOInput& operator=(const GPIOInput&) = delete;

    bool enable(int pin,
                bool trigger_high,
                PullMode pull_mode,
                std::function<void()> callback);

    bool stop();

    void resetTrigger();

    bool setPriority(int schedPolicy, int priority);

    Status getStatus() const;
    const std::string &lastError() const noexcept;

private:
    void monitorLoop();
    void releaseGPIOResources();

    int gpio_pin_;                   ///< BCM GPIO pin number.
    bool trigger_high_;              ///< Rising edge if true, falling if false.
    PullMode pull_mode_;             ///< Pull resistor mode.
    std::function<void()> callback_; ///< User callback function on trigger.

    std::atomic<bool> debounce_triggered_; ///< Debounce one-shot flag.
    std::atomic<bool> running_;            ///< Monitoring active.
    std::atomic<bool> stop_thread_;        ///< Request thread shutdown.

    std::thread monitor_thread_; ///< Thread used to monitor GPIO changes.
    std::mutex monitor_mutex_;   ///< Protects shared state.
    std::condition_variable cv_; ///< Coordinates thread shutdown.

    std::string last_error_;            ///< Last setup/runtime error text.
    Status status_;                     ///< Current operational state.
    ResolvedGPIOLine resolved_line_;    ///< Resolved chip/offset metadata.
    std::unique_ptr<gpiod::chip> chip_; ///< GPIO chip handle.

#if GPIOD_API_MAJOR >= 2
    std::optional<gpiod::line_request> request_;
    gpiod::edge_event_buffer event_buf_{16};
#else
    gpiod::line line_;
#endif
};

extern GPIOInput shutdownMonitor;

#endif // GPIO_INPUT_HPP
