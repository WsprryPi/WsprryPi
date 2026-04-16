/**
 * @file config_handler.hpp
 * @brief Persistent configuration model and JSON/INI translation helpers.
 *
 * This layer owns durable configuration values and their serialized
 * representation. Transient runtime requests such as `--test-tone` do not
 * live here. Frequency entries may include optional `@GPIO[H|L]` metadata
 * that overrides the selected band GPIO for one scheduler slot.
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

#ifndef _CONFIG_HANDLER_HPP
#define _CONFIG_HANDLER_HPP

#include "band_gpio.hpp"
#include "ini_file.hpp"
#include "json.hpp"
#include "wspr_ref_plan.hpp"

#include <array>
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

inline constexpr int kTransmitGpioUnset = -1;
inline constexpr int kDefaultTransmitGpio = 4;
inline constexpr std::array<int, 2> kSupportedTransmitGpio = {4, 20};
inline constexpr int kSelectorGpioUnset = -1;
inline constexpr double WSPR_AUDIO_OFFSET_HZ = 1500.0;
inline constexpr int kDefaultSi5351I2cBus = 1;
inline constexpr int kDefaultSi5351I2cAddress = 0x60;
inline constexpr int kDefaultSi5351ReferenceHz = 27000000;
inline constexpr int kDefaultSi5351TxOutput = 0;

inline constexpr bool is_supported_transmit_gpio(int gpio) noexcept
{
    for (int supported_gpio : kSupportedTransmitGpio)
    {
        if (gpio == supported_gpio)
        {
            return true;
        }
    }

    return false;
}

inline constexpr bool is_valid_selector_gpio(int gpio) noexcept
{
    return gpio >= 0 && gpio <= 27;
}

struct WsprFrequencyEntry
{
    std::string token; ///< Original frequency token without `@GPIO[H|L]`.
    double dial_frequency_hz = 0.0; ///< Resolved WSPR dial frequency in Hz.
    int selector_gpio = kSelectorGpioUnset; ///< Optional per-entry selector GPIO.
    bool selector_gpio_active_high = false; ///< Optional selector GPIO polarity; false means active low.
    bool allow_band_gpio_fallback = false; ///< True when [Band GPIO] may supply the selector.
};

/**
 * @brief  Construct the singleton IniFile instance.
 *
 * Provides a global reference `iniFile` that resolves to
 * `IniFile::instance()`.
 */
inline auto &iniFile = IniFile::instance();

/**
 * @brief Global JSON configuration object.
 *
 * This nlohmann::json object holds the application's configuration in JSON format.
 * It is used to merge and update configuration settings dynamically.
 */
extern nlohmann::json jConfig;

/**
 * @enum ModeType
 * @brief Specifies the mode of operation for the application.
 *
 * This enumeration defines the available modes for operation.
 * - `WSPR`: Represents the WSPR (Weak Signal Propagation Reporter) transmission mode.
 * - `TONE`: Represents transient direct-tone runtime behavior.
 */
enum class ModeType
{
    WSPR, ///< WSPR transmission mode
    TONE, ///< Test tone generation mode
    QRSS, ///< Temporary QRSS test mode
    FSKCW, ///< Temporary FSKCW test mode
    DFCW ///< Temporary DFCW test mode
};

enum class WsprPlannerPreference
{
    Auto = 0,
    PreferPaired,
    RequirePaired
};

enum class TransmitBackendKind
{
    GPIO = 0,
    SI5351
};

inline constexpr const char *transmit_backend_kind_to_string(
    TransmitBackendKind backend) noexcept
{
    switch (backend)
    {
    case TransmitBackendKind::GPIO:
        return "gpio";
    case TransmitBackendKind::SI5351:
        return "si5351";
    }

    return "gpio";
}

struct WsprModeConfig
{
    std::string callsign;
    std::string grid_square;
    int power_dbm = 0;
    std::string frequencies;
    double audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
    WsprPlannerPreference planner_preference = WsprPlannerPreference::Auto;
};

struct QrssModeConfig
{
    std::string message;
    double frequency_hz = 0.0;
    double dot_seconds = 0.0;
};

struct FskcwModeConfig
{
    std::string message;
    double mark_frequency_hz = 0.0;
    double space_frequency_hz = 0.0;
    double dot_seconds = 0.0;
};

struct DfcwModeConfig
{
    std::string message;
    double dot_frequency_hz = 0.0;
    double dash_frequency_hz = 0.0;
    double dot_seconds = 0.0;
};

inline constexpr const char *wspr_planner_preference_to_string(
    WsprPlannerPreference preference) noexcept
{
    switch (preference)
    {
    case WsprPlannerPreference::Auto:
        return "auto";
    case WsprPlannerPreference::PreferPaired:
        return "prefer_paired";
    case WsprPlannerPreference::RequirePaired:
        return "require_paired";
    }

    return "auto";
}

inline constexpr wspr::TransmissionPlanPreference
wspr_planner_preference_to_plan_preference(
    WsprPlannerPreference preference) noexcept
{
    switch (preference)
    {
    case WsprPlannerPreference::Auto:
        return wspr::TransmissionPlanPreference::Auto;
    case WsprPlannerPreference::PreferPaired:
        return wspr::TransmissionPlanPreference::PreferPaired;
    case WsprPlannerPreference::RequirePaired:
        return wspr::TransmissionPlanPreference::RequirePaired;
    }

    return wspr::TransmissionPlanPreference::Auto;
}

/**
 * @brief Global configuration instance for argument parsing and runtime settings.
 *
 * @details
 * Holds all command-line and runtime configuration data not managed directly
 * by the INI file system. Initialized globally and used throughout the application.
 *
 * @see ArgParserConfig, ini, iniMonitor
 */
struct ArgParserConfig
{
    // Runtime
    bool transmit; ///< Transmission mode enabled.

    // WSPR
    std::string callsign;    ///< WSPR callsign.
    std::string grid_square; ///< 4- or 6-character Maidenhead locator.
    int power_dbm;           ///< Transmit power in dBm.
    std::string frequencies; ///< Space-separated user-facing WSPR dial frequency list.

    // Resolved active backend values
    int tx_pin; ///< Active GPIO pin number for RF transmit control.

    // Runtime
    double ppm;      ///< PPM frequency calibration.
    bool use_ntp;    ///< Active backend NTP correction setting.
    bool use_offset; ///< Enable WSPR random frequency offset.
    int power_level; ///< Active backend RF power level.
    TransmitBackendKind transmit_backend; ///< RF hardware backend.
    int gpio_tx_pin; ///< GPIO backend transmit pin.
    int gpio_power_level; ///< GPIO backend power level (0-7).
    bool gpio_use_ntp; ///< GPIO backend NTP correction setting.
    int si5351_i2c_bus; ///< Si5351 I2C bus number.
    int si5351_i2c_address; ///< Si5351 I2C slave address.
    int si5351_reference_hz; ///< Si5351 reference frequency in Hz.
    int si5351_tx_output; ///< Si5351 output clock index (0=CLK0).
    int si5351_power_level; ///< Si5351 drive-strength level (1-4).
    bool use_led;    ///< Enable TX LED indicator.
    int led_pin;     ///< GPIO pin for LED indicator.

    // Runtime
    int web_port;      ///< Web server port number.
    int socket_port;   ///< Socket server port number.
    bool use_shutdown; ///< Enable GPIO-based shutdown feature.
    int shutdown_pin;  ///< GPIO pin used to signal shutdown.

    // Command line only
    bool use_journald;              ///< Route logs to journald instead of streams.
    bool date_time_log;             ///< Prefix logs with timestamp.
    WsprPlannerPreference wspr_planner_preference; ///< Preferred planner behavior for Type 2/3 pairing.
    bool loop_tx;                   ///< Repeat transmission cycle.
    std::atomic<int> tx_iterations; ///< Number of transmission iterations (0 = infinite).
    double wspr_audio_offset_hz;    ///< Runtime WSPR audio offset constant.
    double modulation_dot_seconds;  ///< Shared CW dot length.
    double modulation_fsk_offset_hz; ///< Shared CW shift in Hz.
    double cw_intra_element_gap; ///< CW intra-element gap in dot-length multiples.
    double cw_inter_character_gap; ///< CW inter-character gap in dot-length multiples.
    double cw_inter_word_gap; ///< CW inter-word gap in dot-length multiples.
    std::string cw_fade_shape; ///< CW envelope fade shape.
    int cw_fade_in_ms; ///< CW envelope fade-in duration in milliseconds.
    int cw_fade_out_ms; ///< CW envelope fade-out duration in milliseconds.
    int cw_fade_slice_ms; ///< CW fade approximation slice duration in milliseconds.
    int schedule_start_minute;      ///< CW schedule minute offset within the hour.
    int schedule_repeat_minutes;    ///< CW schedule repeat interval in minutes.

    // Runtime variables
    ModeType mode;                       ///< Current operating mode.
    WsprModeConfig wspr;                 ///< Long-term WSPR mode configuration.
    QrssModeConfig qrss;                 ///< Long-term QRSS mode configuration.
    FskcwModeConfig fskcw;               ///< Long-term FSKCW mode configuration.
    DfcwModeConfig dfcw;                 ///< Long-term DFCW mode configuration.
    bool use_ini;                        ///< Load configuration from INI file.
    std::string ini_filename;            ///< INI file name and path.
    std::vector<double> wspr_dial_freq_set; ///< Parsed WSPR dial frequencies.
    std::vector<WsprFrequencyEntry>
        wspr_frequency_entries; ///< Parsed entries with optional GPIO/polarity metadata.
    bool ntp_good;                       ///< A more qualitative measurement of NTP vs simply running
    std::array<BandGPIOConfig, HAM_BAND_COUNT> band_gpio; ///< Per-band GPIO assignment.

    /**
     * @brief Default constructor initializing all configuration parameters.
     */
    ArgParserConfig()
        : transmit(true),
          callsign(""),
          grid_square(""),
          power_dbm(0),
          frequencies(""),
          tx_pin(kTransmitGpioUnset),
          ppm(0.0),
          use_ntp(false),
          use_offset(false),
          power_level(7),
          transmit_backend(TransmitBackendKind::GPIO),
          gpio_tx_pin(kTransmitGpioUnset),
          gpio_power_level(7),
          gpio_use_ntp(false),
          si5351_i2c_bus(kDefaultSi5351I2cBus),
          si5351_i2c_address(kDefaultSi5351I2cAddress),
          si5351_reference_hz(kDefaultSi5351ReferenceHz),
          si5351_tx_output(kDefaultSi5351TxOutput),
          si5351_power_level(1),
          use_led(false),
          led_pin(-1),
          web_port(-1),
          socket_port(-1),
          use_shutdown(false),
          shutdown_pin(-1),
          use_journald(false),
          date_time_log(false),
          wspr_planner_preference(WsprPlannerPreference::Auto),
          loop_tx(false),
          tx_iterations(0),
          wspr_audio_offset_hz(WSPR_AUDIO_OFFSET_HZ),
          modulation_dot_seconds(3.0),
          modulation_fsk_offset_hz(500.0),
          cw_intra_element_gap(1.0),
          cw_inter_character_gap(3.0),
          cw_inter_word_gap(7.0),
          cw_fade_shape("none"),
          cw_fade_in_ms(0),
          cw_fade_out_ms(0),
          cw_fade_slice_ms(5),
          schedule_start_minute(0),
          schedule_repeat_minutes(10),
          mode(ModeType::WSPR),
          wspr({}),
          qrss({}),
          fskcw({}),
          dfcw({}),
          use_ini(false),
          ini_filename(""),
          wspr_dial_freq_set({}),
          wspr_frequency_entries({}),
          ntp_good(false),
          band_gpio({})
    {
    }

    ArgParserConfig(const ArgParserConfig &other)
        : ArgParserConfig()
    {
        *this = other;
    }

    ArgParserConfig &operator=(const ArgParserConfig &other)
    {
        if (this == &other)
        {
            return *this;
        }

        transmit = other.transmit;
        callsign = other.callsign;
        grid_square = other.grid_square;
        power_dbm = other.power_dbm;
        frequencies = other.frequencies;
        tx_pin = other.tx_pin;
        ppm = other.ppm;
        use_ntp = other.use_ntp;
        use_offset = other.use_offset;
        power_level = other.power_level;
        transmit_backend = other.transmit_backend;
        gpio_tx_pin = other.gpio_tx_pin;
        gpio_power_level = other.gpio_power_level;
        gpio_use_ntp = other.gpio_use_ntp;
        si5351_i2c_bus = other.si5351_i2c_bus;
        si5351_i2c_address = other.si5351_i2c_address;
        si5351_reference_hz = other.si5351_reference_hz;
        si5351_tx_output = other.si5351_tx_output;
        si5351_power_level = other.si5351_power_level;
        use_led = other.use_led;
        led_pin = other.led_pin;
        web_port = other.web_port;
        socket_port = other.socket_port;
        use_shutdown = other.use_shutdown;
        shutdown_pin = other.shutdown_pin;
        use_journald = other.use_journald;
        date_time_log = other.date_time_log;
        wspr_planner_preference = other.wspr_planner_preference;
        loop_tx = other.loop_tx;
        tx_iterations.store(other.tx_iterations.load());
        wspr_audio_offset_hz = other.wspr_audio_offset_hz;
        modulation_dot_seconds = other.modulation_dot_seconds;
        modulation_fsk_offset_hz = other.modulation_fsk_offset_hz;
        cw_intra_element_gap = other.cw_intra_element_gap;
        cw_inter_character_gap = other.cw_inter_character_gap;
        cw_inter_word_gap = other.cw_inter_word_gap;
        cw_fade_shape = other.cw_fade_shape;
        cw_fade_in_ms = other.cw_fade_in_ms;
        cw_fade_out_ms = other.cw_fade_out_ms;
        cw_fade_slice_ms = other.cw_fade_slice_ms;
        schedule_start_minute = other.schedule_start_minute;
        schedule_repeat_minutes = other.schedule_repeat_minutes;
        mode = other.mode;
        wspr = other.wspr;
        qrss = other.qrss;
        fskcw = other.fskcw;
        dfcw = other.dfcw;
        use_ini = other.use_ini;
        ini_filename = other.ini_filename;
        wspr_dial_freq_set = other.wspr_dial_freq_set;
        wspr_frequency_entries = other.wspr_frequency_entries;
        ntp_good = other.ntp_good;
        band_gpio = other.band_gpio;
        return *this;
    }
};

/**
 * @brief Global configuration object.
 *
 * This ArgParserConfig instance holds the application’s configuration settings,
 * typically loaded from an INI file or a JSON configuration.
 */
extern ArgParserConfig config;

struct PreparedConfigCandidate
{
    nlohmann::json normalized_json{};
    ArgParserConfig normalized_config{};
    bool valid = false;
    bool transmit_enabled = false;
    std::string error_reason{};
    nlohmann::json error_details{};
    std::vector<std::string> warnings{};
};

class ConfigValidationError : public std::runtime_error
{
public:
    explicit ConfigValidationError(
        const std::string &message,
        nlohmann::json details = {})
        : std::runtime_error(message), details_(std::move(details))
    {
    }

    const nlohmann::json &details() const noexcept
    {
        return details_;
    }

private:
    nlohmann::json details_{};
};

void init_default_config();
void resolve_backend_specific_config(ArgParserConfig &config) noexcept;
bool si5351_device_detected(
    int i2c_bus,
    int i2c_address,
    int reference_hz,
    std::string *error_message = nullptr);
void set_si5351_detection_override_for_test(bool detected) noexcept;
void clear_si5351_detection_override_for_test() noexcept;

/**
 * @brief Initializes the global configuration JSON object.
 *
 * @details
 * This function sets up a default configuration structure in the global
 * nlohmann::json object, `jConfig`. The JSON object is organized into several
 * sections: "Operation", "GPIO", "Si5351", "Calibration", "WSPR", "CW", and
 * "Band GPIO". Each section contains key/value pairs that represent
 * configuration parameters.
 *
 * @note The JSON values are stored as strings. Adjust the types as needed if numeric
 *       types are required in later processing.
 */
void init_config_json();

/**
 * @brief Patches the global JSON configuration with data from the INI file.
 *
 * @details
 * This function retrieves INI configuration data from the global INI handler object `ini`
 * and converts the data into a JSON object (named `patch`). Each INI section is converted
 * into a JSON object containing key/value pairs. It also records internal INI
 * bookkeeping metadata and merges the resulting patch into the global JSON
 * configuration object `jConfig` using `merge_patch()`.
 *
 * If any exception is thrown while retrieving the INI data, the function catches the exception
 * and returns without modifying `jConfig`.
 *
 * @param filename The name of the INI file to record in the JSON configuration.
 */
void ini_to_json(std::string filename);

/**
 * @brief Parses configuration from a JSON object into an ArgParser struct.
 *
 * @param jConfig The JSON object containing configuration data.
 *
 * Expected JSON structure (example):
 * @code
 * {
 *   "Operation": {
 *       "Mode": "WSPR",
 *       "Transmit": false,
 *       "Transmit Backend": "gpio",
 *       "Use LED": false,
 *       "LED Pin": 18,
 *       "Web Port": 31415,
 *       "Socket Port": 31416,
 *       "Use Shutdown": false,
 *       "Shutdown Button": 19
 *   },
 *   "GPIO": {
 *       "Transmit Pin": 4,
 *       "Power Level": 7,
 *       "Use NTP": true
 *   },
 *   "Si5351": {
 *       "I2C Bus": 1,
 *       "I2C Address": "0x60",
 *       "Reference Frequency": 27000000,
 *       "TX Output": "CLK0",
 *       "Power Level": 1
 *   },
 *   "Calibration": {
 *       "PPM": 0.0
 *   },
 *   "WSPR": {
 *       "Call Sign": "NXXX",
 *       "Grid Square": "ZZ99",
 *       "TX Power": 20,
 *       "Frequency": "20m",
 *       "Planner Preference": "auto",
 *       "Use Random Offset": true
 *   },
 *   "CW": {
 *       "Message": "",
 *       "Base Frequency": 3572000.0,
 *       "Shift Hz": 500.0,
 *       "Dot Seconds": 3.0,
 *       "Intra Element Gap": 1.0,
 *       "Inter Character Gap": 3.0,
 *       "Inter Word Gap": 7.0,
 *       "Fade Shape": "none",
 *       "Fade In Ms": 0,
 *       "Fade Out Ms": 0,
 *       "Fade Slice Ms": 5,
 *       "Start Minute": 0,
 *       "Repeat Minutes": 10
 *   }
 * }
 * @endcode
 */
void json_to_config();

/**
 * @brief Creates a JSON object from the configuration struct.
 *
 * @details
 * This function overlays the configuration stored in an ArgParser struct
 * onto a JSON object. It uses the same structure as the original JSON file,
 * converting booleans and numbers into strings where needed (since the
 * parsing code expects strings).
 *
 * @param config The configuration struct to overlay.
 */
extern void config_to_json();

/**
 * @brief Saves the global JSON configuration back to the INI file.
 *
 * @details
 * If the configuration indicates that an INI file is being used (i.e. `config.use_ini`
 * is true), this function first updates the global JSON configuration by calling
 * `config_to_json()`. It then converts the JSON configuration (`jConfig`)
 * into an internal data structure (`newData`) suitable for the INI handler. Each
 * section in the JSON becomes a key in the map, with its value being an unordered map
 * of key/value pairs. If a JSON value is an array, it is converted to a string using
 * the `dump()` method; otherwise, the value is retrieved as a string.
 *
 * Finally, the new data is set into the global INI handler object (`ini`) using
 * `iniFile.setData(newData)` and saved to disk via `iniFile.save()`.
 *
 * @note This function assumes that all JSON values can be represented as strings.
 */
extern void json_to_ini();

/**
 * @brief Loads the global JSON configuration by merging default JSON and INI file data.
 *
 * @details
 * This function performs a three-step process:
 *  1. Calls `init_config_json()` to create a base JSON configuration with default values.
 *  2. Calls `ini_to_json(filename)` to overlay INI file data (from the given filename)
 *     onto the base JSON configuration.
 *  3. Calls `json_to_config()` to parse the updated JSON configuration into the global
 *     configuration structure (of type `ArgParser`).
 *
 * This layered approach allows default settings to be overridden by INI file values.
 *
 * @param filename The path to the INI file whose data will be merged into the JSON configuration.
 */
extern bool load_json(
    std::string filename,
    std::string *error_message = nullptr,
    std::vector<std::string> *warning_messages = nullptr);

void prepare_ini_config_candidate(
    const std::string &filename,
    PreparedConfigCandidate &candidate_out);

void commit_config_candidate(const PreparedConfigCandidate &candidate);

void copy_runtime_config(const ArgParserConfig &source, ArgParserConfig &target);

/**
 * @brief Prints a formatted JSON object to standard output.
 *
 * @details This function outputs the given JSON object to `std::cout` with
 *          an indent of 4 spaces and ensures key names are sorted.
 *          Useful for debugging or configuration output.
 *
 * @param j The JSON object to dump (will not be modified).
 *
 * @return void
 */
void dump_json(const nlohmann::json &j, std::string tag);

nlohmann::json get_public_config_json();

/**
 * @brief Applies a full patch update from incoming JSON.
 * @details This function receives a JSON object (typically from the web server),
 *          merges it into the current global JSON configuration (`jConfig`),
 *          updates the INI file and global config structure accordingly, and
 *          rebuilds the cleaned `jConfig` from the sanitized config values.
 *
 *          The flow is:
 *            1. Patch the input into `jConfig`.
 *            2. Update the INI file to reflect patched values.
 *            3. Update the config struct from patched values.
 *            4. Overwrite `jConfig` with sanitized config struct values.
 *            5. Dump final JSON (for debugging).
 *
 * @param j The incoming JSON object to patch into global configuration.
 *
 * @throws May throw exceptions from internal calls (e.g., parsing or write errors).
 */
void patch_all_from_web(const nlohmann::json &j);
void set_patch_all_from_web_runtime_apply_suppressed_for_test(bool suppressed) noexcept;

/**
 * @brief Repairs or restores the configuration from stock defaults.
 *
 * Performs either a repair or full restore of the INI configuration using
 * stock defaults, then reloads the runtime configuration state.
 *
 * If repair is selected, only missing or invalid values are corrected.
 * If restore is selected, the configuration is fully reset to stock.
 *
 * After updating the INI file, the configuration is reloaded by converting
 * INI data to JSON and then parsing it into the global configuration.
 *
 * @param attempt_repair If true, performs a repair. If false, performs a
 *                       full restore from stock.
 *
 * @return None.
 */
void repair_from_web(bool attempt_repair);

#endif // _CONFIG_HANDLER_HPP
