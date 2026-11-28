#ifndef _WIN32
#include <unistd.h>
#endif
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

// Primary header for this source file
#include "version.hpp"

#include "build_metadata.hpp"
#include "backend_capabilities.hpp"

#include "logging.hpp"

// Standard library headers
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

#ifdef DEBUG_WSPR
constexpr bool kDebugWspr = true;
#else
constexpr bool kDebugWspr = false;
#endif

namespace
{
    std::optional<int> g_pi_generation_override;
    bool g_hardware_platform_detection_enabled = true;
    std::optional<bool> g_rp1_gpclk_provider_available_override;

    int generation_from_pi_model(const std::string &model)
    {
        // The revision suffix is optional and never identifies the board.
        static const std::regex patterns[] = {
            std::regex(R"(Raspberry Pi (Model [AB](\+| Plus)?|Zero( W)?|Compute Module( 1)?)( Rev [0-9]+\.[0-9]+)?)"),
            std::regex(R"(Raspberry Pi 2 Model B( Rev [0-9]+\.[0-9]+)?)"),
            std::regex(R"(Raspberry Pi (3 Model [AB](\+| Plus)?|Zero 2 W|Compute Module 3(\+| Plus)?)( Rev [0-9]+\.[0-9]+)?)"),
            std::regex(R"(Raspberry Pi (4 Model B|400|Compute Module 4S?)( Rev [0-9]+\.[0-9]+)?)"),
            std::regex(R"(Raspberry Pi (5( Model B)?|500\+?|Compute Module 5)( Rev [0-9]+\.[0-9]+)?)"),
        };
        for (std::size_t index = 0; index < std::size(patterns); ++index)
        {
            if (std::regex_match(model, patterns[index]))
                return static_cast<int>(index) + 1;
        }
        return -1;
    }
}

/**
 * @brief Converts a value to a string view.
 *
 * This function takes a value and returns a `std::string_view` representation.
 * It is typically used for converting macro values or constant expressions into
 * a more manageable and efficient string view.
 *
 * @tparam T The type of the input value (usually a string literal or macro).
 * @param value The value or macro to convert.
 * @return A `std::string_view` representing the input value.
 */
template <typename T>
constexpr std::string_view to_string_view(T value) noexcept
{
    return std::string_view(value);
}

// Fallback definitions for version, branch, executable, and project
#ifndef MAKE_TAG
#define MAKE_TAG "unknown" ///< Fallback for the build tag.
#endif

#ifndef MAKE_BRH
#define MAKE_BRH "unknown" ///< Fallback for the branch name.
#endif

#ifndef MAKE_RAW_BRH
#define MAKE_RAW_BRH "unknown" ///< Fallback for the raw branch name.
#endif

#ifndef MAKE_BRANCH_STATE
#define MAKE_BRANCH_STATE "unknown" ///< Fallback for the Git branch state.
#endif

#ifndef MAKE_COMMIT
#define MAKE_COMMIT "unknown" ///< Fallback for the commit SHA.
#endif

#ifndef MAKE_DIRTY
#define MAKE_DIRTY "unknown" ///< Fallback for the build dirty state.
#endif

#ifndef MAKE_EXE
#define MAKE_EXE "unknown" ///< Fallback for the executable name.
#endif

#ifndef MAKE_PRJ
#define MAKE_PRJ "unknown" ///< Fallback for the project name.
#endif

#if WSPRRYPI_CONTROLLER_ONLY
#ifndef MAKE_CONTROLLER_INPUTS
#define MAKE_CONTROLLER_INPUTS ""
#endif
namespace
{
    static_assert(std::string_view(WSPRRYPI_COMPILED_BACKENDS).empty());
    static_assert(!WSPRRYPI_ANCILLARY_GPIO);
    // The offline artifact verifier reads this section without running the executable.
#if defined(__linux__)
    [[gnu::used, gnu::retain, gnu::section(".wsprrypi.controller")]]
#endif
    const char controller_artifact_identity[] =
        "wsprrypi-controller/1 " MAKE_COMMIT " " MAKE_TAG " " MAKE_DIRTY " " MAKE_CONTROLLER_INPUTS;
}

const char *controller_build_identity() noexcept
{
    return controller_artifact_identity;
}
#endif

// Compile-time string views for sanitized values
constexpr std::string_view SANITIZED_TAG = to_string_view(MAKE_TAG); ///< Sanitized build tag.
constexpr std::string_view SANITIZED_BRH = to_string_view(MAKE_BRH); ///< Sanitized branch name.
constexpr std::string_view RAW_BRH = to_string_view(MAKE_RAW_BRH); ///< Raw branch name.
constexpr std::string_view BRANCH_STATE = to_string_view(MAKE_BRANCH_STATE); ///< Git branch state.
constexpr std::string_view SANITIZED_COMMIT = to_string_view(MAKE_COMMIT); ///< Sanitized commit SHA.
constexpr std::string_view BUILD_DIRTY = to_string_view(MAKE_DIRTY); ///< Build-time dirty state.

std::string get_compiled_backends()
{
    return std::string(WSPRRYPI_COMPILED_BACKENDS);
}

bool has_ancillary_gpio() noexcept
{
    return WSPRRYPI_ANCILLARY_GPIO;
}
constexpr std::string_view SANITIZED_EXE = to_string_view(MAKE_EXE); ///< Sanitized executable name.
constexpr std::string_view SANITIZED_PRJ = to_string_view(MAKE_PRJ); ///< Sanitized project name.

/**
 * @brief Retrieves the executable version.
 *
 * This function returns the version of the executable as a string.
 * If the `MAKE_TAG` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the executable version.
 */
std::string get_exe_version()
{
    return std::string(SANITIZED_TAG);
}

/**
 * @brief Retrieves the current branch name.
 *
 * This function returns the name of the Git branch associated with the build.
 * If the `MAKE_BRH` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the branch name.
 */
std::string get_exe_branch()
{
    return std::string(SANITIZED_BRH);
}

/**
 * @brief Retrieves the raw branch name.
 *
 * This function returns the Git branch name associated with the build.
 * If the `MAKE_RAW_BRH` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the raw build branch name.
 */
std::string get_exe_raw_branch()
{
    return std::string(RAW_BRH);
}

/**
 * @brief Retrieves the Git branch state captured at build time.
 *
 * This distinguishes normal branch builds from detached HEAD or unavailable
 * Git metadata so the update checker does not treat "HEAD" as an upstream
 * branch name.
 *
 * @return A `std::string` containing "branch", "detached", or "unknown".
 */
std::string get_exe_branch_state()
{
    return std::string(BRANCH_STATE);
}

/**
 * @brief Retrieves the current commit SHA.
 *
 * This function returns the Git commit SHA associated with the build.
 * If the `MAKE_COMMIT` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the build commit SHA.
 */
std::string get_exe_commit()
{
    return std::string(SANITIZED_COMMIT);
}

/**
 * @brief Retrieves whether the source tree had local modifications at build time.
 *
 * The value is captured by the build system and describes local modifications
 * at compile time. It does not indicate whether a remote update exists.
 *
 * @return A `std::string` containing "true", "false", or "unknown".
 */
std::string get_exe_build_dirty()
{
    return std::string(BUILD_DIRTY);
}

/**
 * @brief Retrieves the executable name.
 *
 * This function returns the name of the executable as defined by the build system.
 * If the `MAKE_EXE` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the executable name.
 */
std::string get_exe_name()
{
    return std::string(SANITIZED_EXE);
}

/**
 * @brief Retrieves the project name.
 *
 * This function returns the project name as defined by the build system.
 * If the `MAKE_PRJ` macro is not defined, it returns "unknown".
 *
 * @return A `std::string` representing the project name.
 */
std::string get_project_name()
{
    return std::string(SANITIZED_PRJ);
}

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
std::string get_debug_state()
{
    return kDebugWspr ? "DEBUG" : "INFO";
}

/**
 * @brief Represents a mapping between a processor type and its corresponding integer value.
 *
 * This structure is used to associate a specific processor type, represented
 * as a string, with an integer value. It is useful for identifying and
 * categorizing different Raspberry Pi processor models or other devices.
 *
 * @struct ProcessorMapping
 * @var type
 *      The processor type as a string (e.g., "BCM2835", "BCM2711").
 * @var value
 *      The corresponding integer value representing the processor type.
 *
 * @example
 * ```cpp
 * ProcessorMapping mapping = {"BCM2835", 1};
 * std::cout << "Processor: " << mapping.type << ", Value: " << mapping.value << std::endl;
 * ```
 */
struct ProcessorMapping
{
    std::string type; ///< The processor type (e.g., "BCM2835").
    int value;        ///< Integer value representing the processor type.
};

/**
 * @brief Array of processor type mappings for Raspberry Pi models.
 *
 * This array associates specific Raspberry Pi processor types with corresponding
 * integer values. It is used to identify the processor type and facilitate
 * conditional logic based on the hardware platform.
 *
 * Each entry in the array represents a known Broadcom processor used in
 * Raspberry Pi boards, with an associated integer value for easy reference.
 *
 * @var processorMappings
 * @details The integer value typically represents the generation or family of the
 * processor. For example, the BCM2835 corresponds to the original Raspberry Pi
 * models, while the BCM2711 is used in Raspberry Pi 4 and later.
 */
const ProcessorMapping processorMappings[] = {
    {"BCM2835", 0}, ///< Raspberry Pi 1 and Zero series
    {"BCM2836", 1}, ///< Raspberry Pi 2 series
    {"BCM2837", 2}, ///< Raspberry Pi 3 series
    {"BCM2838", 3}, ///< Raspberry Pi 4 and Pi 400 series (alias)
    {"BCM2711", 3}, ///< Raspberry Pi 4 and Pi 400 series
    {"BCM2712", 4}  ///< Raspberry Pi RP1 hosts
};

// A device-tree compatible property is a NUL-delimited list of identifiers.
std::string get_processor_string()
{
    std::ifstream compatible_file("/sys/firmware/devicetree/base/compatible",
                                  std::ios::binary);
    if (!compatible_file)
    {
        llog.logE(ERROR, "Failed to open /sys/firmware/devicetree/base/compatible.");
        return "Unknown CPU";
    }

    std::string compatible;
    while (std::getline(compatible_file, compatible, '\0'))
    {
        if (!compatible.starts_with("brcm,bcm")) continue;
        std::string processor = compatible.substr(5);
        std::transform(processor.begin(), processor.end(), processor.begin(),
            [](unsigned char byte) { return static_cast<char>(std::toupper(byte)); });
        for (const auto &mapping : processorMappings)
        {
            if (processor == mapping.type) return processor;
        }
    }
    return "Unknown CPU Model";
}

/**
 * @brief Retrieves the processor type as an integer value.
 *
 * This function determines the Raspberry Pi's processor type and returns an
 * associated integer value based on predefined mappings in the
 * `processorMappings` array. If the processor type cannot be identified or
 * is unknown, the function returns -1.
 *
 * @return int The integer value corresponding to the detected processor type,
 *             or -1 if the type is unknown or cannot be determined.
 *
 * @details The integer value returned corresponds to the generation or family
 *          of the Broadcom processor used in Raspberry Pi models. For example:
 *          - BCM2835 returns 0 (Raspberry Pi 1 and Zero)
 *          - BCM2836 returns 1 (Raspberry Pi 2)
 *          - BCM2837 returns 2 (Raspberry Pi 3)
 *          - BCM2711 returns 3 (Raspberry Pi 4 and Pi 400)
 *
 * @throws None This function does not throw exceptions but returns -1 if an
 *         error occurs.
 */
int get_processor_type_int()
{
    // Retrieve the processor type as a string
    std::string processorType = get_processor_string();

    // Check if the processor type is empty (indicating an error)
    if (processorType.empty())
    {
        llog.logE(ERROR, "Failed to get processor type.");
        return -1; ///< Return -1 to indicate an error
    }

    // Search for the processor type in the known mappings
    for (const auto &mapping : processorMappings)
    {
        if (processorType == mapping.type)
        {
            return mapping.value; ///< Return the associated integer value
        }
    }

    // If the type is not found, log a warning and return -1
    llog.logE(ERROR, "Unknown processor type: ", processorType);
    return -1;
}

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
std::string get_pi_model()
{
    static std::string model; // cache so we only read once
    if (!model.empty())
        return model;

    std::ifstream modelFile("/proc/device-tree/model", std::ios::binary);
    if (!modelFile.is_open())
    {
        llog.logE(ERROR, "Failed to open /proc/device-tree/model.");
        return "";
    }

    // Read everything up to EOF (the file has no newline)
    std::getline(modelFile, model);
    modelFile.close();

    // Strip a single trailing NUL character if present
    if (!model.empty() && model.back() == '\0')
        model.pop_back();

    return model;
}

int raspberry_pi_generation_from_identity(
    const std::string &model, std::string_view processor)
{
    const int generation = generation_from_pi_model(model);
    if (generation != -1) return generation;
    for (const auto &mapping : processorMappings)
    {
        if (processor == mapping.type) return mapping.value + 1;
    }
    return -1;
}

int get_raspberry_pi_generation()
{
    // An explicit typed fixture identity requires no hardware discovery. Keep
    // it usable when simulated main disables all host platform probing.
    if (g_pi_generation_override.has_value())
    {
        return *g_pi_generation_override;
    }

    if (!g_hardware_platform_detection_enabled)
        return -1;

    static const int generation = [] {
        const int model = raspberry_pi_generation_from_identity(get_pi_model(), {});
        return model != -1 ? model
            : raspberry_pi_generation_from_identity({}, get_processor_string());
    }();
    return generation;
}

void set_hardware_platform_detection_enabled(bool enabled) noexcept
{
    g_hardware_platform_detection_enabled = enabled;
}

bool platform_supports_gpio_clock_transmission(
    std::string *error_message)
{
    const int generation = get_raspberry_pi_generation();
    if (generation >= 1 && generation <= 4)
    {
        return true;
    }

    if (generation >= 5)
    {
        const bool rp1_provider_available =
            g_rp1_gpclk_provider_available_override.value_or(
                #ifndef _WIN32
                !g_pi_generation_override.has_value() &&
                ::access("/dev/rp1-gpclk", R_OK | W_OK) == 0
#else
                false
#endif
            );
        if (generation == 5 && rp1_provider_available)
        {
            return true;
        }

        std::string message = generation == 5
            ? "GPIO transmission on Raspberry Pi 5 requires the canonical RP1 GPCLK provider at /dev/rp1-gpclk."
            : "GPIO transmission mode is unsupported beyond Raspberry Pi 5.";
        const std::string model = get_pi_model();
        if (!model.empty())
        {
            message += " Detected platform: " + model + ".";
        }

        if (error_message != nullptr)
        {
            *error_message = message;
        }
        return false;
    }

    if (error_message != nullptr)
    {
        std::string message =
            "GPIO transmission mode is supported only on Raspberry Pi 1 through 4.";
        const std::string model = get_pi_model();
        if (!model.empty())
        {
            message += " Detected platform: " + model + ".";
        }
        else
        {
            message += " Platform generation could not be confirmed.";
        }
        *error_message = message;
    }

    return false;
}

bool operator_exposes_rp1_gpio() noexcept
{
    return get_raspberry_pi_generation() == 5 && WSPRRYPI_BACKEND_RP1_GPCLK;
}

void set_raspberry_pi_generation_override_for_test(int generation) noexcept
{
    g_pi_generation_override = generation;
}

void clear_raspberry_pi_generation_override_for_test() noexcept
{
    g_pi_generation_override.reset();
}

void set_rp1_gpclk_provider_available_override_for_test(bool available) noexcept
{
    g_rp1_gpclk_provider_available_override = available;
}

void clear_rp1_gpclk_provider_available_override_for_test() noexcept
{
    g_rp1_gpclk_provider_available_override.reset();
}

/**
 * @brief Constructs a formatted decorated version string for the project.
 *
 * This function combines the project name, executable version, and branch name
 * into a human-readable version string. It is useful for logging and version
 * identification purposes.
 *
 * @return std::string A formatted string containing project version information.
 * @example Example output: "MyProject version 1.2.3 (main)."
 */
std::string get_version_string()
{
    // Retrieve project details.
    std::string proj = get_project_name(); ///< Project name.
    std::string ver = get_exe_version();   ///< Executable version.
    std::string br = get_exe_branch();     ///< Git branch name.

    // Construct and return the formatted version string.
    return proj + " version " + ver + " (" + br + ").";
}

/**
 * @brief Constructs a formatted plain version string for the project.
 *
 * This function combines the project name, executable version, and branch name
 * into a version string without any other decorations.
 *
 * @return std::string A formatted string containing project version information.
 * @example Example output: "1.2.3 (main)"
 */
std::string get_raw_version_string()
{
    // Retrieve project details.
    std::string proj = get_project_name(); ///< Project name.
    std::string ver = get_exe_version();   ///< Executable version.
    std::string br = get_exe_branch();     ///< Git branch name.

    // Construct and return the formatted version string.
    return ver + " (" + br + ")";
}

std::string get_os_version_name()
{
#if defined(__APPLE__)
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#else
    std::ifstream file("/etc/os-release");
    if (!file)
        return "Unknown OS";

    constexpr const char prefix[] = "PRETTY_NAME=";

    std::string line;
    std::string pretty;

    while (std::getline(file, line))
    {
        if (line.rfind(prefix, 0) == 0)
        {
            pretty = line.substr(sizeof(prefix) - 1); // exclude null terminator
            break;
        }
    }

    // Strip surrounding quotes if present
    if (pretty.size() >= 2 &&
        pretty.front() == '"' &&
        pretty.back() == '"')
    {
        pretty = pretty.substr(1, pretty.size() - 2);
    }

    return pretty.empty() ? "Unknown OS" : pretty;
#endif
}
