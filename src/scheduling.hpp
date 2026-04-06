/**
 * @file scheduling.hpp
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

#ifndef _SCHEDULING_HPP
#define _SCHEDULING_HPP

// Project headers
#include "arg_parser.hpp"
#include "ppm_manager.hpp"

// Standard library headers
#include <atomic>
#include <condition_variable>
#include <string>
#include <string_view>
#include <thread>

/**
 * @brief Mutex to protect access to the shutdown flag for the WSPR loop.
 *
 * This mutex must be locked before reading or writing \c exitwspr_ready
 * to ensure thread-safe coordination between the signal handler callback
 * and the WSPR loop.
 */
extern std::mutex exitwspr_mtx;

/**
 * @brief Condition variable used to signal the WSPR loop to exit.
 *
 * The signal handler callback will notify this condition variable after
 * setting \c exitwspr_ready to \c true, causing the waiting WSPR loop
 * to wake up and perform shutdown.
 */
extern std::condition_variable exitwspr_cv;

/**
 * @brief Atomic bool used to signal other functions that we are shutting down.
 */
extern std::atomic<bool> exiting_wspr;

/**
 * @brief Flag indicating whether the WSPR loop should terminate.
 *
 * Set to \c true by the signal handler callback under protection of
 * \c exitwspr_mtx, then \c exitwspr_cv is notified so that the WSPR
 * loop can break out of its wait and begin shutdown.
 */
extern bool exitwspr_ready;

/**
 * @brief Atomic bool used to signal other functions that we are shutting down.
 */
extern std::atomic<bool> exiting;

/**
 * @brief Flag indicating if a system shutdown is in progress.
 *
 * @details
 * This atomic flag is used throughout the application to signal when a
 * full system shutdown has been initiated. It is typically set during a
 * GPIO-triggered shutdown or from a critical failure path.
 *
 * Other threads can poll or wait on this flag to terminate safely.
 */
extern std::atomic<bool> shutdown_flag;

/**
 * @brief Callback triggered by a shutdown GPIO event.
 *
 * @details
 * This function is executed when a shutdown pin is activated.
 * It performs visual signaling (e.g., LED blinks), sets shutdown flags,
 * and notifies waiting threads to begin system shutdown procedures.
 *
 * @note This is usually registered with a GPIO monitor.
 */
extern void callback_shutdown_system();

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
 * - Toggles the LED 3 times with 100ms intervals.
 * - Sets `exitwspr_cv` to break out of the main transmission loop.
 * - Sets `shutdown_flag` to mark that a full system shutdown is in progress.
 *
 * @note
 * The LED toggling uses `ledControl.toggleGPIO()` and assumes the hardware
 * supports it.
 */
void shutdown_system();

/**
 * @brief Request a coordinated WSPR loop shutdown and emit an early log.
 *
 * This helper is safe to call from normal thread context, including the
 * dedicated signal-wait thread. It records that shutdown is in progress,
 * emits an INFO-level reason, and wakes the main loop so cleanup starts
 * before later teardown steps can suppress logs.
 *
 * @param reason Human-readable source of the shutdown request.
 * @return true if this call initiated shutdown, false if shutdown was
 *         already in progress.
 */
bool request_wspr_shutdown(std::string_view reason);

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
 * - Sets `exitwspr_cv` to break out of the main wspr_scheduler loop.
 * - Sets `reboot_flag` to mark that a full system reboot is in progress.
 *
 * @note
 * The LED toggling uses `ledControl.toggleGPIO()` and assumes the hardware
 * supports it.
 */
void reboot_system();

/**
 * @brief Initializes the PPM manager and registers a callback.
 *
 * @details
 * This function attempts to initialize the `ppmManager`, which is responsible
 * for calculating or retrieving the system's PPM (parts per million) drift
 * for accurate frequency generation.
 *
 * The initialization result is evaluated, and appropriate logging is performed
 * based on the returned `PPMStatus`. Critical failure conditions such as
 * high PPM or lack of time synchronization cause the function to return `false`.
 * If successful or recoverable, the PPM callback is registered.
 *
 * @return `true` if initialization succeeded or fallback is acceptable.
 * @return `false` if a critical error was detected (e.g., high PPM or unsynced time).
 *
 * @note
 * The `ppm_callback()` will be triggered later to handle live updates to PPM.
 */
bool ppm_init();

/**
 * @brief   Initiates a continuous test‐tone transmission.
 *
 * Stops any ongoing transmission, saves the current mode,
 * switches into TONE mode, and transmits on the first
 * configured frequency using the current power and PPM.
 */
extern void start_test_tone();

/**
 * @brief   Ends the test‐tone and restores the previous mode.
 *
 * If we’re in test‐tone, shut it down, clear the flag,
 * restore lastMode, and re-configure either WSPR or
 * a transient direct-tone startup request if one was active before.
 */
extern void end_test_tone();

/**
 * @brief Runs the main WSPR scheduler and transmission loop.
 *
 * @details
 * Coordinates all core runtime components:
 * - Initializes optional NTP/PPM drift correction.
 * - Starts the TCP command server and sets its priority.
 * - Launches the WSPR transmission thread.
 * - Performs full cleanup and shutdown sequence before exiting.
 *
 * @note This function blocks and runs until `exitwspr_cv` notifies.
 */
extern bool wspr_loop();

/**
 * @brief Synchronize disk and reboot the machine.
 *
 * This function calls sync() to flush filesystem buffers, then
 * invokes the reboot(2) syscall directly. The process must have
 * the CAP_SYS_BOOT capability (typically run as root).
 */
void reboot_machine();

/**
 * @brief Flush filesystems and power off the machine.
 *
 * Calls sync() to ensure all disk buffers are written, then invokes
 * the reboot(2) syscall with the POWER_OFF command. Requires root or
 * the CAP_SYS_BOOT capability.
 */
void shutdown_machine();

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
void send_ws_message(std::string type, std::string state);

/**
 * @brief Apply updated transmission parameters and reinitialize DMA.
 *
 * Retrieves the current PPM value if NTP calibration is enabled, captures
 * the latest configuration settings, and reconfigures the WSPR transmitter
 * with the specified frequency and parameters.
 *
 * @param initial Call with 'true' if this is the first run
 *
 * @throws std::runtime_error if DMA setup or mailbox operations fail within
 *         `configure()`.
 */
void set_config(bool force = false);

#endif // _SCHEDULING_HPP
