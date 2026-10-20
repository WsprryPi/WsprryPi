// config_handler.hpp
#ifndef CONFIG_HANDLER_HPP
#define CONFIG_HANDLER_HPP

#include <string>
#include <vector>

/**
 * @brief Holds all command-line and runtime configuration data.
 *
 * @details
 * This struct is populated from command-line arguments or an INI file,
 * and then accessed throughout the application.
 */
struct ArgParserConfig
{
    /** @brief Enable or disable transmission mode. */
    bool transmit = false;

    /** @brief WSPR callsign (e.g. "AA0NT"). */
    std::string callsign;

    /** @brief Maidenhead grid locator (4 or 6 chars). */
    std::string grid_square;

    /** @brief Transmit power in dBm. */
    int power_dbm = 0;

    /** @brief Comma-separated list of frequencies (Hz). */
    std::string frequencies;

    /** @brief GPIO pin number used for RF transmit control. */
    int tx_pin = -1;

    /** @brief PPM frequency calibration (e.g. +11.135). */
    double ppm = 0.0;

    /** @brief Apply NTP-based frequency correction. */
    bool use_ntp = false;

    /** @brief Apply a small random frequency offset. */
    bool use_offset = false;

    /** @brief Power level for RF hardware (0–7). */
    int power_level = 7;

    /** @brief Enable the TX LED indicator. */
    bool use_led = false;

    /** @brief GPIO pin used by the LED indicator. */
    int led_pin = -1;

    /** @brief Web server listening port. */
    int web_port = -1;

    /** @brief Socket server listening port. */
    int socket_port = -1;

    /** @brief Enable GPIO-based shutdown feature. */
    bool use_shutdown = false;

    /** @brief GPIO pin used to signal shutdown. */
    int shutdown_pin = -1;

    /** @brief Prefix logs with date/time. */
    bool date_time_log = false;

    /** @brief Loop transmission continuously (infinite if true). */
    bool loop_tx = false;

    /** @brief Number of transmission iterations (0=infinite). */
    int tx_iterations = 0;

    /** @brief Load configuration from an INI file. */
    bool use_ini = false;

    /** @brief Path to the INI configuration file. */
    std::string ini_filename;

    /** @brief Parsed list of center frequencies (Hz). */
    std::vector<double> center_freq_set;
};

/** @brief Global configuration instance. */
extern ArgParserConfig config;

#endif // CONFIG_HANDLER_HPP
