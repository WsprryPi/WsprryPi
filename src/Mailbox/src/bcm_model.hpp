/**
 * @file bcm_model.hpp
 *
 * @brief Definitions for identifying Broadcom SoC processor variants.
 * Provides an enumeration of supported BCM host processor IDs (BCM2835,
 * BCM2836, BCM2837, BCM2711) and a helper function to convert the enum
 * to a human-readable string. Used for selecting appropriate mailbox memory
 * flags and PLLD frequencies based on Raspberry Pi hardware revision.
 *
 * This project is licensed under the MIT License. See the repository root
 * LICENSE.md for more information.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
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

#ifndef BCM_CHIP_HPP
#define BCM_CHIP_HPP
#pragma once

#include <string_view>

/**
 * @enum BCMChip
 *
 * @brief Broadcom host SoC processor identifiers.
 *
 * Enumerates the processor variants found in Raspberry Pi boards:
 * BCM2835: Raspberry Pi 1
 * BCM2836: Raspberry Pi 2
 * BCM2837: Raspberry Pi 3
 * BCM2711: Raspberry Pi 4
*/
enum class BCMChip : int {
  BCM_HOST_PROCESSOR_BCM2835 = 0, // BCM2835 (RPi1)
  BCM_HOST_PROCESSOR_BCM2836 = 1, // BCM2836 (RPi2)
  BCM_HOST_PROCESSOR_BCM2837 = 2, // BCM2837 (RPi3)
  BCM_HOST_PROCESSOR_BCM2711 = 3, // BCM2711 (RPi4)
};

/**
 * @brief Convert a BCMChip enum to its string representation.
 *
 * @param chip The BCMChip value to convert.
 *
 * @return A std::string_view containing the processor name, or "Unknown" if
 *         the value does not match a known processor.
*/
constexpr std::string_view to_string(BCMChip chip) noexcept {
    switch (chip) {
      case BCMChip::BCM_HOST_PROCESSOR_BCM2835: return "BCM2835";
      case BCMChip::BCM_HOST_PROCESSOR_BCM2836: return "BCM2836";
      case BCMChip::BCM_HOST_PROCESSOR_BCM2837: return "BCM2837";
      case BCMChip::BCM_HOST_PROCESSOR_BCM2711: return "BCM2711";
    }
    return "Unknown";
}

#endif // BCM_CHIP_HPP
