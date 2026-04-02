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
}

namespace
{
    std::vector<double> parse_center_frequency_set(const nlohmann::json &value)
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
                        "Center Frequency Set string did not parse to an array");
                }

                return parsed.get<std::vector<double>>();
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error(
                    std::string("Invalid Meta.Center Frequency Set: ") + e.what());
            }
        }

        throw std::runtime_error(
            "Meta.Center Frequency Set must be an array or JSON array string");
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
        target["Meta"]["Center Frequency Set"] = nlohmann::json::array();

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
            {"Use NTP", true}};

        target["Server"] = {
            {"Web Port", 31415},
            {"Socket Port", 31416},
            {"Shutdown Button", 19},
            {"Use Shutdown", false}};
    }

    void json_to_config_impl(const nlohmann::json &source, ArgParserConfig &target)
    {
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
        target.center_freq_set = parse_center_frequency_set(source.at("Meta").at("Center Frequency Set"));

        target.transmit = source.at("Control").at("Transmit").get<bool>();

        target.callsign = source.at("Common").at("Call Sign").get<std::string>();
        target.grid_square = source.at("Common").at("Grid Square").get<std::string>();
        target.power_dbm = source.at("Common").at("TX Power").get<int>();
        target.frequencies = json_to_string(source.at("Common").at("Frequency"));
        target.tx_pin = source.at("Common").at("Transmit Pin").get<int>();

        target.ppm = source.at("Extended").at("PPM").get<double>();
        target.use_ntp = source.at("Extended").at("Use NTP").get<bool>();
        target.use_offset = source.at("Extended").at("Offset").get<bool>();
        target.use_led = source.at("Extended").at("Use LED").get<bool>();
        target.led_pin = source.at("Extended").at("LED Pin").get<int>();
        target.power_level = source.at("Extended").at("Power Level").get<int>();

        target.web_port = source.at("Server").at("Web Port").get<int>();
        target.socket_port = source.at("Server").at("Socket Port").get<int>();
        target.use_shutdown = source.at("Server").at("Use Shutdown").get<bool>();
        target.shutdown_pin = source.at("Server").at("Shutdown Button").get<int>();
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
        target["Meta"]["Center Frequency Set"] = source.center_freq_set;

        target["Control"]["Transmit"] = source.transmit;

        target["Common"]["Call Sign"] = source.callsign;
        target["Common"]["Grid Square"] = source.grid_square;
        target["Common"]["TX Power"] = source.power_dbm;
        target["Common"]["Frequency"] = source.frequencies;
        target["Common"]["Transmit Pin"] = source.tx_pin;

        target["Extended"]["PPM"] = source.ppm;
        target["Extended"]["Use NTP"] = source.use_ntp;
        target["Extended"]["Offset"] = source.use_offset;
        target["Extended"]["Use LED"] = source.use_led;
        target["Extended"]["LED Pin"] = source.led_pin;
        target["Extended"]["Power Level"] = source.power_level;

        target["Server"]["Web Port"] = source.web_port;
        target["Server"]["Socket Port"] = source.socket_port;
        target["Server"]["Use Shutdown"] = source.use_shutdown;
        target["Server"]["Shutdown Button"] = source.shutdown_pin;
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
        target.mode = source.mode;
        target.use_ini = source.use_ini;
        target.ini_filename = source.ini_filename;
        target.center_freq_set = source.center_freq_set;
        target.ntp_good = source.ntp_good;
    }

    void ini_to_json_impl(const std::string &filename, nlohmann::json &target)
    {
        nlohmann::json patch;
        const auto ini_data = iniFile.getData();

        for (const auto &section_pair : ini_data)
        {
            const std::string &section = section_pair.first;
            const auto &key_values = section_pair.second;

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
