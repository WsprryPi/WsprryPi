/**
 * @file scheduling.cpp
 * @brief Manages transmit, INI monitoring and scheduling for Wsprry Pi
 *
 * This project is is licensed under the MIT License. See LICENSE.md
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

// Primary header for this source file
#include "scheduling.hpp"

// Project headers
#include "arg_parser.hpp"
#include "band_gpio_selector.hpp"
#include "config_handler.hpp"
#include "gpio_input.hpp"
#include "gpio_output.hpp"
#include "logging.hpp"
#include "ppm_manager.hpp"
#include "signal_handler.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"
#include "wspr_transmit.hpp"

// Standard library headers
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

// System headers
#include <string.h>
#include <sys/reboot.h>   // for reboot()
#include <linux/reboot.h> // for LINUX_REBOOT_CMD_* constants
#include <sys/resource.h>
#include <unistd.h>

/**
 * @brief Selects and controls the GPIO assigned to the active amateur band.
 *
 * This object is used by the transmission callback path to assert the
 * correct GPIO when transmission begins and to release it when the
 * transmission completes, is skipped, or is cancelled.
 */
static BandGPIOSelector bandGPIOSelector;

/**
 * @brief Mutex to protect access to the shutdown flag for the WSPR loop.
 *
 * This mutex must be locked before reading or writing \c exitwspr_ready
 * to ensure thread-safe coordination between the signal handler callback
 * and the WSPR loop.
 */
std::mutex exitwspr_mtx;

/**
 * @brief Condition variable used to signal the WSPR loop to exit.
 *
 * The signal handler callback will notify this condition variable after
 * setting \c exitwspr_ready to \c true, causing the waiting WSPR loop
 * to wake up and perform shutdown.
 */
std::condition_variable exitwspr_cv;

/**
 * @brief Atomic bool used to signal other functions that we are shutting down.
 */
std::atomic<bool> exiting_wspr = false;

/**
 * @brief Flag indicating whether the WSPR loop should terminate.
 *
 * Set to \c true by the signal handler callback under protection of
 * \c exitwspr_mtx, then \c exitwspr_cv is notified so that the WSPR
 * loop can break out of its wait and begin shutdown.
 */
bool exitwspr_ready = false;

/**
 * @brief Round‐robin index into the configured frequency list.
 *
 * Tracks which entry in the `config.center_freq_set` vector will be
 * used for the next WSPR transmission.  Wraps via modulo on each use.
 */
int freq_iterator = 0;

/**
 * @brief Currently active transmit frequency (in Hz).
 *
 * Holds the last frequency value retrieved by `next_frequency()`.
 * A zero value indicates that no frequency is configured or the list was empty.
 */
double current_frequency = 0.0;

/**
 * @brief File-scope self-pipe descriptors for signal notifications.
 *
 * @details Declared `extern` here so that any TU (like scheduling.cpp)
 *          can refer to the same pipe ends.  The *definition* (no `extern`)
 *          remains in exactly one .cpp (main.cpp).
 */
extern int sig_pipe_fds[2];

/**
 * @brief Global mutex for coordinating shutdown and thread safety.
 *
 * Used to protect shared data during shutdown, ensuring only one thread
 * initiates and executes the shutdown procedure.
 */
std::mutex shutdown_mtx;

/**
 * @brief Flag indicating if a system reboot is in progress.
 *
 * @details
 * This atomic flag is used throughout the application to signal when a
 * full system reboot has been initiated. It is typically set from one
 * of the control points (REST or websockets).
 *
 * Other threads can poll or wait on this flag to terminate safely.
 */
std::atomic<bool> reboot_flag{false};

/**
 * @brief Atomic flag indicating that a shutdown sequence has begun.
 *
 * Set by GPIO or system-triggered shutdown paths to initiate coordinated
 * shutdown across all subsystems.
 */
std::atomic<bool> shutdown_flag{false};

/**
 * @brief Stores the previous transmission mode.
 *
 * This variable saves the last value of config.mode before entering
 * test tone mode so that the original mode can be restored later.
 */
ModeType lastMode;

/**
 * @brief Flag indicating if a web-triggered test tone is active.
 *
 * An atomic bool that is true while a test tone transmission is in
 * progress via web controls, and false otherwise.
 */
std::atomic<bool> web_test_tone{false};

static std::string to_lower_copy(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(),
                   out.end(),
                   out.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return out;
}

static std::string format_elapsed(double elapsed)
{
    if (elapsed == 0.0)
        return std::string();

    std::ostringstream oss;
    oss << std::fixed
        << std::setprecision(6)
        << elapsed;
    return oss.str();
}

constexpr LogLevel to_log_level(WsprTransmitter::LogLevel level)
{
    switch (level)
    {
    case WsprTransmitter::LogLevel::DEBUG:
        return LogLevel::DEBUG;
    case WsprTransmitter::LogLevel::INFO:
        return LogLevel::INFO;
    case WsprTransmitter::LogLevel::WARN:
        return LogLevel::WARN;
    case WsprTransmitter::LogLevel::ERROR:
        return LogLevel::ERROR;
    case WsprTransmitter::LogLevel::FATAL:
        return LogLevel::FATAL;
    }

    return LogLevel::INFO; // Safe fallback
}

bool request_wspr_shutdown(std::string_view reason)
{
    const bool already_requested =
        exiting_wspr.exchange(true, std::memory_order_seq_cst);

    if (!reason.empty())
    {
        if (already_requested)
        {
            llog.logS(INFO,
                      "Shutdown already in progress; duplicate request:",
                      reason);
        }
        else
        {
            llog.logS(INFO, "Shutdown requested:", reason);
        }
    }

    {
        std::lock_guard<std::mutex> lk(exitwspr_mtx);
        exitwspr_ready = true;
    }
    exitwspr_cv.notify_one();

    return !already_requested;
}

void transmitter_cb(WsprTransmitter::TransmissionCallbackEvent event,
                    WsprTransmitter::LogLevel level,
                    const std::string &msg,
                    double value)
{
    switch (event)
    {
    case WsprTransmitter::TransmissionCallbackEvent::STARTING:
    {
        const double frequency = value;

        // Assert the precomputed band GPIO.
        bandGPIOSelector.setBandState(true);

        // Turn on LED.
        ledControl.toggleGPIO(true);

        // Notify clients of start.
        send_ws_message("transmit", "starting");

        // Log messages.
        if (!msg.empty() && frequency != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Started transmission (",
                      msg,
                      ") ",
                      wsprTransmitter.formatFrequencyMHz(frequency),
                      " MHz.");
        }
        else if (frequency != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Started transmission: ",
                      wsprTransmitter.formatFrequencyMHz(frequency),
                      " MHz.");
        }
        else if (!msg.empty())
        {
            llog.logS(to_log_level(level),
                      "Started transmission (",
                      msg,
                      ").");
        }
        else
        {
            llog.logS(to_log_level(level),
                      "Started transmission.");
        }
        break;
    }

    case WsprTransmitter::TransmissionCallbackEvent::COMPLETE:
    {
        const double elapsed = value;

        bool do_config = true;

        const std::string s_elapsed = format_elapsed(elapsed);
        const std::string msg_lower = to_lower_copy(msg);

        if (msg_lower.find("canceled") != std::string::npos)
        {
            llog.logS(to_log_level(level),
                      "Transmission cancelled after ",
                      s_elapsed,
                      " seconds.");
            do_config = false;
            send_ws_message("transmit", "canceled");
        }
        else if (!msg.empty() && elapsed != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Completed transmission (",
                      msg,
                      ") ",
                      s_elapsed,
                      " seconds.");
        }
        else if (elapsed != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Completed transmission: ",
                      s_elapsed,
                      " seconds.");
        }
        else if (!msg.empty())
        {
            llog.logS(to_log_level(level),
                      "Completed transmission (",
                      msg,
                      ").");
        }
        else
        {
            llog.logS(to_log_level(level),
                      "Completed transmission.");
        }

        // Deassert and release the prepared band GPIO.
        bandGPIOSelector.setBandState(false);
        bandGPIOSelector.stop();

        // Turn off LED.
        ledControl.toggleGPIO(false);

        // Notify the websocket clients.
        send_ws_message("transmit", "finished");

        // Set config will determine if we have work to do.
        if (do_config)
            set_config();

        break;
    }

    case WsprTransmitter::TransmissionCallbackEvent::SKIPPED:
    {
        // Deassert and release the prepared band GPIO.
        bandGPIOSelector.setBandState(false);
        bandGPIOSelector.stop();

        // Turn off LED in case the UI/state expects an idle indication.
        ledControl.toggleGPIO(false);

        if (!msg.empty())
            llog.logS(to_log_level(level), msg, ".");
        else
            llog.logS(to_log_level(level), "Skipping transmission.");

        // Notify websocket clients.
        send_ws_message("transmit", "skipped");

        // Advance to the next configured slot.
        set_config();

        break;
    }

    case WsprTransmitter::TransmissionCallbackEvent::LOGGING:
    default:
    {
        if (!msg.empty())
            llog.logS(to_log_level(level), msg);

        break;
    }
    }
}

/**
 * @brief  Callback invoked when PPMManager has a new PPM reading.
 *
 * @details
 * Sets the `ppm_reload_pending` flag so that downstream consumers
 * will pick up the new PPM, and marks NTP as “good” once Chrony
 * has delivered a real clock‐drift measurement.
 *
 * @param new_ppm  The latest PPM correction value (ignored here; reload
 *                 logic will pull it from PPMManager when needed).
 */
void ppm_callback(double /*new_ppm*/)
{
    // Notify other subsystems to reload/recalibrate with the fresh PPM.
    ppm_reload_pending.store(true, std::memory_order_relaxed);

    // Now that Chrony has produced a PPM value, we know time is valid.
    if (!config.ntp_good)
    {
        llog.logS(DEBUG, "Chrony service has updated its initial value.");
        config.ntp_good = true;
    }
}

/**
 * @brief   Initialize the PPM subsystem.
 *
 * Registers the PPM callback, initializes the PPMManager, and handles
 * any returned status.  If the Chrony daemon is running, we assume
 * synchronization and never treat unsynchronized time as fatal.
 *
 * @return  true if initialization is considered successful;
 *          false only on a fatal PPM error (e.g. excessive drift).
 */
bool ppm_init()
{
    bool retval = false;

    // Register the PPM update callback
    ppmManager.setPPMCallback(ppm_callback);

    // Perform the normal initialization
    PPMStatus status = ppmManager.initialize();

    // If Chrony is active, assume time is synced
    if (ppmManager.isChronyAlive())
    {
        llog.logS(DEBUG, "Chrony service is active.");
        retval = true;
    }

    switch (status)
    {
    case PPMStatus::SUCCESS:
        llog.logS(INFO, "PPM Manager initialized successfully.");
        break;

    case PPMStatus::WARNING_HIGH_PPM:
        llog.logE(ERROR, "Measured PPM exceeds safe threshold.");
        return false;

    case PPMStatus::ERROR_CHRONY_NOT_FOUND:
        llog.logE(WARN,
                  "Chrony not found; falling back to clock-drift measurement.");
        break;

    case PPMStatus::ERROR_UNSYNCHRONIZED_TIME:
        // Chrony wasn’t yet reporting sync—but if the daemon is running,
        // assume chrony is configured and proceed.
        break;

    default:
        llog.logE(WARN, "Unknown PPMStatus returned from initialize().");
        break;
    }

    return retval;
}

/**
 * @brief Callback function triggered to perform a system shutdown sequence.
 *
 * @details
 * This function is intended to be called when a shutdown GPIO event is triggered.
 * It logs the event and calls shutdown_system().
 */
void callback_shutdown_system()
{
    llog.logS(INFO, "Shutdown called by GPIO:", config.shutdown_pin);
    shutdown_system();
}

/**
 * @brief Perform a system shutdown sequence.
 *
 * @details
 * This function is intended to be called when a shutdown event is triggered.
 * It performs a visual blink pattern on the LED pin if configured, sets the
 * shutdown flags, and notifies all threads waiting on the shutdown condition
 * variable.
 *
 * Specifically:
 * - Toggles the LED 3 times with 200ms intervals.
 * - Sets `exitwspr_cv` to break out of the main transmission loop.
 * - Sets `shutdown_flag` to mark that a full system shutdown is in progress.
 *
 * @note
 * The LED toggling uses `ledControl.toggleGPIO()` and assumes the hardware
 * supports it.
 */
void shutdown_system()
{
    shutdown_flag.store(true, std::memory_order_relaxed);
    request_wspr_shutdown("system power-off requested");

    if (config.use_led)
    {
        // Flash LED three times if configured
        for (int i = 0; i < 3; ++i)
        {
            ledControl.toggleGPIO(true); // LED ON
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            ledControl.toggleGPIO(false); // LED OFF
            if (i < 2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    }
}

/**
 * @brief Perform a system reboot sequence.
 *
 * @details
 * This function is intended to be called when a reboot event is triggered.
 * It performs a visual blink pattern on the LED pin if configured, sets the
 * reboot flags, and notifies all threads waiting on the reboot condition
 * variable.
 *
 * Specifically:
 * - Toggles the LED 2 times with 100ms intervals.
 * - Sets `exitwspr_cv` to break out of the main transmission loop.
 * - Sets `reboot_flag` to mark that a full system reboot is in progress.
 *
 * @note
 * The LED toggling uses `ledControl.toggleGPIO()` and assumes the hardware supports it.
 */
void reboot_system()
{
    reboot_flag.store(true, std::memory_order_relaxed);
    request_wspr_shutdown("system reboot requested");

    if (config.use_led)
    {
        // Flash LED two times if configured
        for (int i = 0; i < 2; ++i)
        {
            ledControl.toggleGPIO(true); // LED ON
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            ledControl.toggleGPIO(false); // LED OFF
            if (i < 2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

/**
 * @brief   Initiates a continuous test‐tone transmission.
 *
 * Stops any ongoing transmission, saves the current mode,
 * switches into TONE mode, and transmits on the first
 * configured frequency using the current power and PPM.
 */
void start_test_tone()
{
    if (!web_test_tone.load())
    {
        web_test_tone.store(true);

        // Save previous mode so we can restore it later
        lastMode = config.mode;

        // Tear down any ongoing WSPR/transmission
        if (wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING)
        {
            llog.logS(INFO, "Stopping an in-process message early.");
        }
        wsprTransmitter.stopAndJoin();

        // Pick the “first” frequency
        auto freq = next_frequency(/*restart=*/true);

        while (wsprTransmitter.getState() == WsprTransmitter::State::TRANSMITTING)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ledControl.toggleGPIO(true);
        llog.logS(INFO, "Beginning test tone requested by web UI.");

        // Switch into tone mode
        config.mode = ModeType::TONE;

        // Set up and start the tone
        wsprTransmitter.configure(freq,
                                  config.power_level,
                                  config.ppm);

        if (!bandGPIOSelector.prepareFrequency(freq))
        {
            llog.logS(WARN,
                      "Unable to prepare band GPIO for test tone at ",
                      lookup.freq_display_string(freq),
                      ".");
        }

        wsprTransmitter.startAsync();
        llog.logS(INFO, "Test tone being transmitted at:", lookup.freq_display_string(freq));
        send_ws_message("transmit", "starting");
    }
}

/**
 * @brief   Ends the test‐tone and restores the previous mode.
 *
 * If we’re in test‐tone, shut it down, clear the flag,
 * restore lastMode, and re-configure either WSPR or
 * (if it wasn’t WSPR) another tone on config.test_tone.
 */
void end_test_tone()
{
    if (web_test_tone.load())
    {
        llog.logS(INFO, "Ending test tone requested by Web UI.");

        // Stop current tone
        wsprTransmitter.stopAndJoin();
        bandGPIOSelector.setBandState(false);
        bandGPIOSelector.stop();
        send_ws_message("transmit", "finished");
        ledControl.toggleGPIO(false);

        // Clear the “we’re testing” flag
        web_test_tone.store(false);

        // Restore whatever mode we were in before
        config.mode = lastMode;

        if (config.mode == ModeType::WSPR)
        {
            // Re-initialize WSPR with next frequency, PPM, etc.
            if (!validate_config_data())
            {
                llog.logE(ERROR, "Initial configuration validation failed.");
                return;
            }
        }
        else
        {
            // It was already a tone, so set it up again
            validate_config_data();
            wsprTransmitter.configure(config.test_tone,
                                      config.power_level,
                                      config.ppm);
            wsprTransmitter.startAsync();

            llog.logS(INFO,
                      "Transmitting tone, hit Ctrl-C to terminate tone.");
        }
    }
}

/**
 * @brief Main control loop for WSPR operation.
 *
 * @details
 * This function initializes and coordinates all core components required for
 * WSPR transmission. It sets up optional NTP-based PPM correction, launches
 * the TCP server, spawns the transmission handler thread, and blocks until
 * a shutdown signal is received.
 *
 * When signaled to exit, it cleanly shuts down all background services and
 * threads in the correct order.
 *
 * @note This function blocks until `exitwspr_cv` is set by another thread.
 */
bool wspr_loop()
{
    // TODO: Feature toggles while I work on configuration items
    bandGPIOSelector.setEnabled(true);    // (true) - Allows logic to run and show debug messages; set false to disable all band GPIO logic and messages
    bandGPIOSelector.setDriveGPIO(false); // (false) - Shows DEBUG messages vs performing actions

    // Display the final configuration after parsing arguments and INI file.
    show_config_values();

    if (config.mode != ModeType::WSPR)
    {
        validate_config_data();
    }

    // Start web server and set priority
    if (config.web_port >= 1024 && config.web_port <= 49151)
    {
        webServer.start(config.web_port);
        webServer.setThreadPriority(SCHED_RR, 10);
    }
    else
    {
        llog.logS(DEBUG, "Skipping web server.");
    }

    // Start socket server and set priority
    if (config.socket_port >= 1024 && config.socket_port <= 49151)
    {
        socketServer.start(config.socket_port, SOCKET_KEEPALIVE);
        socketServer.setThreadPriority(SCHED_RR, 10);
    }
    else
    {
        llog.logS(DEBUG, "Skipping socket server.");
    }

    // Set transmission server and set priority
    wsprTransmitter.setThreadScheduling(SCHED_FIFO, 50);

    // Set transmission event callbacks
    wsprTransmitter.setTransmissionCallbacks(
        [](WsprTransmitter::TransmissionCallbackEvent event,
           WsprTransmitter::LogLevel level,
           const std::string &msg,
           double value)
        {
            transmitter_cb(event, level, msg, value);
        });

    // Monitor INI file for changes
    if (config.use_ini)
    {
        // Start INI monitor
        iniMonitor.filemon(config.ini_filename, callback_ini_changed);
        iniMonitor.setPriority(SCHED_RR, 10);
    }

    llog.logS(INFO, "WSPR loop running.");

    // Startup WSPR configuration should be applied exactly once using the
    // same reload-safe path that handles validation, setup, and scheduling.
    if (config.mode == ModeType::WSPR)
    {
        ini_reload_pending.store(true, std::memory_order_relaxed);
        set_config();
    }
    else if (config.mode == ModeType::TONE)
    {
        wsprTransmitter.configure(config.test_tone, config.power_level, config.ppm);
        wsprTransmitter.startAsync();
        llog.logS(INFO, "transmitting tone, hit Ctrl-C to terminate tone.");
    }

    // -------------------------------------------------------------------------
    // Loop (block wspr_loop only) until shutdown is triggered
    // -------------------------------------------------------------------------
    {
        std::unique_lock<std::mutex> lk(exitwspr_mtx);
        exitwspr_cv.wait(lk, []
                         { return exitwspr_ready; });
    }

    llog.logS(INFO, "WSPR loop termination started.");

    // -------------------------------------------------------------------------
    // Shutdown and cleanup
    // -------------------------------------------------------------------------
    llog.logS(INFO, "Stopping runtime components.");

    llog.logS(INFO, "Stopping transmitter.");
    wsprTransmitter.stopAndJoin(); // Stop the transmitter threads
    llog.logS(INFO, "Transmitter stopped.");

    llog.logS(INFO, "Stopping band GPIO selector.");
    bandGPIOSelector.setBandState(false);
    bandGPIOSelector.stop();
    llog.logS(INFO, "Band GPIO selector stopped.");

    llog.logS(INFO, "Stopping shutdown monitor.");
    shutdownMonitor.stop(); // Stop the GPIO monitor
    llog.logS(INFO, "Shutdown monitor stopped.");

    llog.logS(INFO, "Stopping LED driver.");
    ledControl.stop(); // Stop LED driver
    llog.logS(INFO, "LED driver stopped.");

    llog.logS(INFO, "Stopping configuration monitor.");
    iniMonitor.stop(); // Stop config file monitor
    llog.logS(INFO, "Configuration monitor stopped.");

    llog.logS(INFO, "Stopping PPM manager.");
    ppmManager.stop(); // Stop PPM manager (if active)
    llog.logS(INFO, "PPM manager stopped.");

    llog.logS(INFO, "Stopping web server.");
    webServer.stop(); // Stop web server

    llog.logS(INFO, "Stopping socket server.");
    socketServer.stop(); // Stop the socket server
    llog.logS(INFO, "Socket server stopped.");

    llog.logS(INFO, "Runtime components stopped.");

    if (reboot_flag.load())
    {
        llog.logS(INFO, "Rebooting.");
        std::cerr << "[INFO ] Rebooting." << std::endl;
        reboot_machine();
    }
    if (shutdown_flag.load())
    {
        llog.logS(INFO, "Shutting down.");
        std::cerr << "[INFO ] Shutting down." << std::endl;
        shutdown_machine();
    }

    llog.logS(INFO, get_project_name(), "exiting.");
    // Flush all file system buffers to disk
    sync();

    return true;
}

/**
 * @brief Synchronize disk and reboot the machine.
 *
 * This function calls sync() to flush filesystem buffers, then
 * invokes the reboot(2) syscall directly. The process must have
 * the CAP_SYS_BOOT capability (typically run as root).
 */
void reboot_machine()
{
    // Flush all file system buffers to disk
    sync();

    // Attempt to reboot; LINUX_REBOOT_CMD_RESTART is the same as RB_AUTOBOOT
    if (::reboot(LINUX_REBOOT_CMD_RESTART) < 0)
    {
        llog.logE(ERROR, "Reboot failed:", std::strerror(errno));
    }
}

/**
 * @brief Flush filesystems and power off the machine.
 *
 * Calls sync() to ensure all disk buffers are written, then invokes
 * the reboot(2) syscall with the POWER_OFF command. Requires root or
 * the CAP_SYS_BOOT capability.
 */
void shutdown_machine()
{
    // 1) Flush all pending disk writes
    sync();

    // Power off the system
    // LINUX_REBOOT_CMD_POWER_OFF is equivalent to RB_POWER_OFF
    if (::reboot(LINUX_REBOOT_CMD_POWER_OFF) < 0)
    {
        llog.logE(ERROR, "Shutdown failed:", std::strerror(errno));
    }
}

/**
 * @brief Broadcasts a JSON-formatted WebSocket message to all connected clients.
 *
 * Builds a JSON object containing a message type, state, and current UTC
 * timestamp (ISO 8601), serializes it, and sends it over the WebSocket server.
 *
 * @param[in] type   The message category (e.g., "transmit", "status").
 * @param[in] state  The message state or payload (e.g., "starting", "finished").
 *
 * @note Requires <nlohmann/json.hpp>, <chrono>, <ctime>, <iomanip>, and <sstream>.
 */
void send_ws_message(std::string type, std::string state)
{
    // Build JSON payload
    nlohmann::json j;
    j["type"] = type;
    j["state"] = state;

    // Capture current UTC time and format as ISO 8601 (YYYY-MM-DDThh:mm:ssZ)
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&now_t, &tm_utc);

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    j["timestamp"] = oss.str();

    // Serialize and send to all WebSocket clients
    const std::string message = j.dump();
    socketServer.sendAllClients(message);
}

/**
 * @brief Retrieve the next center frequency, cycling through the configured list.
 *
 * This method returns the next frequency from `config.center_freq_set` in a
 * round-robin fashion.  It uses `freq_iterator` modulo the list size to
 * index into the vector, then increments `freq_iterator` for the subsequent call.
 *
 * @return double
 *   - Next frequency in Hz from the list.
 *   - Returns 0.0 if the list is empty.
 *
 * @note
 *   - `freq_iterator` should be initialized to 0.
 *   - Wrapping is handled via the modulo operation.
 */
double next_frequency(bool reset)
{
    const auto &freqs = config.center_freq_set;
    if (freqs.empty())
    {
        return 0.0;
    }

    // Compute index in [0, freqs.size())
    const size_t idx = freq_iterator % freqs.size();

    // True whenever we’ve wrapped back around to the “first” slot
    if (idx == 0 && !reset)
    {
        // Check if we are doing transmission iterations
        if (!config.use_ini && !config.loop_tx)
        {
            // Atomically decrement and grab the new value:
            int remaining = --config.tx_iterations;

            if (remaining <= 0)
            {
                llog.logS(INFO, "Completed last of TX iterations, signalling shutdown.");
                request_wspr_shutdown("completed configured TX iterations");
            }
            else
            {
                llog.logS(INFO, "WSPR transmissions remaining:", remaining);
            }
        }
    }

    // Fetch frequency
    double freq = freqs[idx];

    // Advance iterator
    ++freq_iterator;

    return freq;
}

/**
 * @brief Apply updated transmission parameters and reinitialize DMA.
 *
 * Retrieves the current PPM value if NTP calibration is enabled, captures
 * the latest configuration settings, and reconfigures the WSPR transmitter
 * with the specified frequency and parameters.
 *
 * @throws std::runtime_error if DMA setup or mailbox operations fail within
 *         `setupTransmission()`.
 */
void set_config(bool force)
{
    // Exit if we are shutting down
    if (exiting_wspr.load())
    {
        llog.logS(DEBUG, "Exiting set_config() early.");
        return;
    }
    else
    {
        llog.logS(DEBUG, "Processing set_config().");
    }

    bool do_config = false;
    bool do_random = false;
    if (force)
    {
        do_config = true;
        freq_iterator = 0;
        current_frequency = 0.0;

        if (config.use_ini)
        {
            std::string load_error;
            std::vector<std::string> warning_messages;
            if (!load_json(config.ini_filename, &load_error, &warning_messages))
            {
                for (const auto &warning_message : warning_messages)
                {
                    llog.logS(WARN, warning_message);
                }

                llog.logS(ERROR,
                          "Configuration reload failed; keeping current config:",
                          load_error);
                config_to_json();
                json_to_ini();
                return;
            }

            for (const auto &warning_message : warning_messages)
            {
                llog.logS(WARN, warning_message);
            }
        }
    }

    // Track actual PPM manager runtime state rather than inferring it from the
    // just-loaded config. Startup and reload both need to enable/disable the
    // subsystem based on whether it is already running.
    const bool ppm_running = ppmManager.isRunning();

    // If we are reloading from INI:
    if (ini_reload_pending.load())
    {
        do_config = true;

        if (config.use_ini)
        {
            std::string load_error;
            std::vector<std::string> warning_messages;
            if (!load_json(config.ini_filename, &load_error, &warning_messages))
            {
                for (const auto &warning_message : warning_messages)
                {
                    llog.logS(WARN, warning_message);
                }

                llog.logS(ERROR,
                          "Configuration reload failed; keeping current config:",
                          load_error);
                config_to_json();
                json_to_ini();
                ini_reload_pending.store(false, std::memory_order_relaxed);
                send_ws_message("configuration", "reload_failed");
                return;
            }

            for (const auto &warning_message : warning_messages)
            {
                llog.logS(WARN, warning_message);
            }
        }

        if (!validate_config_data())
        {
            llog.logE(ERROR, "Configuration validation failed.");
            config.transmit = false;
            config_to_json();
        }
    }

    // See if we need to start NTP (chrony) monitoring
    if (config.use_ntp && !ppm_running)
    {
        ppm_init();
        ppm_reload_pending.store(true, std::memory_order_seq_cst);
    }
    // Or, see if we need to stop it
    else if (!config.use_ntp && ppm_running)
    {
        ppmManager.stop();
        llog.logS(INFO, "PPM Manager disabled.");
        ppm_reload_pending.store(false, std::memory_order_seq_cst);
    }
    else if (force && !config.use_ntp)
    {
        llog.logS(INFO, "PPM Manager disabled.");
    }

    // Update PPM if a change was noted
    if (ppm_reload_pending.load())
    {
        config.ppm = ppmManager.getCurrentPPM();
        wsprTransmitter.applyPpmCorrection(config.ppm);
        llog.logS(INFO, "PPM updated:", config.ppm);

        // Clear pending ppm flags
        ppm_reload_pending.store(false, std::memory_order_relaxed);
    }

    // Get next frequency and indicate if we are (re)setting the stack
    static double last_freq = 0.0;
    current_frequency = next_frequency(force);
    if (current_frequency != last_freq)
    {
        last_freq = current_frequency;
        do_config = true;
    }
    else if (config.use_offset && current_frequency != 0.0)
    {
        // Allow randomization as/if needed
        do_random = true;
    }

    // If we have a change, do setup
    if (do_config || do_random)
    {
        // Do DMA configuration
        wsprTransmitter.configure(
            current_frequency,
            config.power_level,
            config.ppm,
            config.callsign,
            config.grid_square,
            config.power_dbm,
            config.use_offset);

        if (!bandGPIOSelector.prepareFrequency(current_frequency))
        {
            llog.logS(WARN,
                      "Unable to prepare band GPIO for ",
                      wsprTransmitter.formatFrequencyMHz(current_frequency),
                      " MHz.");
        }
    }

    // Enable/disable transmit if/as needed
    if (config.transmit && (do_config || do_random))
    {
        if (do_random)
        {
            llog.logS(DEBUG, "New random frequency.");
        }
        else
        {
            llog.logS(DEBUG, "Setup complete.");
        }
        llog.logS(INFO, "Waiting for next transmission window.");
        wsprTransmitter.startAsync();
    }
    else if (!config.transmit && (do_config || do_random))
    {
        llog.logS(INFO, "Transmissions disabled.");
    }
#ifdef DEBUG_WSPR_TRANSMIT
    wsprTransmitter.dumpParameters();
#endif
}
