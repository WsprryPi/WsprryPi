#ifndef BAND_GPIO_HPP
#define BAND_GPIO_HPP

/**
 * @file band_gpio.hpp
 * @brief Band-to-GPIO mapping helpers.
 */

#include <cstdint>

/**
 * @brief Supported amateur radio bands for GPIO band selection.
 */
enum class HamBand
{
    BAND_2200M,
    BAND_630M,
    BAND_160M,
    BAND_80M,
    BAND_60M,
    BAND_40M,
    BAND_30M,
    BAND_22M,
    BAND_20M,
    BAND_17M,
    BAND_15M,
    BAND_12M,
    BAND_10M,
    BAND_6M
};

/**
 * @brief Return the BCM GPIO assigned to a band.
 *
 * @param band The band to look up.
 * @return The BCM GPIO number, or -1 if the band is invalid.
 */
constexpr int gpio_for_band(HamBand band)
{
    switch (band)
    {
        case HamBand::BAND_2200M: return 17;
        case HamBand::BAND_630M:  return 27;
        case HamBand::BAND_160M:  return 22;
        case HamBand::BAND_80M:   return 23;
        case HamBand::BAND_60M:   return 24;
        case HamBand::BAND_40M:   return 25;
        case HamBand::BAND_30M:   return 5;
        case HamBand::BAND_22M:   return 6;
        case HamBand::BAND_20M:   return 12;
        case HamBand::BAND_17M:   return 13;
        case HamBand::BAND_15M:   return 16;
        case HamBand::BAND_12M:   return 26;
        case HamBand::BAND_10M:   return 20;
        case HamBand::BAND_6M:    return 21;
    }

    return -1;
}

/**
 * @brief Return the display name for a band.
 *
 * @param band The band to convert.
 * @return A null-terminated string describing the band.
 */
constexpr const char *band_to_string(HamBand band)
{
    switch (band)
    {
        case HamBand::BAND_2200M: return "2200m";
        case HamBand::BAND_630M:  return "630m";
        case HamBand::BAND_160M:  return "160m";
        case HamBand::BAND_80M:   return "80m";
        case HamBand::BAND_60M:   return "60m";
        case HamBand::BAND_40M:   return "40m";
        case HamBand::BAND_30M:   return "30m";
        case HamBand::BAND_22M:   return "22m";
        case HamBand::BAND_20M:   return "20m";
        case HamBand::BAND_17M:   return "17m";
        case HamBand::BAND_15M:   return "15m";
        case HamBand::BAND_12M:   return "12m";
        case HamBand::BAND_10M:   return "10m";
        case HamBand::BAND_6M:    return "6m";
    }

    return "unknown";
}

/**
 * @brief Check whether a band maps to a valid GPIO.
 *
 * @param band The band to test.
 * @return True if the band has a valid GPIO mapping.
 */
constexpr bool has_gpio_for_band(HamBand band)
{
    return gpio_for_band(band) >= 0;
}

#endif // BAND_GPIO_HPP
