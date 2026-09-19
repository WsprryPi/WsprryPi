/**
 * @file config_handler.cpp
 * @brief Provides an interface to ArgParserConfig and JSON config
 *
 * This project is licensed under the MIT License. See LICENSE.md
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

#include "wtp_settings_json.hpp"
#include "wtp_runtime_bridge.hpp"
#include "config_handler.hpp"
#include "i2c_bus_inventory.hpp"
#include "config_handler_deserialization.hpp"
#include "config_handler_serialization.hpp"

#include <atomic>
#include "backend_capabilities.hpp"
#include "arg_parser.hpp"
#include "ini_file.hpp"
#include "json.hpp"
#include "logging.hpp"
#include "scheduling.hpp"
#if WSPRRYPI_BACKEND_SI5351
#include "si5351_device.hpp"
#endif
#include "version.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <openssl/sha.h>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

ArgParserConfig config;
nlohmann::json jConfig;

bool transmit_backend_is_compiled(TransmitBackendKind backend) noexcept
{
    switch (backend)
    {
    case TransmitBackendKind::GPIO:
        return WSPRRYPI_BACKEND_RPI_GPIO;
    case TransmitBackendKind::RP1_GPCLK:
        return WSPRRYPI_BACKEND_RP1_GPCLK;
    case TransmitBackendKind::SI5351:
        return WSPRRYPI_BACKEND_SI5351;
    case TransmitBackendKind::SIMULATED:
        return WSPRRYPI_BACKEND_SIMULATED;
    case TransmitBackendKind::WTP:
        return true;
    }
    return false;
}

bool transmit_backend_requires_root(TransmitBackendKind backend) noexcept
{
    switch (backend)
    {
    case TransmitBackendKind::SIMULATED:
    case TransmitBackendKind::WTP:
        return false;
    case TransmitBackendKind::SI5351:
        return build_has_physical_gpio_capability();
    case TransmitBackendKind::GPIO:
    case TransmitBackendKind::RP1_GPCLK:
        return true;
    }
    return true;
}

bool build_has_physical_gpio_capability() noexcept
{
    return WSPRRYPI_BACKEND_RPI_GPIO || WSPRRYPI_BACKEND_RP1_GPCLK ||
        WSPRRYPI_ANCILLARY_GPIO;
}

std::string transmit_backend_unavailable_message(TransmitBackendKind backend)
{
    return std::string("Backend '") + transmit_backend_kind_to_string(backend) +
        "' is valid but unavailable in this build. Compiled backends: " +
        get_compiled_backends() + ".";
}

namespace
{
    bool g_patch_all_from_web_runtime_apply_suppressed_for_test = false;
    std::atomic<double> g_published_wspr_audio_offset_hz{WSPR_AUDIO_OFFSET_HZ};
    std::shared_mutex g_test_tone_planning_snapshot_mutex;
    TestTonePlanningConfigSnapshot g_test_tone_planning_snapshot{};
    std::optional<bool> g_si5351_detection_override;
    std::optional<Si5351AddressInventory> g_si5351_address_inventory_override;

    void publish_test_tone_planning_config(const ArgParserConfig &source)
    {
        TestTonePlanningConfigSnapshot snapshot;
        snapshot.transmit_backend = source.transmit_backend;
        snapshot.allow_unqualified_frequency = source.allow_unqualified_frequency;
        snapshot.allow_non_amateur_frequency = source.allow_non_amateur_frequency;
        snapshot.wspr_audio_offset_hz = source.wspr.audio_offset_hz;
        snapshot.wspr_frequency_profile = source.wspr.frequency_profile;
        snapshot.wspr_band_preferences = source.wspr.band_preferences;
        snapshot.wspr_frequency_entries = source.wspr_frequency_entries;
        snapshot.band_gpio = source.band_gpio;

        std::unique_lock<std::shared_mutex> lock(
            g_test_tone_planning_snapshot_mutex);
        g_test_tone_planning_snapshot = std::move(snapshot);
        g_published_wspr_audio_offset_hz.store(
            source.wspr.audio_offset_hz,
            std::memory_order_release);
    }

    std::string si5351_detection_unavailable_message(
        const std::string &detail = std::string())
    {
        std::string message =
            "Si5351 transmission is unavailable because no Si5351 device was detected on the I2C bus.";
        if (!detail.empty())
        {
            message += " ";
            message += detail;
        }
        return message;
    }

    std::string trim_copy(const std::string &value);


    int parse_integer_config_value(
        const nlohmann::json &source,
        const std::string &context,
        int base = 10)
    {
        if (source.is_number_integer())
        {
            return source.get<int>();
        }

        if (source.is_number_unsigned())
        {
            const auto value = source.get<unsigned int>();
            if (value > static_cast<unsigned int>(std::numeric_limits<int>::max()))
            {
                throw std::runtime_error(context + " is out of range.");
            }
            return static_cast<int>(value);
        }

        if (source.is_string())
        {
            const std::string raw = trim_copy(source.get<std::string>());
            std::size_t consumed = 0;
            try
            {
                const int parsed = std::stoi(raw, &consumed, base);
                if (consumed == raw.size())
                {
                    return parsed;
                }
            }
            catch (const std::invalid_argument &)
            {
            }
            catch (const std::out_of_range &)
            {
                throw std::runtime_error(context + " is out of range.");
            }
        }

        throw std::runtime_error(context + " must be an integer.");
    }


    std::string format_si5351_i2c_address(int address)
    {
        static constexpr char kHexDigits[] = "0123456789ABCDEF";
        unsigned int value = static_cast<unsigned int>(address);
        std::string formatted = "0x";
        formatted.push_back(kHexDigits[(value >> 4) & 0xF]);
        formatted.push_back(kHexDigits[value & 0xF]);
        return formatted;
    }

    int normalize_gpio_transmit_pin(int gpio) noexcept
    {
        return is_supported_transmit_gpio(gpio) ? gpio : kDefaultTransmitGpio;
    }


    const char *mode_type_to_string(ModeType mode) noexcept
    {
        switch (mode)
        {
        case ModeType::WSPR: return "WSPR";
        case ModeType::QRSS: return "QRSS";
        case ModeType::FSKCW: return "FSKCW";
        case ModeType::DFCW: return "DFCW";
        case ModeType::TONE: return "TONE";
        }

        return "WSPR";
    }



    nlohmann::json make_plan_validation_error_details(
        const wspr::TransmissionPlanResult &plan)
    {
        nlohmann::json details;
        details["status"] = "invalid_config";
        details["plan_status"] = std::string(wspr::to_string(plan.status));
        details["message"] = plan.message;

        if (!plan.rationale.empty())
        {
            details["rationale"] = plan.rationale;
        }

        if (!plan.normalized_callsign.empty())
        {
            details["normalized_callsign"] = plan.normalized_callsign;
        }

        if (!plan.normalized_locator.empty())
        {
            details["normalized_locator"] = plan.normalized_locator;
        }

        return details;
    }

    bool validate_wspr_semantics(
        const ArgParserConfig &candidate,
        std::string *error_message,
        nlohmann::json *error_details = nullptr)
    {
        if (candidate.mode != ModeType::WSPR)
        {
            return true;
        }

        const std::string trimmed_callsign = trim_copy(candidate.callsign);
        const std::string trimmed_locator = trim_copy(candidate.grid_square);
        if (trimmed_callsign.empty() || trimmed_locator.empty())
        {
            return true;
        }

        const auto preference =
            wspr_planner_preference_to_plan_preference(
                candidate.wspr_planner_preference);
        const auto plan = wspr::plan_transmission(
            candidate.callsign,
            candidate.grid_square,
            candidate.power_dbm,
            preference);

        if (plan.ok)
        {
            return true;
        }

        const nlohmann::json details = make_plan_validation_error_details(plan);
        if (error_message != nullptr)
        {
            *error_message = plan.message;
        }
        if (error_details != nullptr)
        {
            *error_details = details;
        }
        return false;
    }

    const std::array<std::pair<HamBand, const char *>, HAM_BAND_COUNT> kHamBandJsonKeys = {{
        {HamBand::BAND_2200M, "2200m"},
        {HamBand::BAND_630M, "630m"},
        {HamBand::BAND_160M, "160m"},
        {HamBand::BAND_80M, "80m"},
        {HamBand::BAND_60M, "60m"},
        {HamBand::BAND_40M, "40m"},
        {HamBand::BAND_30M, "30m"},
        {HamBand::BAND_20M, "20m"},
        {HamBand::BAND_17M, "17m"},
        {HamBand::BAND_15M, "15m"},
        {HamBand::BAND_12M, "12m"},
        {HamBand::BAND_10M, "10m"},
        {HamBand::BAND_8M, "8m"},
        {HamBand::BAND_6M, "6m"},
        {HamBand::BAND_5M, "5m"},
        {HamBand::BAND_4M, "4m"},
        {HamBand::BAND_2M, "2m"},
        {HamBand::BAND_1_25M, "1.25m"},
        {HamBand::BAND_70CM, "70cm"},
    }};

    std::string trim_copy(const std::string &value)
    {
        const std::string whitespace = " \t\r\n";
        const std::size_t first = value.find_first_not_of(whitespace);

        if (first == std::string::npos)
        {
            return "";
        }

        const std::size_t last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    BandGPIOConfig make_band_gpio_config(int gpio, bool enabled, bool active_high = false)
    {
        BandGPIOConfig config;
        config.gpio = gpio;
        config.enabled = enabled;
        config.active_high = active_high;
        return config;
    }

    void set_default_band_gpio_config(std::array<BandGPIOConfig, HAM_BAND_COUNT> &band_gpio)
    {
        for (BandGPIOConfig &band_config : band_gpio)
        {
            band_config = make_band_gpio_config(-1, false, false);
        }
    }

    std::string band_gpio_active_high_key(const std::string &band_name)
    {
        return band_name + " Active High";
    }

    bool parse_ini_bool_strict(const std::string &raw_value, const std::string &context)
    {
        const std::string trimmed = trim_copy(raw_value);
        std::string lowered = trimmed;

        std::transform(
            lowered.begin(),
            lowered.end(),
            lowered.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (lowered == "true" || lowered == "t" || lowered == "1" ||
            lowered == "yes" || lowered == "y" || lowered == "on")
        {
            return true;
        }

        if (lowered == "false" || lowered == "f" || lowered == "0" ||
            lowered == "no" || lowered == "n" || lowered == "off")
        {
            return false;
        }

        throw std::runtime_error(
            "Invalid " + context + " value '" + trimmed +
            "'. Expected true or false.");
    }

    int parse_band_gpio_ini_value(const std::string &raw_value, const std::string &band_name)
    {
        const std::string trimmed = trim_copy(raw_value);
        if (trimmed.empty())
        {
            return -1;
        }

        char *end = nullptr;
        long value = std::strtol(trimmed.c_str(), &end, 10);
        if (*end != '\0')
        {
            throw std::runtime_error(
                "Invalid [Band GPIO] value for '" + band_name +
                "': '" + trimmed + "'. Expected an integer GPIO or empty.");
        }

        if (value < -1)
        {
            throw std::runtime_error(
                "Invalid [Band GPIO] value for '" + band_name +
                "': GPIO must be -1, empty, or a non-negative integer.");
        }

        return static_cast<int>(value);
    }

    void patch_band_gpio_from_ini(
        const std::unordered_map<std::string, std::string> &ini_section,
        nlohmann::json &patch)
    {
        const auto retired_22m = ini_section.find("22m");
        const auto retired_22m_active_high = ini_section.find("22m Active High");
        const bool retired_22m_is_disabled =
            retired_22m != ini_section.end() && trim_copy(retired_22m->second).empty() &&
            (retired_22m_active_high == ini_section.end() ||
             !parse_ini_bool_strict(retired_22m_active_high->second,
                 "[Band GPIO] 22m Active High"));
        for (const auto &[key, value] : ini_section)
        {
            if (retired_22m_is_disabled &&
                (key == "22m" || key == "22m Active High"))
            {
                continue;
            }
            bool known_key = false;

            for (const auto &[band, band_name] : kHamBandJsonKeys)
            {
                (void)band;

                if (key == band_name || key == band_gpio_active_high_key(band_name))
                {
                    known_key = true;
                    break;
                }
            }

            if (!known_key)
            {
                throw std::runtime_error(
                    "Unknown key in [Band GPIO]: '" + key + "'.");
            }
        }

        for (const auto &[band, band_name] : kHamBandJsonKeys)
        {
            const auto gpio_it = ini_section.find(band_name);
            const auto active_high_it = ini_section.find(band_gpio_active_high_key(band_name));

            if (gpio_it == ini_section.end() && active_high_it == ini_section.end())
            {
                continue;
            }

            if (gpio_it == ini_section.end())
            {
                throw std::runtime_error(
                    "Missing [Band GPIO] value for '" + std::string(band_name) +
                    "' while '" + band_gpio_active_high_key(band_name) +
                    "' is present.");
            }

            const int gpio = parse_band_gpio_ini_value(gpio_it->second, band_name);
            bool active_high = false;

            if (active_high_it != ini_section.end())
            {
                active_high = parse_ini_bool_strict(
                    active_high_it->second,
                    "[Band GPIO] " + band_gpio_active_high_key(band_name));
            }

            patch["Band GPIO"][band_name] = {
                {"GPIO", gpio},
                {"Enabled", gpio >= 0},
                {"Active High", active_high}};
        }
    }

} // namespace

namespace config_handler_serialization
{
const char *config_serialization_mode_name(ModeType mode) noexcept
{
    return mode_type_to_string(mode);
}

std::string config_serialization_trim(const std::string &value)
{
    return trim_copy(value);
}

int config_serialization_integer(
    const nlohmann::json &source,
    const std::string &context,
    int base)
{
    return parse_integer_config_value(source, context, base);
}

std::string config_serialization_si5351_i2c_address(int address)
{
    return format_si5351_i2c_address(address);
}

int config_serialization_gpio_transmit_pin(int gpio) noexcept
{
    return normalize_gpio_transmit_pin(gpio);
}

const BandJsonKeys &band_json_keys() noexcept
{
    return kHamBandJsonKeys;
}
} // namespace config_handler_serialization

namespace config_handler_deserialization
{
void default_band_gpio_config(
    std::array<BandGPIOConfig, HAM_BAND_COUNT> &band_gpio)
{
    set_default_band_gpio_config(band_gpio);
}
} // namespace config_handler_deserialization

void init_default_config()
{
    // Runtime
    config.transmit = false;
    config.transmit_backend = TransmitBackendKind::GPIO;
    config.wtp = WtpSettings{};
    config.enable_on_boot = EnableOnBootBehavior::Never;

    // WSPR
    config.callsign = "NXXX";
    config.grid_square = "ZZ99";
    config.power_dbm = 20;
    config.frequencies = "20m";
    config.wspr_audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;

    // Runtime
    config.ppm = 0.0;
    config.use_offset = true;
    config.use_led = false;
    config.led_pin = 18;
    config.use_amp = false;
    config.amp_pin = -1;
    config.amp_pin_active_high = false;
    config.gpio_tx_pin = kDefaultTransmitGpio;
    config.gpio_power_level = 7;
    config.rp1_gpio_drive_ma = kDefaultRp1GpioDriveMa;
    config.gpio_use_system_clock_frequency_estimate = true;
    config.gpio_frequency_residual_ppm = 0.0;
    config.gpio_manual_ppm = 0.0;
    config.si5351_ppm = 0.0;
    config.si5351_i2c_bus = kDefaultSi5351I2cBus;
    config.si5351_i2c_address = kDefaultSi5351I2cAddress;
    config.si5351_reference_hz = kDefaultSi5351ReferenceHz;
    config.si5351_reference_source = kDefaultSi5351ReferenceSource;
    config.si5351_crystal_load_capacitance_pf = kDefaultSi5351CrystalLoadCapacitancePf;
    config.si5351_tx_output = kDefaultSi5351TxOutput;
    config.si5351_power_level = 1;
    resolve_backend_specific_config(config);

    config.modulation_dot_seconds = 3.0;
    config.modulation_fsk_offset_hz = 5.0;
    config.cw_intra_element_gap = 1.0;
    config.cw_inter_character_gap = 3.0;
    config.cw_inter_word_gap = 7.0;
    config.dfcw_intra_element_gap = kDefaultDfcwIntraElementGap;
    config.dfcw_inter_character_gap = kDefaultDfcwInterCharacterGap;
    config.dfcw_inter_word_gap = kDefaultDfcwInterWordGap;
    config.cw_fade_shape = "none";
    config.cw_fade_in_ms = 0;
    config.cw_fade_out_ms = 0;
    config.cw_fade_slice_ms = 5;
    config.schedule_start_minute = 0;
    config.schedule_start_second = 5;
    config.schedule_repeat_minutes = 10;

    // Runtime
    config.enable_web = true;
    config.web_port = 31415;
    config.socket_port = 31416;
    config.socket_loopback_only = false;
    config.socket_loopback_family = WebSocketLoopbackFamily::Auto;
    config.use_shutdown = false;
    config.shutdown_pin = 19;

    // Meta
    config.use_ini = true;

    config.wspr.callsign = config.callsign;
    config.wspr.grid_square = config.grid_square;
    config.wspr.power_dbm = config.power_dbm;
    config.wspr.frequencies = config.frequencies;
    config.wspr.frequency_profile = "existing_common";
    config.wspr.audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
    config.wspr.planner_preference = config.wspr_planner_preference;
    config.qrss = QrssModeConfig{};
    config.fskcw = FskcwModeConfig{};
    config.dfcw = DfcwModeConfig{};

    set_default_band_gpio_config(config.band_gpio);
    publish_test_tone_planning_config(config);
}

double current_wspr_audio_offset_hz() noexcept
{
    return g_published_wspr_audio_offset_hz.load(std::memory_order_acquire);
}

TestTonePlanningConfigSnapshot current_test_tone_planning_config_snapshot()
{
    std::shared_lock<std::shared_mutex> lock(g_test_tone_planning_snapshot_mutex);
    return g_test_tone_planning_snapshot;
}

void resolve_backend_specific_config(ArgParserConfig &config) noexcept
{
    if (config.transmit_backend == TransmitBackendKind::WTP) {
        config.tx_pin = 0;
        config.power_level = 0;
        config.ppm = 0;
        config.use_system_clock_frequency_estimate = false;
        return;
    }
    config.tx_pin = config.gpio_tx_pin;
    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        config.power_level = config.si5351_power_level;
        config.use_system_clock_frequency_estimate = false;
        config.ppm = config.si5351_ppm;
        return;
    }

    config.power_level = config.transmit_backend == TransmitBackendKind::RP1_GPCLK
        ? config.rp1_gpio_drive_ma
        : config.gpio_power_level;
    config.use_system_clock_frequency_estimate = config.gpio_use_system_clock_frequency_estimate;
    config.ppm = config.gpio_manual_ppm;
}

bool Si5351AddressInventory::contains(int address) const noexcept
{
    return error.empty() &&
        std::find(addresses.begin(), addresses.end(), address) != addresses.end();
}

bool si5351_device_detected(
    int i2c_bus,
    int i2c_address,
    int reference_hz,
    std::string *error_message)
{
    if (g_si5351_detection_override.has_value())
    {
        if (!*g_si5351_detection_override && error_message != nullptr)
        {
            *error_message = si5351_detection_unavailable_message();
        }
        return *g_si5351_detection_override;
    }

#if WSPRRYPI_BACKEND_SI5351
    Si5351Device::Config device_config;
    device_config.i2c_bus = i2c_bus;
    device_config.i2c_address = static_cast<std::uint8_t>(i2c_address);
    device_config.reference_hz = static_cast<std::uint32_t>(reference_hz);

    Si5351Device device(device_config);
    if (!device.open())
    {
        if (error_message != nullptr)
        {
            *error_message =
                si5351_detection_unavailable_message(device.getLastError());
        }
        return false;
    }

    const bool detected = device.probe();
    const std::string detail = device.getLastError();
    device.close();

    if (!detected && error_message != nullptr)
    {
        *error_message = si5351_detection_unavailable_message(detail);
    }

    return detected;
#else
    (void)i2c_bus;
    (void)i2c_address;
    (void)reference_hz;
    if (error_message != nullptr)
    {
        *error_message =
            "Si5351 detection is unavailable because the Si5351 backend was not compiled.";
    }
    return false;
#endif
}

Si5351AddressInventory discover_si5351_addresses(
    int i2c_bus,
    int reference_hz)
{
    if (g_si5351_address_inventory_override.has_value())
    {
        Si5351AddressInventory result = *g_si5351_address_inventory_override;
        if (result.i2c_bus != i2c_bus)
        {
            result.addresses.clear();
            result.i2c_bus = i2c_bus;
            result.error = "No test Si5351 address inventory is available for I2C bus " +
                std::to_string(i2c_bus) + ".";
        }
        return result;
    }
    if (g_si5351_detection_override.has_value())
    {
        Si5351AddressInventory result;
        result.i2c_bus = i2c_bus;
        if (*g_si5351_detection_override)
        {
            for (int address = 0x60; address <= 0x6F; ++address)
                result.addresses.push_back(address);
        }
        else
        {
            result.error = si5351_detection_unavailable_message();
        }
        return result;
    }

    Si5351AddressInventory result;
    result.i2c_bus = i2c_bus;
    const std::string bus_error = i2c_bus_inventory::selection_error(
        i2c_bus_inventory::discover(), i2c_bus);
    if (!bus_error.empty())
    {
        result.error = bus_error;
        return result;
    }

#if WSPRRYPI_BACKEND_SI5351
    for (int address = 0x60; address <= 0x6F; ++address)
    {
        Si5351Device::Config device_config;
        device_config.i2c_bus = i2c_bus;
        device_config.i2c_address = static_cast<std::uint8_t>(address);
        device_config.reference_hz = static_cast<std::uint32_t>(reference_hz);

        Si5351Device device(device_config);
        if (!device.open())
        {
            result.addresses.clear();
            result.error =
                "Unable to inspect Si5351 addresses on I2C bus " +
                std::to_string(i2c_bus) + ": " + device.getLastError();
            return result;
        }
        if (device.probe()) result.addresses.push_back(address);
        device.close();
    }
#else
    (void)reference_hz;
    result.error =
        "Si5351 address discovery is unavailable because the Si5351 backend was not compiled.";
#endif

    return result;
}

void set_si5351_detection_override_for_test(bool detected) noexcept
{
    g_si5351_detection_override = detected;
}

void clear_si5351_detection_override_for_test() noexcept
{
    g_si5351_detection_override.reset();
}

void set_si5351_address_inventory_override_for_test(
    int i2c_bus,
    const std::vector<int> &addresses,
    const std::string &error)
{
    Si5351AddressInventory inventory;
    inventory.i2c_bus = i2c_bus;
    inventory.error = error;
    for (const int address : addresses)
    {
        if (address >= 0x60 && address <= 0x6F &&
            std::find(inventory.addresses.begin(), inventory.addresses.end(), address) ==
                inventory.addresses.end())
        {
            inventory.addresses.push_back(address);
        }
    }
    std::sort(inventory.addresses.begin(), inventory.addresses.end());
    g_si5351_address_inventory_override = std::move(inventory);
}

void clear_si5351_address_inventory_override_for_test() noexcept
{
    g_si5351_address_inventory_override.reset();
}

namespace
{
    nlohmann::json parse_ini_value(const std::string &raw_value)
    {
        const std::string trimmed = trim_copy(raw_value);
        std::string lowered = trimmed;

        std::transform(
            lowered.begin(),
            lowered.end(),
            lowered.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (lowered == "true" || lowered == "false")
        {
            return lowered == "true";
        }

        char *end = nullptr;
        long lval = std::strtol(trimmed.c_str(), &end, 10);
        if (*end == '\0')
        {
            return lval;
        }

        if (lowered.size() > 2 &&
            lowered[0] == '0' &&
            lowered[1] == 'x')
        {
            end = nullptr;
            long hex_lval = std::strtol(trimmed.c_str(), &end, 0);
            if (*end == '\0')
            {
                return hex_lval;
            }
        }

        end = nullptr;
        double dval = std::strtod(trimmed.c_str(), &end);
        if (*end == '\0')
        {
            return dval;
        }

        const bool looks_like_json =
            !trimmed.empty() &&
            ((trimmed.front() == '[' && trimmed.back() == ']') ||
             (trimmed.front() == '{' && trimmed.back() == '}'));

        if (looks_like_json)
        {
            try
            {
                return nlohmann::json::parse(trimmed);
            }
            catch (const std::exception &)
            {
            }
        }

        return trimmed;
    }


    std::string default_json_value_to_string(const nlohmann::json &value)
    {
        if (value.is_string())
        {
            return value.get<std::string>();
        }

        return value.dump();
    }

    bool is_required_tx_key(const std::string &section, const std::string &key)
    {
        return section == "WSPR" &&
               (key == "Call Sign" ||
                key == "Grid Square" ||
                key == "TX Power" ||
                key == "Frequency");
    }

    bool should_warn_if_missing(const std::string &section, const std::string &key)
    {
        return (section == "Operation" &&
                (key == "Mode" ||
                 key == "Transmit" ||
                 key == "Transmit Backend" ||
                 key == "Enable on Boot" ||
                 key == "Use LED" ||
                 key == "LED Pin" ||
                 key == "Use Amp" ||
                 key == "Amp Pin" ||
                 key == "Amp Pin Active High" ||
                 key == "Web Port" ||
                 key == "Socket Port" ||
                 key == "Use Shutdown" ||
                 key == "Shutdown Button")) ||
               (section == "GPIO" &&
                (key == "Transmit Pin" ||
                 key == "Power Level" ||
                 key == "RP1 Drive mA" ||
                 key == "Use System Clock Frequency Estimate" ||
                 key == "Frequency Residual PPM" ||
                 key == "Manual PPM")) ||
               (section == "WSPR" &&
                (key == "Call Sign" ||
                 key == "Grid Square" ||
                 key == "TX Power" ||
                 key == "Frequency" ||
                 key == "Planner Preference" ||
                 key == "Use Random Offset")) ||
               (section == "Calibration" &&
                key == "PPM") ||
               (section == "Si5351" &&
                (key == "I2C Bus" ||
                 key == "I2C Address" ||
                 key == "Reference Frequency" ||
                 key == "Reference Source" ||
                 key == "Crystal Load Capacitance" ||
                 key == "TX Output" ||
                 key == "Power Level")) ||
               (section == "CW" &&
                (key == "Base Frequency" ||
                 key == "Shift Hz" ||
                 key == "Dot Seconds" ||
                 key == "Intra Element Gap" ||
                 key == "Inter Character Gap" ||
                 key == "Inter Word Gap" ||
                 key == "DFCW Intra Element Gap" ||
                 key == "DFCW Inter Character Gap" ||
                 key == "DFCW Inter Word Gap" ||
                 key == "Fade Shape" ||
                 key == "Fade In Ms" ||
                 key == "Fade Out Ms" ||
                 key == "Fade Slice Ms" ||
                 key == "Start Minute" ||
                 key == "Start Second" ||
                 key == "Repeat Minutes"));
    }

    bool ini_has_nonempty_value(
        const std::map<std::string, std::unordered_map<std::string, std::string>> &ini_data,
        const std::string &section,
        const std::string &key)
    {
        const auto section_it = ini_data.find(section);
        if (section_it == ini_data.end())
        {
            return false;
        }

        const auto key_it = section_it->second.find(key);
        return key_it != section_it->second.end() &&
               !trim_copy(key_it->second).empty();
    }

    bool ini_has_effective_value(
        const std::map<std::string, std::unordered_map<std::string, std::string>> &ini_data,
        const std::string &section,
        const std::string &key)
    {
        if (section == "Operation" && key == "Amp Pin")
        {
            const auto section_it = ini_data.find(section);
            return section_it != ini_data.end() &&
                   section_it->second.find(key) != section_it->second.end();
        }

        return ini_has_nonempty_value(ini_data, section, key);
    }

    void collect_ini_warnings(
        const nlohmann::json &defaults,
        const std::map<std::string, std::unordered_map<std::string, std::string>> &ini_data,
        std::vector<std::string> &warnings,
        bool &missing_required_tx_item)
    {
        for (const auto &section_item : defaults.items())
        {
            if (!section_item.value().is_object())
            {
                continue;
            }

            const std::string &section = section_item.key();

            for (const auto &key_item : section_item.value().items())
            {
                const std::string &key = key_item.key();

                if (!should_warn_if_missing(section, key))
                {
                    continue;
                }

                bool missing_or_empty = false;
                missing_or_empty = !ini_has_effective_value(ini_data, section, key);

                if (!missing_or_empty)
                {
                    continue;
                }

                warnings.push_back(
                    section + "." + key +
                    " missing or empty. Using default '" +
                    default_json_value_to_string(key_item.value()) + "'.");

                if (is_required_tx_key(section, key))
                {
                    missing_required_tx_item = true;
                }
            }
        }
    }

    void init_config_json_impl(nlohmann::json &target)
    {
        target["Meta"] = {
            {"Use INI", false},
            {"INI Filename", ""},
            {"Date Time Log", false},
            {"debug_logging", false},
            {"Loop TX", false},
            {"TX Iterations", 0}};
        target["Operation"] = {
            {"Mode", "WSPR"},
            {"Transmit", false},
            {"Transmit Backend", "gpio"},
            {"Enable on Boot", "Never"},
            {"LED Pin", 18},
            {"Use LED", false},
            {"Use Amp", false},
            {"Amp Pin", -1},
            {"Amp Pin Active High", false},
            {"Web Port", 31415},
            {"Socket Port", 31416},
            {"Use Shutdown", false},
            {"Shutdown Button", 19}};
        target["WTP"] = wtp_settings_json(WtpSettings{});
        target["Experimental"] = {
            {"Allow Unqualified Frequency", false},
            {"Allow Non-Amateur Frequency", false}};

        target["GPIO"] = {
            {"Transmit Pin", kDefaultTransmitGpio},
            {"Power Level", 7},
            {"RP1 Drive mA", kDefaultRp1GpioDriveMa},
            {"Use System Clock Frequency Estimate", true},
            {"Frequency Residual PPM", 0.0},
            {"Manual PPM", 0.0}};

        target["Calibration"] = {
            {"PPM", 0.0}};

        target["Si5351"] = {
            {"I2C Bus", kDefaultSi5351I2cBus},
            {"I2C Address", format_si5351_i2c_address(kDefaultSi5351I2cAddress)},
            {"Reference Frequency", kDefaultSi5351ReferenceHz},
            {"Reference Source", kDefaultSi5351ReferenceSource},
            {"Crystal Load Capacitance", kDefaultSi5351CrystalLoadCapacitancePf},
            {"TX Output", "CLK0"},
            {"Power Level", 1}};

        target["Band GPIO"] = nlohmann::json::object();
        target["WSPR"] = {
            {"Call Sign", "NXXX"},
            {"Grid Square", "ZZ99"},
            {"TX Power", 20},
            {"Frequency", "20m"},
            {"Frequency Profile", "existing_common"},
            {"Band Preferences", nlohmann::json::object()},
            {"Planner Preference", "auto"},
            {"Use Random Offset", true}};
        target["CW"] = {
            {"Message", ""},
            {"Base Frequency", 14096900.0},
            {"Shift Hz", 5.0},
            {"Dot Seconds", 3.0},
            {"Intra Element Gap", 1.0},
            {"Inter Character Gap", 3.0},
            {"Inter Word Gap", 7.0},
            {"DFCW Intra Element Gap", kDefaultDfcwIntraElementGap},
            {"DFCW Inter Character Gap", kDefaultDfcwInterCharacterGap},
            {"DFCW Inter Word Gap", kDefaultDfcwInterWordGap},
            {"Fade Shape", "none"},
            {"Fade In Ms", 0},
            {"Fade Out Ms", 0},
            {"Fade Slice Ms", 5},
            {"Start Minute", 0},
            {"Start Second", 5},
            {"Repeat Minutes", 10}};
        std::array<BandGPIOConfig, HAM_BAND_COUNT> default_band_gpio{};
        set_default_band_gpio_config(default_band_gpio);
        for (const auto &[band, band_name] : kHamBandJsonKeys)
        {
            const BandGPIOConfig &band_config = default_band_gpio[ham_band_index(band)];
            target["Band GPIO"][band_name] = {
                {"GPIO", band_config.gpio},
                {"Enabled", band_config.enabled},
                {"Active High", band_config.active_high}};
        }
    }


    void copy_config(const ArgParserConfig &source, ArgParserConfig &target)
    {
        target.transmit = source.transmit;
        target.enable_on_boot = source.enable_on_boot;
        target.callsign = source.callsign;
        target.grid_square = source.grid_square;
        target.power_dbm = source.power_dbm;
        target.frequencies = source.frequencies;
        target.tx_pin = source.tx_pin;
        target.ppm = source.ppm;
        target.use_system_clock_frequency_estimate = source.use_system_clock_frequency_estimate;
        target.use_offset = source.use_offset;
        target.power_level = source.power_level;
        target.transmit_backend = source.transmit_backend;
        target.wtp = source.wtp;
        target.gpio_tx_pin = source.gpio_tx_pin;
        target.gpio_power_level = source.gpio_power_level;
        target.rp1_gpio_drive_ma = source.rp1_gpio_drive_ma;
        target.gpio_use_system_clock_frequency_estimate = source.gpio_use_system_clock_frequency_estimate;
        target.gpio_frequency_residual_ppm = source.gpio_frequency_residual_ppm;
        target.gpio_manual_ppm = source.gpio_manual_ppm;
        target.si5351_ppm = source.si5351_ppm;
        target.si5351_i2c_bus = source.si5351_i2c_bus;
        target.si5351_i2c_address = source.si5351_i2c_address;
        target.si5351_reference_hz = source.si5351_reference_hz;
        target.si5351_reference_source = source.si5351_reference_source;
        target.si5351_crystal_load_capacitance_pf = source.si5351_crystal_load_capacitance_pf;
        target.si5351_tx_output = source.si5351_tx_output;
        target.si5351_power_level = source.si5351_power_level;
        target.use_led = source.use_led;
        target.led_pin = source.led_pin;
        target.use_amp = source.use_amp;
        target.amp_pin = source.amp_pin;
        target.amp_pin_active_high = source.amp_pin_active_high;
        target.enable_web = source.enable_web;
        target.web_port = source.web_port;
        target.socket_port = source.socket_port;
        target.socket_loopback_only = source.socket_loopback_only;
        target.socket_loopback_family = source.socket_loopback_family;
        target.use_shutdown = source.use_shutdown;
        target.shutdown_pin = source.shutdown_pin;
        target.use_journald = source.use_journald;
        target.date_time_log = source.date_time_log;
        target.debug_logging = source.debug_logging;
        target.allow_unqualified_frequency = source.allow_unqualified_frequency;
        target.allow_non_amateur_frequency = source.allow_non_amateur_frequency;
        target.rp1_development_confirmation_json =
            source.rp1_development_confirmation_json;
        target.wspr_planner_preference = source.wspr_planner_preference;
        target.loop_tx = source.loop_tx;
        target.tx_iterations.store(source.tx_iterations.load());
        target.wspr_audio_offset_hz = source.wspr_audio_offset_hz;
        target.modulation_dot_seconds = source.modulation_dot_seconds;
        target.modulation_fsk_offset_hz = source.modulation_fsk_offset_hz;
        target.cw_intra_element_gap = source.cw_intra_element_gap;
        target.cw_inter_character_gap = source.cw_inter_character_gap;
        target.cw_inter_word_gap = source.cw_inter_word_gap;
        target.dfcw_intra_element_gap = source.dfcw_intra_element_gap;
        target.dfcw_inter_character_gap = source.dfcw_inter_character_gap;
        target.dfcw_inter_word_gap = source.dfcw_inter_word_gap;
        target.cw_fade_shape = source.cw_fade_shape;
        target.cw_fade_in_ms = source.cw_fade_in_ms;
        target.cw_fade_out_ms = source.cw_fade_out_ms;
        target.cw_fade_slice_ms = source.cw_fade_slice_ms;
        target.schedule_start_minute = source.schedule_start_minute;
        target.schedule_start_second = source.schedule_start_second;
        target.schedule_repeat_minutes = source.schedule_repeat_minutes;
        target.mode = source.mode;
        target.wspr = source.wspr;
        target.qrss = source.qrss;
        target.fskcw = source.fskcw;
        target.dfcw = source.dfcw;
        target.use_ini = source.use_ini;
        target.ini_filename = source.ini_filename;
        target.wspr_dial_freq_set = source.wspr_dial_freq_set;
        target.wspr_frequency_entries = source.wspr_frequency_entries;
        target.frequency_estimate_good = source.frequency_estimate_good;
        target.band_gpio = source.band_gpio;
    }

    bool migrate_legacy_gpio_keys(
        std::map<std::string, std::unordered_map<std::string, std::string>> &ini_data,
        std::vector<std::string> &warnings)
    {
        auto gpio_it = ini_data.find("GPIO");
        if (gpio_it == ini_data.end())
        {
            return false;
        }

        auto &gpio = gpio_it->second;
        const auto legacy_it = gpio.find("Use NTP");
        if (legacy_it == gpio.end())
        {
            return false;
        }

        const bool legacy_enabled = parse_ini_bool_strict(
            legacy_it->second,
            "GPIO.Use NTP");
        const auto canonical_it =
            gpio.find("Use System Clock Frequency Estimate");

        bool canonical_enabled = legacy_enabled;
        if (canonical_it != gpio.end())
        {
            canonical_enabled = parse_ini_bool_strict(
                canonical_it->second,
                "GPIO.Use System Clock Frequency Estimate");
            if (canonical_enabled != legacy_enabled)
            {
                warnings.push_back(
                    "GPIO.Use NTP conflicts with GPIO.Use System Clock Frequency Estimate; "
                    "the canonical value was retained and the retired key was removed.");
            }
        }
        else
        {
            gpio["Use System Clock Frequency Estimate"] =
                legacy_enabled ? "true" : "false";
        }

        if (gpio.find("Frequency Residual PPM") == gpio.end())
        {
            gpio["Frequency Residual PPM"] = "0.0";
        }

        if (gpio.find("Manual PPM") == gpio.end())
        {
            std::string manual_ppm = "0.0";
            if (!legacy_enabled)
            {
                const auto calibration_it = ini_data.find("Calibration");
                if (calibration_it != ini_data.end())
                {
                    const auto ppm_it = calibration_it->second.find("PPM");
                    if (ppm_it != calibration_it->second.end() &&
                        !trim_copy(ppm_it->second).empty())
                    {
                        manual_ppm = ppm_it->second;
                    }
                }
            }
            gpio["Manual PPM"] = manual_ppm;
        }

        gpio.erase(legacy_it);
        warnings.push_back(
            std::string("Migrated retired GPIO.Use NTP=") +
            (legacy_enabled ? "true" : "false") +
            " to GPIO.Use System Clock Frequency Estimate=" +
            (canonical_enabled ? "true" : "false") +
            "; the retired key will be removed from the persisted configuration.");
        return true;
    }

    void ini_to_json_impl(
        const std::string &filename,
        const std::map<std::string, std::unordered_map<std::string, std::string>> &ini_data,
        nlohmann::json &target)
    {
        nlohmann::json patch;

        if (ini_data.find("Operation") == ini_data.end())
        {
            throw std::runtime_error("Missing [Operation] section.");
        }

        if (!ini_has_nonempty_value(ini_data, "Operation", "Mode"))
        {
            throw std::runtime_error("Missing [Operation] Mode.");
        }

        for (const auto &section_pair : ini_data)
        {
            const std::string &section = section_pair.first;
            const auto &key_values = section_pair.second;

            if (section == "Band GPIO")
            {
                patch_band_gpio_from_ini(key_values, patch);
                continue;
            }

            // Canonical persistent sections only. Unknown sections, including
            // pre-2.x legacy sections, are not imported or treated as fallbacks.
            if (section != "Meta" &&
                section != "Operation" &&
                section != "GPIO" &&
                section != "Calibration" &&
                section != "Si5351" &&
                section != "WSPR" &&
                section != "CW" &&
                section != "WTP" &&
                section != "Experimental")
            {
                continue;
            }

            for (const auto &kv : key_values)
            {
                const std::string &key = kv.first;
                const std::string trimmed = trim_copy(kv.second);

                if (section == "Operation" && key == "Amp Pin" && trimmed.empty())
                {
                    patch["Operation"]["Amp Pin"] = -1;
                    continue;
                }

                if (trimmed.empty())
                {
                    continue;
                }

                if (section == "Meta")
                {
                    if (key == "debug_logging" || key == "Debug Logging")
                    {
                        patch["Meta"]["debug_logging"] = parse_ini_value(trimmed);
                    }
                    else if (key == "Loop TX" || key == "TX Iterations")
                    {
                        patch["Meta"][key] = parse_ini_value(trimmed);
                    }
                    continue;
                }

                if (section == "WTP" && wtp_settings_json(WtpSettings{}).value(key, nlohmann::json()).is_string())
                    patch[section][key] = trimmed;
                else
                    patch[section][key] = parse_ini_value(trimmed);
            }
        }

        patch["Meta"]["INI Filename"] = filename;
        patch["Meta"]["Use INI"] = true;
        if (patch.contains("Operation") &&
            patch.at("Operation").is_object() &&
            patch.at("Operation").contains("Amp Pin") &&
            !patch.at("Operation").contains("Use Amp"))
        {
            patch["Operation"]["Use Amp"] =
                parse_integer_config_value(
                    patch.at("Operation").at("Amp Pin"),
                    "Operation.Amp Pin") >= 0;
        }
        target.merge_patch(patch);
    }

    bool build_candidate_from_ini(
        const std::string &filename,
        nlohmann::json &candidate_json,
        ArgParserConfig &candidate_config,
        std::string *error_message,
        nlohmann::json *error_details,
        std::vector<std::string> *warning_messages)
    {
        try
        {
            init_config_json_impl(candidate_json);

            // External INI edits are observed by the file monitor before this
            // candidate build runs. Refresh the singleton from disk unless the
            // caller intentionally staged in-memory edits that have not yet
            // been persisted.
            if (!iniFile.hasPendingChanges())
            {
                iniFile.load();
            }

            std::vector<std::string> local_warnings;
            bool missing_required_tx_item = false;
            auto ini_data = iniFile.getData();
            const bool migration_required =
                migrate_legacy_gpio_keys(ini_data, local_warnings);

            collect_ini_warnings(
                candidate_json,
                ini_data,
                local_warnings,
                missing_required_tx_item);

            ini_to_json_impl(filename, ini_data, candidate_json);
            config_handler_deserialization::deserialize_json_to_runtime_config(
                candidate_json, candidate_config);
            candidate_config.enable_web = config.enable_web;

            if (missing_required_tx_item)
            {
                if (warning_messages != nullptr)
                {
                    *warning_messages = local_warnings;
                    warning_messages->push_back(
                        "Transmission disabled until configuration is repaired.");
                }

                if (error_message != nullptr)
                {
                    *error_message = "Missing or empty required configuration items.";
                }

                return false;
            }

            std::string validation_error;
            if (!validate_config_candidate(candidate_config, &validation_error))
            {
                if (error_message != nullptr)
                {
                    *error_message = validation_error;
                }
                return false;
            }

            nlohmann::json semantic_error_details;
            if (candidate_config.transmit &&
                !validate_wspr_semantics(
                    candidate_config,
                    &validation_error,
                    &semantic_error_details))
            {
                if (error_message != nullptr)
                {
                    *error_message = validation_error;
                }
                if (error_details != nullptr)
                {
                    *error_details = semantic_error_details;
                }
                return false;
            }

            if (warning_messages != nullptr)
            {
                *warning_messages = local_warnings;
            }

            if (migration_required)
            {
                candidate_json["Meta"]["Legacy GPIO Migration Required"] = true;
            }

            config_handler_serialization::serialize_runtime_config_to_json(
                candidate_config, candidate_json);
            return true;
        }
        catch (const std::exception &e)
        {
            if (error_message != nullptr)
            {
                *error_message = e.what();
            }
            return false;
        }
    }
} // namespace

void init_config_json()
{
    std::lock_guard update_lock(config_update_mutex());
    init_config_json_impl(jConfig);
}

void ini_to_json(std::string filename)
{
    std::lock_guard update_lock(config_update_mutex());
    ini_to_json_impl(filename, iniFile.getData(), jConfig);
}

void json_to_config()
{
    std::lock_guard update_lock(config_update_mutex());
    config_handler_deserialization::deserialize_json_to_runtime_config(
        jConfig, config);
    publish_test_tone_planning_config(config);
}

namespace {
std::string config_revision_locked() {
    const auto bytes = jConfig.dump();
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
    std::string result = "\"";
    for (unsigned char c : digest) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; }
    return result + "\"";
}
}
std::pair<nlohmann::json, std::string> get_public_config_snapshot() {
    std::lock_guard lock(config_update_mutex());
    return {config_handler_serialization::public_config_from_internal_json(jConfig), config_revision_locked()};
}
nlohmann::json get_public_config_json()
{
    std::lock_guard update_lock(config_update_mutex());
    return config_handler_serialization::public_config_from_internal_json(
        jConfig);
}

namespace
{
std::map<std::string, std::unordered_map<std::string, std::string>>
build_persistent_ini_data(const nlohmann::json &source)
{
    std::map<std::string, std::unordered_map<std::string, std::string>> new_data;

    for (const auto &section : source.items())
    {
        const std::string section_name = section.key();

        if (!section.value().is_object())
        {
            continue;
        }

        if (section_name != "Operation" &&
            section_name != "Meta" &&
            section_name != "GPIO" &&
            section_name != "Calibration" &&
            section_name != "Si5351" &&
            section_name != "WSPR" &&
            section_name != "CW" &&
            section_name != "Experimental" &&
            section_name != "WTP" &&
            section_name != "Band GPIO")
        {
            continue;
        }

        if (section_name == "Band GPIO")
        {
            for (const auto &[band, band_name] : kHamBandJsonKeys)
            {
                (void)band;

                if (!section.value().contains(band_name) ||
                    !section.value().at(band_name).is_object())
                {
                    continue;
                }

                const nlohmann::json &band_config = section.value().at(band_name);
                const int gpio = band_config.value("GPIO", -1);
                const bool enabled = band_config.value("Enabled", false);
                const bool active_high = band_config.value("Active High", false);

                new_data[section_name][band_name] =
                    (enabled && gpio >= 0) ? std::to_string(gpio) : "";
                new_data[section_name][band_gpio_active_high_key(band_name)] =
                    active_high ? "true" : "false";
            }

            continue;
        }

        for (const auto &kv : section.value().items())
        {
            const std::string &key = kv.key();
            const bool persist_key =
                (section_name == "WTP" && wtp_settings_json(WtpSettings{}).contains(key)) ||
                (section_name == "Meta" &&
                 key == "debug_logging") ||
                (section_name == "Operation" &&
                 (key == "Mode" ||
                  key == "Transmit" ||
                  key == "Transmit Backend" ||
                  key == "Enable on Boot" ||
                  key == "Use LED" ||
                  key == "LED Pin" ||
                  key == "Use Amp" ||
                  key == "Amp Pin" ||
                  key == "Amp Pin Active High" ||
                  key == "Web Port" ||
                  key == "Socket Port" ||
                  key == "Use Shutdown" ||
                  key == "Shutdown Button")) ||
                (section_name == "GPIO" &&
                 (key == "Transmit Pin" ||
                  key == "Power Level" ||
                  key == "RP1 Drive mA" ||
                  key == "Use System Clock Frequency Estimate" ||
                  key == "Frequency Residual PPM" ||
                  key == "Manual PPM")) ||
                (section_name == "Calibration" &&
                 key == "PPM") ||
                (section_name == "Si5351" &&
                 (key == "I2C Bus" ||
                  key == "I2C Address" ||
                  key == "Reference Frequency" ||
                  key == "Reference Source" ||
                  key == "Crystal Load Capacitance" ||
                  key == "TX Output" ||
                  key == "Power Level")) ||
                (section_name == "WSPR" &&
                 (key == "Call Sign" ||
                  key == "Grid Square" ||
                  key == "TX Power" ||
                  key == "Frequency" ||
                  key == "Frequency Profile" ||
                  key == "Band Preferences" ||
                  key == "Planner Preference" ||
                  key == "Use Random Offset")) ||
                (section_name == "CW" &&
                 (key == "Message" ||
                  key == "Base Frequency" ||
                  key == "Shift Hz" ||
                  key == "Dot Seconds" ||
                  key == "Intra Element Gap" ||
                  key == "Inter Character Gap" ||
                  key == "Inter Word Gap" ||
                  key == "DFCW Intra Element Gap" ||
                  key == "DFCW Inter Character Gap" ||
                  key == "DFCW Inter Word Gap" ||
                  key == "Fade Shape" ||
                  key == "Fade In Ms" ||
                  key == "Fade Out Ms" ||
                  key == "Fade Slice Ms" ||
                  key == "Start Minute" ||
                  key == "Start Second" ||
                  key == "Repeat Minutes")) ||
                (section_name == "Experimental" &&
                 (key == "Allow Unqualified Frequency" ||
                  key == "Allow Non-Amateur Frequency"));

            if (!persist_key)
            {
                continue;
            }

            std::string out_val;

            if (kv.value().is_array() || kv.value().is_object())
            {
                out_val = kv.value().dump();
            }
            else if (kv.value().is_string())
            {
                out_val = kv.value().get<std::string>();
            }
            else if (section_name == "Operation" && key == "Amp Pin")
            {
                const int amp_pin =
                    parse_integer_config_value(kv.value(), "Operation.Amp Pin");
                out_val = amp_pin >= 0 ? std::to_string(amp_pin) : "";
            }
            else
            {
                out_val = kv.value().dump();
            }

            new_data[section_name][key] = out_val;
        }
    }

    return new_data;
}

void persist_config_json(const nlohmann::json &source)
{
    const auto previous_data = iniFile.getData();
    try
    {
        iniFile.setData(build_persistent_ini_data(source));
        iniFile.save();
    }
    catch (...)
    {
        iniFile.setData(previous_data);
        try
        {
            iniFile.load();
        }
        catch (...)
        {
        }
        throw;
    }
}
} // namespace

void json_to_ini()
{
    std::lock_guard update_lock(config_update_mutex());
    if (!config.use_ini)
    {
        return;
    }

    persist_config_json(jConfig);
}

bool apply_enable_on_boot_startup_policy()
{
    // A route restoration restarts the application, never its transmission.
    // This startup-only override leaves the saved boot preference unchanged.
    if (std::getenv("WSPRRYPI_ROUTE_RESTORE_IDLE") != nullptr ||
        std::getenv("WSPRRYPI_RP1_REBOOT_IDLE") != nullptr)
    {
        config.transmit = false;
        config_to_json();
        json_to_ini();
        return true;
    }

    switch (config.enable_on_boot)
    {
    case EnableOnBootBehavior::Never:
        llog.logS(
            INFO,
            "Operation.Enable on Boot policy Never: setting Operation.Transmit=false for startup.");
        config.transmit = false;
        config_to_json();
        json_to_ini();
        return true;
    case EnableOnBootBehavior::Follow:
        llog.logS(
            INFO,
            "Operation.Enable on Boot policy Follow: leaving Operation.Transmit=",
            config.transmit ? "true" : "false",
            ".");
        return false;
    case EnableOnBootBehavior::Always:
        llog.logS(
            INFO,
            "Operation.Enable on Boot policy Always: setting Operation.Transmit=true for startup.");
        config.transmit = true;
        config_to_json();
        json_to_ini();
        return true;
    }

    return false;
}

bool load_json(
    std::string filename,
    std::string *error_message,
    std::vector<std::string> *warning_messages)
{
    PreparedConfigCandidate candidate;
    prepare_ini_config_candidate(filename, candidate);

    if (!candidate.valid)
    {
        if (error_message != nullptr)
        {
            *error_message = candidate.error_reason;
        }

        if (warning_messages != nullptr)
        {
            *warning_messages = candidate.warnings;
        }

        return false;
    }

    if (warning_messages != nullptr)
    {
        *warning_messages = candidate.warnings;
    }

    commit_config_candidate(candidate);
    return true;
}

void prepare_ini_config_candidate(
    const std::string &filename,
    PreparedConfigCandidate &candidate_out)
{
    candidate_out = PreparedConfigCandidate{};

    if (!build_candidate_from_ini(
            filename,
            candidate_out.normalized_json,
            candidate_out.normalized_config,
            &candidate_out.error_reason,
            &candidate_out.error_details,
            &candidate_out.warnings))
    {
        candidate_out.valid = false;
        candidate_out.transmit_enabled = false;
        return;
    }

    candidate_out.migration_required =
        candidate_out.normalized_json.contains("Meta") &&
        candidate_out.normalized_json.at("Meta").is_object() &&
        candidate_out.normalized_json.at("Meta").value(
            "Legacy GPIO Migration Required",
            false);
    if (candidate_out.normalized_json.contains("Meta") &&
        candidate_out.normalized_json.at("Meta").is_object())
    {
        candidate_out.normalized_json["Meta"].erase(
            "Legacy GPIO Migration Required");
    }

    candidate_out.valid = true;
    candidate_out.transmit_enabled = candidate_out.normalized_config.transmit;
}

void commit_config_candidate(const PreparedConfigCandidate &candidate)
{
    std::lock_guard update_lock(config_update_mutex());
    if (!candidate.valid)
    {
        throw std::invalid_argument(
            "Cannot commit an invalid configuration candidate.");
    }

    if (candidate.migration_required)
    {
        iniFile.set_raw_passthrough_keys(
            {{"Security", "Privileged Network Safety"}});
        iniFile.set_filename(candidate.normalized_config.ini_filename);
        iniFile.erase_value("GPIO", "Use NTP");
        persist_config_json(candidate.normalized_json);
    }

    copy_config(candidate.normalized_config, config);
    publish_test_tone_planning_config(config);
    jConfig = candidate.normalized_json;
    refresh_logger_level_from_config();
}

void copy_runtime_config(const ArgParserConfig &source, ArgParserConfig &target)
{
    copy_config(source, target);
    if (&target == &config)
    {
        publish_test_tone_planning_config(config);
    }
}

void dump_json(const nlohmann::json &j, std::string tag)
{
    llog.logS(DEBUG, tag, "JSON Dump: ", j.dump());
}

void patch_all_from_web(const nlohmann::json &j) { (void)patch_all_from_web_revision(j, {}); }

std::string patch_all_from_web_revision(const nlohmann::json &j, const std::string &expected_revision)
{
    std::lock_guard update_lock(config_update_mutex());
    if (!expected_revision.empty() && expected_revision != config_revision_locked())
        throw std::runtime_error("revision_conflict");
    nlohmann::json candidate_public_json =
        config_handler_serialization::public_config_from_internal_json(jConfig);
    candidate_public_json.merge_patch(j);

    nlohmann::json candidate_json = jConfig;
    config_handler_serialization::apply_public_config_to_internal_json(
        candidate_public_json, candidate_json);

    ArgParserConfig candidate_config;
    std::string error_message;
    nlohmann::json error_details;

    try
    {
        config_handler_deserialization::deserialize_json_to_runtime_config(
            candidate_json, candidate_config);
        candidate_config.enable_web = config.enable_web;

        // Validate new selections against fresh host metadata, never client Platform data.
        // An unchanged unavailable bus must not prevent recovery to another backend.
        if (candidate_config.si5351_i2c_bus != config.si5351_i2c_bus ||
            (candidate_config.transmit_backend == TransmitBackendKind::SI5351 &&
             config.transmit_backend != TransmitBackendKind::SI5351))
        {
            error_message = i2c_bus_inventory::selection_error(
                i2c_bus_inventory::discover(), candidate_config.si5351_i2c_bus);
            if (!error_message.empty()) throw std::runtime_error(error_message);
        }

        const bool si5351_selection_changed =
            candidate_config.si5351_i2c_bus != config.si5351_i2c_bus ||
            candidate_config.si5351_i2c_address != config.si5351_i2c_address;
        const bool selecting_si5351 =
            candidate_config.transmit_backend == TransmitBackendKind::SI5351 &&
            config.transmit_backend != TransmitBackendKind::SI5351;
        if (si5351_selection_changed || selecting_si5351)
        {
            if (candidate_config.si5351_i2c_address < 0x60 ||
                candidate_config.si5351_i2c_address > 0x6F)
            {
                throw std::runtime_error(
                    "Invalid Si5351 I2C address. Expected 0x60 through 0x6F.");
            }
            const auto inventory = discover_si5351_addresses(
                candidate_config.si5351_i2c_bus,
                candidate_config.si5351_reference_hz);
            if (!inventory.error.empty()) throw std::runtime_error(inventory.error);
            if (!inventory.contains(candidate_config.si5351_i2c_address))
            {
                throw std::runtime_error(
                    "No register-compatible Si5351 device was detected at " +
                    config_handler_serialization::config_serialization_si5351_i2c_address(
                        candidate_config.si5351_i2c_address) +
                    " on I2C bus " +
                    std::to_string(candidate_config.si5351_i2c_bus) + ".");
            }
        }

        if (!validate_config_candidate(candidate_config, &error_message))
        {
            throw std::runtime_error(error_message);
        }

        if (candidate_config.mode == ModeType::QRSS ||
            candidate_config.mode == ModeType::FSKCW ||
            candidate_config.mode == ModeType::DFCW)
        {
            std::chrono::nanoseconds message_duration{};
            if (!compute_non_wspr_message_duration(
                    candidate_config,
                    message_duration,
                    &error_message))
            {
                throw std::runtime_error(error_message);
            }

            const auto repeat_interval =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::minutes(
                        candidate_config.schedule_repeat_minutes));
            if (message_duration > repeat_interval)
            {
                validate_non_wspr_repeat_interval_policy(
                    candidate_config,
                    &error_message);
                throw ConfigValidationError(
                    error_message,
                    {
                        {"policy", "cw_duration_repeat_interval"},
                        {"field", "CW.Message"},
                        {"mode",
                         candidate_config.mode == ModeType::QRSS
                             ? "QRSS"
                             : (candidate_config.mode == ModeType::FSKCW
                                    ? "FSKCW"
                                    : "DFCW")},
                        {"message_duration_seconds",
                         std::chrono::duration<double>(message_duration).count()},
                        {"repeat_interval_seconds",
                         std::chrono::duration<double>(repeat_interval).count()},
                    });
            }
        }

        if (candidate_config.transmit &&
            !validate_wspr_semantics(
                candidate_config,
                &error_message,
                &error_details))
        {
            throw ConfigValidationError(error_message, error_details);
        }

        const auto wtp_selection_error = wtp_runtime_selection_error(
            candidate_config.transmit_backend == TransmitBackendKind::WTP
                ? std::optional<WtpSettings>(candidate_config.wtp) : std::nullopt);
        if (!wtp_selection_error.empty()) throw std::runtime_error(wtp_selection_error);
        if (wtp_runtime_selected() && wtp_runtime_defers_reload() && candidate_config.mode != config.mode)
            throw std::runtime_error("Stop Pico work before changing modes");
        config_handler_serialization::serialize_runtime_config_to_json(
            candidate_config, candidate_json);
    }
    catch (const ConfigValidationError &)
    {
        throw;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(
            std::string("Configuration update rejected: ") + e.what());
    }

    copy_config(candidate_config, config);
    publish_test_tone_planning_config(config);
    jConfig = candidate_json;
    refresh_logger_level_from_config();
    json_to_ini();
    if (!g_patch_all_from_web_runtime_apply_suppressed_for_test)
    {
        callback_ini_changed();
    }
    return config_revision_locked();
}

void set_patch_all_from_web_runtime_apply_suppressed_for_test(bool suppressed) noexcept
{
    g_patch_all_from_web_runtime_apply_suppressed_for_test = suppressed;
}

bool persist_rp1_gpclk_route_config(int gpio, std::string *error_message) noexcept
{
    try
    {
        std::lock_guard update_lock(config_update_mutex());
        if (gpio != 4 && gpio != 20)
        {
            if (error_message) *error_message = "RP1 GPCLK route must be GPIO4 or GPIO20.";
            return false;
        }
        ArgParserConfig candidate = config;
        candidate.gpio_tx_pin = gpio;
        if (get_raspberry_pi_generation() == 5 && operator_exposes_rp1_gpio())
        {
            candidate.transmit_backend = TransmitBackendKind::RP1_GPCLK;
            resolve_backend_specific_config(candidate);
        }
        std::string validation_error;
        if (!validate_config_candidate(candidate, &validation_error, false))
        {
            if (error_message) *error_message = validation_error;
            return false;
        }
        nlohmann::json candidate_json = jConfig;
        config_handler_serialization::serialize_runtime_config_to_json(
            candidate, candidate_json);
        persist_config_json(candidate_json);
        copy_config(candidate, config);
        publish_test_tone_planning_config(config);
        jConfig = std::move(candidate_json);
        return true;
    }
    catch (const std::exception &error)
    {
        if (error_message) *error_message = error.what();
        return false;
    }
    catch (...)
    {
        if (error_message) *error_message = "Unknown RP1 GPCLK persistence failure.";
        return false;
    }
}

void repair_from_web(bool attempt_repair)
{
    const std::string filename = config.ini_filename;

    if (attempt_repair)
    {
        iniFile.repair_from_stock(get_raw_version_string());
        iniMonitor.stop();
        iniMonitor.filemon(config.ini_filename, callback_ini_changed);
        iniMonitor.setPriority(SCHED_RR, 10);
    }
    else
    {
        iniFile.reset_to_stock(get_raw_version_string());
        iniMonitor.stop();
        iniMonitor.filemon(config.ini_filename, callback_ini_changed);
        iniMonitor.setPriority(SCHED_RR, 10);
    }

    std::string load_error;
    std::vector<std::string> warning_messages;
    if (!load_json(filename, &load_error, &warning_messages))
    {
        for (const auto &warning_message : warning_messages)
        {
            llog.logS(WARN, warning_message);
        }

        llog.logS(
            ERROR,
            "Failed to reload repaired configuration; previous configuration remains loaded: ",
            load_error);
        return;
    }

    for (const auto &warning_message : warning_messages)
    {
        llog.logS(WARN, warning_message);
    }

    if (attempt_repair)
    {
        llog.logS(INFO, "Configuration file repaired from stock.");
    }
    else
    {
        llog.logS(INFO, "Configuration file restored from stock.");
    }

    send_ws_message("configuration", "reload");
}
