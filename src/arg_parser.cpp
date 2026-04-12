/**
 * @file arg_parser.cpp
 * @brief Parse runtime startup choices and frequency-entry syntax.
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
#include <cctype>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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
std::atomic<std::uint64_t> ini_reload_generation(0);
static std::atomic<bool> startup_config_handoff_ready{false};
static std::atomic<bool> startup_diagnostic_deferral_enabled{false};
static std::mutex deferred_startup_diagnostics_mtx;

struct DeferredStartupDiagnostic
{
    LogLevel level = INFO;
    std::string message;
};

static std::vector<DeferredStartupDiagnostic> deferred_startup_diagnostics;

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

namespace
{
    struct DirectToneStartupRequest
    {
        WsprFrequencyEntry entry{};
        double actual_rf_frequency_hz = 0.0;
    };

    std::optional<DirectToneStartupRequest> direct_tone_startup_request;

    struct QrssStartupRequest
    {
        std::string message;
        double frequency_hz = 0.0;
        double dot_seconds = 0.0;
    };

    std::optional<QrssStartupRequest> qrss_startup_request;

    struct FskcwStartupRequest
    {
        std::string message;
        double mark_frequency_hz = 0.0;
        double space_frequency_hz = 0.0;
        double dot_seconds = 0.0;
    };

    std::optional<FskcwStartupRequest> fskcw_startup_request;

    struct DfcwStartupRequest
    {
        std::string message;
        double dot_frequency_hz = 0.0;
        double dash_frequency_hz = 0.0;
        double dot_seconds = 0.0;
    };

    std::optional<DfcwStartupRequest> dfcw_startup_request;

    void sync_wspr_mode_config(ArgParserConfig &cfg)
    {
        cfg.wspr.callsign = cfg.callsign;
        cfg.wspr.grid_square = cfg.grid_square;
        cfg.wspr.power_dbm = cfg.power_dbm;
        cfg.wspr.frequencies = cfg.frequencies;
        cfg.wspr.audio_offset_hz = cfg.wspr_audio_offset_hz;
        cfg.wspr.planner_preference = cfg.wspr_planner_preference;
    }

    bool persisted_qrss_config_available(const ArgParserConfig &cfg) noexcept
    {
        return !cfg.qrss.message.empty() &&
               cfg.qrss.frequency_hz > 0.0 &&
               cfg.qrss.dot_seconds > 0.0;
    }

    bool persisted_fskcw_config_available(const ArgParserConfig &cfg) noexcept
    {
        return !cfg.fskcw.message.empty() &&
               cfg.fskcw.mark_frequency_hz > 0.0 &&
               cfg.fskcw.space_frequency_hz > 0.0 &&
               cfg.fskcw.mark_frequency_hz > cfg.fskcw.space_frequency_hz &&
               cfg.fskcw.dot_seconds > 0.0;
    }

    bool persisted_dfcw_config_available(const ArgParserConfig &cfg) noexcept
    {
        return !cfg.dfcw.message.empty() &&
               cfg.dfcw.dot_frequency_hz > 0.0 &&
               cfg.dfcw.dash_frequency_hz > 0.0 &&
               cfg.dfcw.dot_frequency_hz != cfg.dfcw.dash_frequency_hz &&
               cfg.dfcw.dot_seconds > 0.0;
    }

    static std::string get_wspr_gpio_suffix_for_entry(
        const WsprFrequencyEntry &entry,
        const ArgParserConfig &config,
        WSPRBandLookup &lookup)
    {
        int gpio = kSelectorGpioUnset;
        bool active_high = false;
        bool enabled = false;

        if (entry.selector_gpio != kSelectorGpioUnset)
        {
            gpio = entry.selector_gpio;
            active_high = entry.selector_gpio_active_high;
            enabled = true;
        }
        else if (entry.allow_band_gpio_fallback)
        {
            const std::optional<HamBand> band =
                lookup.lookup_ham_band(entry.dial_frequency_hz);

            if (band.has_value())
            {
                const BandGPIOConfig &band_cfg =
                    config.band_gpio[ham_band_index(*band)];

                if (band_cfg.enabled && band_cfg.gpio >= 0)
                {
                    gpio = band_cfg.gpio;
                    active_high = band_cfg.active_high;
                    enabled = true;
                }
            }
        }

        if (!enabled)
        {
            return "";
        }

        return " (GPIO" +
               std::to_string(gpio) +
               (active_high ? "H)" : "L)");
    }

} // namespace

static void defer_startup_diagnostic(LogLevel level, std::string message)
{
    std::lock_guard<std::mutex> lock(deferred_startup_diagnostics_mtx);
    deferred_startup_diagnostics.push_back(
        DeferredStartupDiagnostic{level, std::move(message)});
}

static void clear_deferred_startup_diagnostics()
{
    std::lock_guard<std::mutex> lock(deferred_startup_diagnostics_mtx);
    deferred_startup_diagnostics.clear();
}

void emit_deferred_startup_diagnostics()
{
    std::vector<DeferredStartupDiagnostic> pending;
    {
        std::lock_guard<std::mutex> lock(deferred_startup_diagnostics_mtx);
        pending.swap(deferred_startup_diagnostics);
    }

    for (const auto &diagnostic : pending)
    {
        llog.logS(diagnostic.level, diagnostic.message);
    }
}

void set_startup_diagnostic_deferral(bool enabled) noexcept
{
    startup_diagnostic_deferral_enabled.store(enabled, std::memory_order_release);
}

template <typename... Args>
void log_startup_config_message(LogLevel level, Args &&...args)
{
    if (startup_diagnostic_deferral_enabled.load(std::memory_order_acquire))
    {
        std::ostringstream oss;
        (oss << ... << args);
        defer_startup_diagnostic(level, oss.str());
        return;
    }

    llog.logS(level, std::forward<Args>(args)...);
}

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
    if (exiting_wspr.load(std::memory_order_acquire))
    {
        llog.logS(DEBUG, "Ignoring INI reload while shutdown is in progress.");
        return;
    }

    const std::uint64_t generation =
        ini_reload_generation.fetch_add(1, std::memory_order_acq_rel) + 1U;
    ini_reload_pending.store(true, std::memory_order_release);

    const WsprTransmitter::State transmitter_state = wsprTransmitter.getState();

    if (transmitter_state == WsprTransmitter::State::TRANSMITTING)
    {
        llog.logS(
            INFO,
            "INI file changed during transmission; deferring managed reload until the current TX completes (generation ",
            generation,
            ").");
        return;
    }

    llog.logS(INFO, "INI file changed, reloading.");
    (void)set_config(true);
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

static TransmitBackendKind parse_transmit_backend_option(
    const std::string &value)
{
    std::string lowered = trim_copy_string(value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (lowered == "gpio")
    {
        return TransmitBackendKind::GPIO;
    }

    if (lowered == "si5351")
    {
        return TransmitBackendKind::SI5351;
    }

    throw std::invalid_argument(
        "Invalid backend. Expected 'gpio' or 'si5351'.");
}

static int parse_integer_option(
    const char *raw_value,
    const std::string &option_name,
    int base = 10)
{
    std::size_t consumed = 0;
    const std::string value(raw_value == nullptr ? "" : raw_value);
    const int parsed = std::stoi(value, &consumed, base);
    if (consumed != value.size())
    {
        throw std::invalid_argument(option_name + " must be an integer.");
    }

    return parsed;
}

static int parse_si5351_tx_output_option(const char *raw_value)
{
    std::string value = trim_copy_string(raw_value == nullptr ? "" : raw_value);
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (value == "clk0" || value == "0")
        return 0;
    if (value == "clk1" || value == "1")
        return 1;
    if (value == "clk2" || value == "2")
        return 2;

    throw std::invalid_argument(
        "Invalid Si5351 TX output. Expected CLK0, CLK1, CLK2, 0, 1, or 2.");
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
    WsprFrequencyEntry &entry,
    std::string &error_message)
{
    // Frequency-entry tokens may carry @GPIO[H|L] suffixes that override the
    // selected band GPIO for one scheduler slot.
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
        entry.selector_gpio = kSelectorGpioUnset;
        entry.selector_gpio_active_high = false;
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
    std::string gpio_token =
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

    bool parsed_active_high = false;
    const char polarity_suffix = gpio_token.empty() ? '\0' : gpio_token.back();
    if (polarity_suffix == 'H' || polarity_suffix == 'h' ||
        polarity_suffix == 'L' || polarity_suffix == 'l')
    {
        parsed_active_high = (polarity_suffix == 'H' || polarity_suffix == 'h');
        gpio_token.pop_back();
        gpio_token = trim_copy_string(gpio_token);
        if (gpio_token.empty())
        {
            error_message =
                "Invalid frequency token '" + token +
                "': GPIO value is missing before polarity suffix.";
            return false;
        }
    }

    int parsed_gpio = kSelectorGpioUnset;
    if (!parse_gpio_number_strict(gpio_token, parsed_gpio))
    {
        error_message =
            "Invalid frequency token '" + token +
            "': GPIO suffix must be an integer BCM GPIO optionally followed by H or L.";
        return false;
    }

    if (!is_valid_selector_gpio(parsed_gpio))
    {
        error_message =
            "Invalid frequency token '" + token +
            "': GPIO suffix must be between 0 and 27.";
        return false;
    }

    entry.token = base_token;
    entry.selector_gpio = parsed_gpio;
    entry.selector_gpio_active_high = parsed_active_high;
    return true;
}

bool set_direct_tone_startup_request(
    const std::string &raw_token,
    std::string *error_message)
{
    // --test-tone creates a transient startup request only. It does not
    // persist tone mode or RF frequency into configuration files.
    WsprFrequencyEntry entry;
    std::string local_error;
    if (!parse_frequency_entry_token(raw_token, entry, local_error))
    {
        if (error_message != nullptr)
        {
            *error_message = local_error;
        }
        return false;
    }

    try
    {
        const double actual_rf_frequency_hz =
            lookup.parse_string_to_frequency(entry.token, false);
        if (actual_rf_frequency_hz <= 0.0)
        {
            if (error_message != nullptr)
            {
                *error_message = "Invalid direct RF test tone frequency (<=0).";
            }
            return false;
        }

        entry.dial_frequency_hz = actual_rf_frequency_hz;
        direct_tone_startup_request = DirectToneStartupRequest{
            entry,
            actual_rf_frequency_hz};
        return true;
    }
    catch (const std::exception &e)
    {
        if (error_message != nullptr)
        {
            *error_message =
                "Invalid direct RF test tone frequency input: " +
                raw_token + " Exception: " + e.what();
        }
        return false;
    }
}

bool has_direct_tone_startup_request() noexcept
{
    return direct_tone_startup_request.has_value();
}

bool try_get_direct_tone_startup_request(
    WsprFrequencyEntry &entry_out,
    double &actual_rf_frequency_hz_out) noexcept
{
    if (!direct_tone_startup_request.has_value())
    {
        return false;
    }

    entry_out = direct_tone_startup_request->entry;
    actual_rf_frequency_hz_out = direct_tone_startup_request->actual_rf_frequency_hz;
    return true;
}

void clear_direct_tone_startup_request() noexcept
{
    direct_tone_startup_request.reset();
}

bool set_qrss_startup_request(
    const std::string &message,
    const std::string &frequency_hz,
    const std::string &dot_seconds,
    std::string *error_message)
{
    try
    {
        const std::string trimmed_message = trim_copy_string(message);
        if (trimmed_message.empty())
        {
            if (error_message != nullptr)
                *error_message = "Invalid QRSS message.";
            return false;
        }

        const double parsed_frequency_hz = std::stod(frequency_hz);
        if (parsed_frequency_hz <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid QRSS frequency (<=0).";
            return false;
        }

        const double parsed_dot_seconds = std::stod(dot_seconds);
        if (parsed_dot_seconds <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid QRSS dot length (<=0).";
            return false;
        }

        qrss_startup_request = QrssStartupRequest{
            trimmed_message,
            parsed_frequency_hz,
            parsed_dot_seconds};
        return true;
    }
    catch (const std::exception &)
    {
        if (error_message != nullptr)
        {
            *error_message =
                "Invalid QRSS startup request. Expected message, frequency, and dot length.";
        }
        return false;
    }
}

bool has_qrss_startup_request() noexcept
{
    return qrss_startup_request.has_value();
}

bool try_get_qrss_startup_request(
    std::string &message_out,
    double &frequency_hz_out,
    double &dot_seconds_out) noexcept
{
    if (!qrss_startup_request.has_value())
        return false;

    message_out = qrss_startup_request->message;
    frequency_hz_out = qrss_startup_request->frequency_hz;
    dot_seconds_out = qrss_startup_request->dot_seconds;
    return true;
}

void clear_qrss_startup_request() noexcept
{
    qrss_startup_request.reset();
}

bool set_fskcw_startup_request(
    const std::string &message,
    const std::string &mark_frequency_hz,
    const std::string &space_frequency_hz,
    const std::string &dot_seconds,
    std::string *error_message)
{
    try
    {
        const std::string trimmed_message = trim_copy_string(message);
        if (trimmed_message.empty())
        {
            if (error_message != nullptr)
                *error_message = "Invalid FSKCW message.";
            return false;
        }

        const double parsed_mark_frequency_hz = std::stod(mark_frequency_hz);
        if (parsed_mark_frequency_hz <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid FSKCW mark frequency (<=0).";
            return false;
        }

        const double parsed_space_frequency_hz = std::stod(space_frequency_hz);
        if (parsed_space_frequency_hz <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid FSKCW space frequency (<=0).";
            return false;
        }

        if (parsed_mark_frequency_hz <= parsed_space_frequency_hz)
        {
            if (error_message != nullptr)
                *error_message = "FSKCW mark frequency must be greater than space frequency.";
            return false;
        }

        const double parsed_dot_seconds = std::stod(dot_seconds);
        if (parsed_dot_seconds <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid FSKCW dot length (<=0).";
            return false;
        }

        fskcw_startup_request = FskcwStartupRequest{
            trimmed_message,
            parsed_mark_frequency_hz,
            parsed_space_frequency_hz,
            parsed_dot_seconds};
        return true;
    }
    catch (const std::exception &)
    {
        if (error_message != nullptr)
        {
            *error_message =
                "Invalid FSKCW startup request. Expected message, mark frequency, space frequency, and dot length.";
        }
        return false;
    }
}

bool has_fskcw_startup_request() noexcept
{
    return fskcw_startup_request.has_value();
}

bool try_get_fskcw_startup_request(
    std::string &message_out,
    double &mark_frequency_hz_out,
    double &space_frequency_hz_out,
    double &dot_seconds_out) noexcept
{
    if (!fskcw_startup_request.has_value())
        return false;

    message_out = fskcw_startup_request->message;
    mark_frequency_hz_out = fskcw_startup_request->mark_frequency_hz;
    space_frequency_hz_out = fskcw_startup_request->space_frequency_hz;
    dot_seconds_out = fskcw_startup_request->dot_seconds;
    return true;
}

void clear_fskcw_startup_request() noexcept
{
    fskcw_startup_request.reset();
}

bool set_dfcw_startup_request(
    const std::string &message,
    const std::string &dot_frequency_hz,
    const std::string &dash_frequency_hz,
    const std::string &dot_seconds,
    std::string *error_message)
{
    try
    {
        const std::string trimmed_message = trim_copy_string(message);
        if (trimmed_message.empty())
        {
            if (error_message != nullptr)
                *error_message = "Invalid DFCW message.";
            return false;
        }

        const double parsed_dot_frequency_hz = std::stod(dot_frequency_hz);
        if (parsed_dot_frequency_hz <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid DFCW dot frequency (<=0).";
            return false;
        }

        const double parsed_dash_frequency_hz = std::stod(dash_frequency_hz);
        if (parsed_dash_frequency_hz <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid DFCW dash frequency (<=0).";
            return false;
        }

        if (parsed_dot_frequency_hz == parsed_dash_frequency_hz)
        {
            if (error_message != nullptr)
                *error_message = "DFCW dot and dash frequencies must differ.";
            return false;
        }

        const double parsed_dot_seconds = std::stod(dot_seconds);
        if (parsed_dot_seconds <= 0.0)
        {
            if (error_message != nullptr)
                *error_message = "Invalid DFCW dot length (<=0).";
            return false;
        }

        dfcw_startup_request = DfcwStartupRequest{
            trimmed_message,
            parsed_dot_frequency_hz,
            parsed_dash_frequency_hz,
            parsed_dot_seconds};
        return true;
    }
    catch (const std::exception &)
    {
        if (error_message != nullptr)
        {
            *error_message =
                "Invalid DFCW startup request. Expected message, dot frequency, dash frequency, and dot length.";
        }
        return false;
    }
}

bool has_dfcw_startup_request() noexcept
{
    return dfcw_startup_request.has_value();
}

bool try_get_dfcw_startup_request(
    std::string &message_out,
    double &dot_frequency_hz_out,
    double &dash_frequency_hz_out,
    double &dot_seconds_out) noexcept
{
    if (!dfcw_startup_request.has_value())
        return false;

    message_out = dfcw_startup_request->message;
    dot_frequency_hz_out = dfcw_startup_request->dot_frequency_hz;
    dash_frequency_hz_out = dfcw_startup_request->dash_frequency_hz;
    dot_seconds_out = dfcw_startup_request->dot_seconds;
    return true;
}

void clear_dfcw_startup_request() noexcept
{
    dfcw_startup_request.reset();
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
              << "  (sudo) wsprrypi --test-tone {rf_frequency}\n"
              << "    OR\n"
              << "  (sudo) wsprrypi --qrss-message \"TEXT\" --qrss-frequency {hz} --qrss-dot-seconds {seconds}\n"
              << "    OR\n"
              << "  (sudo) wsprrypi --fskcw-message \"TEXT\" --fskcw-mark-frequency {hz} --fskcw-space-frequency {hz} --fskcw-dot-seconds {seconds}\n\n"
              << "    OR\n"
              << "  (sudo) wsprrypi --dfcw-message \"TEXT\" --dfcw-dot-frequency {hz} --dfcw-dash-frequency {hz} --dfcw-dot-seconds {seconds}\n\n"
              << "Options:\n"
              << "  -h, --help\n"
              << "    Display this help message.\n"
              << "  -v, --version\n"
              << "    Show the WsprryPi version.\n"
              << "  -i, --ini-file <file>\n"
              << "    Load parameters from an INI file. Provide the path and filename.\n\n"
              << "  --backend <gpio|si5351>\n"
              << "    Select the RF transmit backend. Default: gpio.\n\n";

    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        std::cerr << "  --power-level <level>\n"
                  << "    Select Si5351 drive-strength level (1-4).\n\n"
                  << "  --si5351-i2c-bus <bus>\n"
                  << "    Select the Si5351 I2C bus. Default: 1.\n"
                  << "  --si5351-i2c-address <addr>\n"
                  << "    Select the Si5351 I2C address. Default: 0x60.\n"
                  << "  --si5351-reference-frequency <hz>\n"
                  << "    Select the Si5351 reference frequency. Default: 27000000.\n"
                  << "  --si5351-tx-output <CLK0|CLK1|CLK2>\n"
                  << "    Select the Si5351 transmit output. Default: CLK0.\n\n";
    }
    else
    {
        std::cerr << "  -a, --transmit-gpio <gpio>\n"
                  << "    Select the RF transmit GPIO (supported: 4 or 20).\n\n"
                  << "  --power-level <level>\n"
                  << "    Select GPIO RF power level (0-7).\n\n"
                  << "  --si5351-i2c-bus <bus>\n"
                  << "    Select the Si5351 I2C bus when --backend si5351 is used. Default: 1.\n"
                  << "  --si5351-i2c-address <addr>\n"
                  << "    Select the Si5351 I2C address when --backend si5351 is used. Default: 0x60.\n"
                  << "  --si5351-reference-frequency <hz>\n"
                  << "    Select the Si5351 reference frequency when --backend si5351 is used. Default: 27000000.\n"
                  << "  --si5351-tx-output <CLK0|CLK1|CLK2>\n"
                  << "    Select the Si5351 transmit output when --backend si5351 is used. Default: CLK0.\n\n";
    }

    std::cerr << "See the documentation for a complete list of available options.\n\n";

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
    llog.logS(DEBUG, "Transmit Backend:",
              transmit_backend_kind_to_string(config.transmit_backend));
    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        std::ostringstream address;
        address << "0x" << std::hex << std::uppercase
                << config.si5351_i2c_address;
        llog.logS(DEBUG, "Si5351 I2C Bus:", config.si5351_i2c_bus);
        llog.logS(DEBUG, "Si5351 I2C Address:", address.str());
        llog.logS(DEBUG, "Si5351 Reference Frequency Hz:",
                  config.si5351_reference_hz);
        llog.logS(DEBUG, "Si5351 TX Output:",
                  std::string("CLK") + std::to_string(config.si5351_tx_output));
    }
    else
    {
        llog.logS(DEBUG, "Transmit GPIO:", config.tx_pin);
    }
    // [Extended]
    llog.logS(DEBUG, "PPM Offset:", config.ppm);
    llog.logS(DEBUG, "Synchronize with NTP:", config.use_ntp ? "true" : "false");
    llog.logS(DEBUG, "Use Frequency Randomization:", config.use_offset ? "true" : "false");
    llog.logS(DEBUG, "WSPR Audio Offset Hz:", config.wspr.audio_offset_hz);
    llog.logS(
        DEBUG,
        config.transmit_backend == TransmitBackendKind::SI5351
            ? "Si5351 Drive Level:"
            : "GPIO Power Level:",
        config.power_level);
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
    if (candidate.modulation_dot_seconds <= 0.0)
    {
        if (error_message != nullptr)
        {
            *error_message = "Modulation dot_seconds must be greater than 0.";
        }

        return false;
    }

    if (candidate.schedule_start_minute < 0 ||
        candidate.schedule_start_minute > 59)
    {
        if (error_message != nullptr)
        {
            *error_message = "Schedule start_minute must be between 0 and 59.";
        }

        return false;
    }

    if (candidate.schedule_repeat_minutes <= 0)
    {
        if (error_message != nullptr)
        {
            *error_message = "Schedule repeat_minutes must be greater than 0.";
        }

        return false;
    }

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
        candidate.transmit_backend == TransmitBackendKind::GPIO &&
        (candidate.mode == ModeType::TONE || candidate.transmit);

    if (requires_valid_transmit_gpio &&
        !is_valid_runtime_transmit_gpio(candidate.tx_pin))
    {
        if (error_message != nullptr)
        {
            *error_message = transmit_gpio_validation_message();
        }

        return false;
    }

    if (candidate.transmit_backend == TransmitBackendKind::SI5351 &&
        (candidate.mode == ModeType::TONE || candidate.transmit))
    {
        if (candidate.si5351_i2c_bus < 0)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Invalid Si5351 I2C bus. Expected a non-negative bus number.";
            }

            return false;
        }

        if (candidate.si5351_i2c_address < 0x03 ||
            candidate.si5351_i2c_address > 0x77)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Invalid Si5351 I2C address. Expected 0x03 through 0x77.";
            }

            return false;
        }

        if (candidate.si5351_reference_hz <= 0)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Invalid Si5351 reference frequency. Expected a positive frequency in Hz.";
            }

            return false;
        }

        if (candidate.si5351_tx_output < 0 ||
            candidate.si5351_tx_output > 2)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Invalid Si5351 TX output. Expected CLK0, CLK1, CLK2, 0, 1, or 2.";
            }

            return false;
        }

        if (candidate.power_level < 1 || candidate.power_level > 4)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Invalid Si5351 power level. Expected 1 through 4.";
            }

            return false;
        }
    }

    if (candidate.mode == ModeType::TONE)
    {
        // Tone mode is valid only when a transient startup request exists.
        if (!has_direct_tone_startup_request())
        {
            if (error_message != nullptr)
            {
                *error_message = "Missing direct RF test tone frequency.";
            }
            return false;
        }

        return true;
    }

    if (candidate.mode == ModeType::QRSS)
    {
        if (!has_qrss_startup_request() &&
            !persisted_qrss_config_available(candidate))
        {
            if (error_message != nullptr)
            {
                *error_message = "Missing QRSS configuration.";
            }
            return false;
        }

        return true;
    }

    if (candidate.mode == ModeType::FSKCW)
    {
        if (!has_fskcw_startup_request() &&
            !persisted_fskcw_config_available(candidate))
        {
            if (error_message != nullptr)
            {
                *error_message = "Missing FSKCW configuration.";
            }
            return false;
        }

        return true;
    }

    if (candidate.mode == ModeType::DFCW)
    {
        if (!has_dfcw_startup_request() &&
            !persisted_dfcw_config_available(candidate))
        {
            if (error_message != nullptr)
            {
                *error_message = "Missing DFCW configuration.";
            }
            return false;
        }

        return true;
    }

    if (candidate.mode != ModeType::WSPR)
    {
        if (error_message != nullptr)
        {
            *error_message = "Mode must be either WSPR, TONE, QRSS, FSKCW, or DFCW.";
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

    std::string callsign = trim_copy_string(candidate.callsign);
    std::string locator = trim_copy_string(candidate.grid_square);

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
    sync_wspr_mode_config(candidate);

    return true;
}

static bool validation_error_is_missing_required(
    const std::string &validation_error)
{
    return validation_error.rfind("Missing", 0) == 0;
}

void apply_runtime_config_side_effects()
{
    const wsprrypi::BackendKind backend_kind =
        config.transmit_backend == TransmitBackendKind::SI5351
            ? wsprrypi::BackendKind::SI5351
            : wsprrypi::BackendKind::RPI_CLOCK_GPIO;
    WsprTransmitter::Si5351RuntimeConfig si5351_config;
    si5351_config.i2c_bus = config.si5351_i2c_bus;
    si5351_config.i2c_address = config.si5351_i2c_address;
    si5351_config.reference_hz = config.si5351_reference_hz;
    si5351_config.tx_output = config.si5351_tx_output;
    si5351_config.power_level = config.power_level;
    si5351_config.app_managed = config.use_ini;
    wsprTransmitter.selectBackend(backend_kind, si5351_config);

    llog.logS(INFO, "Transmit backend:",
              transmit_backend_kind_to_string(config.transmit_backend));
    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        std::ostringstream address;
        address << "0x" << std::hex << std::uppercase
                << config.si5351_i2c_address;
        llog.logS(INFO, "Si5351 I2C bus:", config.si5351_i2c_bus);
        llog.logS(INFO, "Si5351 I2C address:", address.str());
        llog.logS(INFO, "Si5351 reference frequency Hz:",
                  config.si5351_reference_hz);
        llog.logS(INFO, "Si5351 TX output:",
                  std::string("CLK") + std::to_string(config.si5351_tx_output));
        if (config.use_ini)
        {
            std::ostringstream parked_outputs;
            bool first_output = true;
            for (int output = 0; output < 3; ++output)
            {
                if (output == config.si5351_tx_output)
                    continue;

                if (!first_output)
                    parked_outputs << ", ";
                parked_outputs << "CLK" << output;
                first_output = false;
            }

            llog.logS(
                INFO,
                "Si5351 unused output parking: ",
                parked_outputs.str(),
                " held in a safe non-transmitting state; internal PLL remains parked.");
        }
    }
    else
    {
        llog.logS(INFO, "Transmit GPIO:", config.tx_pin);
    }

    if (!config.use_ntp && config.ppm != 0.0)
    {
        log_startup_config_message(INFO,
                                   "PPM value to be used for tone generation: ",
                                   std::fixed,
                                   std::setprecision(2),
                                   config.ppm);
    }
    else if (!config.use_ntp && config.ppm != 0.0)
    {
        config.ppm = 0.0;
        log_startup_config_message(WARN, "NTP disabled and PPM not set.");
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
        WsprFrequencyEntry entry;
        double actual_rf_frequency_hz = 0.0;
        if (!try_get_direct_tone_startup_request(entry, actual_rf_frequency_hz))
        {
            log_startup_config_message(ERROR, " - Missing direct RF test tone frequency.");
            return;
        }

        log_startup_config_message(
            INFO,
            "A direct RF test tone will be generated at:",
            lookup.freq_display_string(actual_rf_frequency_hz));
        return;
    }

    if (config.mode == ModeType::QRSS)
    {
        std::string message;
        double frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!try_get_qrss_startup_request(message, frequency_hz, dot_seconds))
        {
            if (!persisted_qrss_config_available(config))
            {
                log_startup_config_message(ERROR, " - Missing QRSS configuration.");
                return;
            }

            message = config.qrss.message;
            frequency_hz = config.qrss.frequency_hz;
            dot_seconds = config.qrss.dot_seconds;
        }

        log_startup_config_message(
            INFO,
            "QRSS configuration loaded: message='",
            message,
            "' frequency=",
            lookup.freq_display_string(frequency_hz),
            " dot=",
            dot_seconds,
            " s");
        return;
    }

    if (config.mode == ModeType::FSKCW)
    {
        std::string message;
        double mark_frequency_hz = 0.0;
        double space_frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!try_get_fskcw_startup_request(
                message,
                mark_frequency_hz,
                space_frequency_hz,
                dot_seconds))
        {
            if (!persisted_fskcw_config_available(config))
            {
                log_startup_config_message(ERROR, " - Missing FSKCW configuration.");
                return;
            }

            message = config.fskcw.message;
            mark_frequency_hz = config.fskcw.mark_frequency_hz;
            space_frequency_hz = config.fskcw.space_frequency_hz;
            dot_seconds = config.fskcw.dot_seconds;
        }

        log_startup_config_message(
            INFO,
            "FSKCW configuration loaded: message='",
            message,
            "' mark=",
            lookup.freq_display_string(mark_frequency_hz),
            " space=",
            lookup.freq_display_string(space_frequency_hz),
            " dot=",
            dot_seconds,
            " s");
        return;
    }

    if (config.mode == ModeType::DFCW)
    {
        std::string message;
        double dot_frequency_hz = 0.0;
        double dash_frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!try_get_dfcw_startup_request(
                message,
                dot_frequency_hz,
                dash_frequency_hz,
                dot_seconds))
        {
            if (!persisted_dfcw_config_available(config))
            {
                log_startup_config_message(ERROR, " - Missing DFCW configuration.");
                return;
            }

            message = config.dfcw.message;
            dot_frequency_hz = config.dfcw.dot_frequency_hz;
            dash_frequency_hz = config.dfcw.dash_frequency_hz;
            dot_seconds = config.dfcw.dot_seconds;
        }

        log_startup_config_message(
            INFO,
            "DFCW configuration loaded: message='",
            message,
            "' dot=",
            lookup.freq_display_string(dot_frequency_hz),
            " dash=",
            lookup.freq_display_string(dash_frequency_hz),
            " dot=",
            dot_seconds,
            " s");
        return;
    }

    if (config.mode != ModeType::WSPR)
    {
        return;
    }

    if (config.transmit)
    {
        log_startup_config_message(INFO, "WSPR packet payload:");
        log_startup_config_message(INFO, "- Callsign:", config.callsign);
        log_startup_config_message(INFO, "- Locator:", config.grid_square);
        log_startup_config_message(INFO, "- Power:", config.power_dbm, " dBm");

        if (config.wspr_frequency_entries.size() > 1)
        {
            log_startup_config_message(INFO, "Requested WSPR dial frequencies:");

            for (const auto &entry : config.wspr_frequency_entries)
            {
                if (entry.dial_frequency_hz == 0.0)
                {
                    log_startup_config_message(INFO, "- Skip (0.0)");
                }
                else
                {
                    log_startup_config_message(
                        INFO,
                        "- ",
                        lookup.freq_display_string(entry.dial_frequency_hz),
                        get_wspr_gpio_suffix_for_entry(entry, config, lookup));
                }
            }
        }
        else
        {
            const WsprFrequencyEntry &entry = config.wspr_frequency_entries[0];

            if (entry.dial_frequency_hz == 0.0)
            {
                log_startup_config_message(INFO, "Requested WSPR dial frequency:", "Skip (0.0)");
            }
            else
            {
                log_startup_config_message(
                    INFO,
                    "Requested WSPR dial frequency:",
                    lookup.freq_display_string(entry.dial_frequency_hz),
                    get_wspr_gpio_suffix_for_entry(entry, config, lookup));
            }
        }

        if (config.use_offset)
        {
            log_startup_config_message(
                INFO,
                "A random offset will be added to all transmissions.");
        }
    }

    if (!config.use_ini)
    {
        if (config.loop_tx)
        {
            log_startup_config_message(
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
            log_startup_config_message(
                INFO,
                "TX will stop after:",
                config.tx_iterations.load(),
                "iteration(s) of the WSPR dial-frequency list.");
        }
    }
}

bool validate_config_data()
{
    sync_wspr_mode_config(config);
    ini_reload_pending.store(false, std::memory_order_relaxed);

    std::string validation_error;
    if (!validate_config_candidate(config, &validation_error))
    {
        llog.logE(
            FATAL,
            validation_error_is_missing_required(validation_error)
                ? "Missing required parameters."
                : "Invalid configuration.");

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
            config.transmit_backend == TransmitBackendKind::GPIO &&
            !is_valid_runtime_transmit_gpio(config.tx_pin))
        {
            llog.logE(ERROR, " - ", transmit_gpio_validation_message());
        }
        if ((config.mode == ModeType::TONE || config.transmit) &&
            config.transmit_backend == TransmitBackendKind::SI5351)
        {
            if (config.si5351_i2c_bus < 0)
            {
                llog.logE(ERROR,
                          " - Invalid Si5351 I2C bus. Expected a non-negative bus number.");
            }
            if (config.si5351_i2c_address < 0x03 ||
                config.si5351_i2c_address > 0x77)
            {
                llog.logE(ERROR,
                          " - Invalid Si5351 I2C address. Expected 0x03 through 0x77.");
            }
            if (config.si5351_reference_hz <= 0)
            {
                llog.logE(ERROR,
                          " - Invalid Si5351 reference frequency. Expected a positive frequency in Hz.");
            }
            if (config.si5351_tx_output < 0 ||
                config.si5351_tx_output > 2)
            {
                llog.logE(ERROR,
                          " - Invalid Si5351 TX output. Expected CLK0, CLK1, CLK2, 0, 1, or 2.");
            }
            if (config.power_level < 1 || config.power_level > 4)
            {
                llog.logE(ERROR,
                          " - Invalid Si5351 power level. Expected 1 through 4.");
            }
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

    if (config.mode != ModeType::TONE &&
        config.mode != ModeType::WSPR &&
        config.mode != ModeType::QRSS &&
        config.mode != ModeType::FSKCW &&
        config.mode != ModeType::DFCW)
    {
        llog.logE(FATAL, "Mode must be either WSPR, TONE, QRSS, FSKCW, or DFCW.");
        std::exit(EXIT_FAILURE);
    }

    if (config.mode == ModeType::TONE)
    {
        WsprFrequencyEntry entry;
        double actual_rf_frequency_hz = 0.0;
        if (!try_get_direct_tone_startup_request(entry, actual_rf_frequency_hz))
        {
            llog.logE(ERROR, " - Missing direct RF test tone frequency.");
            if (config.use_ini)
            {
                return false;
            }
            std::exit(EXIT_FAILURE);
        }
    }
    else if (config.mode == ModeType::QRSS)
    {
        std::string message;
        double frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!try_get_qrss_startup_request(message, frequency_hz, dot_seconds))
        {
            if (!persisted_qrss_config_available(config))
            {
                llog.logE(ERROR, " - Missing QRSS configuration.");
                if (config.use_ini)
                {
                    return false;
                }
                std::exit(EXIT_FAILURE);
            }
        }
    }
    else if (config.mode == ModeType::FSKCW)
    {
        std::string message;
        double mark_frequency_hz = 0.0;
        double space_frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!try_get_fskcw_startup_request(
                message,
                mark_frequency_hz,
                space_frequency_hz,
                dot_seconds))
        {
            if (!persisted_fskcw_config_available(config))
            {
                llog.logE(ERROR, " - Missing FSKCW configuration.");
                if (config.use_ini)
                {
                    return false;
                }
                std::exit(EXIT_FAILURE);
            }
        }
    }
    else if (config.mode == ModeType::DFCW)
    {
        std::string message;
        double dot_frequency_hz = 0.0;
        double dash_frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!try_get_dfcw_startup_request(
                message,
                dot_frequency_hz,
                dash_frequency_hz,
                dot_seconds))
        {
            if (!persisted_dfcw_config_available(config))
            {
                llog.logE(ERROR, " - Missing DFCW configuration.");
                if (config.use_ini)
                {
                    return false;
                }
                std::exit(EXIT_FAILURE);
            }
        }
    }

    apply_runtime_config_side_effects();
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
    std::vector<WsprFrequencyEntry> parsed_entries;

    for (const std::string &token : split_frequency_tokens(raw_list))
    {
        WsprFrequencyEntry entry;
        std::string entry_error;
        if (!parse_frequency_entry_token(token, entry, entry_error))
        {
            llog.logE(ERROR, entry_error);
            target.wspr_dial_freq_set.clear();
            target.wspr_frequency_entries.clear();
            return false;
        }

        try
        {
            const double freq = lookup.parse_string_to_frequency(entry.token, false);
            entry.dial_frequency_hz = freq;
            entry.allow_band_gpio_fallback = target.use_ini;
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
        target.wspr_frequency_entries = parsed_entries;
        return true;
    }

    if (target.mode != ModeType::WSPR || !target.transmit)
    {
        target.wspr_frequency_entries.clear();
        return true;
    }

    llog.logE(ERROR, "Empty or invalid WSPR dial-frequency list.");
    target.wspr_dial_freq_set.clear();
    target.wspr_frequency_entries.clear();
    return false;
}

bool set_frequencies()
{
    return set_frequencies(config);
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
    bool early_use_journald = config.use_journald;
    bool early_enable_timestamps = config.date_time_log;
    TransmitBackendKind early_backend = config.transmit_backend;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--journald")
        {
            early_use_journald = true;
            early_enable_timestamps = false;
        }
        else if (arg == "--date-time-log")
        {
            early_enable_timestamps = true;
        }
        else if (arg == "--backend" && i + 1 < argc)
        {
            try
            {
                early_backend = parse_transmit_backend_option(argv[i + 1]);
            }
            catch (const std::exception &)
            {
            }
        }
        else if (arg.rfind("--backend=", 0) == 0)
        {
            try
            {
                early_backend =
                    parse_transmit_backend_option(arg.substr(10));
            }
            catch (const std::exception &)
            {
            }
        }
        else if (arg.size() > 1U && arg[0] == '-' && arg[1] != '-')
        {
            for (std::size_t j = 1; j < arg.size(); ++j)
            {
                if (arg[j] == 'J')
                {
                    early_use_journald = true;
                    early_enable_timestamps = false;
                }
                else if (arg[j] == 'D')
                {
                    early_enable_timestamps = true;
                }
            }
        }
    }

    initialize_logger(early_use_journald, early_enable_timestamps);

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            config.transmit_backend = early_backend;
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
    startup_config_handoff_ready.store(false, std::memory_order_release);
    clear_deferred_startup_diagnostics();
    clear_direct_tone_startup_request();
    clear_qrss_startup_request();
    clear_fskcw_startup_request();
    clear_dfcw_startup_request();

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
                        defer_startup_diagnostic(WARN, warning_message);
                    }

                    defer_startup_diagnostic(
                        ERROR,
                        std::string("Configuration load failed: ") + load_error);
                    defer_startup_diagnostic(
                        WARN,
                        "Using safe default configuration. Transmission disabled.");

                    init_default_config();
                    config.ini_filename = *(it + 1);
                    config.use_ini = true;
                    config.loop_tx = true;
                    config.transmit = false;
                    config_to_json();
                    startup_config_handoff_ready.store(true, std::memory_order_release);
                }
                else
                {
                    for (const auto &warning_message : warning_messages)
                    {
                        defer_startup_diagnostic(WARN, warning_message);
                    }

                    startup_config_handoff_ready.store(true, std::memory_order_release);
                }
            }
            catch (const std::exception &e)
            {
                defer_startup_diagnostic(
                    ERROR,
                    std::string("Configuration load failed: ") + e.what());
                defer_startup_diagnostic(
                    WARN,
                    "Using safe default configuration. Transmission disabled.");

                init_default_config();
                config.ini_filename = *(it + 1);
                config.use_ini = true;
                config.loop_tx = true;
                config.transmit = false;
                config_to_json();
                startup_config_handoff_ready.store(true, std::memory_order_release);
            }

            // Remove "-i <file>" from args
            args.erase(it, it + 2);
            break; // Exit loop after removing argument
        }
    }

    // Update argc and argv pointers for getopt_long()
    argc = args.size();
    argv = args.data();
    bool explicit_power_level = false;

    static struct option long_options[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'v'},
        {"use-ntp", no_argument, nullptr, 'n'},         // Via: [Extended] Use NTP = True
        {"repeat", no_argument, nullptr, 'r'},          // Global: config.loop_tx
        {"offset", no_argument, nullptr, 'o'},          // Via: [Extended] Offset = True
        {"journald", no_argument, nullptr, 'J'},        // Global: config.use_journald
        {"date-time-log", no_argument, nullptr, 'D'},   // Global: config.date_time_log
        {"require-paired", no_argument, nullptr, 1001}, // Global: config.wspr_planner_preference
        {"backend", required_argument, nullptr, 1002},
        {"qrss-message", required_argument, nullptr, 1003},
        {"qrss-frequency", required_argument, nullptr, 1004},
        {"qrss-dot-seconds", required_argument, nullptr, 1005},
        {"fskcw-message", required_argument, nullptr, 1006},
        {"fskcw-mark-frequency", required_argument, nullptr, 1007},
        {"fskcw-space-frequency", required_argument, nullptr, 1008},
        {"fskcw-dot-seconds", required_argument, nullptr, 1009},
        {"dfcw-message", required_argument, nullptr, 1010},
        {"dfcw-dot-frequency", required_argument, nullptr, 1011},
        {"dfcw-dash-frequency", required_argument, nullptr, 1012},
        {"dfcw-dot-seconds", required_argument, nullptr, 1013},
        {"si5351-i2c-bus", required_argument, nullptr, 1014},
        {"si5351-i2c-address", required_argument, nullptr, 1015},
        {"si5351-reference-frequency", required_argument, nullptr, 1016},
        {"si5351-tx-output", required_argument, nullptr, 1017},
        {"si5351_i2c_bus", required_argument, nullptr, 1014},
        {"si5351_i2c_address", required_argument, nullptr, 1015},
        {"si5351_reference_frequency", required_argument, nullptr, 1016},
        {"si5351_tx_output", required_argument, nullptr, 1017},
        // Required arguments
        {"ppm", required_argument, nullptr, 'p'},       // Via: [Extended] PPM = 0.0
        {"terminate", required_argument, nullptr, 'x'}, // Global: config.tx_iterations
        {"test-tone", required_argument, nullptr, 't'},
        {"transmit-gpio", required_argument, nullptr, 'a'}, // Via: [Common] Transmit Pin = 4
        {"transmit-pin", required_argument, nullptr, 'a'},
        {"led_pin", required_argument, nullptr, 'l'},         // Via: [Extended] LED Pin = 18
        {"shutdown_button", required_argument, nullptr, 's'}, // Via: [Server] Shutdown Button = 19
        {"power_level", required_argument, nullptr, 'd'},     // Via: [Extended] Power Level = 7
        {"power-level", required_argument, nullptr, 'd'},
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
            config.wspr_planner_preference = WsprPlannerPreference::RequirePaired;
            break;
        }
        case 1002: // Select transmit backend
        {
            {
                config.transmit_backend =
                    parse_transmit_backend_option(optarg);
                if (config.transmit_backend == TransmitBackendKind::SI5351 &&
                    !explicit_power_level)
                {
                    config.power_level = 1;
                }
            }
            catch (const std::exception &e)
            {
                print_usage(e.what(), EXIT_FAILURE);
            }
            break;
        }
        case 1003:
        case 1004:
        case 1005:
        case 1006:
        case 1007:
        case 1008:
        case 1009:
        case 1010:
        case 1011:
        case 1012:
        case 1013:
        {
            break;
        }
        case 1014:
        {
            try
            {
                config.si5351_i2c_bus =
                    parse_integer_option(optarg, "--si5351-i2c-bus");
            }
            catch (const std::exception &e)
            {
                print_usage(e.what(), EXIT_FAILURE);
            }
            break;
        }
        case 1015:
        {
            try
            {
                config.si5351_i2c_address =
                    parse_integer_option(optarg, "--si5351-i2c-address", 0);
            }
            catch (const std::exception &e)
            {
                print_usage(e.what(), EXIT_FAILURE);
            }
            break;
        }
        case 1016:
        {
            try
            {
                config.si5351_reference_hz =
                    parse_integer_option(optarg, "--si5351-reference-frequency");
            }
            catch (const std::exception &e)
            {
                print_usage(e.what(), EXIT_FAILURE);
            }
            break;
        }
        case 1017:
        {
            try
            {
                config.si5351_tx_output =
                    parse_si5351_tx_output_option(optarg);
            }
            catch (const std::exception &e)
            {
                print_usage(e.what(), EXIT_FAILURE);
            }
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
                std::string error_message;
                if (!set_direct_tone_startup_request(optarg, &error_message))
                {
                    print_usage(error_message, EXIT_FAILURE);
                }

                // Direct test tone is a transient startup mode selected from
                // the CLI. It is not persisted into config storage.
                config.mode = ModeType::TONE;
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
                explicit_power_level = true;
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

    std::string qrss_message_arg;
    std::string qrss_frequency_arg;
    std::string qrss_dot_seconds_arg;
    std::string fskcw_message_arg;
    std::string fskcw_mark_frequency_arg;
    std::string fskcw_space_frequency_arg;
    std::string fskcw_dot_seconds_arg;
    std::string dfcw_message_arg;
    std::string dfcw_dot_frequency_arg;
    std::string dfcw_dash_frequency_arg;
    std::string dfcw_dot_seconds_arg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--qrss-message" && i + 1 < argc)
        {
            qrss_message_arg = argv[++i];
        }
        else if (arg == "--qrss-frequency" && i + 1 < argc)
        {
            qrss_frequency_arg = argv[++i];
        }
        else if (arg == "--qrss-dot-seconds" && i + 1 < argc)
        {
            qrss_dot_seconds_arg = argv[++i];
        }
        else if (arg == "--fskcw-message" && i + 1 < argc)
        {
            fskcw_message_arg = argv[++i];
        }
        else if (arg == "--fskcw-mark-frequency" && i + 1 < argc)
        {
            fskcw_mark_frequency_arg = argv[++i];
        }
        else if (arg == "--fskcw-space-frequency" && i + 1 < argc)
        {
            fskcw_space_frequency_arg = argv[++i];
        }
        else if (arg == "--fskcw-dot-seconds" && i + 1 < argc)
        {
            fskcw_dot_seconds_arg = argv[++i];
        }
        else if (arg == "--dfcw-message" && i + 1 < argc)
        {
            dfcw_message_arg = argv[++i];
        }
        else if (arg == "--dfcw-dot-frequency" && i + 1 < argc)
        {
            dfcw_dot_frequency_arg = argv[++i];
        }
        else if (arg == "--dfcw-dash-frequency" && i + 1 < argc)
        {
            dfcw_dash_frequency_arg = argv[++i];
        }
        else if (arg == "--dfcw-dot-seconds" && i + 1 < argc)
        {
            dfcw_dot_seconds_arg = argv[++i];
        }
    }

    const bool any_qrss_arg =
        !qrss_message_arg.empty() ||
        !qrss_frequency_arg.empty() ||
        !qrss_dot_seconds_arg.empty();
    if (any_qrss_arg)
    {
        if (config.use_ini)
        {
            print_usage("QRSS test mode is invalid when using INI file.", EXIT_FAILURE);
        }

        if (qrss_message_arg.empty() ||
            qrss_frequency_arg.empty() ||
            qrss_dot_seconds_arg.empty())
        {
            print_usage(
                "QRSS test mode requires --qrss-message, --qrss-frequency, and --qrss-dot-seconds.",
                EXIT_FAILURE);
        }

        std::string error_message;
        if (!set_qrss_startup_request(
                qrss_message_arg,
                qrss_frequency_arg,
                qrss_dot_seconds_arg,
                &error_message))
        {
            print_usage(error_message, EXIT_FAILURE);
        }

        clear_direct_tone_startup_request();
        clear_fskcw_startup_request();
        clear_dfcw_startup_request();
        config.qrss.message = qrss_message_arg;
        config.qrss.frequency_hz = std::stod(qrss_frequency_arg);
        config.qrss.dot_seconds = std::stod(qrss_dot_seconds_arg);
        config.mode = ModeType::QRSS;
    }

    const bool any_fskcw_arg =
        !fskcw_message_arg.empty() ||
        !fskcw_mark_frequency_arg.empty() ||
        !fskcw_space_frequency_arg.empty() ||
        !fskcw_dot_seconds_arg.empty();
    if (any_fskcw_arg)
    {
        if (config.use_ini)
        {
            print_usage("FSKCW test mode is invalid when using INI file.", EXIT_FAILURE);
        }

        if (fskcw_message_arg.empty() ||
            fskcw_mark_frequency_arg.empty() ||
            fskcw_space_frequency_arg.empty() ||
            fskcw_dot_seconds_arg.empty())
        {
            print_usage(
                "FSKCW test mode requires --fskcw-message, --fskcw-mark-frequency, --fskcw-space-frequency, and --fskcw-dot-seconds.",
                EXIT_FAILURE);
        }

        std::string error_message;
        if (!set_fskcw_startup_request(
                fskcw_message_arg,
                fskcw_mark_frequency_arg,
                fskcw_space_frequency_arg,
                fskcw_dot_seconds_arg,
                &error_message))
        {
            print_usage(error_message, EXIT_FAILURE);
        }

        clear_direct_tone_startup_request();
        clear_qrss_startup_request();
        clear_dfcw_startup_request();
        config.fskcw.message = fskcw_message_arg;
        config.fskcw.mark_frequency_hz = std::stod(fskcw_mark_frequency_arg);
        config.fskcw.space_frequency_hz = std::stod(fskcw_space_frequency_arg);
        config.fskcw.dot_seconds = std::stod(fskcw_dot_seconds_arg);
        config.mode = ModeType::FSKCW;
    }

    const bool any_dfcw_arg =
        !dfcw_message_arg.empty() ||
        !dfcw_dot_frequency_arg.empty() ||
        !dfcw_dash_frequency_arg.empty() ||
        !dfcw_dot_seconds_arg.empty();
    if (any_dfcw_arg)
    {
        if (config.use_ini)
        {
            print_usage("DFCW test mode is invalid when using INI file.", EXIT_FAILURE);
        }

        if (dfcw_message_arg.empty() ||
            dfcw_dot_frequency_arg.empty() ||
            dfcw_dash_frequency_arg.empty() ||
            dfcw_dot_seconds_arg.empty())
        {
            print_usage(
                "DFCW test mode requires --dfcw-message, --dfcw-dot-frequency, --dfcw-dash-frequency, and --dfcw-dot-seconds.",
                EXIT_FAILURE);
        }

        std::string error_message;
        if (!set_dfcw_startup_request(
                dfcw_message_arg,
                dfcw_dot_frequency_arg,
                dfcw_dash_frequency_arg,
                dfcw_dot_seconds_arg,
                &error_message))
        {
            print_usage(error_message, EXIT_FAILURE);
        }

        clear_direct_tone_startup_request();
        clear_qrss_startup_request();
        clear_fskcw_startup_request();
        config.dfcw.message = dfcw_message_arg;
        config.dfcw.dot_frequency_hz = std::stod(dfcw_dot_frequency_arg);
        config.dfcw.dash_frequency_hz = std::stod(dfcw_dash_frequency_arg);
        config.dfcw.dot_seconds = std::stod(dfcw_dot_seconds_arg);
        config.mode = ModeType::DFCW;
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

            sync_wspr_mode_config(config);
        }
    }
    // Re-save any config changes in the JSON
    config_to_json();

    return true;
}

bool consume_startup_config_handoff() noexcept
{
    return startup_config_handoff_ready.exchange(false, std::memory_order_acq_rel);
}
