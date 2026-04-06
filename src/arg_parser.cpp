
/**
 * @file arg_parser.cpp
 * @brief Command-line argument parser and configuration handler.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
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

// Primary header for this source file
#include "arg_parser.hpp"

// Project headers
#include "config_handler.hpp"
#include "gpio_input.hpp"
#include "gpio_output.hpp"
#include "logging.hpp"
#include "scheduling.hpp"
#include "signal_handler.hpp"
#include "wspr_band_lookup.hpp"
#include "wspr_transmit.hpp"

// Standard library headers
#include <algorithm>
#include <atomic>
#include <cstring>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// System headers
#include <getopt.h>

/**
 * @brief Global instance of the MonitorFile for INI file change detection.
 *
 * The `iniMonitor` object continuously monitors the specified INI file for changes.
 * It provides real-time notifications when the file is modified, enabling the application
 * to reload configuration settings dynamically without requiring a restart.
 *
 * This instance is typically used alongside the `ini` object to automatically re-validate
 * and apply updated configuration settings.
 *
 * The `iniMonitor` object works by checking the file's last modified timestamp and comparing
 * it with the previous known state. If a change is detected, it returns `true` on `changed()`.
 *
 * @see https://github.com/WsprryPi/MonitorFile for detailed documentation and examples.
 */
MonitorFile iniMonitor;

/**
 * @brief Instance of WSPRBandLookup.
 *
 * This instance of WSPRBandLookup is used to translate frequency representations:
 * - Converts from a short-hand (Hx) to a higher order (e.g., MHz) and vice versa.
 * - Validates frequency values.
 * - Translates terms (e.g., "20m") into a valid WSPR frequency.
 *
 * Use this instance for any operations requiring frequency conversions and validation
 * within the WSPR system.
 */
WSPRBandLookup lookup;

/**
 * @brief Semaphore indicating a pending INI file reload.
 *
 * The `ini_reload_pending` atomic flag acts as a semaphore to signal when an
 * INI file change has been detected and a configuration reload is required.
 * This ensures that the reload process does not conflict with an ongoing
 * transmission.
 *
 * - `true` indicates that an INI reload is pending.
 * - `false` indicates that no reload is currently required.
 *
 * This flag is typically set when the `iniMonitor` detects a file change and
 * is checked periodically by the INI monitoring thread. If a transmission is
 * in progress, the reload is deferred until the transmission completes.
 *
 * @note The atomic nature ensures thread-safe access across multiple threads.
 */
std::atomic<bool> ini_reload_pending(false);

/**
 * @brief Atomic flag indicating that a new PPM value needs to be applied.
 *
 * Set to `true` when a new PPM value has been received, signaling that
 * subsystems should reload or reconfigure based on the new frequency offset.
 */
std::atomic<bool> ppm_reload_pending(false);

/**
 * @brief List of allowed WSPR power levels.
 *
 * This constant vector stores the permitted power levels for WSPR transmissions.
 * Each element in the vector represents a discrete power setting (in dBm) that can be used
 * for a WSPR message. The defined levels ensure that only valid and recognized power levels
 * are applied during transmissions.
 *
 * The allowed power levels are: 0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60.
 */
const std::vector<int> wspr_power_levels = {0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60};

/**
 * @brief Callback for INI file change detection
 *
 * This function runs as a background thread, periodically checking
 * if the monitored INI file has been modified. When a change is detected:
 *
 * - It sets a deferred reload flag (`ini_reload_pending`) to apply the
 *   changes after the transmission completes.
 */
void callback_ini_changed()
{
    ini_reload_pending.store(true, std::memory_order_relaxed);
    if (wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING)
    {
        if (config.transmit)
        {
            // Transmit not changed, make pending change
            llog.logS(INFO, "INI file changed, reload after transmission.");
        }
        else // We are or are setting transmissions to disabled
        {
            // Execute reconfig immediately.
            set_config(true);
        }
    }
    else
    {
        // We're not transmitting, jam it in
        llog.logS(INFO, "INI file changed, reloading.");
        set_config(true);
    }
}

static void normalize_wspr_callsign(std::string &callsign)
{
    std::transform(callsign.begin(), callsign.end(), callsign.begin(), ::toupper);
}

static bool is_valid_runtime_transmit_gpio(int gpio) noexcept
{
    return is_supported_transmit_gpio(gpio);
}

static std::string transmit_gpio_validation_message()
{
    std::ostringstream oss;
    oss << "Invalid transmit GPIO. Supported GPIO values: ";

    for (std::size_t i = 0; i < kSupportedTransmitGpio.size(); ++i)
    {
        if (i != 0U)
        {
            oss << ", ";
        }

        oss << kSupportedTransmitGpio[i];
    }

    oss << ".";
    return oss.str();
}

static bool parse_tx_gpio_polarity(std::string_view raw_value, bool &active_high_out)
{
    std::string lowered(raw_value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (lowered == "high" || lowered == "active-high" || lowered == "active_high")
    {
        active_high_out = true;
        return true;
    }

    if (lowered == "low" || lowered == "active-low" || lowered == "active_low")
    {
        active_high_out = false;
        return true;
    }

    return false;
}

/**
 * @brief Rounds an input power level to the nearest valid WSPR power level.
 *
 * This function compares the given power level to the list of allowed WSPR power levels
 * and returns the level with the smallest absolute difference. It ensures that the
 * transmitted power level is one of the permitted values.
 *
 * @param power The input power level to round.
 * @return The nearest valid WSPR power level.
 */
int round_to_nearest_wspr_power(int power)
{
    // Start by assuming the first allowed power level is the closest.
    int closest = wspr_power_levels[0];
    int min_diff = std::abs(power - closest);

    // Iterate through the list of allowed power levels.
    for (int level : wspr_power_levels)
    {
        int diff = std::abs(power - level);
        // Update closest if the current level has a smaller difference.
        if (diff < min_diff)
        {
            closest = level;
            min_diff = diff;
        }
    }
    return closest;
}

static void normalize_wspr_locator(std::string &locator)
{
    std::transform(locator.begin(), locator.end(), locator.begin(), ::toupper);
}

/**
 * @brief Joins frequency strings from a specified starting index.
 *
 * This function concatenates elements from the provided vector of strings into a single string,
 * starting from the specified index. Each element is separated by a space. If the starting index is
 * equal to or greater than the number of elements in the vector, an empty string is returned.
 *
 * @param args A vector containing frequency strings.
 * @param start_index The index from which to start joining the frequency strings.
 * @return A string composed of the joined frequency values, or an empty string if no frequencies are available.
 */
std::string join_frequencies(const std::vector<std::string> &args, size_t start_index)
{
    if (start_index >= args.size())
    {
        return ""; // Return empty string if there are no frequencies
    }

    std::ostringstream oss;
    for (size_t i = start_index; i < args.size(); ++i)
    {
        if (i > start_index)
        {
            oss << " "; // Add space between elements
        }
        oss << args[i];
    }
    return oss.str();
}

bool token_looks_numeric_frequency(std::string_view token)
{
    return std::any_of(
        token.begin(),
        token.end(),
        [](unsigned char c)
        {
            return std::isdigit(c) != 0;
        });
}

static std::string trim_copy_string(std::string value)
{
    const auto first = value.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
    {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

static bool parse_gpio_number_strict(
    std::string_view raw_value,
    int &gpio_out) noexcept
{
    try
    {
        std::size_t consumed = 0;
        const std::string raw_string(raw_value);
        const int parsed_gpio = std::stoi(raw_string, &consumed);
        if (consumed != raw_string.size())
        {
            return false;
        }

        gpio_out = parsed_gpio;
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

static std::vector<std::string> split_frequency_tokens(const std::string &raw_list)
{
    std::string normalized = raw_list;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');

    std::istringstream iss(normalized);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token)
    {
        tokens.push_back(token);
    }

    return tokens;
}

static bool parse_frequency_entry_token(
    std::string_view raw_token,
    WsprDialFrequencyEntry &entry,
    std::string &error_message)
{
    const std::string token = trim_copy_string(std::string(raw_token));
    if (token.empty())
    {
        error_message = "Frequency token is empty.";
        return false;
    }

    const std::size_t at_pos = token.find('@');
    if (at_pos == std::string::npos)
    {
        entry.token = token;
        entry.control_gpio = kFrequencyEntryControlGpioUnset;
        return true;
    }

    if (token.find('@', at_pos + 1U) != std::string::npos)
    {
        error_message =
            "Invalid frequency token '" + token +
            "': only one @GPIO suffix is allowed.";
        return false;
    }

    const std::string base_token = trim_copy_string(token.substr(0, at_pos));
    const std::string gpio_token =
        trim_copy_string(token.substr(at_pos + 1U));

    if (base_token.empty())
    {
        error_message =
            "Invalid frequency token '" + token +
            "': frequency or band designator is missing before @GPIO.";
        return false;
    }

    if (gpio_token.empty())
    {
        error_message =
            "Invalid frequency token '" + token +
            "': GPIO value is missing after @.";
        return false;
    }

    int parsed_gpio = kFrequencyEntryControlGpioUnset;
    if (!parse_gpio_number_strict(gpio_token, parsed_gpio))
    {
        error_message =
            "Invalid frequency token '" + token +
            "': GPIO suffix must be an integer BCM GPIO.";
        return false;
    }

    if (!is_valid_frequency_entry_control_gpio(parsed_gpio))
    {
        error_message =
            "Invalid frequency token '" + token +
            "': GPIO suffix must be between 0 and 27.";
        return false;
    }

    entry.token = base_token;
    entry.control_gpio = parsed_gpio;
    return true;
}

/**
 * @brief Displays the usage information for the WsprryPi application.
 *
 * This function prints a brief help message to `std::cerr`, outlining
 * the command-line syntax and key options available. Optionally, an error
 * message can be displayed before the usage information.
 *
 * The function also determines whether the program exits or continues
 * running based on the `exit_code` parameter.
 *
 * @note This function **always terminates the program**, unless `exit_code`
 *       is `3`, in which case it simply returns.
 *
 * @param message An optional error message to display before the usage
 *        information. If empty, only the usage message is shown.
 * @param exit_code Determines the program's exit behavior:
 *        - `0` → Exits with `EXIT_SUCCESS`.
 *        - `1` → Exits with `EXIT_FAILURE`.
 *        - `3` → Returns from the function without exiting.
 *        - Any other value → Calls `std::exit(exit_code)`.
 *
 * @example
 * **Returning (does not exit):**
 * @code
 * print_usage();               // Prints usage and returns (default: exit_code = 3).
 * print_usage("Invalid args"); // Prints error message, then usage, returns.
 * print_usage(3);              // Prints usage and returns.
 * @endcode
 *
 * **Exiting the program:**
 * @code
 * print_usage(0);              // Prints usage and exits with EXIT_SUCCESS.
 * print_usage(1);              // Prints usage and exits with EXIT_FAILURE.
 * print_usage("Fatal error", 1); // Prints error message, then exits with EXIT_FAILURE.
 * print_usage("Custom exit", 5); // Prints error message, then exits with code 5.
 * @endcode
 */
void print_usage(const std::string &message, int exit_code)
{
    if (!message.empty())
    {
        std::cout << "\n"
                  << message << std::endl;
    }
    else
    {
        std::cerr << "\n"
                  << get_version_string() << std::endl;
    }

    std::cerr << "\nUsage:\n"
              << "  (sudo) wsprrypi [options] callsign gridsquare transmit_power dial_frequency <f2> <f3> ...\n"
              << "    OR\n"
              << "  (sudo) wsprrypi --test-tone {rf_frequency}\n\n"
              << "Options:\n"
              << "  -h, --help\n"
              << "    Display this help message.\n"
              << "  -v, --version\n"
              << "    Show the WsprryPi version.\n"
              << "  -i, --ini-file <file>\n"
              << "    Load parameters from an INI file. Provide the path and filename.\n\n"
              << "  -a, --transmit-gpio <gpio>\n"
              << "    Select the RF transmit GPIO (supported: 4 or 20).\n\n"
              << "  --tx-gpio-polarity <high|low>\n"
              << "    Set polarity for per-frequency LPF/control GPIO outputs.\n\n"
              << "See the documentation for a complete list of available options.\n\n";

    // Handle exit behavior
    switch (exit_code)
    {
    case 0:
        std::exit(EXIT_SUCCESS);
        break;
    case 1:
        std::exit(EXIT_FAILURE);
        break;
    case 3:
        return; // Simply return without exiting
    default:
        std::exit(exit_code);
        break;
    }
}

/**
 * @brief Displays the current configuration values from the INI file.
 *
 * If an INI file is loaded, this function logs the current configuration
 * settings such as transmit settings, power levels, and additional options.
 * The output format is structured for readability.
 *
 * @param reload If true, indicates that the configuration has been reloaded.
 */
void show_config_values(bool reload)
{
    // Print current configuration details
    //
    // [Control]
    llog.logS(DEBUG, "Transmit Enabled:", config.transmit ? "true" : "false");
    // [Common]
    llog.logS(DEBUG, "Call Sign:", config.callsign);
    llog.logS(DEBUG, "Grid Square:", config.grid_square);
    llog.logS(DEBUG, "Transmit Power:", config.power_dbm);
    llog.logS(DEBUG, "WSPR Dial Frequencies:", config.frequencies);
    llog.logS(DEBUG, "Transmit Pin:", config.tx_pin);
    llog.logS(DEBUG,
              "Frequency Control GPIO Polarity:",
              config.tx_freq_control_active_high ? "active high" : "active low");
    // [Extended]
    llog.logS(DEBUG, "PPM Offset:", config.ppm);
    llog.logS(DEBUG, "Synchronize with NTP:", config.use_ntp ? "true" : "false");
    llog.logS(DEBUG, "Use Frequency Randomization:", config.use_offset ? "true" : "false");
    llog.logS(DEBUG, "WSPR Audio Offset Hz:", config.wspr_audio_offset_hz);
    llog.logS(DEBUG, "Power Level:", config.power_level);
    llog.logS(DEBUG, "Use LED:", config.use_led ? "true" : "false");
    llog.logS(DEBUG, "LED on GPIO", config.led_pin);
    // [Server]
    llog.logS(DEBUG, "Web server runs on port:", config.web_port);
    llog.logS(DEBUG, "Socket server runs on port:", config.socket_port);
    llog.logS(DEBUG, "Use shutdown button:", config.use_shutdown ? "true" : "false");
    llog.logS(DEBUG, "Shutdown button GPIO", config.shutdown_pin);
}

/**
 * @brief Validates and loads configuration data from the INI class.
 *
 * This function extracts configuration values from the INI class, ensuring that
 * required parameters such as callsign, grid square, transmit power, and
 * WSPR dial-frequency list are properly set. If any critical parameter is missing
 * or invalid,
 * the function logs the error and exits the program.
 *
 * @return true if the configuration is valid, false otherwise.
 */

bool validate_config_candidate(
    ArgParserConfig &candidate,
    std::string *error_message)
{
    const bool frequencies_ok = set_frequencies(candidate);
    if (!frequencies_ok && !trim_copy_string(candidate.frequencies).empty())
    {
        if (error_message != nullptr && error_message->empty())
        {
            *error_message = "Invalid WSPR dial-frequency list.";
        }

        return false;
    }

    const bool requires_valid_transmit_gpio =
        candidate.mode == ModeType::TONE || candidate.transmit;

    if (requires_valid_transmit_gpio &&
        !is_valid_runtime_transmit_gpio(candidate.tx_pin))
    {
        if (error_message != nullptr)
        {
            *error_message = transmit_gpio_validation_message();
        }

        return false;
    }

    if (candidate.mode == ModeType::TONE)
    {
        return true;
    }

    if (candidate.mode != ModeType::WSPR)
    {
        if (error_message != nullptr)
        {
            *error_message = "Mode must be either WSPR or TONE.";
        }
        return false;
    }

    if (!candidate.transmit)
    {
        if (!frequencies_ok)
        {
            candidate.wspr_dial_freq_set.clear();
        }

        return true;
    }

    std::string callsign = candidate.callsign;
    std::string locator = candidate.grid_square;

    const bool missing_call_sign = callsign.empty();
    const bool missing_grid_square = locator.empty();
    const bool invalid_tx_power = candidate.power_dbm <= 0;
    const bool no_frequencies = candidate.wspr_dial_freq_set.empty();

    if (missing_call_sign || missing_grid_square ||
        invalid_tx_power || no_frequencies)
    {
        std::ostringstream oss;
        oss << "Missing required parameters.";
        if (missing_call_sign)
        {
            oss << " Missing callsign.";
        }
        if (missing_grid_square)
        {
            oss << " Missing grid square.";
        }
        if (invalid_tx_power)
        {
            oss << " TX power must be greater than 0 dBm.";
        }
        if (no_frequencies)
        {
            oss << " At least one WSPR dial frequency must be specified.";
        }

        if (error_message != nullptr)
        {
            *error_message = oss.str();
        }
        return false;
    }

    normalize_wspr_callsign(callsign);
    normalize_wspr_locator(locator);

    candidate.callsign = callsign;
    candidate.grid_square = locator;
    candidate.power_dbm = round_to_nearest_wspr_power(candidate.power_dbm);

    return true;
}

bool validate_config_data()
{
    ini_reload_pending.store(false, std::memory_order_relaxed);

    std::string validation_error;
    if (!validate_config_candidate(config, &validation_error))
    {
        llog.logE(FATAL, "Missing required parameters.");

        if (config.callsign.empty())
        {
            llog.logE(ERROR, " - Missing callsign.");
        }
        if (config.grid_square.empty())
        {
            llog.logE(ERROR, " - Missing grid square.");
        }
        if (config.power_dbm <= 0)
        {
            llog.logE(ERROR, " - TX power must be greater than 0 dBm.");
        }
        if (config.wspr_dial_freq_set.empty())
        {
            llog.logE(ERROR, " - At least one WSPR dial frequency must be specified.");
        }
        if ((config.mode == ModeType::TONE || config.transmit) &&
            !is_valid_runtime_transmit_gpio(config.tx_pin))
        {
            llog.logE(ERROR, " - ", transmit_gpio_validation_message());
        }

        if (config.use_ini)
        {
            llog.logE(ERROR, "Please check the INI file for missing or invalid values.");
            return false;
        }

        llog.logE(ERROR, "Please check your configuration for missing or invalid values.");
        llog.logE(ERROR, "Try: wsprrypi --help");
        std::cerr << std::endl;
        std::exit(EXIT_FAILURE);
    }

    llog.logS(INFO, "Transmit GPIO:", config.tx_pin);
    llog.logS(INFO,
              "Frequency-entry control GPIO polarity:",
              config.tx_freq_control_active_high ? "active high" : "active low");

    if (!config.use_ntp && config.ppm != 0.0)
    {
        llog.logS(INFO, "PPM value to be used for tone generation: ",
                  std::fixed, std::setprecision(2), config.ppm);
    }
    else if (!config.use_ntp && config.ppm != 0.0)
    {
        config.ppm = 0.0;
        llog.logE(WARN, "NTP disabled and PPM not set.");
    }

    if (config.use_led && (config.led_pin >= 0 && config.led_pin <= 27))
    {
        ledControl.enableGPIOPin(config.led_pin, true);
    }
    else
    {
        llog.logS(DEBUG, "Invalid or disabled LED settings, turning off LED.");
        ledControl.stop();
    }

    if (config.use_shutdown && (config.shutdown_pin >= 0 && config.shutdown_pin <= 27))
    {
        shutdownMonitor.enable(
            config.shutdown_pin,
            false,
            GPIOInput::PullMode::PullUp,
            callback_shutdown_system);
        shutdownMonitor.setPriority(SCHED_RR, 10);
    }
    else
    {
        llog.logS(DEBUG, "Disabling shutdown pin functionality.");
        shutdownMonitor.stop();
    }

    if (config.mode == ModeType::TONE)
    {
            llog.logS(
            INFO,
            "A direct RF test tone will be generated at:",
            lookup.freq_display_string(config.test_tone));
    }
    else if (config.mode == ModeType::WSPR)
    {
        if (config.transmit)
        {
            llog.logS(INFO, "WSPR packet payload:");
            llog.logS(INFO, "- Callsign:", config.callsign);
            llog.logS(INFO, "- Locator:", config.grid_square);
            llog.logS(INFO, "- Power:", config.power_dbm, " dBm");

            if (config.wspr_dial_freq_set.size() > 1)
            {
                llog.logS(INFO, "Requested WSPR dial frequencies:");

                for (const auto &freq : config.wspr_dial_freq_set)
                {
                    if (freq == 0.0)
                    {
                        llog.logS(INFO, "- Skip (0.0)");
                    }
                    else
                    {
                        llog.logS(INFO, "- ", lookup.freq_display_string(freq));
                    }
                }
            }
            else
            {
                llog.logS(
                    INFO,
                    "Requested WSPR dial frequency:",
                    lookup.freq_display_string(config.wspr_dial_freq_set[0]));
            }

            if (config.use_offset)
            {
                llog.logS(
                    INFO,
                    "A random offset will be added to all transmissions.");
            }
        }

        if (!config.use_ini)
        {
            if (config.loop_tx)
            {
                llog.logS(
                    INFO,
                    "Transmissions will continue until it receives a signal to stop.");
            }
            else
            {
                if (config.tx_iterations.load() <= 0)
                {
                    config.tx_iterations.store(1);
                    config.transmit = true;
                }
                llog.logS(
                    INFO,
                    "TX will stop after:",
                    config.tx_iterations.load(),
                    "iteration(s) of the WSPR dial-frequency list.");
            }
        }
    }
    else
    {
        llog.logE(FATAL, "Mode must be either WSPR or TONE.");
        std::exit(EXIT_FAILURE);
    }

    return true;
}

/**
 * @brief Parse and validate the configured WSPR dial-frequency list.
 *
 * @param target The configuration object to parse into.
 * @return `true` if at least one valid frequency was parsed or retained.
 */
bool set_frequencies(ArgParserConfig &target)
{
    std::string raw_list;
    try
    {
        raw_list = target.frequencies;
    }
    catch (const std::exception &e)
    {
        llog.logE(WARN, "Failed to read WSPR dial-frequency list:", e.what());
        raw_list.clear();
    }

    std::vector<double> parsed_frequencies;
    std::vector<WsprDialFrequencyEntry> parsed_entries;

    for (const std::string &token : split_frequency_tokens(raw_list))
    {
        WsprDialFrequencyEntry entry;
        std::string entry_error;
        if (!parse_frequency_entry_token(token, entry, entry_error))
        {
            llog.logE(ERROR, entry_error);
            target.wspr_dial_freq_set.clear();
            target.wspr_dial_frequency_entries.clear();
            return false;
        }

        try
        {
            const double freq = lookup.parse_string_to_frequency(entry.token, false);
            entry.dial_frequency_hz = freq;
            if (token_looks_numeric_frequency(entry.token))
            {
                const auto legacy_alias =
                    lookup.legacy_actual_wspr_alias_for_frequency(freq);
                if (legacy_alias.has_value())
                {
                    llog.logS(
                        WARN,
                        "Numeric WSPR frequency token '",
                        entry.token,
                        "' exactly matches the legacy actual RF value for alias '",
                        *legacy_alias,
                        "'. WsprryPi now interprets WSPR numeric frequencies as dial frequencies. ",
                        "If this value came from an older config, update it to the dial frequency explicitly.");
                }
            }
            parsed_frequencies.push_back(freq);
            parsed_entries.push_back(entry);
        }
        catch (const std::invalid_argument &)
        {
            llog.logE(WARN, "Ignoring invalid WSPR frequency token:", token);
        }
    }

    if (!parsed_frequencies.empty())
    {
        target.wspr_dial_freq_set = parsed_frequencies;
        target.wspr_dial_frequency_entries = parsed_entries;
        return true;
    }

    if (target.mode != ModeType::WSPR || !target.transmit)
    {
        target.wspr_dial_frequency_entries.clear();
        return true;
    }

    llog.logE(ERROR, "Empty or invalid WSPR dial-frequency list.");
    target.wspr_dial_freq_set.clear();
    target.wspr_dial_frequency_entries.clear();
    return false;
}

bool set_frequencies()
{
    return set_frequencies(config);
}

bool load_from_ini()
{
    // Attempt to load INI file if enabled
    bool loaded = config.use_ini && iniFile.load();

    if (!loaded)
    {
        return false;
    }

    // Load Control section
    try
    {
        config.transmit = iniFile.get_bool_value("Control", "Transmit");
    }
    catch (...)
    {
    }

    // Load Common section
    try
    {
        config.callsign = iniFile.get_string_value("Common", "Call Sign");
    }
    catch (...)
    {
    }
    try
    {
        config.grid_square = iniFile.get_string_value("Common", "Grid Square");
    }
    catch (...)
    {
    }
    try
    {
        config.power_dbm = iniFile.get_int_value("Common", "TX Power");
    }
    catch (...)
    {
    }
    try
    {
        config.frequencies = iniFile.get_string_value("Common", "Frequency");
    }
    catch (...)
    {
    }
    try
    {
        config.tx_pin = iniFile.get_int_value("Common", "Transmit Pin");
    }
    catch (...)
    {
    }

    // Load Extended section
    try
    {
        config.ppm = iniFile.get_double_value("Extended", "PPM");
    }
    catch (...)
    {
    }
    try
    {
        config.use_ntp = iniFile.get_bool_value("Extended", "Use NTP");
    }
    catch (...)
    {
    }
    try
    {
        config.use_offset = iniFile.get_bool_value("Extended", "Offset");
    }
    catch (...)
    {
    }
    try
    {
        config.power_level = iniFile.get_int_value("Extended", "Power Level");
    }
    catch (...)
    {
    }
    try
    {
        config.use_led = iniFile.get_bool_value("Extended", "Use LED");
    }
    catch (...)
    {
    }
    try
    {
        config.led_pin = iniFile.get_int_value("Extended", "LED Pin");
    }
    catch (...)
    {
    }

    // Load Server section
    try
    {
        config.web_port = iniFile.get_int_value("Server", "Web Port");
    }
    catch (...)
    {
    }
    try
    {
        config.socket_port = iniFile.get_int_value("Server", "Socket Port");
    }
    catch (...)
    {
    }
    try
    {
        config.use_shutdown = iniFile.get_bool_value("Server", "Use Shutdown");
    }
    catch (...)
    {
    }
    try
    {
        config.shutdown_pin = iniFile.get_int_value("Server", "Shutdown Button");
    }
    catch (...)
    {
    }

    // Synchronize config with global JSON object
    config_to_json();

    return true;
}

/**
 * @brief Handles early-exit command-line options.
 *
 * Scans the raw argument list for options that should be handled immediately,
 * before normal configuration and argument parsing occur.
 *
 * Supported early options:
 * - `-h`, `--help`
 * - `-v`, `--version`
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return true if an early option was handled and the program exited.
 * @return false if normal parsing should continue.
 */
bool handle_early_cli_options(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            print_usage("", EXIT_SUCCESS);
            return true;
        }

        if (arg == "-v" || arg == "--version")
        {
            std::cout << get_version_string() << std::endl;
            std::exit(EXIT_SUCCESS);
        }
    }

    return false;
}

bool parse_command_line(int argc, char *argv[])
{
    // Check if any arguments (besides the program name) were provided.
    if (argc == 1) // No arguments or options provided.
    {
        print_usage("No arguments provided.", EXIT_FAILURE);
    }

    // Create original JSON
    init_config_json();
    json_to_config();
    std::vector<char *> args(argv, argv + argc); // Copy arguments for modification

    // First pass: Look for "-i <file>" before processing other options
    for (auto it = args.begin() + 1; it != args.end(); ++it)
    {
        if ((std::string(*it) == "-i" || std::string(*it) == "--ini-file") && (it + 1) != args.end())
        {
            config.ini_filename = *(it + 1);
            config.use_ini = true;
            config.loop_tx = true;
            // Stage, validate, and commit the INI contents.
            std::string load_error;
            std::vector<std::string> warning_messages;

            try
            {
                iniFile.set_filename(config.ini_filename);

                if (!load_json(config.ini_filename, &load_error, &warning_messages))
                {
                    for (const auto &warning_message : warning_messages)
                    {
                        llog.logS(WARN, warning_message);
                    }

                    llog.logS(ERROR, "Configuration load failed:", load_error);
                    llog.logS(WARN, "Using safe default configuration. Transmission disabled.");

                    init_default_config();
                    config.ini_filename = *(it + 1);
                    config.use_ini = true;
                    config.loop_tx = true;
                    config.transmit = false;
                    config_to_json();
                }
                else
                {
                    for (const auto &warning_message : warning_messages)
                    {
                        llog.logS(WARN, warning_message);
                    }
                }
            }
            catch (const std::exception &e)
            {
                llog.logS(ERROR, "Configuration load failed:", e.what());
                llog.logS(WARN, "Using safe default configuration. Transmission disabled.");

                init_default_config();
                config.ini_filename = *(it + 1);
                config.use_ini = true;
                config.loop_tx = true;
                config.transmit = false;
                config_to_json();
            }

            // Remove "-i <file>" from args
            args.erase(it, it + 2);
            break; // Exit loop after removing argument
        }
    }

    // Update argc and argv pointers for getopt_long()
    argc = args.size();
    argv = args.data();

    static struct option long_options[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'v'},
        {"use-ntp", no_argument, nullptr, 'n'},       // Via: [Extended] Use NTP = True
        {"repeat", no_argument, nullptr, 'r'},        // Global: config.loop_tx
        {"offset", no_argument, nullptr, 'o'},        // Via: [Extended] Offset = True
        {"journald", no_argument, nullptr, 'J'},      // Global: config.use_journald
        {"date-time-log", no_argument, nullptr, 'D'}, // Global: config.date_time_log
        {"require-paired", no_argument, nullptr, 1001}, // Global: config.require_paired_plan
        {"tx-gpio-polarity", required_argument, nullptr, 1002},
        // Required arguments
        {"ppm", required_argument, nullptr, 'p'},       // Via: [Extended] PPM = 0.0
        {"terminate", required_argument, nullptr, 'x'}, // Global: config.tx_iterations
        {"test-tone", required_argument, nullptr, 't'}, // Global: config.test_tone
        {"transmit-gpio", required_argument, nullptr, 'a'}, // Via: [Common] Transmit Pin = 4
        {"transmit-pin", required_argument, nullptr, 'a'},
        {"led_pin", required_argument, nullptr, 'l'},         // Via: [Extended] LED Pin = 18
        {"shutdown_button", required_argument, nullptr, 's'}, // Via: [Server] Shutdown Button = 19
        {"power_level", required_argument, nullptr, 'd'},     // Via: [Extended] Power Level = 7
        {"web-port", required_argument, nullptr, 'w'},        // Via: [Server] Port = 31415
        {"socket-port", required_argument, nullptr, 'k'},     // Via: [Server] Port = 31416
        {nullptr, 0, nullptr, 0}};

    while (true)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "h?vnroJDp:x:t:a:l:s:d:w:k:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
        // No arguments
        case 'h': // Help/Usage
        case '?':
        {
            print_usage(EXIT_SUCCESS);
        }
        case 'v': // Version
        {
            std::cout << get_version_string() << std::endl;
            std::exit(EXIT_SUCCESS);
        }
        case 'n': // Use NTP
        {
            config.use_ntp = true;
            break;
        }
        case 'r': // Repeat
        {
            config.loop_tx = true;
            break;
        }
        case 'o': // Use Offset
        {
            config.use_offset = true;
            break;
        }
        case 'J': // Use journald logging backend
        {
            config.use_journald = true;
            break;
        }
        case 'D': // Add date/time stamps to stream logging
        {
            config.date_time_log = true;
            break;
        }
        case 1001: // Require paired WSPR planning
        {
            config.require_paired_plan = true;
            break;
        }
        case 1002:
        {
            bool active_high = false;
            if (!parse_tx_gpio_polarity(optarg, active_high))
            {
                print_usage(
                    "Invalid TX GPIO polarity. Expected 'high' or 'low'.",
                    EXIT_FAILURE);
            }

            config.tx_freq_control_active_high = active_high;
            break;
        }
        // Required arguments
        case 'p': // Apply PPM
        {
            try
            {
                double ppm = std::stod(optarg);
                double clamped_ppm = std::clamp(ppm, -200.0, 200.0);

                if (ppm != clamped_ppm)
                {
                    llog.logE(ERROR, "PPM value is outside bounds (-200 to 200), applying clamped value:", clamped_ppm);
                }

                // Apply the clamped value
                config.ppm = clamped_ppm;
                config.use_ntp = false;
            }
            catch (const std::exception &)
            {
                config.use_ntp = true;
                llog.logE(ERROR, "Error parsing PPM value, defaulting to NTP:", optarg);
            }
            break;
        }
        case 'x': // Terminate after x iterations
        {
            if (!config.use_ini)
            {
                try
                {
                    config.tx_iterations.store(std::stoi(optarg));
                }
                catch (const std::invalid_argument &)
                {
                    llog.logE(ERROR, "Invalid number format for transmit iterations:", optarg, "- Using default (1).");
                }
                catch (const std::out_of_range &)
                {
                    llog.logE(ERROR, "Number out of range for transmit iterations:", optarg, "- Using default (1).");
                }
                // Set config.tx_iterations to at least 1
                config.tx_iterations.store((config.tx_iterations.load() == 0) ? 1 : config.tx_iterations.load()); // Equal to at least 1
            }
            break;
        }
        case 't': // Use test-tone
        {
            if (!config.use_ini)
            {
                try
                {
                    // `--test-tone` is an explicit direct-RF path. Do not apply
                    // the WSPR USB dial-to-RF audio offset to this value.
                    config.test_tone = lookup.parse_string_to_frequency(optarg, false);
                    config.mode = ModeType::TONE;

                    if (config.test_tone <= 0.0)
                    {
                        print_usage("Invalid direct RF test tone frequency (<=0).", EXIT_FAILURE);
                    }
                }
                catch (const std::invalid_argument &e)
                {
                    std::string error_message = "Invalid direct RF test tone frequency input: " +
                                                std::string(optarg) +
                                                " Exception: " + e.what();
                    print_usage(error_message, EXIT_FAILURE);
                }
            }
            else
            {
                print_usage("Test tone is invalid when using INI file.", EXIT_FAILURE);
            }
            break;
        }
        case 'a': // Specify transmit pin
        {
            try
            {
                std::size_t consumed = 0;
                const int transmit_pin = std::stoi(optarg, &consumed);
                if (consumed != std::strlen(optarg) ||
                    !is_valid_runtime_transmit_gpio(transmit_pin))
                {
                    print_usage(transmit_gpio_validation_message(), EXIT_FAILURE);
                }

                config.tx_pin = transmit_pin;
            }
            catch (const std::exception &)
            {
                print_usage(transmit_gpio_validation_message(), EXIT_FAILURE);
            }
            break;
        }
        case 'l': // LED Pin
        {
            try
            {
                int led_pin = std::stoi(optarg);
                if (led_pin < 0 || led_pin > 27)
                {
                    print_usage("Invalid LED pin.", EXIT_FAILURE);
                }

                else
                {
                    config.led_pin = led_pin;
                    config.use_led = true;
                }
            }
            catch (const std::exception &)
            {
                print_usage("Invalid LED pin.", EXIT_FAILURE);
            }
            break;
        }
        case 's': // Shutdown button/pin
        {
            try
            {
                int shutdown_pin = std::stoi(optarg);
                if (shutdown_pin < 0 || shutdown_pin > 27)
                {
                    print_usage("Invalid shutdown pin.", EXIT_FAILURE);
                }

                else
                {
                    config.shutdown_pin = shutdown_pin;
                    config.use_shutdown = true;
                }
            }
            catch (const std::exception &)
            {
                print_usage("Invalid shutdown pin.", EXIT_FAILURE);
            }
            break;
        }
        case 'd': // Power Level 0-7
        {
            try
            {
                int power = std::stoi(optarg);
                if (power < 0 or power > 7)
                {
                    config.power_level = 7;
                }
                else
                {
                    config.power_level = power;
                }
            }
            catch (const std::exception &)
            {
                llog.logE(WARN, "Invalid power level, defaulting to 7.");
                config.power_level = 7;
            }
            break;
        }
        case 'w': // Set web port number
        {
            try
            {
                int port = std::stoi(optarg);
                if (port < 1024 || port > 49151)
                {
                    llog.logS(WARN, "Invalid web number. Using default: 31415.");
                    config.web_port = 31415;
                }
                else
                {
                    config.web_port = port;
                }
            }
            catch (const std::exception &)
            {
                llog.logE(WARN, "Invalid web port, defaulting to 31415.");
                config.web_port = 31415;
            }
            break;
        }
        case 'k': // Set socket port number
        {
            try
            {
                int port = std::stoi(optarg);
                if (port < 1024 || port > 49151)
                {
                    llog.logS(WARN, "Invalid socket port number. Using default: 31416.");
                    config.web_port = 31416;
                }
                else
                {
                    config.web_port = port;
                }
            }
            catch (const std::exception &)
            {
                llog.logE(WARN, "Invalid socket port, defaulting to 31416.");
                config.web_port = 31415;
            }
            break;
        }
        default:
        {
            llog.logE(ERROR, "Unknown argument: '", static_cast<char>(c), "'");
            break;
        }
        }
    }

    if (config.mode == ModeType::WSPR)
    {
        if (!config.use_ini)
        {
            // Handle positional arguments after parsing options
            std::vector<std::string> positional_args;
            for (int i = optind; i < argc; ++i)
            {
                positional_args.push_back(argv[i]);
            }

            // Extract required positional arguments
            if (positional_args.size() < 4)
            {
                print_usage("Missing required positional arguments: callsign, gridsquare, power, and dial_frequency.", EXIT_FAILURE);
            }

            std::string callsign = positional_args[0];
            normalize_wspr_callsign(callsign);
            config.callsign = callsign;

            std::string gridsquare = positional_args[1];
            normalize_wspr_locator(gridsquare);
            config.grid_square = gridsquare;

            // Validate power to standard values
            try
            {
                int power = std::stoi(positional_args[2]);
                int rounded_power = round_to_nearest_wspr_power(power);
                if (power != rounded_power)
                {
                    llog.logS(DEBUG, "Power rounded to standard value:", rounded_power);
                }
                config.power_dbm = rounded_power;
            }
            catch (...)
            {
                print_usage("Invalid power value. Must be an integer.", EXIT_FAILURE);
            }

            // Put frequencies in string, validate later
            // Convert frequencies (positional_args[3] and beyond) into a space-separated string
            try
            {
                config.frequencies = join_frequencies(positional_args, 3);
            }
            catch (const std::exception &e)
            {
                print_usage(std::string("Failed to capture frequencies: ") + e.what(), EXIT_FAILURE);
            }
        }
    }
    // Re-save any config changes in the JSON
    config_to_json();

    return true;
}
