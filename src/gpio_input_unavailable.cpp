/**
 * @file gpio_input_unavailable.cpp
 * @brief Hardware-free GPIO input implementation for builds without ancillary GPIO.
 *
 * This project is licensed under the MIT License. See LICENSE.md for more
 * information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "gpio_input.hpp"

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
      status_(Status::NotConfigured)
{
}

GPIOInput::~GPIOInput() = default;

bool GPIOInput::enable(
    int pin,
    bool trigger_high,
    PullMode pull_mode,
    std::function<void()> callback)
{
    gpio_pin_ = pin;
    trigger_high_ = trigger_high;
    pull_mode_ = pull_mode;
    callback_ = std::move(callback);
    status_ = Status::Error;
    last_error_ = "Ancillary GPIO is unavailable in this build.";
    return false;
}

bool GPIOInput::stop()
{
    running_ = false;
    status_ = Status::Stopped;
    return true;
}

void GPIOInput::resetTrigger()
{
    debounce_triggered_ = false;
}

bool GPIOInput::setPriority(int, int)
{
    return false;
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
}

void GPIOInput::releaseGPIOResources()
{
}
