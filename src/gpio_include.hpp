/**
 * @file gpio_include.hpp
 * @brief Centralizes libgpiod includes and API-major selection.
 *
 * This project is licensed under the MIT License. See LICENSE.md for more
 * information.
 *
 * Copyright © 2023 - 2026 Lee C. Bussy (@LBussy).
 *
 * The build system should define GPIOD_API_MAJOR (for example via pkg-config
 * detection in the Makefile). This header intentionally avoids fragile
 * auto-detection via private headers.
 */

#ifndef GPIO_INCLUDE_HPP
#define GPIO_INCLUDE_HPP

#include <gpiod.hpp>

/*
 * The build system should define GPIOD_API_MAJOR.
 * Fall back to v1 if it is not defined.
 */
#ifndef GPIOD_API_MAJOR
#  define GPIOD_API_MAJOR 1
#endif

#if GPIOD_API_MAJOR >= 2
using GpioLineOffset = gpiod::line::offset;

inline GpioLineOffset gpio_line_offset(unsigned int offset)
{
    return gpiod::line::offset(offset);
}
#else
using GpioLineOffset = unsigned int;

inline GpioLineOffset gpio_line_offset(unsigned int offset)
{
    return offset;
}
#endif

// #ifdef DEBUG_WSPR
// #  ifndef __PRINTED_GPIOD_VERSION
// #    define __PRINTED_GPIOD_VERSION 1
// #    if GPIOD_API_MAJOR >= 2
// #      pragma message "Compiling with libgpiod v2 API"
// #    else
// #      pragma message "Compiling with libgpiod v1 API"
// #    endif
// #  endif
// #endif

#endif // GPIO_INCLUDE_HPP
