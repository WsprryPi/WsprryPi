/**
 * @file arg_parser_reporting.cpp
 * @brief Emits argument-parser usage and runtime configuration diagnostics.
 */

#include "arg_parser.hpp"

#include "config_handler.hpp"
#include "logging.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

/**
 * @brief Displays the usage information for the WsprryPi application.
 *
 * This function prints a brief help message to `std::cerr`, outlining
 * the command-line syntax and key options available. Optionally, an error
 * message can be displayed before the usage information.
 *
 * The function also determines whether the program exits or continues
 * running based on the `exit_code` parameter.
 *
 * @note This function **always terminates the program**, unless `exit_code`
 *       is `3`, in which case it simply returns.
 *
 * @param message An optional error message to display before the usage
 *        information. If empty, only the usage message is shown.
 * @param exit_code Determines the program's exit behavior:
 *        - `0` → Exits with `EXIT_SUCCESS`.
 *        - `1` → Exits with `EXIT_FAILURE`.
 *        - `3` → Returns from the function without exiting.
 *        - Any other value → Calls `std::exit(exit_code)`.
 *
 * @example
 * **Returning (does not exit):**
 * @code
 * print_usage();               // Prints usage and returns (default: exit_code = 3).
 * print_usage("Invalid args"); // Prints error message, then usage, returns.
 * print_usage(3);              // Prints usage and returns.
 * @endcode
 *
 * **Exiting the program:**
 * @code
 * print_usage(0);              // Prints usage and exits with EXIT_SUCCESS.
 * print_usage(1);              // Prints usage and exits with EXIT_FAILURE.
 * print_usage("Fatal error", 1); // Prints error message, then exits with EXIT_FAILURE.
 * print_usage("Custom exit", 5); // Prints error message, then exits with code 5.
 * @endcode
 */
void print_usage(const std::string &message, int exit_code)
{
    if (!message.empty())
    {
        std::cout << "\n"
                  << message << std::endl;
    }
    else
    {
        std::cerr << "\n"
                  << get_version_string() << std::endl;
    }

    std::cerr << "\nUsage:\n"
              << "  (sudo) wsprrypi [options] CALLSIGN GRID POWER FREQ [FREQ...]\n"
              << "  (sudo) wsprrypi -i /path/to/wsprrypi.ini\n"
              << "  (sudo) wsprrypi --test-tone RF_FREQ [backend/options]\n"
              << "  (sudo) wsprrypi --mode QRSS --cw-message TEXT --cw-base-frequency FREQ [cw/options]\n"
              << "  (sudo) wsprrypi --qrss-message TEXT --qrss-frequency HZ --qrss-dot-seconds SEC\n"
              << "  (sudo) wsprrypi --fskcw-message TEXT --fskcw-mark-frequency HZ --fskcw-space-frequency HZ --fskcw-dot-seconds SEC\n"
              << "  (sudo) wsprrypi --dfcw-message TEXT --dfcw-dot-frequency HZ --dfcw-dash-frequency HZ --dfcw-dot-seconds SEC\n\n"
              << "General:\n"
              << "  -h, --help                         Display this help message.\n"
              << "  -v, --version                      Show the WsprryPi version.\n"
              << "      --list-backends                List compiled transmission backends.\n"
              << "  -i, --ini-file <file>              Load and monitor an INI file for daemon/service style operation.\n"
              << "  -r, --repeat                       Repeat direct CLI transmissions until stopped.\n"
              << "  -x, --terminate <count>            Stop after count iterations of the direct CLI WSPR frequency list.\n"
              << "  -J, --journald                     Use journald for log output.\n"
              << "  -D, --date-time-log                Prefix stream log output with date/time stamps.\n"
              << "  --debug-logging, --no-debug-logging\n"
              << "                                     Enable or disable DEBUG-level application logging.\n\n"
              << "Experimental Frequency Policy (CLI/INI only):\n"
              << "  --allow-unqualified-frequency, --no-allow-unqualified-frequency\n"
              << "                                     Allow or deny unqualified backend/mode combinations.\n"
              << "  --allow-non-amateur-frequency, --no-allow-non-amateur-frequency\n"
              << "                                     Allow or deny frequencies outside recognized amateur bands.\n"
              << "                                     Outside-band transmission requires both allow flags.\n\n"
              << "WSPR Identity And Frequency:\n"
              << "  Positional CALLSIGN GRID POWER FREQ [FREQ...]\n"
              << "                                     Transmit WSPR directly from CLI. POWER is rounded to a standard WSPR dBm value.\n"
              << "  FREQ                               Band aliases such as 20m; whole-number-Hz or unit-qualified dial frequencies; or 0 to skip.\n"
              << "                                     Append @GPIO, @GPIOH, or @GPIOL for one-slot selector GPIO overrides.\n"
              << "  --planner-preference <auto|prefer_paired|require_paired>\n"
              << "                                     Select single-frame or paired WSPR planning policy.\n"
              << "  -o, --offset                       Enable random WSPR transmit offset.\n"
              << "  --no-offset                        Disable random WSPR transmit offset.\n\n"
              << "Backend Selection:\n"
              << "  Compiled backends: " << get_compiled_backends() << "\n"
              << "  Ancillary GPIO: " << (has_ancillary_gpio() ? "enabled" : "disabled") << "\n"
              << "  --backend <gpio|rp1-gpclk|si5351|simulated|wtp>\n"
              << "                                     Select the backend. Default: gpio.\n"
              << "                                     simulated is transient, non-RF, and never persisted.\n"
              << "                                     rp1-gpclk is an explicit Experimental Pi 5 backend and never a gpio fallback.\n"
              << "  --power-level <level>\n"
              << "    Set transmit power level for the active backend:\n"
              << "      GPIO: 0-7\n"
              << "      RP1 GPIO: 2, 4, 8, or 12 mA\n"
              << "      Si5351: 1-4\n"
              << "  --gpio-power-level <0-7>           Set GPIO backend RF power level.\n"
              << "  --rp1-gpio-drive-ma <2|4|8|12>    Set Raspberry Pi 5 RP1 pad drive. Default: 2 mA.\n"
              << "  --rp1-development-confirmation-json <json>\n"
              << "                                     Bind transient RP1 development confirmation to finite local requests.\n"
              << "  --si5351-power-level <1-4>         Set Si5351 drive-strength level.\n\n"
              << "GPIO Backend:\n"
              << "  -a, --transmit-gpio <4|20>         Select the RF transmit GPIO.\n"
              << "      --transmit-pin <4|20>          Legacy alias for --transmit-gpio.\n"
              << "  -n, --use-system-clock-frequency-estimate\n"
              << "                                     Enable the system-clock frequency estimate for GPIO.\n"
              << "      --no-system-clock-frequency-estimate\n"
              << "                                     Disable the estimate and use GPIO manual PPM.\n"
              << "  -p, --gpio-manual-ppm <value>      Set GPIO manual correction (-200 through 200) and disable the estimate.\n"
              << "      --gpio-frequency-residual-ppm <value>\n"
              << "                                     Set residual correction added to the system-clock estimate.\n"
              << "                                     Positive means fast; negative means slow.\n\n"
              << "Si5351 Backend:\n"
              << "  --si5351-ppm <value>              Set Si5351 reference-oscillator correction (-200 through 200).\n"
              << "  --si5351-i2c-bus <bus>             Linux I2C bus number. Default: 1.\n"
              << "  --si5351-i2c-address <addr>        I2C address, decimal or 0x-prefixed hex. Valid: 0x60 through 0x6F.\n"
              << "  --si5351-reference-frequency <hz>  Reference oscillator frequency in Hz.\n"
              << "  --si5351-reference-source <source> external_tcxo or crystal.\n"
              << "  --si5351-crystal-load-capacitance <pf>  Crystal load: 6, 8, or 10 pF.\n"
              << "  --si5351-tx-output <CLK0|CLK1|CLK2>\n"
              << "                                     Transmit clock output. CLI/INI only; not exposed in the Web UI.\n\n"
              << "CW, QRSS, FSKCW, And DFCW:\n"
              << "  --mode <WSPR|QRSS|FSKCW|DFCW>      Select direct CLI mode. Non-WSPR modes use the CW options below.\n"
              << "  --cw-message <text>                Message for QRSS, FSKCW, or DFCW.\n"
              << "  --cw-base-frequency <freq>         Base RF frequency. Accepts whole-number Hz or Hz/kHz/MHz/GHz suffixes.\n"
              << "  --cw-shift-hz <hz>                 FSKCW/DFCW frequency shift in Hz.\n"
              << "  --cw-dot-seconds <seconds>         Dot length in seconds.\n"
              << "  --cw-start-minute <0-59>           Scheduled non-WSPR start minute.\n"
              << "  --cw-start-second <0-59>           Seconds after the scheduled CW start minute. Default: 5.\n"
              << "  --cw-repeat-minutes <minutes>      Scheduled non-WSPR repeat interval.\n"
              << "  --cw-intra-element-gap <multiple>  Gap between Morse elements.\n"
              << "  --cw-inter-character-gap <multiple>\n"
              << "                                     Gap between Morse characters.\n"
              << "  --cw-inter-word-gap <multiple>     Gap between Morse words.\n"
              << "  --dfcw-intra-element-gap <multiple>\n"
              << "                                     DFCW off gap between dot/dash symbols.\n"
              << "  --dfcw-inter-character-gap <multiple>\n"
              << "                                     DFCW off gap between characters.\n"
              << "  --dfcw-inter-word-gap <multiple>   DFCW off gap between words.\n"
              << "  --cw-fade-shape <none|linear|raised_cosine>\n"
              << "                                     Envelope fade shape. Advanced CLI/INI control hidden from normal Web UI.\n"
              << "  --cw-fade-in-ms <ms>, --cw-fade-out-ms <ms>, --cw-fade-slice-ms <ms>\n"
              << "                                     Advanced CW envelope timing controls.\n"
              << "  --qrss-message/--qrss-frequency/--qrss-dot-seconds\n"
              << "                                     Legacy transient QRSS startup options; all three are required together.\n"
              << "  --fskcw-message/--fskcw-mark-frequency/--fskcw-space-frequency/--fskcw-dot-seconds\n"
              << "                                     Legacy transient FSKCW startup options; all four are required together.\n"
              << "  --dfcw-message/--dfcw-dot-frequency/--dfcw-dash-frequency/--dfcw-dot-seconds\n"
              << "                                     Legacy transient DFCW startup options; all four are required together.\n\n"
              << "GPIO Controls And Service Ports:\n"
              << "  --no-web                           Disable the HTTP web UI and WebSocket server for this run.\n"
              << "  --no-http                          Disable only the HTTP web UI for this run.\n"
              << "  -w, --web-port <port>              HTTP REST/Web UI port. Default: 31415.\n"
              << "  -k, --socket-port <port>           WebSocket server port. Default: 31416.\n"
              << "      --wtp-server-port <port>       WTP Plain LAN listener port for this process. Default: 31417.\n"
              << "      --socket-loopback-only        Bind WebSocket control to loopback only.\n"
              << "      --socket-loopback-family <auto|ipv6|ipv4>\n"
              << "                                     Select loopback family; auto prefers IPv6.\n"
              << "  -l, --led_pin <gpio>               Enable the TX LED on the given GPIO.\n"
              << "      --led-pin <gpio>               Alias for --led_pin.\n"
              << "      --use-led, --no-led            Enable or disable the TX LED using the configured/default pin.\n"
              << "      --amp-pin <gpio>               Configure external amplifier control GPIO.\n"
              << "      --amp-pin-active-high          Set Amp Pin polarity active-high.\n"
              << "      --amp-pin-active-low           Set Amp Pin polarity active-low. Default.\n"
              << "      --no-amp-pin                   Disable external amplifier control.\n"
              << "  -s, --shutdown_button <gpio>       Enable shutdown button monitoring on the given GPIO.\n"
              << "      --shutdown-button <gpio>       Alias for --shutdown_button.\n"
              << "      --use-shutdown, --no-shutdown  Enable or disable shutdown button monitoring.\n"
              << "  Band GPIO                          Configure per-band selector GPIOs in INI or with FREQ @GPIO suffixes.\n\n"
              << "Test Tone:\n"
              << "  -t, --test-tone <rf_frequency>     Start a transient direct RF test tone using whole-number Hz or a unit-qualified value.\n"
              << "                                     Band aliases resolve to their WSPR dial frequency; use an explicit value for another RF carrier.\n"
              << "                                     Invalid with --ini-file.\n\n";

    // Handle exit behavior
    switch (exit_code)
    {
    case 0:
        std::exit(EXIT_SUCCESS);
        break;
    case 1:
        std::exit(EXIT_FAILURE);
        break;
    case 3:
        return; // Simply return without exiting
    default:
        std::exit(exit_code);
        break;
    }
}

void show_config_values(bool reload)
{
    llog.logS(DEBUG, "Transmit Enabled: ", config.transmit ? "true" : "false");
    llog.logS(DEBUG, "Call Sign: ", config.callsign);
    llog.logS(DEBUG, "Grid Square: ", config.grid_square);
    llog.logS(DEBUG, "Transmit Power: ", config.power_dbm);
    llog.logS(DEBUG, "WSPR Dial Frequencies: ", config.frequencies);
    llog.logS(
        DEBUG,
        "Transmit Backend: ",
        transmit_backend_kind_to_string(config.transmit_backend));
    if (config.transmit_backend == TransmitBackendKind::SI5351)
    {
        std::ostringstream address;
        address << "0x" << std::hex << std::uppercase
                << config.si5351_i2c_address;
        llog.logS(DEBUG, "Si5351 I2C Bus: ", config.si5351_i2c_bus);
        llog.logS(DEBUG, "Si5351 I2C Address: ", address.str());
        llog.logS(
            DEBUG,
            "Si5351 Reference Frequency Hz: ",
            config.si5351_reference_hz);
        llog.logS(
            DEBUG,
            "Si5351 Reference Source: ",
            config.si5351_reference_source);
        llog.logS(
            DEBUG,
            "Si5351 Crystal Load Capacitance pF: ",
            config.si5351_crystal_load_capacitance_pf);
        llog.logS(
            DEBUG,
            "Si5351 TX Output: ",
            std::string("CLK") + std::to_string(config.si5351_tx_output));
    }
    else
    {
        llog.logS(DEBUG, "Transmit GPIO: ", config.tx_pin);
    }
    llog.logS(DEBUG, "PPM Offset: ", config.ppm);
    llog.logS(DEBUG, "WSPR Audio Offset Hz: ", config.wspr.audio_offset_hz);
    llog.logS(
        DEBUG,
        config.transmit_backend == TransmitBackendKind::SI5351
            ? "Si5351 Drive Level: "
            : config.transmit_backend == TransmitBackendKind::RP1_GPCLK
                ? "RP1 GPIO Drive mA: "
                : "GPIO Power Level: ",
        config.power_level);
    llog.logS(DEBUG, "Use LED: ", config.use_led ? "true" : "false");
    llog.logS(DEBUG, "LED on GPIO", config.led_pin);
    llog.logS(
        DEBUG,
        "Debug Logging: ",
        config.debug_logging ? "true" : "false");
    llog.logS(
        DEBUG, "Web UI enabled: ", config.enable_web ? "true" : "false");
    llog.logS(DEBUG, "Web server runs on port: ", config.web_port);
    llog.logS(DEBUG, "Socket server runs on port: ", config.socket_port);
    llog.logS(
        DEBUG,
        "Use shutdown button: ",
        config.use_shutdown ? "true" : "false");
    llog.logS(DEBUG, "Shutdown button GPIO", config.shutdown_pin);

    (void)reload;
}
