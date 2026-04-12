/**
 * @file band_gpio_selector.cpp
 * @brief Runtime GPIO lifecycle for scheduler-selected amateur bands.
 *
 * This file implements the BandGPIOSelector class, which selects the
 * appropriate GPIO based on the active amateur band and controls its
 * state during transmission.
 *
 * It bridges frequency-based band lookup (WSPRBandLookup) with GPIO
 * control (GPIOOutput), using configuration provided by band_gpio.*.
 * Scheduling decides which band should be active; this helper only
 * prepares, asserts, and releases the selected output safely.
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

#include "logging.hpp"

bool BandGPIOSelector::prepareBand(HamBand band)
{
    return prepareBand(band, gpio_config_for_band(band));
}

bool BandGPIOSelector::prepareBand(
    HamBand band,
    const BandGPIOConfig &config)
{
    if (!enabled_)
    {
        llog.logS(DEBUG, tag,
                  "Unified scheduler selector prepare skipped; band GPIO control disabled.");
        return true;
    }

    stop();

    if (!config.enabled || config.gpio < 0)
    {
        llog.logS(DEBUG, tag,
                  "Unified scheduler selector prepare failed for band ",
                  band_to_string(band),
                  ": no enabled GPIO is configured.");
        return false;
    }

    has_band_ = true;
    current_band_ = band;
    current_config_ = config;

    if (!drive_gpio_)
    {
        llog.logS(DEBUG, tag,
                  "Unified scheduler selector prepared band ",
                  band_to_string(band),
                  ", GPIO ",
                  config.gpio,
                  ", polarity ",
                  (config.active_high ? "active high" : "active low"),
                  ", drive ",
                  (drive_gpio_ ? "enabled" : "disabled"));
        return true;
    }

    if (!gpio_.enableGPIOPin(config.gpio, config.active_high))
    {
        if (!gpio_.lastError().empty())
        {
            llog.logS(ERROR, tag,
                      "Unified scheduler selector failed to prepare band ",
                      band_to_string(band),
                      ", GPIO ",
                      config.gpio,
                      ": ",
                      gpio_.lastError());
        }
        else
        {
            llog.logS(ERROR, tag,
                      "Unified scheduler selector failed to prepare band ",
                      band_to_string(band),
                      ", GPIO ",
                      config.gpio,
                      ".");
        }
        has_band_ = false;
        current_config_ = BandGPIOConfig{};
        return false;
    }

    llog.logS(DEBUG, tag,
              "Unified scheduler selector prepared band ",
              band_to_string(band),
              ", GPIO ",
              config.gpio,
              ", polarity ",
              (config.active_high ? "active high" : "active low"),
              ", drive enabled");

    return true;
}

bool BandGPIOSelector::setBandState(bool state)
{
    if (!enabled_)
    {
        llog.logS(DEBUG, tag,
                  "Unified scheduler selector ",
                  (state ? "assert" : "deassert"),
                  " skipped; band GPIO control disabled.");
        return true;
    }

    if (!has_band_ || !current_config_.enabled || current_config_.gpio < 0)
    {
        llog.logS(DEBUG, tag,
                  "Unified scheduler selector failed to ",
                  (state ? "assert" : "deassert"),
                  ": no band GPIO is prepared.");
        return false;
    }

    if (!drive_gpio_)
    {
        llog.logS(DEBUG, tag,
                  "Unified scheduler selector ",
                  (state ? "assert band " : "deassert band "),
                  band_to_string(current_band_),
                  ", GPIO ",
                  current_config_.gpio,
                  ", polarity ",
                  (current_config_.active_high ? "active high" : "active low"),
                  ", drive disabled");
        return true;
    }

    const bool ok = gpio_.toggleGPIO(state);

    llog.logS(ok ? DEBUG : ERROR,
              tag,
              "Unified scheduler selector ",
              (state ? "asserted band " : "deasserted band "),
              band_to_string(current_band_),
              ", GPIO ",
              current_config_.gpio,
              ", polarity ",
              (current_config_.active_high ? "active high" : "active low"),
              ", result ",
              (ok ? "ok" : "failed"));

    return ok;
}

void BandGPIOSelector::stop()
{
    if (!enabled_)
    {
        return;
    }

    if (drive_gpio_)
    {
        if (has_band_)
        {
            llog.logS(DEBUG, tag,
                      "Unified scheduler selector releasing band ",
                      band_to_string(current_band_),
                      ", GPIO ",
                      current_config_.gpio,
                      ".");
        }
        gpio_.stop();
    }
    else
    {
        if (!drive_gpio_ && has_band_)
        {
            llog.logS(DEBUG, tag,
                      "Unified scheduler selector releasing band ",
                      band_to_string(current_band_),
                      ", GPIO ",
                      current_config_.gpio,
                      ", drive ",
                      (drive_gpio_ ? "enabled" : "disabled"));
        }
    }

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
