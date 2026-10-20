/**
 * @file config_handler_serialization.cpp
 * @brief Serializes accepted runtime configuration into its JSON model.
 */

#include "wtp_settings_json.hpp"
#include "wtp_server_settings_json.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "arg_parser.hpp"
#include "i2c_bus_inventory.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace config_handler_serialization
{
void serialize_runtime_config_to_json(
    const ArgParserConfig &source,
    nlohmann::json &target)
{
    target["Meta"]["Use INI"] = source.use_ini;
    target["Meta"]["INI Filename"] = source.ini_filename;
    target["Meta"]["Date Time Log"] = source.date_time_log;
    target["Meta"]["debug_logging"] = source.debug_logging;
    target["Meta"]["Loop TX"] = source.loop_tx;
    target["Meta"]["TX Iterations"] = source.tx_iterations.load();

    target["Operation"]["Mode"] = config_serialization_mode_name(
        source.mode == ModeType::TONE ? ModeType::WSPR : source.mode);
    target["Operation"]["Transmit"] = source.transmit;
    target["WTP"] = wtp_settings_json(source.wtp);
    target["WTP Server"] = wtp_server_settings_json(source.wtp_server);
    if (source.transmit_backend != TransmitBackendKind::SIMULATED)
        target["Operation"]["Transmit Backend"] =
            transmit_backend_kind_to_string(source.transmit_backend);
    else if (!target["Operation"].contains("Transmit Backend"))
        target["Operation"]["Transmit Backend"] = "gpio";
    target["Operation"]["Enable on Boot"] =
        enable_on_boot_behavior_to_string(source.enable_on_boot);
    target["Operation"]["Use LED"] = source.use_led;
    target["Operation"]["LED Pin"] = source.led_pin;
    const bool use_amp =
        source.use_amp && source.amp_pin >= 0 && source.amp_pin <= 27;
    target["Operation"]["Use Amp"] = use_amp;
    target["Operation"]["Amp Pin"] = source.amp_pin;
    target["Operation"]["Amp Pin Active High"] = source.amp_pin_active_high;
    target["Operation"]["Web Port"] = source.web_port;
    target["Operation"]["Socket Port"] = source.socket_port;
    target["Operation"]["Use Shutdown"] = source.use_shutdown;
    target["Operation"]["Shutdown Button"] = source.shutdown_pin;
    target["Experimental"]["Allow Unqualified Frequency"] =
        source.allow_unqualified_frequency;
    target["Experimental"]["Allow Non-Amateur Frequency"] =
        source.allow_non_amateur_frequency;

    target["GPIO"]["Transmit Pin"] =
        config_serialization_gpio_transmit_pin(source.gpio_tx_pin);
    target["GPIO"]["Power Level"] = source.gpio_power_level;
    target["GPIO"]["RP1 Drive mA"] = source.rp1_gpio_drive_ma;
    target["GPIO"]["Use System Clock Frequency Estimate"] =
        source.gpio_use_system_clock_frequency_estimate;
    target["GPIO"]["Frequency Residual PPM"] =
        source.gpio_frequency_residual_ppm;
    target["GPIO"]["Manual PPM"] = source.gpio_manual_ppm;

    target["Calibration"]["PPM"] = source.si5351_ppm;

    target["Si5351"]["I2C Bus"] = source.si5351_i2c_bus;
    target["Si5351"]["I2C Address"] =
        config_serialization_si5351_i2c_address(source.si5351_i2c_address);
    target["Si5351"]["Reference Frequency"] = source.si5351_reference_hz;
    target["Si5351"]["Reference Source"] = source.si5351_reference_source;
    target["Si5351"]["Crystal Load Capacitance"] =
        source.si5351_crystal_load_capacitance_pf;
    target["Si5351"]["TX Output"] =
        std::string("CLK") + std::to_string(source.si5351_tx_output);
    target["Si5351"]["Power Level"] = source.si5351_power_level;

    target["WSPR"]["Call Sign"] = source.wspr.callsign;
    target["WSPR"]["Grid Square"] = source.wspr.grid_square;
    target["WSPR"]["TX Power"] = source.wspr.power_dbm;
    target["WSPR"]["Frequency"] = source.wspr.frequencies;
    target["WSPR"]["Frequency Profile"] = source.wspr.frequency_profile;
    nlohmann::json serialized_band_preferences = nlohmann::json::object();
    for (const auto &[band, preference] : source.wspr.band_preferences)
    {
        if (const auto *preset = std::get_if<std::string>(&preference))
            serialized_band_preferences[band] = *preset;
        else
            serialized_band_preferences[band] = std::get<std::uint64_t>(preference);
    }
    target["WSPR"]["Band Preferences"] = std::move(serialized_band_preferences);
    target["WSPR"]["Planner Preference"] =
        wspr_planner_preference_to_string(source.wspr.planner_preference);
    target["WSPR"]["Use Random Offset"] = source.use_offset;

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
    if (!std::isfinite(cw_base_frequency_hz) || cw_base_frequency_hz <= 0.0)
        cw_base_frequency_hz = 14096900.0;

    target["CW"]["Message"] = cw_message;
    target["CW"]["Base Frequency"] = cw_base_frequency_hz;
    target["CW"]["Shift Hz"] = cw_shift_hz;
    target["CW"]["Dot Seconds"] = source.modulation_dot_seconds;
    target["CW"]["Intra Element Gap"] = source.cw_intra_element_gap;
    target["CW"]["Inter Character Gap"] = source.cw_inter_character_gap;
    target["CW"]["Inter Word Gap"] = source.cw_inter_word_gap;
    target["CW"]["DFCW Intra Element Gap"] = source.dfcw_intra_element_gap;
    target["CW"]["DFCW Inter Character Gap"] = source.dfcw_inter_character_gap;
    target["CW"]["DFCW Inter Word Gap"] = source.dfcw_inter_word_gap;
    target["CW"]["Fade Shape"] = source.cw_fade_shape;
    target["CW"]["Fade In Ms"] = source.cw_fade_in_ms;
    target["CW"]["Fade Out Ms"] = source.cw_fade_out_ms;
    target["CW"]["Fade Slice Ms"] = source.cw_fade_slice_ms;
    target["CW"]["Start Minute"] = source.schedule_start_minute;
    target["CW"]["Start Second"] = source.schedule_start_second;
    target["CW"]["Repeat Minutes"] = source.schedule_repeat_minutes;

    for (const auto &[band, band_name] : band_json_keys())
    {
        const BandGPIOConfig &band_config = source.band_gpio[ham_band_index(band)];
        target["Band GPIO"][band_name]["GPIO"] = band_config.gpio;
        target["Band GPIO"][band_name]["Enabled"] = band_config.enabled;
        target["Band GPIO"][band_name]["Active High"] = band_config.active_high;
    }
}

nlohmann::json public_config_from_internal_json(const nlohmann::json &source)
{
    std::string gpio_support_error;
    const bool runtime_gpio_clock_transmission_supported =
        platform_supports_gpio_clock_transmission(&gpio_support_error);
    const bool rp1_gpio_operator_visible = operator_exposes_rp1_gpio();
    const bool gpio_clock_transmission_supported =
        runtime_gpio_clock_transmission_supported &&
        (get_raspberry_pi_generation() != 5 || rp1_gpio_operator_visible);
    if (get_raspberry_pi_generation() == 5 &&
        rp1_gpio_operator_visible &&
        !runtime_gpio_clock_transmission_supported)
    {
        gpio_support_error =
            "The canonical RP1 GPCLK provider is unavailable. Review the "
            "RP1 clock route status below; transmission remains disabled.";
    }
    else if (runtime_gpio_clock_transmission_supported &&
             !gpio_clock_transmission_supported)
    {
        gpio_support_error =
            "GPIO controls on Raspberry Pi 5 require a compatible RP1 route "
            "controller and an active canonical provider. Transmission remains "
            "disabled until both are available.";
    }
    bool si5351_detected = true;
    std::string si5351_detection_error;
    Si5351AddressInventory si5351_address_inventory;
    if (source.contains("Si5351") && source.at("Si5351").is_object())
    {
        const nlohmann::json &si5351 = source.at("Si5351");
        const int i2c_bus =
            si5351.contains("I2C Bus")
                ? config_serialization_integer(si5351.at("I2C Bus"), "Si5351.I2C Bus")
                : kDefaultSi5351I2cBus;
        const int i2c_address =
            si5351.contains("I2C Address")
                ? config_serialization_integer(
                      si5351.at("I2C Address"),
                      "Si5351.I2C Address",
                      0)
                : kDefaultSi5351I2cAddress;
        const int reference_hz =
            si5351.contains("Reference Frequency")
                ? config_serialization_integer(
                      si5351.at("Reference Frequency"),
                      "Si5351.Reference Frequency")
                : kDefaultSi5351ReferenceHz;
        si5351_address_inventory = cached_si5351_addresses(
            i2c_bus, reference_hz);
        si5351_detected = si5351_address_inventory.contains(i2c_address);
        if (!si5351_detected)
        {
            si5351_detection_error = si5351_address_inventory.error.empty()
                ? "No register-compatible Si5351 device was detected at the configured I2C address."
                : si5351_address_inventory.error;
        }
    }

    nlohmann::json public_json;
    public_json["WTP"] = source.value("WTP", wtp_settings_json(WtpSettings{}));
    public_json["WTP Server"] =
        source.value("WTP Server", wtp_server_settings_json(WtpServerSettings{}));
    public_json["Operation"] = source.at("Operation");
    public_json["GPIO"] = source.at("GPIO");
    public_json["Calibration"] = source.at("Calibration");
    public_json["Si5351"] = source.at("Si5351");
    public_json["WSPR"] = {
        {"Call Sign", source.at("WSPR").at("Call Sign")},
        {"Grid Square", source.at("WSPR").at("Grid Square")},
        {"TX Power", source.at("WSPR").at("TX Power")},
        {"Frequency", source.at("WSPR").at("Frequency")},
        {"Frequency Profile", source.at("WSPR").at("Frequency Profile")},
        {"Band Preferences", source.at("WSPR").at("Band Preferences")},
        {"Planner Preference", source.at("WSPR").at("Planner Preference")},
        {"Use Random Offset", source.at("WSPR").at("Use Random Offset")}};
    public_json["CW"] = source.at("CW");
    public_json["Band GPIO"] = source.at("Band GPIO");
    const auto i2c_inventory = i2c_bus_inventory::discover();
    auto i2c_buses = nlohmann::json::array();
    for (const auto &bus : i2c_inventory.buses)
        i2c_buses.push_back({{"Number", bus.number}, {"Name", bus.name}});
    auto si5351_addresses = nlohmann::json::array();
    for (const int address : si5351_address_inventory.addresses)
        si5351_addresses.push_back(
            config_serialization_si5351_i2c_address(address));
    public_json["Platform"] = {
        {"I2C Buses", i2c_buses},
        {"I2C Bus Discovery Error", i2c_inventory.error},
        {"Si5351 Address Bus", si5351_address_inventory.i2c_bus},
        {"Si5351 Inventory Checked", si5351_address_inventory.checked},
        {"Si5351 Addresses", si5351_addresses},
        {"Si5351 Address Discovery Error", si5351_address_inventory.error},
        {"Model", get_pi_model()},
        {"Raspberry Pi Generation", get_raspberry_pi_generation()},
        {"GPIO Clock Transmission Supported",
         gpio_clock_transmission_supported},
        {"GPIO Clock Transmission Error",
         gpio_clock_transmission_supported ? std::string()
                                           : gpio_support_error},
        {"RP1 GPIO Operator Visible", rp1_gpio_operator_visible},
        {"Si5351 Detected", si5351_detected},
        {"Si5351 Detection Error",
         si5351_detected ? std::string() : si5351_detection_error}};
    // Absence of a check is unknown, never a claim that hardware is missing/present.
    if (!si5351_address_inventory.checked)
        public_json["Platform"].erase("Si5351 Detected");
    return public_json;
}

void apply_public_config_to_internal_json(
    const nlohmann::json &public_json,
    nlohmann::json &internal_json)
{
    if (public_json.contains("WTP")) {
        if (!public_json.at("WTP").is_object())
            throw std::runtime_error("WTP settings must be an object.");
        for (const auto &item : public_json.at("WTP").items()) {
            if (!wtp_settings_json(WtpSettings{}).contains(item.key()))
                throw std::runtime_error("Unknown WTP setting: " + item.key());
            internal_json["WTP"][item.key()] = item.value();
        }
    }
    if (public_json.contains("WTP Server")) {
        if (!public_json.at("WTP Server").is_object())
            throw std::runtime_error("WTP Server settings must be an object.");
        for (const auto &item : public_json.at("WTP Server").items()) {
            if (!wtp_server_settings_json(WtpServerSettings{}).contains(item.key()))
                throw std::runtime_error("Unknown WTP Server setting: " + item.key());
            internal_json["WTP Server"][item.key()] = item.value();
        }
    }
    if (public_json.contains("Meta"))
    {
        const auto &meta = public_json.at("Meta");
        if (meta.contains("debug_logging"))
            internal_json["Meta"]["debug_logging"] = meta.at("debug_logging");
    }

    if (public_json.contains("Operation"))
    {
        const auto &operation = public_json.at("Operation");
        if (operation.contains("Mode"))
            internal_json["Operation"]["Mode"] = operation.at("Mode");
        if (operation.contains("Transmit"))
            internal_json["Operation"]["Transmit"] = operation.at("Transmit");
        if (operation.contains("Transmit Backend"))
        {
            const std::string backend = config_serialization_trim(
                operation.at("Transmit Backend").get<std::string>());
            std::string normalized_backend = backend;
            std::transform(
                normalized_backend.begin(), normalized_backend.end(),
                normalized_backend.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            internal_json["Operation"]["Transmit Backend"] =
                get_raspberry_pi_generation() == 5 &&
                        operator_exposes_rp1_gpio() &&
                        normalized_backend == "gpio"
                    ? "rp1-gpclk"
                    : backend;
        }
        if (operation.contains("Enable on Boot"))
            internal_json["Operation"]["Enable on Boot"] = operation.at("Enable on Boot");
        if (operation.contains("Use LED"))
            internal_json["Operation"]["Use LED"] = operation.at("Use LED");
        if (operation.contains("LED Pin"))
            internal_json["Operation"]["LED Pin"] = operation.at("LED Pin");
        if (operation.contains("Use Amp"))
            internal_json["Operation"]["Use Amp"] = operation.at("Use Amp");
        if (operation.contains("Amp Pin"))
            internal_json["Operation"]["Amp Pin"] = operation.at("Amp Pin");
        if (!operation.contains("Use Amp") && operation.contains("Amp Pin"))
        {
            internal_json["Operation"]["Use Amp"] =
                config_serialization_integer(
                    operation.at("Amp Pin"), "Operation.Amp Pin") >= 0;
        }
        if (operation.contains("Amp Pin Active High"))
            internal_json["Operation"]["Amp Pin Active High"] = operation.at("Amp Pin Active High");
        if (operation.contains("Web Port"))
            internal_json["Operation"]["Web Port"] = operation.at("Web Port");
        if (operation.contains("Socket Port"))
            internal_json["Operation"]["Socket Port"] = operation.at("Socket Port");
        if (operation.contains("Use Shutdown"))
            internal_json["Operation"]["Use Shutdown"] = operation.at("Use Shutdown");
        if (operation.contains("Shutdown Button"))
            internal_json["Operation"]["Shutdown Button"] = operation.at("Shutdown Button");
    }

    if (public_json.contains("GPIO"))
    {
        if (public_json.at("GPIO").is_object() &&
            public_json.at("GPIO").contains("Use NTP"))
        {
            throw std::runtime_error(
                "GPIO.Use NTP is retired and accepted only during INI migration; "
                "use GPIO.Use System Clock Frequency Estimate.");
        }
        const bool rp1_gpio_hidden_from_operator =
            get_raspberry_pi_generation() == 5 && !operator_exposes_rp1_gpio();
        if (!rp1_gpio_hidden_from_operator)
            internal_json["GPIO"] = public_json.at("GPIO");
    }
    if (public_json.contains("Calibration"))
        internal_json["Calibration"] = public_json.at("Calibration");
    if (public_json.contains("Si5351"))
        internal_json["Si5351"] = public_json.at("Si5351");
    if (public_json.contains("WSPR"))
    {
        const auto &wspr = public_json.at("WSPR");
        if (wspr.contains("Call Sign"))
            internal_json["WSPR"]["Call Sign"] = wspr.at("Call Sign");
        if (wspr.contains("Grid Square"))
            internal_json["WSPR"]["Grid Square"] = wspr.at("Grid Square");
        if (wspr.contains("TX Power"))
            internal_json["WSPR"]["TX Power"] = wspr.at("TX Power");
        if (wspr.contains("Frequency"))
            internal_json["WSPR"]["Frequency"] = wspr.at("Frequency");
        if (wspr.contains("Frequency Profile"))
            internal_json["WSPR"]["Frequency Profile"] = wspr.at("Frequency Profile");
        if (wspr.contains("Band Preferences"))
            internal_json["WSPR"]["Band Preferences"] = wspr.at("Band Preferences");
        if (wspr.contains("Planner Preference"))
            internal_json["WSPR"]["Planner Preference"] = wspr.at("Planner Preference");
        if (wspr.contains("Use Random Offset"))
            internal_json["WSPR"]["Use Random Offset"] = wspr.at("Use Random Offset");
        if (internal_json.contains("WSPR") && internal_json["WSPR"].is_object())
            internal_json["WSPR"].erase("WSPR Dial Frequency Set");
    }
    if (public_json.contains("CW"))
        internal_json["CW"] = public_json.at("CW");
    if (public_json.contains("Band GPIO"))
        internal_json["Band GPIO"] = public_json.at("Band GPIO");
}
} // namespace config_handler_serialization

void config_to_json()
{
    std::lock_guard update_lock(config_update_mutex());
    config_handler_serialization::serialize_runtime_config_to_json(
        config, jConfig);
}
