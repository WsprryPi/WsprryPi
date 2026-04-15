/**
 * @file gpio_input.cpp
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

#include "gpio_input.hpp"

#include "logging.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

#include <pthread.h>

// Global instance.
GPIOInput shutdownMonitor;

GPIOInput::GPIOInput()
    : gpio_pin_(-1),
      trigger_high_(false),
      pull_mode_(PullMode::None),
      callback_(),
      debounce_triggered_(false),
      running_(false),
      stop_thread_(false),
      monitor_thread_(),
      monitor_mutex_(),
      cv_(),
      last_error_(),
      status_(Status::NotConfigured),
      resolved_line_(),
      chip_(nullptr)
#if GPIOD_API_MAJOR >= 2
      ,
      request_(std::nullopt),
      event_buf_{16}
#endif
{
}

GPIOInput::~GPIOInput()
{
    (void)stop();
}

void GPIOInput::releaseGPIOResources()
{
#if GPIOD_API_MAJOR >= 2
    if (request_)
    {
        request_.reset();
    }
#else
    try
    {
        line_.release();
    }
    catch (...)
    {
        // Ignore stale or already released handles.
    }
#endif

    chip_.reset();
}

bool GPIOInput::enable(int pin,
                       bool trigger_high,
                       PullMode pull_mode,
                       std::function<void()> callback)
{
    last_error_.clear();

    if (running_)
    {
        (void)stop();
    }

    {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        // Defensive cleanup for stale libgpiod handles left behind by a
        // previous failed init or prior stop before we request the line again.
        releaseGPIOResources();
    }

    {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        gpio_pin_ = pin;
        trigger_high_ = trigger_high;
        pull_mode_ = pull_mode;
        callback_ = std::move(callback);
        debounce_triggered_ = false;
        stop_thread_ = false;
        status_ = Status::NotConfigured;
        resolved_line_ = ResolvedGPIOLine{};
    }

    try
    {
        std::string resolution_error;
        if (!resolve_gpio_line(gpio_pin_, resolved_line_, resolution_error))
        {
            last_error_ =
                "GPIOInput resolution failure for BCM " +
                std::to_string(gpio_pin_) + ": " + resolution_error;
            llog.logS(
                ERROR,
                last_error_);
            status_ = Status::Error;
            running_ = false;
            return false;
        }

#if GPIOD_API_MAJOR >= 2
        chip_ = std::make_unique<gpiod::chip>(resolved_line_.chip_path);

        gpiod::line_settings ls;
        ls.set_direction(gpiod::line::direction::INPUT);
        ls.set_edge_detection(trigger_high_
            ? gpiod::line::edge::RISING
            : gpiod::line::edge::FALLING);

        ls.set_debounce_period(std::chrono::milliseconds(100));

        switch (pull_mode_)
        {
            case PullMode::PullUp:
                ls.set_bias(gpiod::line::bias::PULL_UP);
                break;
            case PullMode::PullDown:
                ls.set_bias(gpiod::line::bias::PULL_DOWN);
                break;
            default:
                ls.set_bias(gpiod::line::bias::DISABLED);
                break;
        }

        auto builder = chip_->prepare_request();
        builder.set_consumer("GPIOInput");
        gpiod::line::offset off = resolved_line_.offset;
        request_ = builder.add_line_settings(off, ls).do_request();

        llog.logS(DEBUG,
                  "GPIOInput: request success for ",
                  describe_resolved_gpio_line(resolved_line_),
                  " edge:",
                  trigger_high_ ? "RISING" : "FALLING",
                  " bias:",
                  (pull_mode_ == PullMode::PullUp)   ? "PULL_UP" :
                  (pull_mode_ == PullMode::PullDown) ? "PULL_DOWN" :
                                                       "DISABLED", ".");
#else
        chip_ = std::make_unique<gpiod::chip>(resolved_line_.chip_path);

        line_ = chip_->get_line(
            static_cast<unsigned int>(resolved_line_.offset));

        gpiod::line_request req;
        req.consumer = "GPIOInput";
        req.request_type = trigger_high_
            ? gpiod::line_request::EVENT_RISING_EDGE
            : gpiod::line_request::EVENT_FALLING_EDGE;

        req.flags = (pull_mode_ == PullMode::PullUp)
            ? gpiod::line_request::FLAG_BIAS_PULL_UP
            : (pull_mode_ == PullMode::PullDown)
                ? gpiod::line_request::FLAG_BIAS_PULL_DOWN
                : 0;

        line_.request(req);

        llog.logS(DEBUG,
                  "GPIOInput: request success for ",
                  describe_resolved_gpio_line(resolved_line_),
                  " edge:",
                  trigger_high_ ? "RISING" : "FALLING",
                  " bias:",
                  (pull_mode_ == PullMode::PullUp)   ? "PULL_UP" :
                  (pull_mode_ == PullMode::PullDown) ? "PULL_DOWN" :
                                                       "DISABLED", ".");
#endif
    }
    catch (const std::exception& e)
    {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        last_error_ =
            "GPIOInput request failure for " +
            describe_resolved_gpio_line(resolved_line_) + ": " + e.what();
        releaseGPIOResources();
        resolved_line_ = ResolvedGPIOLine{};
        llog.logS(ERROR, last_error_);
        status_ = Status::Error;
        running_ = false;
        return false;
    }

    try
    {
        running_ = true;
        status_ = Status::Running;
        monitor_thread_ = std::thread(&GPIOInput::monitorLoop, this);
    }
    catch (const std::exception& e)
    {
        last_error_ =
            "GPIOInput thread start failure for " +
            describe_resolved_gpio_line(resolved_line_) + ": " + e.what();
        llog.logS(ERROR, last_error_);
        {
            std::lock_guard<std::mutex> lock(monitor_mutex_);
            releaseGPIOResources();
            resolved_line_ = ResolvedGPIOLine{};
        }
        status_ = Status::Error;
        running_ = false;
        return false;
    }

    return true;
}

bool GPIOInput::stop()
{
    const bool was_running = running_.exchange(false);

    stop_thread_ = true;
    cv_.notify_all();

    if (monitor_thread_.joinable())
    {
        monitor_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        releaseGPIOResources();
        resolved_line_ = ResolvedGPIOLine{};
        status_ = Status::Stopped;
    }

    return was_running;
}

void GPIOInput::resetTrigger()
{
    debounce_triggered_ = false;
}

bool GPIOInput::setPriority(int schedPolicy, int priority)
{
    if (!monitor_thread_.joinable())
    {
        return false;
    }

    sched_param sch_params;
    sch_params.sched_priority = priority;

    int ret = pthread_setschedparam(
        monitor_thread_.native_handle(),
        schedPolicy,
        &sch_params);

    return (ret == 0);
}

GPIOInput::Status GPIOInput::getStatus() const
{
    return status_;
}

const std::string &GPIOInput::lastError() const noexcept
{
    return last_error_;
}

void GPIOInput::monitorLoop()
{
    while (!stop_thread_)
    {
        try
        {
#if GPIOD_API_MAJOR >= 2
            if (request_ && request_->wait_edge_events(std::chrono::seconds(1)))
            {
                auto count = request_->read_edge_events(event_buf_);
                if (count > 0 && !debounce_triggered_)
                {
                    llog.logS(DEBUG,
                              "GPIOInput: edge event on ",
                              describe_resolved_gpio_line(resolved_line_),
                              ", count ",
                              count,
                              ".");
                    debounce_triggered_ = true;
                    if (callback_)
                    {
                        callback_();
                    }
                }
            }
#else
            if (line_.event_wait(std::chrono::seconds(1)))
            {
                (void)line_.event_read();
                if (!debounce_triggered_)
                {
                    llog.logS(DEBUG,
                              "GPIOInput: edge event on ",
                              describe_resolved_gpio_line(resolved_line_),
                              ".");
                    debounce_triggered_ = true;
                    if (callback_)
                    {
                        callback_();
                    }
                }
            }
#endif
        }
        catch (const std::exception& e)
        {
            last_error_ =
                "GPIOInput loop failure for " +
                describe_resolved_gpio_line(resolved_line_) + ": " + e.what();
            status_ = Status::Error;
            running_ = false;
            stop_thread_ = true;
            llog.logS(ERROR, last_error_);
        }
    }
}
