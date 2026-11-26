/* SPDX-License-Identifier: MIT */
#ifndef RP1_GPIO_DRIVE_OPTIONS_HPP
#define RP1_GPIO_DRIVE_OPTIONS_HPP

#include <array>

inline constexpr int kDefaultRp1GpioDriveMa = 2;
inline constexpr std::array<int, 4> kSupportedRp1GpioDriveMa = {2, 4, 8, 12};

inline constexpr bool is_supported_rp1_gpio_drive_ma(int drive_ma) noexcept
{
    for (const int supported : kSupportedRp1GpioDriveMa)
    {
        if (drive_ma == supported)
        {
            return true;
        }
    }
    return false;
}

#endif // RP1_GPIO_DRIVE_OPTIONS_HPP
