/**
 * @file arg_parser.hpp
 * @brief Parse runtime startup choices and frequency-entry syntax.
 *
 * This layer translates CLI input into runtime configuration and transient
 * startup requests. Persistent configuration remains in `config_handler.*`.
 * In particular, `--test-tone` creates a transient startup request rather
 * than persistent config, and frequency tokens may carry optional `@GPIO`
 * suffixes consumed later by scheduling.
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

#ifndef ARG_PARSER_HPP
#define ARG_PARSER_HPP

// Project headers
#include "config_handler.hpp"
#include "ini_file.hpp"
#include "lcblog.hpp"
#include "monitorfile.hpp"
#include "version.hpp"
#include "wspr_band_lookup.hpp"

// Standard library headers
#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>

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
extern MonitorFile iniMonitor;

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
extern WSPRBandLookup lookup;

/**
 * @brief Atomic variable representing the current WSPR transmission interval.
 *
 * This variable defines the transmission interval for WSPR signals.
 * It can be set to one of the predefined constants:
 * - `WSPR_2` for a 2-minute interval.
 * - `WSPR_15` for a 15-minute interval.
 *
 * This value is updated dynamically based on the INI configuration
 * and influences when the scheduler triggers the next transmission.
 *
 * @note Access to this variable is thread-safe due to its atomic nature.
 */
extern std::atomic<int> wspr_interval;

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
extern std::atomic<bool> ini_reload_pending;
extern std::atomic<std::uint64_t> ini_reload_generation;

/**
 * @brief Atomic flag indicating that a new PPM value needs to be applied.
 *
 * Set to `true` when a new PPM value has been received, signaling that
 * subsystems should reload or reconfigure based on the new frequency offset.
 */
extern std::atomic<bool> ppm_reload_pending;

void apply_runtime_config_side_effects();

/**
 * @brief Called when the INI file is modified.
 *
 * @details
 * Executed by the `MonitorFile` watcher thread. Sets a deferred reload flag
 * (`ini_reload_pending`) to apply changes after the current transmission finishes.
 */
void callback_ini_changed();

/**
 * @brief Prints usage information to standard error.
 *
 * @details
 * Shows supported command-line options and optionally displays an error message.
 *
 * @param message Optional error message (shown above usage output).
 * @param exit_code Exit behavior:
 *   - `0`: Exit with `EXIT_SUCCESS`
 *   - `1`: Exit with `EXIT_FAILURE`
 *   - `3`: Print help and return without exiting
 *   - any other: Call `std::exit(exit_code)`
 */
void print_usage(const std::string &message = "", int exit_code = 3);

/**
 * @brief Overload for `print_usage()` that accepts only an exit code.
 *
 * @param exit_code Exit behavior (see above).
 */
inline void print_usage(int exit_code)
{
    print_usage("", exit_code);
}

/**
 * @brief Prints the current INI configuration values.
 *
 * @param reload If true, includes "reload" context in the log.
 */
extern void show_config_values(bool reload = false);

/**
 * @brief Validates and applies configuration values from the INI file.
 *
 * @return true if configuration is valid and applied.
 * @return false if validation fails (application exits).
 */
extern bool validate_config_data();

bool validate_config_candidate(
    ArgParserConfig &candidate,
    std::string *error_message = nullptr);

bool set_frequencies();
bool set_frequencies(ArgParserConfig &target);

bool set_direct_tone_startup_request(
    const std::string &raw_token,
    std::string *error_message = nullptr);
bool has_direct_tone_startup_request() noexcept;
bool try_get_direct_tone_startup_request(
    WsprDialFrequencyEntry &entry_out,
    double &actual_rf_frequency_hz_out) noexcept;
void clear_direct_tone_startup_request() noexcept;

/**
 * @brief Parses command-line arguments and configures the program settings.
 *
 * This function processes command-line options using `getopt_long()`, applying
 * values to the program configuration. It first checks for an INI file (`-i`)
 * before processing other options to ensure that command-line arguments can
 * override INI file settings.
 *
 * It validates required parameters and logs any errors, ensuring proper
 * configuration before execution.
 *
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line argument strings.
 * @return true if parsing is successful, false if an error occurs.
 */
bool handle_early_cli_options(int argc, char *argv[]);

/**
 * @brief Parses command-line arguments and applies overrides.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector.
 * @return true if parsing succeeds; false otherwise.
 */
bool parse_command_line(int argc, char *argv[]);

#endif // ARG_PARSER_HPP
