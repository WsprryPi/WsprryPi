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
    std::string trim_copy(const std::string &value);

    WsprPlannerPreference parse_wspr_planner_preference(
        const nlohmann::json &meta)
    {
        const std::string raw =
            trim_copy(meta.value("Planner Preference", std::string("auto")));
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
            {"Mode", source.at("Meta").at("Mode")},
            {"Use INI", source.at("Meta").at("Use INI")},
            {"INI Filename", source.at("Meta").at("INI Filename")},
            {"Date Time Log", source.at("Meta").at("Date Time Log")},
            {"Loop TX", source.at("Meta").at("Loop TX")},
            {"TX Iterations", source.at("Meta").at("TX Iterations")}};

        public_json["Runtime"] = {
            {"Transmit", source.at("Control").at("Transmit")},
            {"Transmit Pin", source.at("Common").at("Transmit Pin")},
            {"PPM", source.at("Extended").at("PPM")},
            {"Use NTP", source.at("Extended").at("Use NTP")},
            {"Offset", source.at("Extended").at("Offset")},
            {"Power Level", source.at("Extended").at("Power Level")},
            {"Use LED", source.at("Extended").at("Use LED")},
            {"LED Pin", source.at("Extended").at("LED Pin")},
            {"Web Port", source.at("Server").at("Web Port")},
            {"Socket Port", source.at("Server").at("Socket Port")},
            {"Use Shutdown", source.at("Server").at("Use Shutdown")},
            {"Shutdown Button", source.at("Server").at("Shutdown Button")},
            {"Frequency Control GPIO Polarity",
             source.at("Extended").value("Frequency Control GPIO Polarity", false)}};

        public_json["Modulation"] = source.at("Modulation");
        public_json["Schedule"] = source.at("Schedule");
        public_json["WSPR"] = source.at("WSPR");
        public_json["QRSS"] = source.at("QRSS");
        public_json["FSKCW"] = source.at("FSKCW");
        public_json["DFCW"] = source.at("DFCW");
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
                internal_json["Control"]["Transmit"] = runtime.at("Transmit");
            if (runtime.contains("Transmit Pin"))
                internal_json["Common"]["Transmit Pin"] = runtime.at("Transmit Pin");
            if (runtime.contains("PPM"))
                internal_json["Extended"]["PPM"] = runtime.at("PPM");
            if (runtime.contains("Use NTP"))
                internal_json["Extended"]["Use NTP"] = runtime.at("Use NTP");
            if (runtime.contains("Offset"))
                internal_json["Extended"]["Offset"] = runtime.at("Offset");
            if (runtime.contains("Power Level"))
                internal_json["Extended"]["Power Level"] = runtime.at("Power Level");
            if (runtime.contains("Use LED"))
                internal_json["Extended"]["Use LED"] = runtime.at("Use LED");
            if (runtime.contains("LED Pin"))
                internal_json["Extended"]["LED Pin"] = runtime.at("LED Pin");
            if (runtime.contains("Web Port"))
                internal_json["Server"]["Web Port"] = runtime.at("Web Port");
            if (runtime.contains("Socket Port"))
                internal_json["Server"]["Socket Port"] = runtime.at("Socket Port");
            if (runtime.contains("Use Shutdown"))
                internal_json["Server"]["Use Shutdown"] = runtime.at("Use Shutdown");
            if (runtime.contains("Shutdown Button"))
                internal_json["Server"]["Shutdown Button"] = runtime.at("Shutdown Button");
            if (runtime.contains("Frequency Control GPIO Polarity"))
            {
                internal_json["Extended"]["Frequency Control GPIO Polarity"] =
                    runtime.at("Frequency Control GPIO Polarity");
            }
        }

        if (public_json.contains("WSPR"))
            internal_json["WSPR"] = public_json.at("WSPR");
        if (public_json.contains("Modulation"))
            internal_json["Modulation"] = public_json.at("Modulation");
        if (public_json.contains("Schedule"))
            internal_json["Schedule"] = public_json.at("Schedule");
        if (public_json.contains("QRSS"))
            internal_json["QRSS"] = public_json.at("QRSS");
        if (public_json.contains("FSKCW"))
            internal_json["FSKCW"] = public_json.at("FSKCW");
        if (public_json.contains("DFCW"))
            internal_json["DFCW"] = public_json.at("DFCW");
        if (public_json.contains("Band GPIO"))
            internal_json["Band GPIO"] = public_json.at("Band GPIO");

        // Keep legacy WSPR keys internal-only but mirrored for compatibility.
        internal_json["Common"]["Call Sign"] = internal_json["WSPR"]["Call Sign"];
        internal_json["Common"]["Grid Square"] = internal_json["WSPR"]["Grid Square"];
        internal_json["Common"]["TX Power"] = internal_json["WSPR"]["TX Power"];
        internal_json["Common"]["Frequency"] = internal_json["WSPR"]["Frequency"];
        internal_json["Extended"]["WSPR Audio Offset Hz"] =
            internal_json["WSPR"]["Audio Offset Hz"];
        internal_json["Meta"]["Planner Preference"] =
            internal_json["WSPR"]["Planner Preference"];
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
    // Control
    config.transmit = false;

    // Common
    config.callsign = "NXXX";
    config.grid_square = "ZZ99";
    config.power_dbm = 20;
    config.frequencies = "20m";
    config.tx_pin = kDefaultTransmitGpio;

    // Extended
    config.ppm = 0.0;
    config.use_ntp = true;
    config.use_offset = true;
    config.wspr_audio_offset_hz = 1500.0;
    config.use_led = false;
    config.led_pin = 18;
    config.power_level = 7;
    config.modulation_dot_seconds = 3.0;
    config.modulation_fsk_offset_hz = 500.0;
    config.schedule_start_minute = 0;
    config.schedule_repeat_minutes = 10;

    // Server
    config.web_port = 31415;
    config.socket_port = 31416;
    config.use_shutdown = false;
    config.shutdown_pin = 19;

    // Meta
    config.use_ini = true;
    config.tx_freq_control_active_high = false;

    config.wspr.callsign = config.callsign;
    config.wspr.grid_square = config.grid_square;
    config.wspr.power_dbm = config.power_dbm;
    config.wspr.frequencies = config.frequencies;
    config.wspr.audio_offset_hz = config.wspr_audio_offset_hz;
    config.wspr.planner_preference = config.wspr_planner_preference;
    config.qrss = QrssModeConfig{};
    config.fskcw = FskcwModeConfig{};
    config.dfcw = DfcwModeConfig{};

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
        if (ini_has_nonempty_value(ini_data, section, key))
        {
            return true;
        }

        if (section == "Control" && key == "Transmit")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Transmit");
        }

        if (section == "Common" && key == "Call Sign")
        {
            return ini_has_nonempty_value(ini_data, "WSPR", "Call Sign");
        }

        if (section == "Common" && key == "Grid Square")
        {
            return ini_has_nonempty_value(ini_data, "WSPR", "Grid Square");
        }

        if (section == "Common" && key == "TX Power")
        {
            return ini_has_nonempty_value(ini_data, "WSPR", "TX Power");
        }

        if (section == "Common" && key == "Frequency")
        {
            return ini_has_nonempty_value(ini_data, "WSPR", "Frequency");
        }

        if (section == "Common" && key == "Transmit Pin")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Transmit Pin");
        }

        if (section == "Extended" && key == "PPM")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "PPM");
        }

        if (section == "Extended" && key == "Use NTP")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Use NTP");
        }

        if (section == "Extended" && key == "Offset")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Offset");
        }

        if (section == "Extended" && key == "Use LED")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Use LED");
        }

        if (section == "Extended" && key == "LED Pin")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "LED Pin");
        }

        if (section == "Extended" && key == "Power Level")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Power Level");
        }

        if (section == "Server" && key == "Web Port")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Web Port");
        }

        if (section == "Server" && key == "Socket Port")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Socket Port");
        }

        if (section == "Server" && key == "Use Shutdown")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Use Shutdown");
        }

        if (section == "Server" && key == "Shutdown Button")
        {
            return ini_has_nonempty_value(ini_data, "Runtime", "Shutdown Button");
        }

        return false;
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
            {"Planner Preference", "auto"},
            {"Loop TX", false},
            {"TX Iterations", 0}};
        target["Meta"]["WSPR Dial Frequency Set"] = nlohmann::json::array();
        target["Modulation"] = {
            {"Dot Seconds", 3.0},
            {"FSK Offset Hz", 500.0}};
        target["Schedule"] = {
            {"Start Minute", 0},
            {"Repeat Minutes", 10}};

        target["Common"] = {
            {"Call Sign", "NXXX"},
            {"Frequency", "20m"},
            {"Grid Square", "ZZ99"},
            {"TX Power", 20},
            {"Transmit Pin", kDefaultTransmitGpio}};

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
        target["WSPR"] = {
            {"Call Sign", "NXXX"},
            {"Grid Square", "ZZ99"},
            {"TX Power", 20},
            {"Frequency", "20m"},
            {"Audio Offset Hz", 1500.0},
            {"Planner Preference", "auto"}};
        target["QRSS"] = {
            {"Message", ""},
            {"Frequency", 0.0},
            {"Dot Seconds", 0.0}};
        target["FSKCW"] = {
            {"Message", ""},
            {"Mark Frequency", 0.0},
            {"Space Frequency", 0.0},
            {"Dot Seconds", 0.0}};
        target["DFCW"] = {
            {"Message", ""},
            {"Dot Frequency", 0.0},
            {"Dash Frequency", 0.0},
            {"Dot Seconds", 0.0}};
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
            parse_wspr_planner_preference(source.at("Meta"));
        target.loop_tx = source.at("Meta").at("Loop TX").get<bool>();
        target.tx_iterations.store(source.at("Meta").at("TX Iterations").get<int>());
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
        target.tx_freq_control_active_high =
            source.at("Extended").value("Frequency Control GPIO Polarity", false);
        target.modulation_dot_seconds =
            source.contains("Modulation") &&
                    source.at("Modulation").contains("Dot Seconds")
                ? source.at("Modulation").at("Dot Seconds").get<double>()
                : target.modulation_dot_seconds;
        target.modulation_fsk_offset_hz =
            source.contains("Modulation") &&
                    source.at("Modulation").contains("FSK Offset Hz")
                ? source.at("Modulation").at("FSK Offset Hz").get<double>()
                : target.modulation_fsk_offset_hz;
        target.schedule_start_minute =
            source.contains("Schedule") &&
                    source.at("Schedule").contains("Start Minute")
                ? source.at("Schedule").at("Start Minute").get<int>()
                : target.schedule_start_minute;
        target.schedule_repeat_minutes =
            source.contains("Schedule") &&
                    source.at("Schedule").contains("Repeat Minutes")
                ? source.at("Schedule").at("Repeat Minutes").get<int>()
                : target.schedule_repeat_minutes;
        if (source.at("Extended").contains("WSPR Audio Offset Hz"))
        {
            target.wspr_audio_offset_hz =
                source.at("Extended").at("WSPR Audio Offset Hz").get<double>();
        }
        else
        {
            target.wspr_audio_offset_hz = 1500.0;
        }
        target.wspr.callsign =
            source.contains("WSPR") && source.at("WSPR").contains("Call Sign")
                ? source.at("WSPR").at("Call Sign").get<std::string>()
                : target.callsign;
        target.wspr.grid_square =
            source.contains("WSPR") && source.at("WSPR").contains("Grid Square")
                ? source.at("WSPR").at("Grid Square").get<std::string>()
                : target.grid_square;
        target.wspr.power_dbm =
            source.contains("WSPR") && source.at("WSPR").contains("TX Power")
                ? source.at("WSPR").at("TX Power").get<int>()
                : target.power_dbm;
        target.wspr.frequencies =
            source.contains("WSPR") && source.at("WSPR").contains("Frequency")
                ? json_to_string(source.at("WSPR").at("Frequency"))
                : target.frequencies;
        target.wspr.audio_offset_hz =
            source.contains("WSPR") && source.at("WSPR").contains("Audio Offset Hz")
                ? source.at("WSPR").at("Audio Offset Hz").get<double>()
                : target.wspr_audio_offset_hz;
        target.wspr.planner_preference =
            source.contains("WSPR")
                ? parse_wspr_planner_preference(source.at("WSPR"))
                : target.wspr_planner_preference;
        target.qrss.message =
            source.contains("QRSS") && source.at("QRSS").contains("Message")
                ? source.at("QRSS").at("Message").get<std::string>()
                : target.qrss.message;
        target.qrss.frequency_hz =
            source.contains("QRSS") && source.at("QRSS").contains("Frequency")
                ? source.at("QRSS").at("Frequency").get<double>()
                : target.qrss.frequency_hz;
        target.qrss.dot_seconds =
            source.contains("QRSS") && source.at("QRSS").contains("Dot Seconds")
                ? source.at("QRSS").at("Dot Seconds").get<double>()
                : target.qrss.dot_seconds;
        target.fskcw.message =
            source.contains("FSKCW") && source.at("FSKCW").contains("Message")
                ? source.at("FSKCW").at("Message").get<std::string>()
                : target.fskcw.message;
        target.fskcw.mark_frequency_hz =
            source.contains("FSKCW") && source.at("FSKCW").contains("Mark Frequency")
                ? source.at("FSKCW").at("Mark Frequency").get<double>()
                : target.fskcw.mark_frequency_hz;
        target.fskcw.space_frequency_hz =
            source.contains("FSKCW") && source.at("FSKCW").contains("Space Frequency")
                ? source.at("FSKCW").at("Space Frequency").get<double>()
                : target.fskcw.space_frequency_hz;
        target.fskcw.dot_seconds =
            source.contains("FSKCW") && source.at("FSKCW").contains("Dot Seconds")
                ? source.at("FSKCW").at("Dot Seconds").get<double>()
                : target.fskcw.dot_seconds;
        target.dfcw.message =
            source.contains("DFCW") && source.at("DFCW").contains("Message")
                ? source.at("DFCW").at("Message").get<std::string>()
                : target.dfcw.message;
        target.dfcw.dot_frequency_hz =
            source.contains("DFCW") && source.at("DFCW").contains("Dot Frequency")
                ? source.at("DFCW").at("Dot Frequency").get<double>()
                : target.dfcw.dot_frequency_hz;
        target.dfcw.dash_frequency_hz =
            source.contains("DFCW") && source.at("DFCW").contains("Dash Frequency")
                ? source.at("DFCW").at("Dash Frequency").get<double>()
                : target.dfcw.dash_frequency_hz;
        target.dfcw.dot_seconds =
            source.contains("DFCW") && source.at("DFCW").contains("Dot Seconds")
                ? source.at("DFCW").at("Dot Seconds").get<double>()
                : target.dfcw.dot_seconds;

        target.callsign = target.wspr.callsign;
        target.grid_square = target.wspr.grid_square;
        target.power_dbm = target.wspr.power_dbm;
        target.frequencies = target.wspr.frequencies;
        target.wspr_audio_offset_hz = target.wspr.audio_offset_hz;
        target.wspr_planner_preference = target.wspr.planner_preference;
        target.use_led = source.at("Extended").at("Use LED").get<bool>();
        target.led_pin = source.at("Extended").at("LED Pin").get<int>();
        target.power_level = source.at("Extended").at("Power Level").get<int>();

        target.web_port = source.at("Server").at("Web Port").get<int>();
        target.socket_port = source.at("Server").at("Socket Port").get<int>();
        target.use_shutdown = source.at("Server").at("Use Shutdown").get<bool>();
        target.shutdown_pin = source.at("Server").at("Shutdown Button").get<int>();
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
        target["Meta"]["Planner Preference"] =
            wspr_planner_preference_to_string(source.wspr_planner_preference);
        target["Meta"]["Loop TX"] = source.loop_tx;
        target["Meta"]["TX Iterations"] = source.tx_iterations.load();
        target["Meta"]["WSPR Dial Frequency Set"] = source.wspr_dial_freq_set;
        target["Meta"]["Center Frequency Set"] = source.wspr_dial_freq_set;
        target["Modulation"]["Dot Seconds"] = source.modulation_dot_seconds;
        target["Modulation"]["FSK Offset Hz"] = source.modulation_fsk_offset_hz;
        target["Schedule"]["Start Minute"] = source.schedule_start_minute;
        target["Schedule"]["Repeat Minutes"] = source.schedule_repeat_minutes;

        target["Control"]["Transmit"] = source.transmit;

        target["Common"]["Call Sign"] = source.callsign;
        target["Common"]["Grid Square"] = source.grid_square;
        target["Common"]["TX Power"] = source.power_dbm;
        target["Common"]["Frequency"] = source.frequencies;
        target["Common"]["Transmit Pin"] = source.tx_pin;
        target["WSPR"]["Call Sign"] = source.wspr.callsign;
        target["WSPR"]["Grid Square"] = source.wspr.grid_square;
        target["WSPR"]["TX Power"] = source.wspr.power_dbm;
        target["WSPR"]["Frequency"] = source.wspr.frequencies;
        target["WSPR"]["Audio Offset Hz"] = source.wspr.audio_offset_hz;
        target["WSPR"]["Planner Preference"] =
            wspr_planner_preference_to_string(source.wspr.planner_preference);
        target["QRSS"]["Message"] = source.qrss.message;
        target["QRSS"]["Frequency"] = source.qrss.frequency_hz;
        target["QRSS"]["Dot Seconds"] = source.qrss.dot_seconds;
        target["FSKCW"]["Message"] = source.fskcw.message;
        target["FSKCW"]["Mark Frequency"] = source.fskcw.mark_frequency_hz;
        target["FSKCW"]["Space Frequency"] = source.fskcw.space_frequency_hz;
        target["FSKCW"]["Dot Seconds"] = source.fskcw.dot_seconds;
        target["DFCW"]["Message"] = source.dfcw.message;
        target["DFCW"]["Dot Frequency"] = source.dfcw.dot_frequency_hz;
        target["DFCW"]["Dash Frequency"] = source.dfcw.dash_frequency_hz;
        target["DFCW"]["Dot Seconds"] = source.dfcw.dot_seconds;

        target["Extended"]["PPM"] = source.ppm;
        target["Extended"]["Use NTP"] = source.use_ntp;
        target["Extended"]["Offset"] = source.use_offset;
        target["Extended"]["WSPR Audio Offset Hz"] = source.wspr_audio_offset_hz;
        target["Extended"]["Use LED"] = source.use_led;
        target["Extended"]["LED Pin"] = source.led_pin;
        target["Extended"]["Power Level"] = source.power_level;
        target["Extended"]["Frequency Control GPIO Polarity"] =
            source.tx_freq_control_active_high;

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
        target.wspr_dial_frequency_entries = source.wspr_dial_frequency_entries;
        target.tx_freq_control_active_high = source.tx_freq_control_active_high;
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
            candidate_config.tx_freq_control_active_high =
                config.tx_freq_control_active_high;

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
            section_name != "Control" &&
            section_name != "Common" &&
            section_name != "Extended" &&
            section_name != "Server" &&
            section_name != "Modulation" &&
            section_name != "Schedule" &&
            section_name != "WSPR" &&
            section_name != "QRSS" &&
            section_name != "FSKCW" &&
            section_name != "DFCW" &&
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
        candidate_config.tx_freq_control_active_high =
            config.tx_freq_control_active_high;

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
