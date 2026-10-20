/**
 * @file gpio_output_unavailable.cpp
 * @brief Hardware-free GPIO output implementation for builds without ancillary GPIO.
 *
 * This project is licensed under the MIT License. See LICENSE.md for more
 * information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "gpio_output.hpp"

GPIOOutput ledControl;
GPIOOutput ampControl;

GPIOOutput::GPIOOutput()
    : pin_(-1),
      active_high_(true),
      enabled_(false),
      last_logical_state_(false),
      last_error_()
{
}

GPIOOutput::~GPIOOutput() = default;

bool GPIOOutput::enableGPIOPin(int pin, bool active_high)
{
    pin_ = pin;
    active_high_ = active_high;
    enabled_ = false;
    last_error_ = "Ancillary GPIO is unavailable in this build.";
    return false;
}

void GPIOOutput::stop()
{
    enabled_ = false;
    last_logical_state_ = false;
}

bool GPIOOutput::toggleGPIO(bool state)
{
    if (!state)
    {
        last_logical_state_ = false;
        last_error_.clear();
        return true;
    }
    last_error_ = "Ancillary GPIO is unavailable in this build.";
    return false;
}

int GPIOOutput::compute_physical_state(bool logical_state) const
{
    return active_high_ ? static_cast<int>(logical_state)
                        : static_cast<int>(!logical_state);
}

void GPIOOutput::setTestMode(bool) noexcept
{
}

bool GPIOOutput::testModeEnabled() noexcept
{
    return false;
}

void GPIOOutput::clearTestEvents() noexcept
{
}

std::vector<GPIOOutput::TestEvent> GPIOOutput::testEvents()
{
    return {};
}

std::optional<bool> GPIOOutput::testLogicalStateForPin(int) noexcept
{
    return std::nullopt;
}
