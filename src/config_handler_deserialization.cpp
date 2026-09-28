/**
 * @file config_handler_deserialization.cpp
 * @brief Deserializes the JSON configuration model into runtime configuration.
 */

#include "wtp_settings_json.hpp"
#include "config_handler_deserialization.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "band_lookup.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace config_handler_deserialization
{
namespace
{
constexpr double kManualPpmMin = -200.0;
constexpr double kManualPpmMax = 200.0;

WsprPlannerPreference parse_wspr_planner_preference(
    const nlohmann::json &wspr)
{
    const std::string raw =
        config_handler_serialization::config_serialization_trim(wspr.value("Planner Preference", std::string("auto")));
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

std::string parse_wspr_frequency_profile(const nlohmann::json &wspr)
{
    const std::string raw =
        config_handler_serialization::config_serialization_trim(wspr.value("Frequency Profile", std::string("existing_common")));
    std::string lowered = raw;
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(lowered.begin(), lowered.end(), '-', '_');

    if (lowered.empty() || lowered == "existing_common" ||
        lowered == "existing/common")
        return "existing_common";
    if (lowered == "wrc15" || lowered == "wrc_15")
        return "wrc15";

    throw std::runtime_error(
        "Invalid WSPR.Frequency Profile '" + raw +
        "'. Expected existing_common or wrc15.");
}

TransmitBackendKind parse_transmit_backend_kind(
    const nlohmann::json &operation)
{
    const std::string raw =
        config_handler_serialization::config_serialization_trim(operation.value("Transmit Backend", std::string("gpio")));
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
    {
        return TransmitBackendKind::GPIO;
    }
    if (lowered == "wtp") return TransmitBackendKind::WTP;
    if (lowered == "si5351")
    {
        return TransmitBackendKind::SI5351;
    }
    if (lowered == "rp1-gpclk")
    {
        return TransmitBackendKind::RP1_GPCLK;
    }
    if (lowered == "simulated")
        throw std::runtime_error(
            "Operation.Transmit Backend 'simulated' is transient and cannot be persisted.");

    throw std::runtime_error(
        "Invalid Operation.Transmit Backend. Expected 'gpio', 'rp1-gpclk', 'si5351', or 'wtp'; simulated is CLI-only.");
}

EnableOnBootBehavior parse_enable_on_boot_behavior(
    const nlohmann::json &operation)
{
    if (!operation.contains("Enable on Boot"))
    {
        return EnableOnBootBehavior::Never;
    }

    const std::string raw =
        config_handler_serialization::config_serialization_trim(operation.at("Enable on Boot").get<std::string>());
    std::string lowered = raw;
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (lowered == "never")
        return EnableOnBootBehavior::Never;
    if (lowered == "follow")
        return EnableOnBootBehavior::Follow;
    if (lowered == "always")
        return EnableOnBootBehavior::Always;

    throw std::runtime_error(
        "Invalid Operation.Enable on Boot. Expected 'Never', 'Follow', or 'Always'.");
}

int parse_strict_integer_config_value(
    const nlohmann::json &source,
    const std::string &context)
{
    if (!source.is_number_integer() && !source.is_number_unsigned())
    {
        throw std::runtime_error(context + " must be an integer.");
    }
    return config_handler_serialization::config_serialization_integer(source, context);
}

double parse_manual_ppm_value(
    const nlohmann::json &source,
    const std::string &context)
{
    double ppm = 0.0;
    if (source.is_number())
    {
        ppm = source.get<double>();
    }
    else if (source.is_string())
    {
        const std::string raw = config_handler_serialization::config_serialization_trim(source.get<std::string>());
        std::size_t consumed = 0;
        try
        {
            ppm = std::stod(raw, &consumed);
        }
        catch (const std::invalid_argument &)
        {
            throw std::runtime_error(context + " must be a number.");
        }
        catch (const std::out_of_range &)
        {
            throw std::runtime_error(context + " is out of range.");
        }

        if (consumed != raw.size())
        {
            throw std::runtime_error(context + " must be a number.");
        }
    }
    else
    {
        throw std::runtime_error(context + " must be a number.");
    }

    if (!std::isfinite(ppm))
    {
        throw std::runtime_error(context + " must be a finite number.");
    }

    const double clamped_ppm = std::clamp(ppm, kManualPpmMin, kManualPpmMax);
    if (ppm != clamped_ppm)
    {
        llog.logS(
            WARN,
            context,
            " is outside bounds (-200 to 200), applying clamped value: ",
            clamped_ppm);
    }

    return clamped_ppm;
}

double parse_gpio_ppm_value(
    const nlohmann::json &source,
    const std::string &context)
{
    double ppm = 0.0;
    if (source.is_number())
    {
        ppm = source.get<double>();
    }
    else if (source.is_string())
    {
        const std::string raw = config_handler_serialization::config_serialization_trim(source.get<std::string>());
        std::size_t consumed = 0;
        try
        {
            ppm = std::stod(raw, &consumed);
        }
        catch (const std::exception &)
        {
            throw std::runtime_error(context + " must be a number.");
        }
        if (consumed != raw.size())
        {
            throw std::runtime_error(context + " must be a number.");
        }
    }
    else
    {
        throw std::runtime_error(context + " must be a number.");
    }

    if (!std::isfinite(ppm))
    {
        throw std::runtime_error(context + " must be a finite number.");
    }
    if (ppm < kManualPpmMin || ppm > kManualPpmMax)
    {
        throw std::runtime_error(context + " must be within -200 to 200 PPM.");
    }
    if (context == "GPIO.Manual PPM" && std::fpclassify(ppm) == FP_SUBNORMAL)
    {
        throw std::runtime_error(
            "GPIO.Manual PPM is too small to use. Enter 0 or a measured value.");
    }
    return ppm;
}

double parse_cw_base_frequency_value(
    const nlohmann::json &source,
    const std::string &context)
{
    auto normalize_frequency_hz = [&](double value) -> double
    {
        if (!std::isfinite(value) || value <= 0.0)
        {
            throw std::runtime_error(
                context +
                " must be a positive whole-number Hz value or a value with Hz, kHz, MHz, or GHz.");
        }

        const double rounded = std::round(value);
        if (std::fabs(value - rounded) > 1e-6)
        {
            throw std::runtime_error(
                context +
                " must resolve to a whole-number frequency in Hz.");
        }

        return rounded;
    };

    if (source.is_number())
    {
        return normalize_frequency_hz(source.get<double>());
    }

    if (source.is_string())
    {
        const std::string raw = config_handler_serialization::config_serialization_trim(source.get<std::string>());
        if (raw.empty())
        {
            throw std::runtime_error(
                context +
                " must be a whole-number Hz value or a value with Hz, kHz, MHz, or GHz.");
        }

        const std::size_t unit_start = raw.find_first_not_of("0123456789.");
        const std::string numeric_part =
            unit_start == std::string::npos ? raw : raw.substr(0, unit_start);
        const std::string unit_part =
            unit_start == std::string::npos ? std::string() : config_handler_serialization::config_serialization_trim(raw.substr(unit_start));

        if (numeric_part.empty())
        {
            throw std::runtime_error(
                context +
                " must start with a numeric frequency value.");
        }

        double value = 0.0;
        std::size_t consumed = 0;
        try
        {
            value = std::stod(numeric_part, &consumed);
        }
        catch (const std::invalid_argument &)
        {
            throw std::runtime_error(
                context +
                " must be a whole-number Hz value or a value with Hz, kHz, MHz, or GHz.");
        }
        catch (const std::out_of_range &)
        {
            throw std::runtime_error(context + " is out of range.");
        }

        if (consumed != numeric_part.size())
        {
            throw std::runtime_error(
                context +
                " must be a whole-number Hz value or a value with Hz, kHz, MHz, or GHz.");
        }

        std::string lowered_unit = unit_part;
        std::transform(
            lowered_unit.begin(),
            lowered_unit.end(),
            lowered_unit.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (lowered_unit.empty())
        {
            if (numeric_part.find('.') != std::string::npos)
            {
                const std::size_t decimal_point = numeric_part.find('.');
                const std::string fractional_part = numeric_part.substr(decimal_point + 1);
                const bool zero_fraction =
                    !fractional_part.empty() &&
                    std::all_of(
                        fractional_part.begin(),
                        fractional_part.end(),
                        [](char c)
                        {
                            return c == '0';
                        });
                if (!zero_fraction)
                {
                    throw std::runtime_error(
                        context +
                        " must use Hz, kHz, MHz, or GHz when the value includes a decimal point.");
                }
            }
            return normalize_frequency_hz(value);
        }

        if (lowered_unit == "ghz")
        {
            value *= 1e9;
        }
        else if (lowered_unit == "mhz")
        {
            value *= 1e6;
        }
        else if (lowered_unit == "khz")
        {
            value *= 1e3;
        }
        else if (lowered_unit != "hz")
        {
            throw std::runtime_error(
                context +
                " must use a supported unit suffix: Hz, kHz, MHz, or GHz.");
        }

        return normalize_frequency_hz(value);
    }

    throw std::runtime_error(
        context +
        " must be a whole-number Hz value or a value with Hz, kHz, MHz, or GHz.");
}

int parse_si5351_tx_output(const nlohmann::json &source)
{
    if (source.is_number_integer() || source.is_number_unsigned())
    {
        return config_handler_serialization::config_serialization_integer(
            source,
            "Si5351.TX Output");
    }

    const std::string raw =
        config_handler_serialization::config_serialization_trim(source.get<std::string>());
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

std::string parse_si5351_reference_source(const nlohmann::json &source)
{
    std::string value = config_handler_serialization::config_serialization_trim(source.get<std::string>());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "external_tcxo" || value == "crystal") return value;
    throw std::runtime_error(
        "Invalid Si5351.Reference Source. Expected external_tcxo or crystal.");
}

ModeType parse_mode_type(const nlohmann::json &operation)
{
    if (!operation.contains("Mode"))
    {
        throw std::runtime_error("Missing Operation.Mode.");
    }

    const std::string raw =
        config_handler_serialization::config_serialization_trim(operation.at("Mode").get<std::string>());
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

std::string parse_cw_fade_shape(const nlohmann::json &cw)
{
    std::string raw = config_handler_serialization::config_serialization_trim(cw.value("Fade Shape", std::string("none")));
    std::transform(
        raw.begin(),
        raw.end(),
        raw.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    std::replace(raw.begin(), raw.end(), '-', '_');

    if (raw.empty() || raw == "none" || raw == "linear" || raw == "raised_cosine")
    {
        return raw.empty() ? std::string("none") : raw;
    }

    throw std::runtime_error(
        "Invalid CW.Fade Shape. Expected none, linear, or raised_cosine.");
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

std::string parse_cw_message_value(const nlohmann::json &value)
{
    if (value.is_string())
    {
        return config_handler_serialization::config_serialization_trim(value.get<std::string>());
    }

    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return value.dump();
    }

    throw std::runtime_error(
        "Invalid CW.Message. Expected a string or integer number.");
}


} // namespace

void deserialize_json_to_runtime_config(const nlohmann::json &source, ArgParserConfig &target)
{
    default_band_gpio_config(target.band_gpio);
    target.enable_web = true;

    target.use_ini = source.at("Meta").at("Use INI").get<bool>();
    target.ini_filename = source.at("Meta").at("INI Filename").get<std::string>();
    target.date_time_log = source.at("Meta").at("Date Time Log").get<bool>();
    target.debug_logging =
        source.at("Meta").value("debug_logging", false);
    target.mode = parse_mode_type(source.at("Operation"));
    target.wspr_planner_preference =
        parse_wspr_planner_preference(source.at("WSPR"));
    target.loop_tx = source.at("Meta").at("Loop TX").get<bool>();
    target.tx_iterations.store(source.at("Meta").at("TX Iterations").get<int>());
    target.wspr_dial_freq_set.clear();

    target.transmit = source.at("Operation").at("Transmit").get<bool>();
    target.enable_on_boot =
        parse_enable_on_boot_behavior(source.at("Operation"));
    target.transmit_backend =
        parse_transmit_backend_kind(source.at("Operation"));
    target.wtp = parse_wtp_settings(source.value("WTP", nlohmann::json::object()),
                                    target.transmit_backend == TransmitBackendKind::WTP);
    const nlohmann::json gpio =
        source.contains("GPIO") ? source.at("GPIO") : nlohmann::json::object();
    target.gpio_tx_pin =
        gpio.contains("Transmit Pin")
            ? gpio.at("Transmit Pin").get<int>()
            : kDefaultTransmitGpio;
    if (transmit_backend_uses_gpio_output(target.transmit_backend))
    {
        target.gpio_tx_pin = config_handler_serialization::config_serialization_gpio_transmit_pin(target.gpio_tx_pin);
    }
    target.gpio_power_level =
        gpio.contains("Power Level")
            ? gpio.at("Power Level").get<int>()
            : 7;
    target.rp1_gpio_drive_ma =
        gpio.contains("RP1 Drive mA")
            ? gpio.at("RP1 Drive mA").get<int>()
            : kDefaultRp1GpioDriveMa;
    if (!is_supported_rp1_gpio_drive_ma(target.rp1_gpio_drive_ma))
    {
        throw std::runtime_error(
            "GPIO.RP1 Drive mA must be 2, 4, 8, or 12.");
    }
    target.gpio_use_system_clock_frequency_estimate =
        gpio.contains("Use System Clock Frequency Estimate")
            ? gpio.at("Use System Clock Frequency Estimate").get<bool>()
            : true;
    target.gpio_frequency_residual_ppm = parse_gpio_ppm_value(
        gpio.value("Frequency Residual PPM", 0.0),
        "GPIO.Frequency Residual PPM");
    target.gpio_manual_ppm = parse_gpio_ppm_value(
        gpio.value("Manual PPM", 0.0),
        "GPIO.Manual PPM");
    const nlohmann::json si5351 =
        source.contains("Si5351") ? source.at("Si5351") : nlohmann::json::object();
    target.si5351_i2c_bus =
        si5351.contains("I2C Bus")
            ? config_handler_serialization::config_serialization_integer(si5351.at("I2C Bus"), "Si5351.I2C Bus")
            : kDefaultSi5351I2cBus;
    target.si5351_i2c_address =
        si5351.contains("I2C Address")
            ? config_handler_serialization::config_serialization_integer(si5351.at("I2C Address"), "Si5351.I2C Address", 0)
            : kDefaultSi5351I2cAddress;
    target.si5351_reference_hz =
        si5351.contains("Reference Frequency")
            ? config_handler_serialization::config_serialization_integer(
                  si5351.at("Reference Frequency"),
                  "Si5351.Reference Frequency")
            : kDefaultSi5351ReferenceHz;
    target.si5351_reference_source = si5351.contains("Reference Source")
        ? parse_si5351_reference_source(si5351.at("Reference Source"))
        : kDefaultSi5351ReferenceSource;
    target.si5351_crystal_load_capacitance_pf =
        si5351.contains("Crystal Load Capacitance")
            ? config_handler_serialization::config_serialization_integer(si5351.at("Crystal Load Capacitance"),
                                         "Si5351.Crystal Load Capacitance")
            : kDefaultSi5351CrystalLoadCapacitancePf;
    target.si5351_tx_output =
        si5351.contains("TX Output")
            ? parse_si5351_tx_output(si5351.at("TX Output"))
            : kDefaultSi5351TxOutput;
    target.si5351_power_level =
        si5351.contains("Power Level")
            ? si5351.at("Power Level").get<int>()
            : 1;
    target.si5351_ppm = parse_manual_ppm_value(
        source.at("Calibration").at("PPM"),
        "Calibration.PPM");
    resolve_backend_specific_config(target);
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
    target.cw_intra_element_gap =
        source.contains("CW") &&
                source.at("CW").contains("Intra Element Gap")
            ? source.at("CW").at("Intra Element Gap").get<double>()
            : target.cw_intra_element_gap;
    target.cw_inter_character_gap =
        source.contains("CW") &&
                source.at("CW").contains("Inter Character Gap")
            ? source.at("CW").at("Inter Character Gap").get<double>()
            : target.cw_inter_character_gap;
    target.cw_inter_word_gap =
        source.contains("CW") &&
                source.at("CW").contains("Inter Word Gap")
            ? source.at("CW").at("Inter Word Gap").get<double>()
            : target.cw_inter_word_gap;
    target.dfcw_intra_element_gap =
        source.contains("CW") &&
                source.at("CW").contains("DFCW Intra Element Gap")
            ? source.at("CW").at("DFCW Intra Element Gap").get<double>()
            : kDefaultDfcwIntraElementGap;
    target.dfcw_inter_character_gap =
        source.contains("CW") &&
                source.at("CW").contains("DFCW Inter Character Gap")
            ? source.at("CW").at("DFCW Inter Character Gap").get<double>()
            : kDefaultDfcwInterCharacterGap;
    target.dfcw_inter_word_gap =
        source.contains("CW") &&
                source.at("CW").contains("DFCW Inter Word Gap")
            ? source.at("CW").at("DFCW Inter Word Gap").get<double>()
            : kDefaultDfcwInterWordGap;
    target.cw_fade_shape =
        source.contains("CW") ? parse_cw_fade_shape(source.at("CW")) : "none";
    target.cw_fade_in_ms =
        source.contains("CW") &&
                source.at("CW").contains("Fade In Ms")
            ? source.at("CW").at("Fade In Ms").get<int>()
            : target.cw_fade_in_ms;
    target.cw_fade_out_ms =
        source.contains("CW") &&
                source.at("CW").contains("Fade Out Ms")
            ? source.at("CW").at("Fade Out Ms").get<int>()
            : target.cw_fade_out_ms;
    target.cw_fade_slice_ms =
        source.contains("CW") &&
                source.at("CW").contains("Fade Slice Ms")
            ? source.at("CW").at("Fade Slice Ms").get<int>()
            : target.cw_fade_slice_ms;
    target.schedule_start_minute =
        source.contains("CW") &&
                source.at("CW").contains("Start Minute")
            ? source.at("CW").at("Start Minute").get<int>()
            : target.schedule_start_minute;
    target.schedule_start_second =
        source.contains("CW") &&
                source.at("CW").contains("Start Second")
            ? parse_strict_integer_config_value(
                  source.at("CW").at("Start Second"),
                  "CW.Start Second")
            : 5;
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
    target.wspr.frequency_profile =
        parse_wspr_frequency_profile(source.at("WSPR"));
    target.wspr.band_preferences.clear();
    const auto &band_preferences = source.at("WSPR").value(
        "Band Preferences", nlohmann::json::object());
    if (!band_preferences.is_object())
        throw std::runtime_error("WSPR.Band Preferences must be an object.");
    BandLookup preference_lookup;
    for (const auto &item : band_preferences.items())
    {
        std::string band = config_handler_serialization::config_serialization_trim(item.key());
        std::transform(
            band.begin(), band.end(), band.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (item.value().is_string())
        {
            const std::string preset = item.value().get<std::string>();
            if (band == "60m" && preset.find(':') == std::string::npos)
                throw std::runtime_error(
                    "WSPR band preference for 60m must use a qualified preset.");
            const double frequency =
                preference_lookup.parse_string_to_frequency(preset, false);
            const auto correlated = preference_lookup.lookup_ham_band(frequency);
            if (!correlated || band_to_string(*correlated) != band)
                throw std::runtime_error(
                    "WSPR band preference for " + band +
                    " must resolve within that band.");
            target.wspr.band_preferences[band] = preset;
            continue;
        }
        if (!item.value().is_number_unsigned() &&
            !item.value().is_number_integer())
            throw std::runtime_error(
                "WSPR.Band Preferences values must be preset strings or integral Hz values.");
        if (item.value().is_number_integer() &&
            !item.value().is_number_unsigned() &&
            item.value().get<std::int64_t>() <= 0)
            throw std::runtime_error(
                "WSPR band preference frequencies must be positive integral Hz values.");
        const auto frequency = item.value().get<std::uint64_t>();
        if (frequency == 0)
            throw std::runtime_error(
                "WSPR band preference frequencies must be positive integral Hz values.");
        const auto correlated = preference_lookup.lookup_ham_band(
            static_cast<double>(frequency));
        if (!correlated || band_to_string(*correlated) != band)
            throw std::runtime_error(
                "WSPR band preference for " + band +
                " must resolve within that band.");
        target.wspr.band_preferences[band] = frequency;
    }
    target.wspr.audio_offset_hz =
        WSPR_AUDIO_OFFSET_HZ;
    target.wspr.planner_preference =
        parse_wspr_planner_preference(source.at("WSPR"));
    const auto &cw = source.at("CW");
    const std::string cw_message =
        cw.contains("Message")
            ? parse_cw_message_value(cw.at("Message"))
            : std::string();
    const double cw_base_frequency_hz =
        cw.contains("Base Frequency")
            ? parse_cw_base_frequency_value(cw.at("Base Frequency"), "CW.Base Frequency")
            : 14096900.0;
    const double cw_shift_hz =
        cw.value("Shift Hz", target.modulation_fsk_offset_hz);
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
    target.use_led = source.at("Operation").at("Use LED").get<bool>();
    target.led_pin = source.at("Operation").at("LED Pin").get<int>();
    target.amp_pin =
        source.at("Operation").contains("Amp Pin")
            ? config_handler_serialization::config_serialization_integer(
                  source.at("Operation").at("Amp Pin"),
                  "Operation.Amp Pin")
            : -1;
    target.use_amp =
        source.at("Operation").contains("Use Amp")
            ? source.at("Operation").value("Use Amp", false)
            : target.amp_pin >= 0;
    if (target.transmit_backend == TransmitBackendKind::WTP && target.use_amp)
        throw std::runtime_error("Pico WTP requires amplifier GPIO control disabled.");
    if (target.amp_pin < 0)
    {
        target.use_amp = false;
    }
    target.amp_pin_active_high =
        source.at("Operation").value("Amp Pin Active High", false);

    target.web_port = source.at("Operation").at("Web Port").get<int>();
    target.socket_port = source.at("Operation").at("Socket Port").get<int>();
    target.use_shutdown = source.at("Operation").at("Use Shutdown").get<bool>();
    target.shutdown_pin = source.at("Operation").at("Shutdown Button").get<int>();
    target.use_journald = false;
    const auto experimental =
        source.value("Experimental", nlohmann::json::object());
    target.allow_unqualified_frequency =
        experimental.value("Allow Unqualified Frequency", false);
    target.allow_non_amateur_frequency =
        experimental.value("Allow Non-Amateur Frequency", false);

    // Missing Band GPIO data is allowed; explicit disabled defaults stay in place.
    const auto band_gpio_section_it = source.find("Band GPIO");
    if (band_gpio_section_it == source.end() || !band_gpio_section_it->is_object())
    {
        return;
    }

    for (const auto &[band, band_name] : config_handler_serialization::band_json_keys())
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

} // namespace config_handler_deserialization
