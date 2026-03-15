#ifndef BAND_GPIO_SELECTOR_HPP
#define BAND_GPIO_SELECTOR_HPP

#include "band_gpio.hpp"
#include "gpio_output.hpp"

/**
 * @brief Controls a single GPIO output using ham band names.
 *
 * This class maps ham bands to BCM GPIO numbers and uses GPIOOutput
 * to enable and drive the selected pin.
 */
class BandGPIOSelector
{
public:
    /**
     * @brief Select a band and enable its GPIO pin.
     *
     * Any previously active GPIO pin is released before the new one is enabled.
     *
     * @param band The ham band to select.
     * @param active_high True for active-high output.
     * @return True on success, false on failure.
     */
    bool selectBand(HamBand band, bool active_high = true);

    /**
     * @brief Set the currently selected band GPIO active or inactive.
     *
     * @param state True for active, false for inactive.
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

private:
    GPIOOutput gpio_;
    bool has_band_ = false;
    HamBand current_band_ = HamBand::BAND_2200M;
};

#endif // BAND_GPIO_SELECTOR_HPP
