/**
 * @file scheduling.cpp
 * @brief Orchestration layer for planning and committing transmissions.
 *
 * This file owns planning policy for the current architecture. It is the
 * only layer that decides:
 * - Auto versus RequirePaired WSPR planning.
 * - WSPR versus direct-tone execution mode.
 * - Random WSPR RF offset application.
 * - Per-frequency control GPIO metadata and selector preparation.
 * - When a built request is committed to the transmitter.
 *
 * The transmitter only consumes committed `WsprTransmissionRequest`
 * snapshots. The backend only realizes hardware for the backend-neutral
 * execution plan derived from that committed request.
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
#include "frequency_semantics.hpp"
#include "gpio_input.hpp"
#include "gpio_output.hpp"
#include "logging.hpp"
#include "ppm_manager.hpp"
#include "signal_handler.hpp"
#include "wspr_reference_adapter.hpp"
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
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
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
static void log_selected_frequency_entry_gpio(
    const WsprDialFrequencyEntry &entry);

/**
 * @brief Runtime owner for the currently prepared per-frequency selector GPIO.
 *
 * Scheduling prepares and tears down this selector around the committed
 * request for the active slot. It is intentionally file-local so per-
 * frequency control remains an orchestration concern rather than a
 * transmitter or backend policy concern.
 */
class FrequencyEntryGPIOSelector
{
public:
    bool prepare(const WsprDialFrequencyEntry &entry, bool active_high)
    {
        stop();
        last_error_.clear();

        if (entry.control_gpio == kFrequencyEntryControlGpioUnset)
        {
            return true;
        }

        if (!gpio_.enableGPIOPin(entry.control_gpio, active_high))
        {
            last_error_ = gpio_.lastError();
            return false;
        }

        has_gpio_ = true;
        return true;
    }

    bool setState(bool state)
    {
        if (!has_gpio_)
        {
            return true;
        }

        return gpio_.toggleGPIO(state);
    }

    void stop()
    {
        if (!has_gpio_)
        {
            return;
        }

        gpio_.stop();
        has_gpio_ = false;
    }

    const std::string &lastError() const noexcept
    {
        return last_error_;
    }

private:
    GPIOOutput gpio_{};
    bool has_gpio_ = false;
    std::string last_error_{};
};

static FrequencyEntryGPIOSelector frequencyEntryGPIOSelector;

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
 * @brief Round‐robin index into the configured WSPR dial-frequency list.
 *
 * Tracks which entry in the `config.wspr_dial_freq_set` vector will be
 * used for the next WSPR transmission.  Wraps via modulo on each use.
 */
int freq_iterator = 0;

/**
 * @brief Currently active WSPR dial frequency (in Hz).
 *
 * Holds the last dial frequency selected by the scheduler.
 * A zero value indicates that no frequency is configured or the list was empty.
 */
double current_dial_frequency = 0.0;
WsprDialFrequencyEntry current_frequency_entry{};
WsprTransmissionRequest current_transmission_request{};

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
std::atomic<bool> shutdown_after_current_transmission{false};
std::atomic<bool> shutdown_after_wspr_plan{false};
/**
 * @brief Scheduler-owned paired WSPR plan being continued across slots.
 *
 * When a paired plan is selected, the scheduler saves the full prepared
 * plan and the frequency entry that produced it. The second slot reuses
 * this saved scheduler state instead of asking the planner for a new
 * policy decision.
 */
PreparedWsprTransmission active_wspr_plan{};
std::size_t active_wspr_frame_index = 0;
double active_wspr_plan_dial_frequency = 0.0;
WsprDialFrequencyEntry active_wspr_plan_frequency_entry{};
bool active_wspr_plan_in_progress = false;

/**
 * @brief Tear down every selector prepared for the active committed request.
 *
 * This is the single teardown path for scheduler-owned selector GPIO state.
 * Any code that needs to release band-selection or per-frequency control
 * GPIOs must call this helper rather than stopping selectors individually.
 */
static void stop_active_transmission_selectors() noexcept
{
    bandGPIOSelector.setBandState(false);
    bandGPIOSelector.stop();
    frequencyEntryGPIOSelector.setState(false);
    frequencyEntryGPIOSelector.stop();
}

/**
 * @brief Prepare the per-frequency control GPIO for the next committed slot.
 *
 * This helper logs the selected entry metadata and prepares the transient
 * runtime selector state that belongs to the current scheduler slot. It
 * does not start transmission or commit a request by itself.
 *
 * @param entry Frequency entry selected by the scheduler.
 * @param failure_level Log level used if preparation fails.
 * @return `true` if no GPIO is needed or preparation succeeded.
 */
static bool prepare_frequency_entry_gpio_or_log(
    const WsprDialFrequencyEntry &entry,
    LogLevel failure_level)
{
    log_selected_frequency_entry_gpio(entry);
    if (frequencyEntryGPIOSelector.prepare(
            entry,
            config.tx_freq_control_active_high))
    {
        return true;
    }

    llog.logS(
        failure_level,
        "Unable to prepare frequency entry control GPIO ",
        entry.control_gpio,
        " for ",
        entry.token,
        ".",
        frequencyEntryGPIOSelector.lastError().empty()
            ? ""
            : std::string(" ") + frequencyEntryGPIOSelector.lastError());
    return false;
}

/**
 * @brief Commit the single execution request consumed by the transmitter.
 *
 * This is the execution boundary between orchestration and transmitter
 * layers. All WSPR and tone execution must pass through this helper so the
 * transmitter only ever sees a complete, scheduler-owned request snapshot.
 *
 * @param request Fully built execution request for one transmitter run.
 */
static void commit_execution_request(
    const WsprTransmissionRequest &request)
{
    current_transmission_request = request;
    wsprTransmitter.configureExecution(current_transmission_request);
}

static void reset_active_wspr_plan_state()
{
    active_wspr_plan = PreparedWsprTransmission{};
    active_wspr_frame_index = 0;
    active_wspr_plan_dial_frequency = 0.0;
    active_wspr_plan_frequency_entry = WsprDialFrequencyEntry{};
    active_wspr_plan_in_progress = false;
}

static bool active_wspr_plan_has_more_frames_after_current() noexcept
{
    return active_wspr_plan_in_progress &&
           (active_wspr_frame_index + 1U) < active_wspr_plan.frameCount();
}

static WsprDialFrequencyEntry next_frequency_entry(bool reset);

/**
 * @brief Return the prepared plan for a single frame from a saved plan.
 *
 * The scheduler uses this when continuing a paired transmission so each
 * committed request still represents exactly one execution slot even when
 * the planner originally returned multiple frames.
 */
static PreparedWsprTransmission slot_plan_for_frame(
    const PreparedWsprTransmission &plan,
    std::size_t frame_index)
{
    PreparedWsprTransmission slot_plan;
    slot_plan.plan_type = plan.plan_type;
    slot_plan.callsign = plan.callsign;
    slot_plan.locator = plan.locator;
    slot_plan.power_dbm = plan.power_dbm;
    slot_plan.frames.push_back(plan.frames.at(frame_index));
    return slot_plan;
}

static bool is_auto_paired_upgrade_eligible() noexcept
{
    return config.mode == ModeType::WSPR &&
           (config.callsign.find('/') != std::string::npos) &&
           config.grid_square.size() == 6U;
}

void consume_tx_iteration_if_needed()
{
    if (config.use_ini || config.loop_tx)
    {
        return;
    }

    if (config.tx_iterations.load(std::memory_order_acquire) <= 0)
    {
        return;
    }

    int remaining = --config.tx_iterations;

    if (remaining <= 0)
    {
        if (active_wspr_plan_has_more_frames_after_current())
        {
            shutdown_after_wspr_plan.store(true, std::memory_order_release);
            llog.logS(
                INFO,
                "Completed last of TX iterations, signalling shutdown "
                "after paired transmission.");
        }
        else
        {
            shutdown_after_current_transmission.store(
                true,
                std::memory_order_release);
            llog.logS(
                INFO,
                "Completed last of TX iterations, signalling shutdown "
                "after current transmission.");
        }
    }
    else
    {
        llog.logS(INFO, "WSPR transmissions remaining:", remaining);
    }
}

static void log_selected_frequency_entry_gpio(
    const WsprDialFrequencyEntry &entry)
{
    if (entry.control_gpio == kFrequencyEntryControlGpioUnset)
    {
        llog.logS(INFO,
                  "Selected frequency entry control GPIO: none for ",
                  entry.token,
                  ".");
        return;
    }

    llog.logS(INFO,
              "Selected frequency entry control GPIO: ",
              entry.control_gpio,
              " (",
              config.tx_freq_control_active_high ? "active high" : "active low",
              ") for ",
              entry.token,
              ".");
}

static double maybe_apply_wspr_random_offset(double actual_rf_frequency_hz)
{
    if (!config.use_offset || actual_rf_frequency_hz == 0.0)
    {
        return actual_rf_frequency_hz;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-1.0, 1.0);
    return actual_rf_frequency_hz + dis(gen) * kWsprRandomOffsetHz;
}

/**
 * @brief Build the scheduler-side request for a direct tone execution.
 *
 * This request is fully committed at the orchestration layer. The
 * transmitter must not infer any additional policy from tone mode.
 */
static WsprTransmissionRequest make_tone_request(
    double actual_rf_frequency_hz,
    double dial_frequency_hz,
    const WsprDialFrequencyEntry &entry)
{
    WsprTransmissionRequest request;
    request.mode = WsprTransmissionMode::TONE;
    request.dial_frequency_hz = dial_frequency_hz;
    request.actual_rf_frequency_hz = actual_rf_frequency_hz;
    request.ppm = config.ppm;
    request.power_level = config.power_level;
    request.tx_gpio = config.tx_pin;
    request.frequency_control_gpio = entry.control_gpio;
    request.frequency_control_active_high = config.tx_freq_control_active_high;
    request.frequency_entry_label = entry.token;
    return request;
}

/**
 * @brief Build the startup direct-tone request from transient CLI state.
 *
 * Startup tone mode is transient runtime state created by `--test-tone`.
 * It is not persistent configuration.
 */
static WsprTransmissionRequest make_direct_tone_request(
    double actual_rf_frequency_hz)
{
    WsprDialFrequencyEntry entry;
    double ignored_actual_rf_frequency_hz = 0.0;
    if (try_get_direct_tone_startup_request(entry, ignored_actual_rf_frequency_hz))
    {
        return make_tone_request(
            actual_rf_frequency_hz,
            actual_rf_frequency_hz,
            entry);
    }

    return make_tone_request(
        actual_rf_frequency_hz,
        actual_rf_frequency_hz,
        WsprDialFrequencyEntry{});
}

/**
 * @brief Build the scheduler-side request for one WSPR execution slot.
 *
 * The request captures all execution-time state, including the prepared
 * WSPR frame for this slot, the committed RF frequency, and scheduler-owned
 * per-frequency GPIO metadata.
 */
static WsprTransmissionRequest make_wspr_request(
    const PreparedWsprTransmission &slot_plan,
    double dial_frequency_hz,
    double actual_rf_frequency_hz,
    const WsprDialFrequencyEntry &entry,
    double applied_offset_hz)
{
    WsprTransmissionRequest request;
    request.mode = WsprTransmissionMode::WSPR;
    request.wspr_plan = slot_plan;
    request.dial_frequency_hz = dial_frequency_hz;
    request.actual_rf_frequency_hz = actual_rf_frequency_hz;
    request.ppm = config.ppm;
    request.power_level = config.power_level;
    request.tx_gpio = config.tx_pin;
    request.use_offset = config.use_offset;
    request.applied_offset_hz = applied_offset_hz;
    request.frequency_control_gpio = entry.control_gpio;
    request.frequency_control_active_high = config.tx_freq_control_active_high;
    request.frequency_entry_label = entry.token;
    return request;
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

/**
 * @brief Build the next committed WSPR request for the current slot.
 *
 * This is scheduler policy code. It chooses Auto versus RequirePaired,
 * records paired continuation state, and builds exactly one slot-scoped
 * execution request. If a paired plan spans multiple slots, later slots
 * reuse the saved scheduler plan instead of making a new planning choice.
 *
 * @param actual_rf_frequency_hz RF frequency already chosen by the scheduler.
 * @param request_out Receives the committed request snapshot for one slot.
 * @return `true` if the request was built successfully.
 */
static bool configure_current_wspr_transmission(
    double actual_rf_frequency_hz,
    WsprTransmissionRequest &request_out)
{
    try
    {
        PreparedWsprTransmission plan;
        PreparedWsprTransmission slot_plan;
        bool paired_requested = config.require_paired_plan;
        bool auto_upgraded = false;

        if (active_wspr_plan_in_progress)
        {
            // Continue the already selected paired plan. This reuses saved
            // scheduler state and does not invoke a new planning policy
            // decision for the second slot.
            plan = active_wspr_plan;
            slot_plan = slot_plan_for_frame(plan, active_wspr_frame_index);

            llog.logS(INFO,
                      "Scheduling paired WSPR frame ",
                      static_cast<int>(active_wspr_frame_index + 1U),
                      " of ",
                      static_cast<int>(plan.frameCount()),
                      " for the next WSPR slot.");
        }
        else
        {
            if (paired_requested)
            {
                llog.logS(INFO,
                          "Paired WSPR planning explicitly requested.");

                plan = build_prepared_wspr_transmission(
                    config.callsign,
                    config.grid_square,
                    config.power_dbm,
                    wspr::TransmissionPlanPreference::RequirePaired);
            }
            else
            {
                const bool paired_upgrade_eligible =
                    is_auto_paired_upgrade_eligible();

                try
                {
                    plan = build_prepared_wspr_transmission(
                        config.callsign,
                        config.grid_square,
                        config.power_dbm,
                        wspr::TransmissionPlanPreference::Auto);
                }
                catch (const std::exception &)
                {
                    if (!paired_upgrade_eligible)
                        throw;

                    llog.logS(
                        INFO,
                        "Auto-upgrading to paired WSPR plan because "
                        "callsign is compound and locator is 6 characters.");

                    PreparedWsprTransmission paired_plan =
                        build_prepared_wspr_transmission(
                            config.callsign,
                            config.grid_square,
                            config.power_dbm,
                            wspr::TransmissionPlanPreference::RequirePaired);

                    plan = std::move(paired_plan);
                    auto_upgraded = true;
                }

                if (!auto_upgraded &&
                    plan.frameCount() <= 1U &&
                    paired_upgrade_eligible)
                {
                    llog.logS(
                        INFO,
                        "Auto-upgrading to paired WSPR plan because "
                        "callsign is compound and locator is 6 characters.");

                    PreparedWsprTransmission paired_plan =
                        build_prepared_wspr_transmission(
                            config.callsign,
                            config.grid_square,
                            config.power_dbm,
                            wspr::TransmissionPlanPreference::RequirePaired);

                    if (paired_plan.frameCount() > 1U)
                    {
                        plan = std::move(paired_plan);
                        auto_upgraded = true;
                    }
                }
            }

            if (plan.frameCount() > 1U)
            {
                active_wspr_plan = plan;
                active_wspr_frame_index = 0;
                active_wspr_plan_dial_frequency = current_dial_frequency;
                active_wspr_plan_frequency_entry = current_frequency_entry;
                active_wspr_plan_in_progress = true;
            }
            else
            {
                reset_active_wspr_plan_state();
            }

            slot_plan = slot_plan_for_frame(plan, active_wspr_frame_index);
        }

        llog.logS(INFO,
                  "Selected WSPR plan: ",
                  plan.plan_type,
                  ", frames: ",
                  static_cast<int>(plan.frames.size()),
                  ", paired requested: ",
                  paired_requested ? "true" : "false",
                  ", auto-upgraded: ",
                  auto_upgraded ? "true" : "false",
                  ".");

        request_out = make_wspr_request(
            slot_plan,
            current_dial_frequency,
            actual_rf_frequency_hz,
            current_frequency_entry,
            0.0);

        if (plan.frameCount() > 1U)
        {
            llog.logS(
                INFO,
                "Prepared paired WSPR transmission with ",
                static_cast<int>(plan.frameCount()),
                " frames using plan ",
                plan.plan_type,
                ".");
        }

        return true;
    }
    catch (const std::exception &e)
    {
        reset_active_wspr_plan_state();
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        llog.logE(ERROR, "WSPR encoding/configuration failed: ", e.what());
        return false;
    }
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

        if (config.mode == ModeType::WSPR &&
            (!active_wspr_plan_in_progress || active_wspr_frame_index == 0U))
        {
            consume_tx_iteration_if_needed();
        }

        // Assert the precomputed band GPIO.
        bandGPIOSelector.setBandState(true);
        frequencyEntryGPIOSelector.setState(true);

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
        if (!msg.empty() && elapsed != 0.0)
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

        // Deassert and release the prepared selectors.
        stop_active_transmission_selectors();

        // Turn off LED.
        ledControl.toggleGPIO(false);

        // Notify the websocket clients.
        send_ws_message("transmit", "finished");

        const bool shutdown_when_idle =
            shutdown_after_current_transmission.exchange(false, std::memory_order_acq_rel);
        const bool shutdown_when_plan_finishes =
            shutdown_after_wspr_plan.load(std::memory_order_acquire);

        if (do_config && active_wspr_plan_has_more_frames_after_current())
        {
            ++active_wspr_frame_index;
        }
        else if (active_wspr_plan_in_progress)
        {
            reset_active_wspr_plan_state();
        }

        if (shutdown_when_idle && do_config)
        {
            request_wspr_shutdown("completed configured TX iterations");
            do_config = false;
        }
        else if (shutdown_when_plan_finishes && do_config &&
                 !active_wspr_plan_in_progress)
        {
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            request_wspr_shutdown("completed configured TX iterations");
            do_config = false;
        }

        // Set config will determine if we have work to do.
        if (do_config)
            set_config();

        break;
    }

    case WsprTransmitter::TransmissionCallbackEvent::CANCELLED:
    {
        const double elapsed = value;
        const std::string s_elapsed = format_elapsed(elapsed);

        llog.logS(to_log_level(level),
                  "Transmission cancelled after ",
                  s_elapsed,
                  " seconds.");

        stop_active_transmission_selectors();
        ledControl.toggleGPIO(false);
        send_ws_message("transmit", "canceled");

        shutdown_after_current_transmission.store(false, std::memory_order_release);
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();

        break;
    }

    case WsprTransmitter::TransmissionCallbackEvent::SKIPPED:
    {
        // Deassert and release the prepared selectors.
        stop_active_transmission_selectors();

        // Turn off LED in case the UI/state expects an idle indication.
        ledControl.toggleGPIO(false);

        if (!msg.empty())
            llog.logS(to_log_level(level), msg, ".");
        else
            llog.logS(to_log_level(level), "Skipping transmission.");

        // Notify websocket clients.
        send_ws_message("transmit", "skipped");

        shutdown_after_current_transmission.store(false, std::memory_order_release);
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();

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
 * @brief Start a transient runtime tone using scheduler-owned setup.
 *
 * The scheduler stops any active run, reuses the first configured
 * frequency entry, prepares selector GPIO state, commits a tone request,
 * and starts the transmitter. Tone mode here is runtime-only behavior.
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

        // Pick the first configured scheduler frequency entry, then commit
        // the resolved RF frequency into the request before execution.
        const WsprDialFrequencyEntry entry =
            next_frequency_entry(/*restart=*/true);
        const double dial_freq = entry.dial_frequency_hz;
        current_frequency_entry = entry;
        const double actual_rf_freq = resolve_actual_rf_frequency_hz(
            dial_freq,
            config.wspr_audio_offset_hz,
            FrequencyPath::WsprDial);

        while (
            wsprTransmitter.getState() ==
            WsprTransmitter::State::TRANSMITTING)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ledControl.toggleGPIO(true);
        llog.logS(INFO, "Beginning test tone requested by web UI.");

        // Switch into tone mode
        config.mode = ModeType::TONE;

        // Set up and start the tone
        llog.logS(
            DEBUG,
            "Resolved WSPR dial frequency ",
            lookup.freq_display_string(dial_freq),
            " to actual RF ",
            lookup.freq_display_string(actual_rf_freq),
            " using audio offset ",
            config.wspr_audio_offset_hz,
            " Hz.");
        commit_execution_request(
            make_tone_request(actual_rf_freq, dial_freq, entry));

        (void)prepare_frequency_entry_gpio_or_log(entry, WARN);

        if (!bandGPIOSelector.prepareFrequency(dial_freq))
        {
            llog.logS(WARN,
                      "Unable to prepare band GPIO for WSPR dial frequency ",
                      lookup.freq_display_string(dial_freq),
                      ".");
        }

        wsprTransmitter.startAsync();
        llog.logS(INFO,
                  "WSPR-band test tone using dial frequency:",
                  lookup.freq_display_string(dial_freq));
        send_ws_message("transmit", "starting");
    }
}

/**
 * @brief End the transient runtime tone and restore prior orchestration.
 *
 * This stops the current tone, tears down selector lifecycle state through
 * the scheduler helper, and then resumes the pre-tone runtime mode.
 */
void end_test_tone()
{
    if (web_test_tone.load())
    {
        llog.logS(INFO, "Ending test tone requested by Web UI.");

        // Stop current tone
        wsprTransmitter.stopAndJoin();
        stop_active_transmission_selectors();
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
            WsprDialFrequencyEntry entry;
            double actual_rf_frequency_hz = 0.0;
            if (!try_get_direct_tone_startup_request(
                    entry,
                    actual_rf_frequency_hz))
            {
                llog.logE(ERROR,
                          "Unable to restore direct test tone; no "
                          "transient tone request is active.");
                return;
            }

            validate_config_data();
            commit_execution_request(
                make_direct_tone_request(actual_rf_frequency_hz));
            (void)prepare_frequency_entry_gpio_or_log(entry, WARN);
            wsprTransmitter.startAsync();

            llog.logS(INFO,
                      "Transmitting tone, hit Ctrl-C to terminate tone.");
        }
    }
}

/**
 * @brief Main orchestration loop for startup, scheduling, and shutdown.
 *
 * @details
 * This loop validates configuration, starts long-lived services, prepares
 * the initial committed execution request, and then runs until shutdown.
 * WSPR startup goes through the same reload-safe scheduling path used for
 * later reconfiguration so request construction remains centralized here.
 *
 * @note This function blocks until `exitwspr_cv` is set by another thread.
 */
bool wspr_loop()
{
    bool any_band_gpio_enabled = false;
    for (int i = 0; i < HAM_BAND_COUNT; ++i)
    {
        if (config.band_gpio[i].enabled && config.band_gpio[i].gpio >= 0)
        {
            any_band_gpio_enabled = true;
            break;
        }
    }

    bandGPIOSelector.setEnabled(any_band_gpio_enabled);
    bandGPIOSelector.setDriveGPIO(any_band_gpio_enabled);

    // Display the final configuration after parsing arguments and INI file.
    show_config_values();

    if (config.mode != ModeType::WSPR)
    {
        validate_config_data();
    }
    else
    {
        // Validate the startup WSPR configuration before any long-lived
        // services are started so malformed CLI frequency lists fail cleanly.
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
        WsprDialFrequencyEntry entry;
        double actual_rf_frequency_hz = 0.0;
        if (!try_get_direct_tone_startup_request(entry, actual_rf_frequency_hz))
        {
            llog.logE(ERROR, "Direct RF test tone requested without a startup tone request.");
            return false;
        }

        commit_execution_request(
            make_direct_tone_request(actual_rf_frequency_hz));
        (void)prepare_frequency_entry_gpio_or_log(entry, WARN);
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

    llog.logS(INFO, "Stopping frequency entry GPIO selector.");
    llog.logS(INFO, "Stopping band GPIO selector.");
    stop_active_transmission_selectors();
    llog.logS(INFO, "Band GPIO selector stopped.");
    llog.logS(INFO, "Frequency entry GPIO selector stopped.");

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
 * @brief Return the next configured scheduler frequency entry.
 *
 * The scheduler owns round-robin traversal of configured frequency entries,
 * including their optional `@GPIO` metadata. When `reset` is true, the next
 * returned entry is the first configured slot.
 *
 * @param reset True to restart from the first configured entry.
 * @return The next configured entry, or a default-constructed entry if none
 *         are configured.
 */
WsprDialFrequencyEntry next_frequency_entry(bool reset)
{
    if (reset)
    {
        freq_iterator = 0;
    }

    const auto &entries = config.wspr_dial_frequency_entries;
    if (entries.empty())
    {
        return WsprDialFrequencyEntry{};
    }

    const std::size_t idx =
        static_cast<std::size_t>(freq_iterator % entries.size());
    const WsprDialFrequencyEntry entry = entries[idx];

    ++freq_iterator;
    return entry;
}

/**
 * @brief Reload scheduler state and commit the next execution request.
 *
 * This function is the central orchestration path for startup, reload, PPM
 * updates, random WSPR offset application, paired-slot continuation, GPIO
 * selector preparation, and request commit. The transmitter receives only
 * the final committed request built here.
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
        current_dial_frequency = 0.0;
        current_frequency_entry = WsprDialFrequencyEntry{};
        current_transmission_request = WsprTransmissionRequest{};
        reset_active_wspr_plan_state();
        shutdown_after_wspr_plan.store(false, std::memory_order_release);

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
            return;
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
        llog.logS(INFO, "PPM updated:", config.ppm);
        do_config = true;

        // Clear pending ppm flags
        ppm_reload_pending.store(false, std::memory_order_relaxed);
    }

    // Get next frequency and indicate if we are (re)setting the stack
    static double last_freq = 0.0;
    static WsprDialFrequencyEntry last_frequency_entry{};
    if (active_wspr_plan_in_progress && active_wspr_frame_index > 0U)
    {
        current_dial_frequency = active_wspr_plan_dial_frequency;
        current_frequency_entry = active_wspr_plan_frequency_entry;
        do_config = true;
    }
    else
    {
        current_frequency_entry = next_frequency_entry(force);
        current_dial_frequency = current_frequency_entry.dial_frequency_hz;
    }

    const bool frequency_entry_changed =
        current_frequency_entry.token != last_frequency_entry.token ||
        current_frequency_entry.control_gpio != last_frequency_entry.control_gpio;

    if (current_dial_frequency != last_freq || frequency_entry_changed)
    {
        last_freq = current_dial_frequency;
        last_frequency_entry = current_frequency_entry;
        do_config = true;
    }
    else if (config.use_offset && current_dial_frequency != 0.0)
    {
        // Allow randomization as/if needed
        do_random = true;
    }

    // If we have a change, do setup
    if (do_config || do_random)
    {
        const double base_actual_rf_frequency_hz = resolve_actual_rf_frequency_hz(
            current_dial_frequency,
            config.wspr_audio_offset_hz,
            FrequencyPath::WsprDial);
        const double actual_rf_frequency_hz =
            maybe_apply_wspr_random_offset(base_actual_rf_frequency_hz);
        const double applied_offset_hz =
            actual_rf_frequency_hz - base_actual_rf_frequency_hz;

        // Do DMA configuration
        llog.logS(
            DEBUG,
            "Resolved WSPR dial frequency ",
            lookup.freq_display_string(current_dial_frequency),
            " to actual RF ",
            lookup.freq_display_string(actual_rf_frequency_hz),
            " using audio offset ",
            config.wspr_audio_offset_hz,
            " Hz.");
        if (!configure_current_wspr_transmission(
                actual_rf_frequency_hz,
                current_transmission_request))
        {
            config.transmit = false;
            config_to_json();
            return;
        }
        current_transmission_request.applied_offset_hz = applied_offset_hz;
        commit_execution_request(current_transmission_request);

        if (!prepare_frequency_entry_gpio_or_log(
                current_frequency_entry,
                ERROR))
        {
            stop_active_transmission_selectors();
            config.transmit = false;
            config_to_json();
            request_wspr_shutdown("frequency entry control GPIO unavailable");
            return;
        }

        if (!bandGPIOSelector.prepareFrequency(current_dial_frequency))
        {
            llog.logS(WARN,
                      "Unable to prepare band GPIO for ",
                      wsprTransmitter.formatFrequencyMHz(current_dial_frequency),
                      " MHz.");
            config.transmit = false;
            config_to_json();
            return;
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
