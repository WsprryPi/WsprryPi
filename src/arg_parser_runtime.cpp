/**
 * @file arg_parser_runtime.cpp
 * @brief Applies validated argument-parser configuration to runtime services.
 */

#include "arg_parser.hpp"
#include "arg_parser_internal.hpp"

#include "band_lookup.hpp"
#include "config_handler.hpp"
#include "gpio_input.hpp"
#include "gpio_output.hpp"
#include "logging.hpp"
#include "scheduling.hpp"
#include "system_clock_frequency_estimate.hpp"
#include "wspr_transmit.hpp"
#include "wtp_runtime_bridge.hpp"

#include <iomanip>
#include <sched.h>
#include <sstream>
#include <string>

void apply_runtime_config_side_effects()
{
    const wsprrypi::BackendKind backend_kind =
        config.transmit_backend == TransmitBackendKind::SIMULATED
            ? wsprrypi::BackendKind::SIMULATED
            : config.transmit_backend == TransmitBackendKind::SI5351
                ? wsprrypi::BackendKind::SI5351
                : config.transmit_backend == TransmitBackendKind::RP1_GPCLK
                    ? wsprrypi::BackendKind::RP1_GPCLK
                    : wsprrypi::BackendKind::RPI_CLOCK_GPIO;
    WsprTransmitter::Si5351RuntimeConfig si5351_config;
    si5351_config.i2c_bus = config.si5351_i2c_bus;
    si5351_config.i2c_address = config.si5351_i2c_address;
    si5351_config.reference_hz = config.si5351_reference_hz;
    si5351_config.reference_source = config.si5351_reference_source == "crystal"
        ? WsprTransmitter::Si5351RuntimeConfig::ReferenceSource::CRYSTAL
        : WsprTransmitter::Si5351RuntimeConfig::ReferenceSource::EXTERNAL_TCXO;
    si5351_config.crystal_load_capacitance_pf =
        config.si5351_crystal_load_capacitance_pf;
    si5351_config.tx_output = config.si5351_tx_output;
    si5351_config.power_level = config.power_level;
    si5351_config.app_managed = config.use_ini;
    if (config.transmit_backend == TransmitBackendKind::WTP) {
        if (!wtp_runtime_selected()) wsprTransmitter.shutdownForProcessExit();
        select_wtp_runtime(config.wtp);
        if (!wtp_runtime_ready()) (void)wtp_runtime_inspect();
    } else {
        select_wtp_runtime(std::nullopt);
        wsprTransmitter.selectBackend(backend_kind, si5351_config);
    }
    if (config.transmit_backend != TransmitBackendKind::WTP)
        wsprTransmitter.setTransmitNow(backend_kind == wsprrypi::BackendKind::SIMULATED);

    llog.logS(INFO,
              "Transmit backend: ",
              transmit_backend_kind_to_string(config.transmit_backend));

    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        std::ostringstream address;
        address << "0x" << std::hex << std::uppercase
                << config.si5351_i2c_address;
        llog.logS(DEBUG, "Si5351 I2C bus: ", config.si5351_i2c_bus);
        llog.logS(DEBUG, "Si5351 I2C address: ", address.str());
        llog.logS(DEBUG, "Si5351 reference frequency Hz: ",
                  config.si5351_reference_hz);
        llog.logS(DEBUG, "Si5351 reference source: ", config.si5351_reference_source);
        llog.logS(DEBUG, "Si5351 crystal load capacitance pF: ",
                  config.si5351_crystal_load_capacitance_pf);
        llog.logS(DEBUG, "Si5351 TX output: ",
                  std::string("CLK") +
                      std::to_string(config.si5351_tx_output));
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
                DEBUG,
                "Si5351 unused output parking: ",
                parked_outputs.str(),
                " held in a safe non-transmitting state; internal PLL remains parked.");
        }
    }
    else if (config.transmit_backend != TransmitBackendKind::WTP)
    {
        llog.logS(INFO, "Transmit GPIO: ", config.tx_pin);
    }
    if (!config.use_system_clock_frequency_estimate && config.ppm != 0.0)
    {
        log_startup_config_message(INFO,
                                   "PPM value to be used for tone generation: ",
                                   std::fixed,
                                   std::setprecision(2),
                                   config.ppm);
    }
    else if (config.transmit_backend != TransmitBackendKind::WTP && !config.use_system_clock_frequency_estimate && config.ppm == 0.0)
    {
        log_startup_config_message(WARN, "System-clock frequency estimate disabled and manual GPIO PPM is zero.");
    }

    if (config.use_led && (config.led_pin >= 0 && config.led_pin <= 27))
    {
        if (!ledControl.enableGPIOPin(config.led_pin, true))
        {
            llog.logS(ERROR,
                      "Failed to enable TX LED GPIO ",
                      config.led_pin,
                      ": ",
                      ledControl.lastError());
        }
    }
    else
    {
        llog.logS(DEBUG, "Invalid or disabled LED settings, turning off LED.");
        ledControl.stop();
    }

    if (config.use_amp && config.amp_pin >= 0 && config.amp_pin <= 27)
    {
        if (!ampControl.enableGPIOPin(
                config.amp_pin,
                config.amp_pin_active_high))
        {
            llog.logS(ERROR,
                      "Failed to enable Amp Control GPIO ",
                      config.amp_pin,
                      ": ",
                      ampControl.lastError());
        }
    }
    else
    {
        llog.logS(DEBUG, "Invalid or disabled Amp Control settings, turning off Amp Control.");
        ampControl.stop();
    }

    if (config.use_shutdown && (config.shutdown_pin >= 0 && config.shutdown_pin <= 27))
    {
        if (!shutdownMonitor.enable(
            config.shutdown_pin,
            false,
            GPIOInput::PullMode::PullUp,
            callback_shutdown_system))
        {
            llog.logS(ERROR,
                      "Failed to enable shutdown monitor GPIO ",
                      config.shutdown_pin,
                      ": ",
                      shutdownMonitor.lastError());
        }
        else if (!shutdownMonitor.setPriority(SCHED_RR, 10))
        {
            llog.logS(WARN,
                      "Shutdown monitor GPIO ",
                      config.shutdown_pin,
                      " enabled, but failed to raise thread priority.");
        }
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
            "A direct RF test tone will be generated at: ",
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
            if (!arg_parser_internal::persisted_qrss_config_available(config))
            {
                log_startup_config_message(ERROR, " - Missing QRSS configuration.");
                return;
            }

            message = config.qrss.message;
            frequency_hz = config.qrss.frequency_hz;
            dot_seconds = config.qrss.dot_seconds;
        }

        log_startup_config_message(INFO, "QRSS configuration loaded:");
        log_startup_config_message(INFO, "- Message: \"", message, "\"");
        log_startup_config_message(
            INFO,
            "- Base Freq: ",
            lookup.freq_display_string(frequency_hz));
        log_startup_config_message(INFO, "- Dot Timing: ", dot_seconds, "s");
        if (transmit_backend_uses_gpio_output(config.transmit_backend))
        {
            log_startup_config_message(
                INFO,
                config.transmit_backend == TransmitBackendKind::RP1_GPCLK
                    ? "- RP1 GPIO Drive mA: "
                    : "- GPIO Transmit Power: ",
                config.power_level);
        }
        else if (config.transmit_backend == TransmitBackendKind::SI5351)
        {
            log_startup_config_message(
                INFO,
                "- Si5351 Transmit Power: ",
                config.si5351_power_level);
        }
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
            if (!arg_parser_internal::persisted_fskcw_config_available(config))
            {
                log_startup_config_message(ERROR, " - Missing FSKCW configuration.");
                return;
            }

            message = config.fskcw.message;
            mark_frequency_hz = config.fskcw.mark_frequency_hz;
            space_frequency_hz = config.fskcw.space_frequency_hz;
            dot_seconds = config.fskcw.dot_seconds;
        }

        log_startup_config_message(INFO, "FSKCW configuration loaded: ");
        log_startup_config_message(INFO, "- Message: \"", message, "\"");
        log_startup_config_message(
            INFO,
            "- Mark Freq: ",
            lookup.freq_display_string(mark_frequency_hz));
        log_startup_config_message(
            INFO,
            "- Space Freq: ",
            lookup.freq_display_string(space_frequency_hz));
        log_startup_config_message(INFO, "- Dot Timing: ", dot_seconds, "s");
        if (transmit_backend_uses_gpio_output(config.transmit_backend))
        {
            log_startup_config_message(
                INFO,
                config.transmit_backend == TransmitBackendKind::RP1_GPCLK
                    ? "- RP1 GPIO Drive mA: "
                    : "- GPIO Transmit Power: ",
                config.power_level);
        }
        else if (config.transmit_backend == TransmitBackendKind::SI5351)
        {
            log_startup_config_message(
                INFO,
                "- Si5351 Transmit Power: ",
                config.si5351_power_level);
        }
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
            if (!arg_parser_internal::persisted_dfcw_config_available(config))
            {
                log_startup_config_message(ERROR, " - Missing DFCW configuration.");
                return;
            }

            message = config.dfcw.message;
            dot_frequency_hz = config.dfcw.dot_frequency_hz;
            dash_frequency_hz = config.dfcw.dash_frequency_hz;
            dot_seconds = config.dfcw.dot_seconds;
        }

        log_startup_config_message(INFO, "DFCW configuration loaded:");
        log_startup_config_message(INFO, "- Message: \"", message, "\"");
        log_startup_config_message(
            INFO,
            "- Dot Freq: ",
            lookup.freq_display_string(dot_frequency_hz));
        log_startup_config_message(
            INFO,
            "- Dash Freq: ",
            lookup.freq_display_string(dash_frequency_hz));
        log_startup_config_message(INFO, "- Dot Timing: ", dot_seconds, "s");
        if (transmit_backend_uses_gpio_output(config.transmit_backend))
        {
            log_startup_config_message(
                INFO,
                config.transmit_backend == TransmitBackendKind::RP1_GPCLK
                    ? "- RP1 GPIO Drive mA: "
                    : "- GPIO Transmit Power: ",
                config.power_level);
        }
        else if (config.transmit_backend == TransmitBackendKind::SI5351)
        {
            log_startup_config_message(
                INFO,
                "- Si5351 Transmit Power: ",
                config.si5351_power_level);
        }
        return;
    }

    if (config.mode != ModeType::WSPR)
    {
        return;
    }

    if (config.transmit)
    {
        log_startup_config_message(INFO, "WSPR packet payload:");
        log_startup_config_message(INFO, "- Callsign: ", config.callsign);
        log_startup_config_message(INFO, "- Locator: ", config.grid_square);
        log_startup_config_message(INFO, "- Power: ", config.power_dbm, " dBm");
        if (transmit_backend_uses_gpio_output(config.transmit_backend))
        {
            log_startup_config_message(
                INFO,
                config.transmit_backend == TransmitBackendKind::RP1_GPCLK
                    ? "- RP1 GPIO Drive mA: "
                    : "- GPIO Transmit Power: ",
                config.power_level);
        }
        else if (config.transmit_backend == TransmitBackendKind::SI5351)
        {
            log_startup_config_message(
                INFO,
                "- Si5351 Transmit Power: ",
                config.si5351_power_level);
        }

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
                        arg_parser_internal::get_wspr_gpio_suffix_for_entry(entry, config, lookup));
                }
            }
        }
        else
        {
            const WsprFrequencyEntry &entry = config.wspr_frequency_entries[0];

            if (entry.dial_frequency_hz == 0.0)
            {
                log_startup_config_message(INFO, "Requested WSPR dial frequency: ", "Skip (0.0)");
            }
            else
            {
                log_startup_config_message(
                    INFO,
                    "Requested WSPR dial frequency: ",
                    lookup.freq_display_string(entry.dial_frequency_hz),
                    arg_parser_internal::get_wspr_gpio_suffix_for_entry(entry, config, lookup));
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
                "TX will stop after: ",
                config.tx_iterations.load(),
                "iteration(s) of the WSPR dial-frequency list.");
        }
    }
}
