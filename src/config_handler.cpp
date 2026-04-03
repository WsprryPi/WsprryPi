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
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

ArgParserConfig config;
nlohmann::json jConfig;

namespace
{
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
    // Control
    config.transmit = false;

    // Common
    config.callsign = "NXXX";
    config.grid_square = "ZZ99";
    config.power_dbm = 20;
    config.frequencies = "20m";
    config.tx_pin = 4;

    // Extended
    config.ppm = 0.0;
    config.use_ntp = true;
    config.use_offset = true;
    config.wspr_audio_offset_hz = 1500.0;
    config.use_led = false;
    config.led_pin = 18;
    config.power_level = 7;

    // Server
    config.web_port = 31415;
    config.socket_port = 31416;
    config.use_shutdown = false;
    config.shutdown_pin = 19;

    // Meta
    config.use_ini = true;

    set_default_band_gpio_config(config.band_gpio);
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
                    std::string("Invalid Meta WSPR dial frequency set: ") + e.what());
            }
        }

        throw std::runtime_error(
            "Meta.WSPR Dial Frequency Set must be an array or JSON array string");
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
        return section == "Common" &&
               (key == "Call Sign" ||
                key == "Grid Square" ||
                key == "TX Power" ||
                key == "Frequency");
    }

    bool should_warn_if_missing(const std::string &section, const std::string &key)
    {
        return (section == "Control" && key == "Transmit") ||
               (section == "Common" &&
                (key == "Call Sign" ||
                 key == "Grid Square" ||
                 key == "TX Power" ||
                 key == "Frequency" ||
                 key == "Transmit Pin")) ||
               (section == "Extended" &&
                (key == "PPM" ||
                 key == "Use NTP" ||
                 key == "Offset" ||
                 key == "Use LED" ||
                 key == "LED Pin" ||
                 key == "Power Level")) ||
               (section == "Server" &&
                (key == "Web Port" ||
                 key == "Socket Port" ||
                 key == "Use Shutdown" ||
                 key == "Shutdown Button"));
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
            const auto section_it = ini_data.find(section);

            for (const auto &key_item : section_item.value().items())
            {
                const std::string &key = key_item.key();

                if (!should_warn_if_missing(section, key))
                {
                    continue;
                }

                bool missing_or_empty = false;

                if (section_it == ini_data.end())
                {
                    missing_or_empty = true;
                }
                else
                {
                    const auto key_it = section_it->second.find(key);
                    if (key_it == section_it->second.end() ||
                        trim_copy(key_it->second).empty())
                    {
                        missing_or_empty = true;
                    }
                }

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
            {"TX Iterations", 0},
            {"Test Tone", 730000.0}};
        target["Meta"]["WSPR Dial Frequency Set"] = nlohmann::json::array();

        target["Common"] = {
            {"Call Sign", "NXXX"},
            {"Frequency", "20m"},
            {"Grid Square", "ZZ99"},
            {"TX Power", 20},
            {"Transmit Pin", 4}};

        target["Control"] = {
            {"Transmit", false}};

        target["Extended"] = {
            {"LED Pin", 18},
            {"Offset", true},
            {"PPM", 0.0},
            {"Power Level", 7},
            {"Use LED", false},
            {"Use NTP", true},
            {"WSPR Audio Offset Hz", 1500.0}};

        target["Server"] = {
            {"Web Port", 31415},
            {"Socket Port", 31416},
            {"Shutdown Button", 19},
            {"Use Shutdown", false}};

        target["Band GPIO"] = nlohmann::json::object();
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

        const std::string mode_str = source.at("Meta").at("Mode").get<std::string>();
        if (mode_str == "WSPR")
        {
            target.mode = ModeType::WSPR;
        }
        else if (mode_str == "TONE")
        {
            target.mode = ModeType::TONE;
        }
        else
        {
            target.mode = ModeType::WSPR;
        }

        target.use_ini = source.at("Meta").at("Use INI").get<bool>();
        target.ini_filename = source.at("Meta").at("INI Filename").get<std::string>();
        target.date_time_log = source.at("Meta").at("Date Time Log").get<bool>();
        target.loop_tx = source.at("Meta").at("Loop TX").get<bool>();
        target.tx_iterations.store(source.at("Meta").at("TX Iterations").get<int>());
        target.test_tone = source.at("Meta").at("Test Tone").get<double>();
        const auto &meta = source.at("Meta");
        if (meta.contains("WSPR Dial Frequency Set") &&
            !meta.at("WSPR Dial Frequency Set").empty())
        {
            target.wspr_dial_freq_set =
                parse_wspr_dial_frequency_set(meta.at("WSPR Dial Frequency Set"));
        }
        else if (
            meta.contains("Center Frequency Set") &&
            !meta.at("Center Frequency Set").empty())
        {
            target.wspr_dial_freq_set =
                parse_wspr_dial_frequency_set(meta.at("Center Frequency Set"));
        }
        else
        {
            target.wspr_dial_freq_set.clear();
        }

        target.transmit = source.at("Control").at("Transmit").get<bool>();

        target.callsign = source.at("Common").at("Call Sign").get<std::string>();
        target.grid_square = source.at("Common").at("Grid Square").get<std::string>();
        target.power_dbm = source.at("Common").at("TX Power").get<int>();
        target.frequencies = json_to_string(source.at("Common").at("Frequency"));
        target.tx_pin = source.at("Common").at("Transmit Pin").get<int>();

        target.ppm = source.at("Extended").at("PPM").get<double>();
        target.use_ntp = source.at("Extended").at("Use NTP").get<bool>();
        target.use_offset = source.at("Extended").at("Offset").get<bool>();
        if (source.at("Extended").contains("WSPR Audio Offset Hz"))
        {
            target.wspr_audio_offset_hz =
                source.at("Extended").at("WSPR Audio Offset Hz").get<double>();
        }
        else
        {
            target.wspr_audio_offset_hz = 1500.0;
        }
        target.use_led = source.at("Extended").at("Use LED").get<bool>();
        target.led_pin = source.at("Extended").at("LED Pin").get<int>();
        target.power_level = source.at("Extended").at("Power Level").get<int>();

        target.web_port = source.at("Server").at("Web Port").get<int>();
        target.socket_port = source.at("Server").at("Socket Port").get<int>();
        target.use_shutdown = source.at("Server").at("Use Shutdown").get<bool>();
        target.shutdown_pin = source.at("Server").at("Shutdown Button").get<int>();

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
        target["Meta"]["Mode"] = "WSPR";
        if (source.mode == ModeType::TONE)
        {
            target["Meta"]["Mode"] = "TONE";
        }

        target["Meta"]["Use INI"] = source.use_ini;
        target["Meta"]["INI Filename"] = source.ini_filename;
        target["Meta"]["Date Time Log"] = source.date_time_log;
        target["Meta"]["Loop TX"] = source.loop_tx;
        target["Meta"]["TX Iterations"] = source.tx_iterations.load();
        target["Meta"]["Test Tone"] = source.test_tone;
        target["Meta"]["WSPR Dial Frequency Set"] = source.wspr_dial_freq_set;
        target["Meta"]["Center Frequency Set"] = source.wspr_dial_freq_set;

        target["Control"]["Transmit"] = source.transmit;

        target["Common"]["Call Sign"] = source.callsign;
        target["Common"]["Grid Square"] = source.grid_square;
        target["Common"]["TX Power"] = source.power_dbm;
        target["Common"]["Frequency"] = source.frequencies;
        target["Common"]["Transmit Pin"] = source.tx_pin;

        target["Extended"]["PPM"] = source.ppm;
        target["Extended"]["Use NTP"] = source.use_ntp;
        target["Extended"]["Offset"] = source.use_offset;
        target["Extended"]["WSPR Audio Offset Hz"] = source.wspr_audio_offset_hz;
        target["Extended"]["Use LED"] = source.use_led;
        target["Extended"]["LED Pin"] = source.led_pin;
        target["Extended"]["Power Level"] = source.power_level;

        target["Server"]["Web Port"] = source.web_port;
        target["Server"]["Socket Port"] = source.socket_port;
        target["Server"]["Use Shutdown"] = source.use_shutdown;
        target["Server"]["Shutdown Button"] = source.shutdown_pin;

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
        target.use_led = source.use_led;
        target.led_pin = source.led_pin;
        target.web_port = source.web_port;
        target.socket_port = source.socket_port;
        target.use_shutdown = source.use_shutdown;
        target.shutdown_pin = source.shutdown_pin;
        target.date_time_log = source.date_time_log;
        target.loop_tx = source.loop_tx;
        target.tx_iterations.store(source.tx_iterations.load());
        target.test_tone = source.test_tone;
        target.wspr_audio_offset_hz = source.wspr_audio_offset_hz;
        target.mode = source.mode;
        target.use_ini = source.use_ini;
        target.ini_filename = source.ini_filename;
        target.wspr_dial_freq_set = source.wspr_dial_freq_set;
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
        std::vector<std::string> *warning_messages)
    {
        try
        {
            init_config_json_impl(candidate_json);

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

        if (section_name != "Control" &&
            section_name != "Common" &&
            section_name != "Extended" &&
            section_name != "Server" &&
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

            new_data[section_name][kv.key()] = out_val;
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
    nlohmann::json candidate_json;
    ArgParserConfig candidate_config;
    std::string local_error_message;
    std::vector<std::string> local_warning_messages;

    if (!build_candidate_from_ini(
            filename,
            candidate_json,
            candidate_config,
            &local_error_message,
            &local_warning_messages))
    {
        if (error_message != nullptr)
        {
            *error_message = local_error_message;
        }

        if (warning_messages != nullptr)
        {
            *warning_messages = local_warning_messages;
        }

        return false;
    }

    if (warning_messages != nullptr)
    {
        *warning_messages = local_warning_messages;
    }

    copy_config(candidate_config, config);
    jConfig = candidate_json;
    return true;
}

void dump_json(const nlohmann::json &j, std::string tag)
{
    llog.logS(DEBUG, tag, "JSON Dump:", j.dump());
}

void patch_all_from_web(const nlohmann::json &j)
{
    nlohmann::json candidate_json = jConfig;
    candidate_json.merge_patch(j);

    ArgParserConfig candidate_config;
    std::string error_message;

    try
    {
        json_to_config_impl(candidate_json, candidate_config);

        if (!validate_config_candidate(candidate_config, &error_message))
        {
            throw std::runtime_error(error_message);
        }

        config_to_json_impl(candidate_config, candidate_json);
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
