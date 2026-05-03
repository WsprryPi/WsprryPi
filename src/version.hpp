/**
 * @file version.cpp
 * @brief Provides software and hardware version information.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
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

#ifndef VERSION_H
#define VERSION_H

#include <string>

/**
 * @brief Retrieves the project name.
 *
 * This function returns the project name as defined by the build system.
 * If the `MAKE_PRJ` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the project name.
 */
extern std::string get_project_name();

/**
 * @brief Retrieves the executable name.
 *
 * This function returns the name of the executable as defined by the build system.
 * If the `MAKE_EXE` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the executable name.
 */
extern std::string get_exe_name();

/**
 * @brief Retrieves the executable semantic version.
 *
 * @return A `std::string` representing the executable version.
 */
extern std::string get_exe_version();

/**
 * @brief Retrieves the build branch name.
 *
 * @return A `std::string` representing the build branch name.
 */
extern std::string get_exe_branch();

/**
 * @brief Retrieves the raw build branch name.
 *
 * @return A `std::string` representing the raw build branch name.
 */
extern std::string get_exe_raw_branch();

/**
 * @brief Retrieves the build commit SHA.
 *
 * @return A `std::string` representing the build commit SHA.
 */
extern std::string get_exe_commit();

/**
 * @brief Retrieves whether the source tree had local modifications at build time.
 *
 * @return A `std::string` containing "true", "false", or "unknown".
 */
extern std::string get_exe_build_dirty();

/**
 * @brief Retrieves the current debug state based on the build configuration.
 *
 * This function determines whether the current build is a debug or release
 * build by checking the presence of the `DEBUG_BUILD` macro. If the macro is
 * defined, it returns `"DEBUG"`. Otherwise, it returns `"INFO"` as the default
 * log level for non-debug builds.
 *
 * @return std::string The current debug state, either "DEBUG" or "INFO".
 *
 * @note The `DEBUG_BUILD` macro must be defined during compilation for the
 *       debug state to be set correctly. Use `-DDEBUG_BUILD` in the Makefile
 *       or build system when compiling a debug target.
 *
 * @example
 * std::string debug_state = get_debug_state();
 * std::cout << "Current debug state: " << debug_state << std::endl;
 */
extern std::string get_debug_state();

/**
 * @brief Retrieves the Raspberry Pi model as a string.
 *
 * This function reads the `/proc/device-tree/model` file to determine the
 * specific Raspberry Pi model. It returns the model as a string.
 *
 * @return std::string The Raspberry Pi model. If the model cannot be
 *         determined, an empty string is returned.
 *
 * @details
 * The function opens the `/proc/device-tree/model` file and reads the
 * first line, which typically contains the model description. If the file
 * cannot be opened, an error is logged, and an empty string is returned.
 *
 * @example
 * ```cpp
 * std::string piModel = get_pi_model();
 * std::cout << "Raspberry Pi Model: " << piModel << std::endl;
 * ```
 */
extern std::string get_pi_model();
int get_raspberry_pi_generation();
bool platform_supports_gpio_clock_transmission(
    std::string *error_message = nullptr);
void set_raspberry_pi_generation_override_for_test(int generation) noexcept;
void clear_raspberry_pi_generation_override_for_test() noexcept;

/**
 * @brief Return the operating system version name.
 *
 * Reads the operating system identification from the standard
 * /etc/os-release file and returns the PRETTY_NAME value.
 *
 * If the file is unavailable or the value cannot be determined,
 * the function returns "Unknown OS".
 *
 * @return Human-readable operating system version string.
 */
std::string get_os_version_name();

/**
 * @brief Constructs a formatted version string for the project.
 *
 * This function combines the project name, executable version, and branch name
 * into a human-readable version string. It is useful for logging and version
 * identification purposes.
 *
 * @return std::string A formatted string containing project version information.
 * @example Example output: "MyProject version 1.2.3 (main)."
 */
extern std::string get_version_string();

/**
 * @brief Constructs a formatted plain version string for the project.
 *
 * This function combines the project name, executable version, and branch name
 * into a version string without any other decorations.
 *
 * @return std::string A formatted string containing project version information.
 * @example Example output: "1.2.3 (main)"
 */
extern std::string get_raw_version_string();

/**
 * @brief Retrieves the BCM host peripheral base address from the device tree.
 *
 * This function queries the device tree to determine the base address of the
 * Broadcom (BCM) host peripherals, which is essential for low-level hardware
 * access on Raspberry Pi systems.
 *
 * @return unsigned The base address of the BCM host peripherals.
 *         Returns `0x20000000` if the address cannot be determined.
 *
 * @details
 * The function first attempts to read the address from the `/proc/device-tree/soc/ranges`
 * file at offset 4. If that fails (returns 0), it tries again at offset 8. If both attempts
 * fail, it falls back to the default base address `0x20000000` (common for older Raspberry Pi models).
 */
extern unsigned get_peripheral_address(void);

#endif // VERSION_H
