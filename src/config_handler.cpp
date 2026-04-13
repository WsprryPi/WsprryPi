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

#include "config_handler.hpp"

#include "arg_parser.hpp"
#include "ini_file.hpp"
#include "json.hpp"
#include "logging.hpp"
#include "scheduling.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

ArgParserConfig config;
nlohmann::json jConfig;

namespace
{
    std::string trim_copy(const std::string &value);

    WsprPlannerPreference parse_wspr_planner_preference(
        const nlohmann::json &wspr)
    {
        const std::string raw =
            trim_copy(wspr.value("Planner Preference", std::string("auto")));
        std::string lowered = raw;
        std::transform(
            lowered.begin(),
            lowered.end(),
            lowered.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (lowered.empty() || lowered == "auto")
        {
            return WsprPlannerPreference::Auto;
        }

        if (lowered == "prefer_paired" || lowered == "prefer-paired")
        {
            return WsprPlannerPreference::PreferPaired;
        }

        if (lowered == "require_paired" || lowered == "require-paired")
        {
            return WsprPlannerPreference::RequirePaired;
        }

        throw std::runtime_error(
            "Invalid planner preference '" + raw +
            "'. Expected auto, prefer_paired, or require_paired.");
    }

    TransmitBackendKind parse_transmit_backend_kind(
        const nlohmann::json &runtime)
    {
        const std::string raw =
            trim_copy(runtime.value("Transmit Backend", std::string("gpio")));
        std::string lowered = raw;
        std::transform(
            lowered.begin(),
            lowered.end(),
            lowered.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (lowered.empty() || lowered == "gpio")
            return TransmitBackendKind::GPIO;
        if (lowered == "si5351")
            return TransmitBackendKind::SI5351;

        throw std::runtime_error(
            "Invalid Runtime.Transmit Backend. Expected 'gpio' or 'si5351'.");
    }

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
            const int parsed = std::stoi(raw, &consumed, base);
            if (consumed == raw.size())
            {
                return parsed;
            }
        }

        throw std::runtime_error(context + " must be an integer.");
    }

    int parse_si5351_tx_output(const nlohmann::json &source)
    {
        if (source.is_number_integer() || source.is_number_unsigned())
        {
            return parse_integer_config_value(
                source,
                "Si5351.TX Output");
        }

        const std::string raw =
            trim_copy(source.get<std::string>());
        std::string lowered = raw;
        std::transform(
            lowered.begin(),
            lowered.end(),
            lowered.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (lowered == "clk0" || lowered == "0")
            return 0;
        if (lowered == "clk1" || lowered == "1")
            return 1;
        if (lowered == "clk2" || lowered == "2")
            return 2;

        throw std::runtime_error(
            "Invalid Si5351.TX Output. Expected CLK0, CLK1, CLK2, 0, 1, or 2.");
    }

    ModeType parse_mode_type(const nlohmann::json &meta)
    {
        const std::string raw =
            trim_copy(meta.value("Mode", std::string("WSPR")));
        std::string upper = raw;
        std::transform(
            upper.begin(),
            upper.end(),
            upper.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            });

        if (upper.empty() || upper == "WSPR")
            return ModeType::WSPR;
        if (upper == "QRSS")
            return ModeType::QRSS;
        if (upper == "FSKCW")
            return ModeType::FSKCW;
        if (upper == "DFCW")
            return ModeType::DFCW;
        if (upper == "TONE")
            return ModeType::TONE;

        throw std::runtime_error(
            "Invalid mode '" + raw +
            "'. Expected WSPR, QRSS, FSKCW, DFCW, or TONE.");
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

    nlohmann::json public_config_from_internal(const nlohmann::json &source)
    {
        nlohmann::json public_json;
        public_json["Meta"] = {
            {"Mode", source.at("Meta").at("Mode")}};

        public_json["Runtime"] = {
            {"Transmit", source.at("Runtime").at("Transmit")},
            {"Transmit Backend", source.at("Runtime").at("Transmit Backend")},
            {"Use LED", source.at("Runtime").at("Use LED")},
            {"LED Pin", source.at("Runtime").at("LED Pin")},
            {"Web Port", source.at("Runtime").at("Web Port")},
            {"Socket Port", source.at("Runtime").at("Socket Port")},
            {"Use Shutdown", source.at("Runtime").at("Use Shutdown")},
            {"Shutdown Button", source.at("Runtime").at("Shutdown Button")}};

        public_json["GPIO"] = source.at("GPIO");
        public_json["Calibration"] = source.at("Calibration");
        public_json["Si5351"] = source.at("Si5351");
        public_json["WSPR"] = source.at("WSPR");
        public_json["CW"] = source.at("CW");
        public_json["Band GPIO"] = source.at("Band GPIO");
        return public_json;
    }

    void apply_public_config_to_internal(
        const nlohmann::json &public_json,
        nlohmann::json &internal_json)
    {
        if (public_json.contains("Meta"))
        {
            const auto &meta = public_json.at("Meta");
            if (meta.contains("Mode"))
                internal_json["Meta"]["Mode"] = meta.at("Mode");
            if (meta.contains("Use INI"))
                internal_json["Meta"]["Use INI"] = meta.at("Use INI");
            if (meta.contains("INI Filename"))
                internal_json["Meta"]["INI Filename"] = meta.at("INI Filename");
            if (meta.contains("Date Time Log"))
                internal_json["Meta"]["Date Time Log"] = meta.at("Date Time Log");
            if (meta.contains("Loop TX"))
                internal_json["Meta"]["Loop TX"] = meta.at("Loop TX");
            if (meta.contains("TX Iterations"))
                internal_json["Meta"]["TX Iterations"] = meta.at("TX Iterations");
        }

        if (public_json.contains("Runtime"))
        {
            const auto &runtime = public_json.at("Runtime");
            if (runtime.contains("Transmit"))
                internal_json["Runtime"]["Transmit"] = runtime.at("Transmit");
            if (runtime.contains("Transmit Backend"))
                internal_json["Runtime"]["Transmit Backend"] = runtime.at("Transmit Backend");
            if (runtime.contains("Use LED"))
                internal_json["Runtime"]["Use LED"] = runtime.at("Use LED");
            if (runtime.contains("LED Pin"))
                internal_json["Runtime"]["LED Pin"] = runtime.at("LED Pin");
            if (runtime.contains("Web Port"))
                internal_json["Runtime"]["Web Port"] = runtime.at("Web Port");
            if (runtime.contains("Socket Port"))
                internal_json["Runtime"]["Socket Port"] = runtime.at("Socket Port");
            if (runtime.contains("Use Shutdown"))
                internal_json["Runtime"]["Use Shutdown"] = runtime.at("Use Shutdown");
            if (runtime.contains("Shutdown Button"))
                internal_json["Runtime"]["Shutdown Button"] = runtime.at("Shutdown Button");
        }

        if (public_json.contains("GPIO"))
            internal_json["GPIO"] = public_json.at("GPIO");
        if (public_json.contains("Calibration"))
            internal_json["Calibration"] = public_json.at("Calibration");
        if (public_json.contains("Si5351"))
            internal_json["Si5351"] = public_json.at("Si5351");
        if (public_json.contains("WSPR"))
            internal_json["WSPR"] = public_json.at("WSPR");
        if (public_json.contains("CW"))
            internal_json["CW"] = public_json.at("CW");
        if (public_json.contains("Band GPIO"))
            internal_json["Band GPIO"] = public_json.at("Band GPIO");
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
        {HamBand::BAND_22M, "22m"},
        {HamBand::BAND_20M, "20m"},
        {HamBand::BAND_17M, "17m"},
        {HamBand::BAND_15M, "15m"},
        {HamBand::BAND_12M, "12m"},
        {HamBand::BAND_10M, "10m"},
        {HamBand::BAND_6M, "6m"},
        {HamBand::BAND_4M, "4m"},
        {HamBand::BAND_2M, "2m"},
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
        band_gpio[ham_band_index(HamBand::BAND_2200M)] = make_band_gpio_config(17, true);
        band_gpio[ham_band_index(HamBand::BAND_630M)] = make_band_gpio_config(27, true);
        band_gpio[ham_band_index(HamBand::BAND_160M)] = make_band_gpio_config(22, true);
        band_gpio[ham_band_index(HamBand::BAND_80M)] = make_band_gpio_config(23, true);
        band_gpio[ham_band_index(HamBand::BAND_60M)] = make_band_gpio_config(24, true);
        band_gpio[ham_band_index(HamBand::BAND_40M)] = make_band_gpio_config(25, true);
        band_gpio[ham_band_index(HamBand::BAND_30M)] = make_band_gpio_config(5, true);
        band_gpio[ham_band_index(HamBand::BAND_22M)] = make_band_gpio_config(6, true);
        band_gpio[ham_band_index(HamBand::BAND_20M)] = make_band_gpio_config(12, true);
        band_gpio[ham_band_index(HamBand::BAND_17M)] = make_band_gpio_config(13, true);
        band_gpio[ham_band_index(HamBand::BAND_15M)] = make_band_gpio_config(16, true);
        band_gpio[ham_band_index(HamBand::BAND_12M)] = make_band_gpio_config(26, true);
        band_gpio[ham_band_index(HamBand::BAND_10M)] = make_band_gpio_config(20, true);
        band_gpio[ham_band_index(HamBand::BAND_6M)] = make_band_gpio_config(21, true);
        band_gpio[ham_band_index(HamBand::BAND_4M)] = make_band_gpio_config(-1, false);
        band_gpio[ham_band_index(HamBand::BAND_2M)] = make_band_gpio_config(-1, false);
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
        for (const auto &[key, value] : ini_section)
        {
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

void init_default_config()
{
    // Runtime
    config.transmit = false;
    config.transmit_backend = TransmitBackendKind::GPIO;

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
    config.gpio_tx_pin = kDefaultTransmitGpio;
    config.gpio_power_level = 7;
    config.gpio_use_ntp = true;
    config.si5351_i2c_bus = kDefaultSi5351I2cBus;
    config.si5351_i2c_address = kDefaultSi5351I2cAddress;
    config.si5351_reference_hz = kDefaultSi5351ReferenceHz;
    config.si5351_tx_output = kDefaultSi5351TxOutput;
    config.si5351_power_level = 1;
    resolve_backend_specific_config(config);

    config.modulation_fsk_offset_hz = 500.0;
    config.schedule_start_minute = 0;
    config.schedule_repeat_minutes = 10;

    // Runtime
    config.web_port = 31415;
    config.socket_port = 31416;
    config.use_shutdown = false;
    config.shutdown_pin = 19;

    // Meta
    config.use_ini = true;

    config.wspr.callsign = config.callsign;
    config.wspr.grid_square = config.grid_square;
    config.wspr.power_dbm = config.power_dbm;
    config.wspr.frequencies = config.frequencies;
    config.wspr.audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
    config.wspr.planner_preference = config.wspr_planner_preference;
    config.qrss = QrssModeConfig{};
    config.fskcw = FskcwModeConfig{};
    config.dfcw = DfcwModeConfig{};

    set_default_band_gpio_config(config.band_gpio);
}

void resolve_backend_specific_config(ArgParserConfig &config) noexcept
{
    config.tx_pin = config.gpio_tx_pin;
    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        config.power_level = config.si5351_power_level;
        config.use_ntp = false;
        return;
    }

    config.power_level = config.gpio_power_level;
    config.use_ntp = config.gpio_use_ntp;
}

namespace
{
    std::vector<double> parse_wspr_dial_frequency_set(const nlohmann::json &value)
    {
        if (value.is_array())
        {
            return value.get<std::vector<double>>();
        }

        if (value.is_string())
        {
            const std::string raw_value = trim_copy(value.get<std::string>());

            if (raw_value.empty())
            {
                return {};
            }

            try
            {
                nlohmann::json parsed = nlohmann::json::parse(raw_value);

                if (!parsed.is_array())
                {
                    throw std::runtime_error(
                        "WSPR dial frequency set string did not parse to an array");
                }

                return parsed.get<std::vector<double>>();
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error(
                    std::string("Invalid WSPR.WSPR Dial Frequency Set: ") + e.what());
            }
        }

        throw std::runtime_error(
            "WSPR.WSPR Dial Frequency Set must be an array or JSON array string");
    }

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

    std::string json_to_string(const nlohmann::json &j)
    {
        if (j.is_string())
        {
            return j.get<std::string>();
        }

        if (j.is_number())
        {
            return std::to_string(j.get<double>());
        }

        return j.dump();
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
        return (section == "Runtime" &&
                (key == "Transmit" ||
                 key == "Transmit Backend" ||
                 key == "Use LED" ||
                 key == "LED Pin" ||
                 key == "Web Port" ||
                 key == "Socket Port" ||
                 key == "Use Shutdown" ||
                 key == "Shutdown Button")) ||
               (section == "GPIO" &&
                (key == "Transmit Pin" ||
                 key == "Power Level" ||
                 key == "Use NTP")) ||
               (section == "WSPR" &&
                (key == "Call Sign" ||
                 key == "Grid Square" ||
                 key == "TX Power" ||
                 key == "Frequency" ||
                 key == "Planner Preference" ||
                 key == "Use Random Offset")) ||
               (section == "Meta" && key == "Mode") ||
               (section == "Calibration" &&
                key == "PPM") ||
               (section == "Si5351" &&
                (key == "I2C Bus" ||
                 key == "I2C Address" ||
                 key == "Reference Frequency" ||
                 key == "TX Output" ||
                 key == "Power Level")) ||
               (section == "CW" &&
                (key == "Base Frequency" ||
                 key == "Shift Hz" ||
                 key == "Dot Seconds" ||
                 key == "Start Minute" ||
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
            {"Mode", "WSPR"},
            {"Use INI", false},
            {"INI Filename", ""},
            {"Date Time Log", false},
            {"Loop TX", false},
            {"TX Iterations", 0}};
        target["Runtime"] = {
            {"Transmit", false},
            {"Transmit Backend", "gpio"},
            {"LED Pin", 18},
            {"Use LED", false},
            {"Web Port", 31415},
            {"Socket Port", 31416},
            {"Use Shutdown", false},
            {"Shutdown Button", 19}};

        target["GPIO"] = {
            {"Transmit Pin", kDefaultTransmitGpio},
            {"Power Level", 7},
            {"Use NTP", true}};

        target["Calibration"] = {
            {"PPM", 0.0}};

        target["Si5351"] = {
            {"I2C Bus", kDefaultSi5351I2cBus},
            {"I2C Address", kDefaultSi5351I2cAddress},
            {"Reference Frequency", kDefaultSi5351ReferenceHz},
            {"TX Output", "CLK0"},
            {"Power Level", 1}};

        target["Band GPIO"] = nlohmann::json::object();
        target["WSPR"] = {
            {"Call Sign", "NXXX"},
            {"Grid Square", "ZZ99"},
            {"TX Power", 20},
            {"Frequency", "20m"},
            {"Planner Preference", "auto"},
            {"Use Random Offset", true},
            {"WSPR Dial Frequency Set", nlohmann::json::array()}};
        target["CW"] = {
            {"Message", ""},
            {"Base Frequency", 3572000.0},
            {"Shift Hz", 500.0},
            {"Dot Seconds", 3.0},
            {"Start Minute", 0},
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

    void json_to_config_impl(const nlohmann::json &source, ArgParserConfig &target)
    {
        set_default_band_gpio_config(target.band_gpio);

        target.use_ini = source.at("Meta").at("Use INI").get<bool>();
        target.ini_filename = source.at("Meta").at("INI Filename").get<std::string>();
        target.date_time_log = source.at("Meta").at("Date Time Log").get<bool>();
        target.mode = parse_mode_type(source.at("Meta"));
        target.wspr_planner_preference =
            parse_wspr_planner_preference(source.at("WSPR"));
        target.loop_tx = source.at("Meta").at("Loop TX").get<bool>();
        target.tx_iterations.store(source.at("Meta").at("TX Iterations").get<int>());
        const auto &wspr = source.at("WSPR");
        if (wspr.contains("WSPR Dial Frequency Set") &&
            !wspr.at("WSPR Dial Frequency Set").empty())
        {
            target.wspr_dial_freq_set =
                parse_wspr_dial_frequency_set(wspr.at("WSPR Dial Frequency Set"));
        }
        else
        {
            target.wspr_dial_freq_set.clear();
        }

        target.transmit = source.at("Runtime").at("Transmit").get<bool>();
        target.transmit_backend =
            parse_transmit_backend_kind(source.at("Runtime"));
        const nlohmann::json gpio =
            source.contains("GPIO") ? source.at("GPIO") : nlohmann::json::object();
        target.gpio_tx_pin =
            gpio.contains("Transmit Pin")
                ? gpio.at("Transmit Pin").get<int>()
                : kDefaultTransmitGpio;
        target.gpio_power_level =
            gpio.contains("Power Level")
                ? gpio.at("Power Level").get<int>()
                : 7;
        target.gpio_use_ntp =
            gpio.contains("Use NTP")
                ? gpio.at("Use NTP").get<bool>()
                : true;
        const nlohmann::json si5351 =
            source.contains("Si5351") ? source.at("Si5351") : nlohmann::json::object();
        target.si5351_i2c_bus =
            si5351.contains("I2C Bus")
                ? parse_integer_config_value(si5351.at("I2C Bus"), "Si5351.I2C Bus")
                : kDefaultSi5351I2cBus;
        target.si5351_i2c_address =
            si5351.contains("I2C Address")
                ? parse_integer_config_value(si5351.at("I2C Address"), "Si5351.I2C Address", 0)
                : kDefaultSi5351I2cAddress;
        target.si5351_reference_hz =
            si5351.contains("Reference Frequency")
                ? parse_integer_config_value(
                      si5351.at("Reference Frequency"),
                      "Si5351.Reference Frequency")
                : kDefaultSi5351ReferenceHz;
        target.si5351_tx_output =
            si5351.contains("TX Output")
                ? parse_si5351_tx_output(si5351.at("TX Output"))
                : kDefaultSi5351TxOutput;
        target.si5351_power_level =
            si5351.contains("Power Level")
                ? si5351.at("Power Level").get<int>()
                : 1;
        resolve_backend_specific_config(target);
        target.ppm = source.at("Calibration").at("PPM").get<double>();
        target.use_offset = source.at("WSPR").at("Use Random Offset").get<bool>();
        target.modulation_dot_seconds =
            source.contains("CW") &&
                    source.at("CW").contains("Dot Seconds")
                ? source.at("CW").at("Dot Seconds").get<double>()
                : target.modulation_dot_seconds;
        target.modulation_fsk_offset_hz =
            source.contains("CW") &&
                    source.at("CW").contains("Shift Hz")
                ? source.at("CW").at("Shift Hz").get<double>()
                : target.modulation_fsk_offset_hz;
        target.schedule_start_minute =
            source.contains("CW") &&
                    source.at("CW").contains("Start Minute")
                ? source.at("CW").at("Start Minute").get<int>()
                : target.schedule_start_minute;
        target.schedule_repeat_minutes =
            source.contains("CW") &&
                    source.at("CW").contains("Repeat Minutes")
                ? source.at("CW").at("Repeat Minutes").get<int>()
                : target.schedule_repeat_minutes;
        target.wspr_audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
        target.wspr.callsign =
            source.at("WSPR").at("Call Sign").get<std::string>();
        target.wspr.grid_square =
            source.at("WSPR").at("Grid Square").get<std::string>();
        target.wspr.power_dbm =
            source.at("WSPR").at("TX Power").get<int>();
        target.wspr.frequencies =
            json_to_string(source.at("WSPR").at("Frequency"));
        target.wspr.audio_offset_hz =
            WSPR_AUDIO_OFFSET_HZ;
        target.wspr.planner_preference =
            parse_wspr_planner_preference(source.at("WSPR"));
        const auto &cw = source.at("CW");
        const std::string cw_message =
            cw.value("Message", std::string(""));
        const double cw_base_frequency_hz =
            cw.value("Base Frequency", 0.0);
        const double cw_shift_hz =
            cw.value("Shift Hz", 0.0);
        target.qrss.message =
            cw_message;
        target.qrss.frequency_hz = cw_base_frequency_hz;
        target.qrss.dot_seconds = target.modulation_dot_seconds;
        target.fskcw.message = cw_message;
        target.fskcw.space_frequency_hz = cw_base_frequency_hz;
        target.fskcw.mark_frequency_hz = cw_base_frequency_hz + cw_shift_hz;
        target.fskcw.dot_seconds = target.modulation_dot_seconds;
        target.dfcw.message = cw_message;
        target.dfcw.dot_frequency_hz = cw_base_frequency_hz;
        target.dfcw.dash_frequency_hz = cw_base_frequency_hz + cw_shift_hz;
        target.dfcw.dot_seconds = target.modulation_dot_seconds;

        target.callsign = target.wspr.callsign;
        target.grid_square = target.wspr.grid_square;
        target.power_dbm = target.wspr.power_dbm;
        target.frequencies = target.wspr.frequencies;
        target.wspr_audio_offset_hz = WSPR_AUDIO_OFFSET_HZ;
        target.wspr_planner_preference = target.wspr.planner_preference;
        target.use_led = source.at("Runtime").at("Use LED").get<bool>();
        target.led_pin = source.at("Runtime").at("LED Pin").get<int>();

        target.web_port = source.at("Runtime").at("Web Port").get<int>();
        target.socket_port = source.at("Runtime").at("Socket Port").get<int>();
        target.use_shutdown = source.at("Runtime").at("Use Shutdown").get<bool>();
        target.shutdown_pin = source.at("Runtime").at("Shutdown Button").get<int>();
        target.use_journald = false;

        // Missing Band GPIO data is allowed; seeded defaults stay in place.
        const auto band_gpio_section_it = source.find("Band GPIO");
        if (band_gpio_section_it == source.end() || !band_gpio_section_it->is_object())
        {
            return;
        }

        for (const auto &[band, band_name] : kHamBandJsonKeys)
        {
            const auto band_config_it = band_gpio_section_it->find(band_name);
            if (band_config_it == band_gpio_section_it->end() || !band_config_it->is_object())
            {
                continue;
            }

            BandGPIOConfig &band_config = target.band_gpio[ham_band_index(band)];

            if (band_config_it->contains("GPIO"))
            {
                band_config.gpio = band_config_it->at("GPIO").get<int>();
            }

            if (band_config_it->contains("Enabled"))
            {
                band_config.enabled = band_config_it->at("Enabled").get<bool>();
            }

            if (band_config_it->contains("Active High"))
            {
                band_config.active_high = band_config_it->at("Active High").get<bool>();
            }
        }
    }

    void config_to_json_impl(const ArgParserConfig &source, nlohmann::json &target)
    {
        target["Meta"]["Mode"] =
            mode_type_to_string(
                source.mode == ModeType::TONE ? ModeType::WSPR : source.mode);
        target["Meta"]["Use INI"] = source.use_ini;
        target["Meta"]["INI Filename"] = source.ini_filename;
        target["Meta"]["Date Time Log"] = source.date_time_log;
        target["Meta"]["Loop TX"] = source.loop_tx;
        target["Meta"]["TX Iterations"] = source.tx_iterations.load();

        target["Runtime"]["Transmit"] = source.transmit;
        target["Runtime"]["Transmit Backend"] =
            transmit_backend_kind_to_string(source.transmit_backend);
        target["Runtime"]["Use LED"] = source.use_led;
        target["Runtime"]["LED Pin"] = source.led_pin;
        target["Runtime"]["Web Port"] = source.web_port;
        target["Runtime"]["Socket Port"] = source.socket_port;
        target["Runtime"]["Use Shutdown"] = source.use_shutdown;
        target["Runtime"]["Shutdown Button"] = source.shutdown_pin;

        target["GPIO"]["Transmit Pin"] = source.gpio_tx_pin;
        target["GPIO"]["Power Level"] = source.gpio_power_level;
        target["GPIO"]["Use NTP"] = source.gpio_use_ntp;

        target["Calibration"]["PPM"] = source.ppm;

        target["Si5351"]["I2C Bus"] = source.si5351_i2c_bus;
        target["Si5351"]["I2C Address"] = source.si5351_i2c_address;
        target["Si5351"]["Reference Frequency"] = source.si5351_reference_hz;
        target["Si5351"]["TX Output"] =
            std::string("CLK") + std::to_string(source.si5351_tx_output);
        target["Si5351"]["Power Level"] = source.si5351_power_level;

        target["WSPR"]["Call Sign"] = source.wspr.callsign;
        target["WSPR"]["Grid Square"] = source.wspr.grid_square;
        target["WSPR"]["TX Power"] = source.wspr.power_dbm;
        target["WSPR"]["Frequency"] = source.wspr.frequencies;
        target["WSPR"]["Planner Preference"] =
            wspr_planner_preference_to_string(source.wspr.planner_preference);
        target["WSPR"]["Use Random Offset"] = source.use_offset;
        target["WSPR"]["WSPR Dial Frequency Set"] = source.wspr_dial_freq_set;

        std::string cw_message = source.qrss.message;
        double cw_base_frequency_hz = source.qrss.frequency_hz;
        double cw_shift_hz = source.modulation_fsk_offset_hz;
        if (source.mode == ModeType::FSKCW)
        {
            cw_message = source.fskcw.message;
            cw_base_frequency_hz = source.fskcw.space_frequency_hz;
            cw_shift_hz =
                source.fskcw.mark_frequency_hz - source.fskcw.space_frequency_hz;
        }
        else if (source.mode == ModeType::DFCW)
        {
            cw_message = source.dfcw.message;
            cw_base_frequency_hz = source.dfcw.dot_frequency_hz;
            cw_shift_hz =
                source.dfcw.dash_frequency_hz - source.dfcw.dot_frequency_hz;
        }
        target["CW"]["Message"] = cw_message;
        target["CW"]["Base Frequency"] = cw_base_frequency_hz;
        target["CW"]["Shift Hz"] = cw_shift_hz;
        target["CW"]["Dot Seconds"] = source.modulation_dot_seconds;
        target["CW"]["Start Minute"] = source.schedule_start_minute;
        target["CW"]["Repeat Minutes"] = source.schedule_repeat_minutes;

        for (const auto &[band, band_name] : kHamBandJsonKeys)
        {
            const BandGPIOConfig &band_config = source.band_gpio[ham_band_index(band)];
            target["Band GPIO"][band_name]["GPIO"] = band_config.gpio;
            target["Band GPIO"][band_name]["Enabled"] = band_config.enabled;
            target["Band GPIO"][band_name]["Active High"] = band_config.active_high;
        }
    }

    void copy_config(const ArgParserConfig &source, ArgParserConfig &target)
    {
        target.transmit = source.transmit;
        target.callsign = source.callsign;
        target.grid_square = source.grid_square;
        target.power_dbm = source.power_dbm;
        target.frequencies = source.frequencies;
        target.tx_pin = source.tx_pin;
        target.ppm = source.ppm;
        target.use_ntp = source.use_ntp;
        target.use_offset = source.use_offset;
        target.power_level = source.power_level;
        target.transmit_backend = source.transmit_backend;
        target.gpio_tx_pin = source.gpio_tx_pin;
        target.gpio_power_level = source.gpio_power_level;
        target.gpio_use_ntp = source.gpio_use_ntp;
        target.si5351_i2c_bus = source.si5351_i2c_bus;
        target.si5351_i2c_address = source.si5351_i2c_address;
        target.si5351_reference_hz = source.si5351_reference_hz;
        target.si5351_tx_output = source.si5351_tx_output;
        target.si5351_power_level = source.si5351_power_level;
        target.socket_port = source.socket_port;
        target.shutdown_pin = source.shutdown_pin;
        target.use_journald = source.use_journald;
        target.date_time_log = source.date_time_log;
        target.wspr_planner_preference = source.wspr_planner_preference;
        target.loop_tx = source.loop_tx;
        target.tx_iterations.store(source.tx_iterations.load());
        target.wspr_audio_offset_hz = source.wspr_audio_offset_hz;
        target.modulation_dot_seconds = source.modulation_dot_seconds;
        target.modulation_fsk_offset_hz = source.modulation_fsk_offset_hz;
        target.schedule_start_minute = source.schedule_start_minute;
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
        target.ntp_good = source.ntp_good;
        target.band_gpio = source.band_gpio;
    }

    void ini_to_json_impl(const std::string &filename, nlohmann::json &target)
    {
        nlohmann::json patch;
        const auto ini_data = iniFile.getData();

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
                section != "Runtime" &&
                section != "GPIO" &&
                section != "Calibration" &&
                section != "Si5351" &&
                section != "WSPR" &&
                section != "CW")
            {
                continue;
            }

            for (const auto &kv : key_values)
            {
                const std::string &key = kv.first;
                const std::string trimmed = trim_copy(kv.second);

                if (trimmed.empty())
                {
                    continue;
                }

                patch[section][key] = parse_ini_value(trimmed);
            }
        }

        patch["Meta"]["INI Filename"] = filename;
        patch["Meta"]["Use INI"] = true;
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
            const auto ini_data = iniFile.getData();

            collect_ini_warnings(
                candidate_json,
                ini_data,
                local_warnings,
                missing_required_tx_item);

            ini_to_json_impl(filename, candidate_json);
            json_to_config_impl(candidate_json, candidate_config);

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
            if (!validate_wspr_semantics(
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

            config_to_json_impl(candidate_config, candidate_json);
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
    init_config_json_impl(jConfig);
}

void ini_to_json(std::string filename)
{
    ini_to_json_impl(filename, jConfig);
}

void json_to_config()
{
    json_to_config_impl(jConfig, config);
}

nlohmann::json get_public_config_json()
{
    return public_config_from_internal(jConfig);
}

void config_to_json()
{
    config_to_json_impl(config, jConfig);
}

void json_to_ini()
{
    if (!config.use_ini)
    {
        return;
    }

    std::map<std::string, std::unordered_map<std::string, std::string>> new_data;

    for (auto &section : jConfig.items())
    {
        const std::string section_name = section.key();

        if (!section.value().is_object())
        {
            continue;
        }

        if (section_name != "Meta" &&
            section_name != "Runtime" &&
            section_name != "GPIO" &&
            section_name != "Calibration" &&
            section_name != "Si5351" &&
            section_name != "WSPR" &&
            section_name != "CW" &&
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

        for (auto &kv : section.value().items())
        {
            const std::string &key = kv.key();
            const bool persist_key =
                (section_name == "Meta" &&
                 key == "Mode") ||
                (section_name == "Runtime" &&
                 (key == "Transmit" ||
                  key == "Transmit Backend" ||
                  key == "Use LED" ||
                  key == "LED Pin" ||
                  key == "Web Port" ||
                  key == "Socket Port" ||
                  key == "Use Shutdown" ||
                  key == "Shutdown Button")) ||
                (section_name == "GPIO" &&
                 (key == "Transmit Pin" ||
                  key == "Power Level" ||
                  key == "Use NTP")) ||
                (section_name == "Calibration" &&
                 key == "PPM") ||
                (section_name == "Si5351" &&
                 (key == "I2C Bus" ||
                  key == "I2C Address" ||
                  key == "Reference Frequency" ||
                  key == "TX Output" ||
                  key == "Power Level")) ||
                (section_name == "WSPR" &&
                 (key == "Call Sign" ||
                  key == "Grid Square" ||
                  key == "TX Power" ||
                  key == "Frequency" ||
                  key == "Planner Preference" ||
                  key == "Use Random Offset")) ||
                (section_name == "CW" &&
                 (key == "Message" ||
                  key == "Base Frequency" ||
                  key == "Shift Hz" ||
                  key == "Dot Seconds" ||
                  key == "Start Minute" ||
                  key == "Repeat Minutes"));

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
            else
            {
                out_val = kv.value().dump();
            }

            new_data[section_name][key] = out_val;
        }
    }

    iniFile.setData(new_data);
    iniFile.save();
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

    candidate_out.valid = true;
    candidate_out.transmit_enabled = candidate_out.normalized_config.transmit;
}

void commit_config_candidate(const PreparedConfigCandidate &candidate)
{
    if (!candidate.valid)
    {
        throw std::invalid_argument(
            "Cannot commit an invalid configuration candidate.");
    }

    copy_config(candidate.normalized_config, config);
    jConfig = candidate.normalized_json;
}

void copy_runtime_config(const ArgParserConfig &source, ArgParserConfig &target)
{
    copy_config(source, target);
}

void dump_json(const nlohmann::json &j, std::string tag)
{
    llog.logS(DEBUG, tag, "JSON Dump:", j.dump());
}

void patch_all_from_web(const nlohmann::json &j)
{
    nlohmann::json candidate_public_json = public_config_from_internal(jConfig);
    candidate_public_json.merge_patch(j);

    nlohmann::json candidate_json = jConfig;
    apply_public_config_to_internal(candidate_public_json, candidate_json);

    ArgParserConfig candidate_config;
    std::string error_message;
    nlohmann::json error_details;

    try
    {
        json_to_config_impl(candidate_json, candidate_config);

        if (!validate_config_candidate(candidate_config, &error_message))
        {
            throw std::runtime_error(error_message);
        }

        if (!validate_wspr_semantics(
                candidate_config,
                &error_message,
                &error_details))
        {
            throw ConfigValidationError(error_message, error_details);
        }

        const auto band_gpio_section_it = candidate_json.find("Band GPIO");
        if (band_gpio_section_it != candidate_json.end() &&
            band_gpio_section_it->is_object())
        {
            for (WsprFrequencyEntry &entry : candidate_config.wspr_frequency_entries)
            {
                if (entry.selector_gpio == kSelectorGpioUnset)
                {
                    entry.allow_band_gpio_fallback = true;
                }
            }
        }

        config_to_json_impl(candidate_config, candidate_json);
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
    jConfig = candidate_json;
    json_to_ini();
    send_ws_message("configuration", "reload");
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
            "Failed to reload repaired configuration; keeping current config:",
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
