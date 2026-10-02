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

#include "config_types.hpp"
#include "ini_file.hpp"
#include "json.hpp"

#include <stdexcept>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>


/**
 * @brief  Construct the singleton IniFile instance.
 *
 * Provides a global reference `iniFile` that resolves to
 * `IniFile::instance()`.
 */
inline auto &iniFile = IniFile::instance();

// Serializes public revision snapshots with JSON/INI reload and web mutations.
// Translation/persistence helpers nest inside an already locked update.
inline std::recursive_mutex &config_update_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}


/**
 * @brief Global JSON configuration object.
 *
 * This nlohmann::json object holds the application's configuration in JSON format.
 * It is used to merge and update configuration settings dynamically.
 */
extern nlohmann::json jConfig;

struct PreparedConfigCandidate
{
    nlohmann::json normalized_json{};
    ArgParserConfig normalized_config{};
    std::string prepared_revision{};
    bool valid = false;
    bool transmit_enabled = false;
    std::string error_reason{};
    nlohmann::json error_details{};
    std::vector<std::string> warnings{};
    bool migration_required = false;
    bool preserve_interactive_takeover = false;
};

struct Si5351AddressInventory
{
    int i2c_bus = -1;
    std::vector<int> addresses{};
    std::string error{};
    bool checked = false;

    bool contains(int address) const noexcept;
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

/**
 * @brief Returns the most recently published accepted WSPR audio offset.
 *
 * This is a read-only snapshot for concurrent catalog/status consumers.  It
 * must not be used to modify the runtime configuration.
 */
double current_wspr_audio_offset_hz() noexcept;

/**
 * @brief Returns a coherent value copy of accepted Test Tone planning inputs.
 */
TestTonePlanningConfigSnapshot current_test_tone_planning_config_snapshot();

void resolve_backend_specific_config(ArgParserConfig &config) noexcept;
bool si5351_device_detected(
    int i2c_bus,
    int i2c_address,
    int reference_hz,
    std::string *error_message = nullptr);
void set_si5351_inventory_command_override_for_test(const std::vector<std::string>& command);
void clear_si5351_inventory_command_override_for_test();
Si5351AddressInventory cached_si5351_addresses(int i2c_bus, int reference_hz);
Si5351AddressInventory discover_si5351_addresses(
    int i2c_bus,
    int reference_hz,
    int selected_address = -1);
void set_si5351_detection_override_for_test(bool detected) noexcept;
void clear_si5351_detection_override_for_test() noexcept;
void set_si5351_address_inventory_override_for_test(
    int i2c_bus,
    const std::vector<int> &addresses,
    const std::string &error = {});
void clear_si5351_address_inventory_override_for_test() noexcept;

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
 *       "Enable on Boot": "Never",
 *       "Use LED": false,
 *       "LED Pin": 18,
 *       "Use Amp": false,
 *       "Amp Pin": -1,
 *       "Amp Pin Active High": false,
 *       "Web Port": 31415,
 *       "Socket Port": 31416,
 *       "Use Shutdown": false,
 *       "Shutdown Button": 19
 *   },
 *   "GPIO": {
 *       "Transmit Pin": 4,
 *       "Power Level": 7,
 *       "Use System Clock Frequency Estimate": true
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
 *       "Frequency Profile": "existing_common",
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
 *       "DFCW Intra Element Gap": 0.333333,
 *       "DFCW Inter Character Gap": 1.0,
 *       "DFCW Inter Word Gap": 3.0,
 *       "Fade Shape": "none",
 *       "Fade In Ms": 0,
 *       "Fade Out Ms": 0,
 *       "Fade Slice Ms": 5,
 *       "Start Minute": 0,
 *       "Start Second": 5,
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
 * @brief Applies the startup policy for Operation.Enable on Boot.
 *
 * Never and Always update Operation.Transmit and persist through the normal
 * JSON-to-INI path. Follow leaves the loaded Transmit setting unchanged.
 *
 * @return true when Operation.Transmit was set and persisted by the policy.
 */
bool apply_enable_on_boot_startup_policy();

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
enum class LocalEnableAction { Noninteractive, EndNow, FinishCurrent };
std::string patch_all_from_web_revision(const nlohmann::json &j, const std::string &expected_revision,
    LocalEnableAction action = LocalEnableAction::Noninteractive, bool confirmed = false,
    const std::string& observed_owner = {}, const std::string& observed_job = {});
std::pair<nlohmann::json, std::string> get_public_config_snapshot();
void set_patch_all_from_web_runtime_apply_suppressed_for_test(bool suppressed) noexcept;
bool persist_rp1_gpclk_route_config(int gpio, std::string *error_message = nullptr) noexcept;

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
