#include "band_gpio_selector.hpp"

/*
Example code:

#include "band_gpio_selector.hpp"
#include "band_gpio.hpp"

BandGPIOSelector bandSelector;

bool set_transmit_band(HamBand band)
{
    if (!bandSelector.selectBand(band))
    {
        return false;
    }

    return bandSelector.setBandState(true);
}

void clear_transmit_band()
{
    bandSelector.setBandState(false);
}

set_transmit_band(HamBand::BAND_40M);
transmit();
clear_transmit_band();

 */

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

bool BandGPIOSelector::setBandState(bool state)
{
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
