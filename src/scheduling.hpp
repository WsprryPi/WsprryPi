/**
 * @file scheduling.hpp
 * @brief Orchestration layer for planning and committing transmissions.
 *
 * This layer owns scheduling and request construction for the current
 * architecture. It decides whether a slot runs WSPR or direct tone,
 * applies any random WSPR offset, resolves band-selector GPIO state from
 * the scheduler source frequency, and commits the single execution
 * request consumed by the transmitter.
 *
 * The transmitter executes only already-committed requests. Hardware
 * realization remains inside the backend. All execution must cross the
 * scheduler-to-transmitter boundary as a committed request.
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
#include "transmission_request.hpp"
#include "wspr_transmit_types.hpp"

// Standard library headers
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
extern std::atomic<bool> reboot_flag;

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
 * @brief Start a transient runtime test tone from the orchestration layer.
 *
 * Stops any ongoing execution, preserves the previous runtime mode, and
 * commits a tone request built from the first configured scheduler
 * frequency entry. This is transient runtime behavior; it does not
 * persist tone mode into configuration files.
 */
struct TestToneStartResult
{
    bool started = false;
    bool already_active = false;
    bool blocked_by_active_transmission = false;
    bool blocked_by_enabled_transmission = false;
    std::string message;
};

TestToneStartResult start_test_tone();

/**
 * @brief End the transient runtime test tone and restore prior flow.
 *
 * Stops the active tone, tears down scheduler-owned selector GPIO state,
 * restores the previous runtime mode, and then resumes either normal WSPR
 * orchestration or the transient direct-tone startup request that was
 * active before the web-triggered tone.
 */
struct TestToneStopResult
{
    bool stopped = false;
    bool tone_was_active = false;
    bool scheduler_restored = false;
    bool deferred_reload_reconciled = false;
    std::string message;
};

TestToneStopResult end_test_tone();

/**
 * @brief Run the main orchestration loop.
 *
 * @details
 * Coordinates all core runtime components:
 * - Validates startup configuration.
 * - Initializes optional NTP/PPM drift correction.
 * - Starts the TCP command server and sets its priority.
 * - Commits the initial execution request for WSPR or direct tone.
 * - Launches the transmitter using only committed requests.
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
void send_ws_message(
    std::string type,
    std::string state,
    std::string message = std::string(),
    std::optional<int> cw_active_char_index_override = std::nullopt);

std::string websocket_tx_state_for_message(
    std::string_view type,
    std::string_view state,
    std::string_view current_tx_state);

struct StopTransmissionResult
{
    bool transmission_active = false;
    bool stop_performed = false;
    bool transmit_disabled = false;
    bool persisted = false;
    std::string message;
};

StopTransmissionResult stop_transmission_by_user_request(bool persist_transmit = true);

struct WsprRuntimeStatusSnapshot
{
    std::string tx_state;
    std::string runtime_mode;
    std::string next_transmission_at;
    double frequency_hz = 0.0;
    double offset_hz = 0.0;
    bool frequency_is_skip = false;
    std::string plan_type;
    int power_dbm = 0;
    std::size_t frame_count = 0;
    std::size_t current_frame = 0; // 1-based, 0 when unavailable
    std::string callsign_raw;
    std::string callsign_normalized;
    std::string locator_raw;
    std::string locator_normalized;
    std::string frame_callsign;
    std::string frame_locator;
    std::string cw_message;
    int cw_active_char_index = -1;
};

WsprRuntimeStatusSnapshot current_tx_runtime_status_snapshot();

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
bool set_config(bool force = false);
bool compute_non_wspr_message_duration(
    const ArgParserConfig &cfg,
    std::chrono::nanoseconds &duration_out,
    std::string *error_message = nullptr);
bool validate_non_wspr_repeat_interval_policy(
    const ArgParserConfig &cfg,
    std::string *error_message = nullptr);
bool web_server_start_enabled(const ArgParserConfig &cfg) noexcept;
bool websocket_server_start_enabled(const ArgParserConfig &cfg) noexcept;
bool transmitter_reload_should_defer() noexcept;
void transmitter_cb(WsprTransmissionCallbackEvent event,
                    WsprTransmitLogLevel level,
                    const std::string &msg,
                    double value);

bool managed_reload_tx_inhibited_state() noexcept;
bool managed_reload_tx_inhibited_for_test() noexcept;
void reset_managed_reload_runtime_for_test() noexcept;
void set_scheduler_execution_suppressed_for_test(bool suppressed) noexcept;

enum class CommittedExecutionRouteForTest
{
    NONE = 0,
    LEGACY,
    CONTROLLER_WSPR,
    CONTROLLER_TONE,
};

CommittedExecutionRouteForTest committed_execution_route_for_test() noexcept;
void reset_committed_execution_route_for_test() noexcept;
std::size_t tx_led_assert_request_count_for_test() noexcept;
std::size_t tx_led_deassert_request_count_for_test() noexcept;
std::size_t tx_led_failure_count_for_test() noexcept;
void reset_tx_led_request_counts_for_test() noexcept;
bool tx_led_active_for_test() noexcept;
bool reconcile_tx_led_after_transmitter_stop_for_test(const char *context) noexcept;

void reset_band_gpio_prepare_call_count_for_test() noexcept;
std::size_t band_gpio_prepare_call_count_for_test() noexcept;

void set_band_gpio_selector_for_test(bool enabled, bool drive_gpio) noexcept;

bool current_band_gpio_selection_for_test(
    BandGPIOConfig &config_out,
    std::string &band_label_out) noexcept;
std::vector<BandGPIOConfig> initialized_selector_gpios_for_test();
bool selector_gpio_logical_state_for_test(
    int gpio,
    bool &logical_state_out) noexcept;
void stop_active_transmission_selectors_for_test() noexcept;
void stop_runtime_components_for_test() noexcept;
bool park_active_transmission_selectors_for_test() noexcept;
bool restore_committed_band_gpio_selection_for_test(bool assert_state) noexcept;
std::vector<BandGPIOConfig> selector_shutdown_cleanup_targets_for_test();
void seed_selector_shutdown_state_for_test(
    const BandGPIOConfig &active_config,
    const std::vector<BandGPIOConfig> &idle_configs) noexcept;
void run_final_selector_gpio_shutdown_cleanup_for_test() noexcept;
void clear_current_wspr_runtime_state_for_test() noexcept;
TransmissionRequest current_transmission_request_for_test();
std::optional<wsprrypi::TransmissionRequest> current_controller_request_for_test();
void reset_current_transmission_request_for_test() noexcept;
void reset_current_controller_request_for_test() noexcept;

#endif // _SCHEDULING_HPP
