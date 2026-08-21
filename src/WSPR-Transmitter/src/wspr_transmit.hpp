/**
 * @file wspr_transmit.hpp
 * @brief Transmitter boundary for executing committed requests.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
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

#ifndef WSPR_TRANSMIT_HPP
#define WSPR_TRANSMIT_HPP

// C++ standard library headers
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

// POSIX and system headers
#include <sys/time.h> // for struct timeval

// Project headers
#include "execution_plan.hpp"
#include "execution_plan_compiler.hpp"
#include "rpi_standard_feld_progress_bridge.hpp"
#include "transmission_controller.hpp"
#include "prepared_wspr_transmission.hpp"
#include "wspr_transmit_types.hpp"

class WsprTransmitBackend;
class WsprRpiBackend;
class IControllerBridge : public wsprrypi::IExecutionContext
{
public:
    virtual ~IControllerBridge() = default;
    virtual WsprTransmitState backendStateValue() const noexcept = 0;
    virtual void backendSetStateValue(WsprTransmitState state) noexcept = 0;
    virtual bool backendShouldStop() const noexcept = 0;
    virtual void backendSignalStopRequest() noexcept = 0;
    virtual void backendRequestStopTxNoJoin() noexcept = 0;
    virtual bool backendWaitInterruptableFor(std::chrono::nanoseconds duration) = 0;
    bool stopRequested() const noexcept override { return backendShouldStop(); }
    bool waitInterruptibleFor(std::chrono::nanoseconds duration) override
    {
        return backendWaitInterruptableFor(duration);
    }
    virtual void backendThrowIfStopRequested(const char *context) = 0;
    virtual void backendReportExecutionProgress(std::size_t event_index) noexcept = 0;
    void reportExecutionProgress(std::size_t event_index) noexcept override
    {
        backendReportExecutionProgress(event_index);
    }
    std::chrono::nanoseconds logicalNow() const noexcept override
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }
    virtual bool backendReportRasterProgress(
        std::uint64_t generation,
        std::size_t event_index,
        const wsprrypi::RfEvent::RasterProgress& progress)
    {
        (void)event_index;
        (void)generation;
        (void)progress;
        return false;
    }
    virtual bool backendActivateRasterProgress(
        const wsprrypi::ExecutionPlan&, std::uint64_t) { return false; }
    virtual bool backendFinalizeRasterProgress(
        std::uint64_t, wsprrypi::RpiStandardFeldExecutionTerminal, bool) { return false; }
    virtual void backendFireTransmitCallback(WsprTransmissionCallbackEvent event,
                                             WsprTransmitLogLevel level,
                                             const std::string &msg,
                                             double value) = 0;
    virtual bool backendRestartCurrentConfiguration() = 0;
};

/**
 * @class WsprTransmitter
 * @brief Controller/facade for executing committed WSPR or tone requests.
 *
 * @details
 *   `WsprTransmitter` owns the public API, committed execution state, and
 *   transmit timing loop. Hardware-specific work is delegated to an active
 *   `WsprTransmitBackend` implementation.
 *
 *   Responsibilities of the controller include:
 *   - Accepting a fully selected execution request from the orchestration
 *     layer.
 *   - Building a backend-neutral `WsprTransmissionPlan` snapshot.
 *   - Choosing when transmission starts and when each symbol is emitted.
 *   - Managing scheduler, worker threads, stop requests, and high-level
 *     transmission state.
 *   - Forwarding logging and lifecycle callbacks.
 *
 *   Responsibilities intentionally excluded from the transmitter include:
 *   - WSPR planning policy such as Auto versus RequirePaired.
 *   - Tone versus WSPR mode selection.
 *   - Random WSPR offset selection.
 *   - Band-selector GPIO choice and preparation.
 *
 *   Responsibilities intentionally left to the backend include:
 *   - Hardware setup and teardown
 *   - RF output enable/disable
 *   - Platform-specific symbol emission
 *   - Fault detection and recovery implementation
 *
 *   The controller's runtime lifecycle is:
 *   1. `configureExecution(...)`
 *   2. `startAsync()`
 *   3. Backend prepare/configure
 *   4. Timed symbol emission through `emitSymbol(...)`
 *   5. Output stop and cleanup on completion, cancellation, or fault
 */
class WsprTransmitter : public IControllerBridge
{
public:
    struct RuntimeExecutionStatus
    {
        wsprrypi::TransmissionMode mode{wsprrypi::TransmissionMode::WSPR};
        std::string cw_message;
        int cw_active_char_index{-1};
    };

    struct Si5351RuntimeConfig
    {
        enum class ReferenceSource
        {
            EXTERNAL_TCXO,
            CRYSTAL
        };

        int i2c_bus = 1;
        int i2c_address = 0x60;
        int reference_hz = 27000000;
        ReferenceSource reference_source = ReferenceSource::EXTERNAL_TCXO;
        int crystal_load_capacitance_pf = 10;
        int tx_output = 0;
        int power_level = 1;
        bool app_managed = false;

        bool operator==(const Si5351RuntimeConfig &) const = default;
    };

    struct SimulatedRuntimeConfig
    {
        bool virtual_time = true;
        std::string trace_path = "/tmp/wsprrypi-simulated-trace.json";
        bool fail_configure = false;
        long fail_event = -1;
        long cancel_event = -1;
        bool fail_cleanup = false;
        bool fail_startup_quiesce = false;

        bool operator==(const SimulatedRuntimeConfig &) const = default;
    };

    /**
     * @enum State
     * @brief High-level transmission state for the transmitter.
     *
     * @details
     *   This describes whether the transmitter is available to run, idle, or
     *   actively emitting RF.
     *
     * Invariants:
     * - requestStopTx() never transitions to DISABLED.
     * - Stopping TX preserves hardware readiness.
     */
    using State = WsprTransmitState;

    constexpr const char *stateToString(State state) noexcept
    {
        return wsprTransmitStateToString(state);
    }

    /**
     * @enum LogLevel
     * @brief Log level for callback messages.
     */
    using LogLevel = WsprTransmitLogLevel;

    /**
     * @brief Convert a State to a lowercase std::string.
     *
     * @param state State value to convert.
     * @return Lowercase string describing the state.
     */
    std::string stateToStringLower(State state);
    bool activeExecutionIsTone() const noexcept;
    bool activeExecutionIsWspr() const noexcept;
    RuntimeExecutionStatus runtimeExecutionStatusSnapshot() const;
    std::string reloadDeferDebugState() const;
    void clearExecutionStateAfterStop() noexcept;

    /**
     * @brief Constructs a WSPR transmitter with default settings.
     *
     * This is for global constructions, parameters are set via
     * configure().
     */
    WsprTransmitter();

    /**
     * @brief Destroys the WSPR transmitter.
     *
     * Cleans up any allocated resources and stops
     * any running transmission threads.
     */
    ~WsprTransmitter();

    /**
     * @brief Deleted copy constructor.
     *
     * @details
     *   Copying a WsprTransmitter instance is disallowed to prevent multiple
     *   objects from owning the same hardware, DMA resources, or background
     *   threads.
     */
    WsprTransmitter(WsprTransmitter const &) = delete;

    /**
     * @brief Deleted copy assignment operator.
     *
     * @details
     *   Assignment is disabled to avoid transferring ownership of active
     *   hardware mappings or thread state between instances.
     */
    WsprTransmitter &operator=(WsprTransmitter const &) = delete;

    /**
     * @brief Deleted move constructor.
     *
     * @details
     *   Moving is disallowed to ensure the transmitter remains at a stable
     *   address for DMA structures, callbacks, and global references.
     */
    WsprTransmitter(WsprTransmitter &&) = delete;

    /**
     * @brief Deleted move assignment operator.
     *
     * @details
     *   Move assignment is disabled to prevent partial transfer of ownership
     *   of hardware resources and internal synchronization primitives.
     */
    WsprTransmitter &operator=(WsprTransmitter &&) = delete;

    /**
     * @brief Identifies which transmission callback event is being reported.
     */
    using TransmissionCallbackEvent = WsprTransmissionCallbackEvent;

    /**
     * @brief Signature for user-provided transmission callback.
     *
     * @param event Indicates whether the callback is for transmission start,
     *              completion, skip, or logging.
     * @param level Log level for the message.
     * @param msg   Descriptor string for the transmission; may be empty.
     * @param value For STARTING, the active transmit frequency in Hz.
     *              For COMPLETE or CANCELLED, the elapsed transmission time in seconds.
     *              For SKIPPED, this value is ignored.
     *              For LOGGING, this value is ignored.
     */
    using TransmissionCallback =
        std::function<void(TransmissionCallbackEvent event,
                           LogLevel level,
                           const std::string &msg,
                           double value)>;

    /**
     * @brief Install an optional callback for transmission notifications.
     *
     * @param[in] cb
     *   Called asynchronously when a transmission starts, completes, or is
     *   skipped. The first argument identifies which event is being
     *   reported. If null, no notifications are made.
     */
    void setTransmissionCallbacks(TransmissionCallback cb = {});

    /**
     * @brief Format a frequency in MHz using the transmitter's display rules.
     *
     * @details
     *   This helper ensures callbacks and internal logs format frequencies the
     *   same way, so debug and release builds display identical values.
     *
     * @param frequency_hz Frequency in Hz.
     * @return Frequency formatted in MHz with six digits after the decimal.
     */
    static std::string formatFrequencyMHz(double frequency_hz);

    /**
     * @brief Configure one committed execution request.
     *
     * @details
     *   The caller provides the fully selected execution request for the next
     *   run. This method does not decide policy such as tone versus WSPR,
     *   paired planning, GPIO selection, or in-band offset selection.
     */
    void configureExecution(const TransmissionRequest &request);
    void configureExecution(const wsprrypi::TransmissionRequest& request,
                            const TransmissionRequest& legacy_request);

    void selectBackend(wsprrypi::BackendKind backend_kind);
    void selectBackend(
        wsprrypi::BackendKind backend_kind,
        const Si5351RuntimeConfig &si5351_config);
    void selectBackend(
        wsprrypi::BackendKind backend_kind,
        const Si5351RuntimeConfig &si5351_config,
        const SimulatedRuntimeConfig &simulated_config);

    /**
     * Place the selected backend into its safe startup state before any
     * execution request is configured or scheduled.
     */
    wsprrypi::StartupQuiesceResult quiesceForStartup();

    /**
     * @brief Configure POSIX scheduling policy and priority for future
     *        transmissions.
     *
     * @details
     *   This must be called before `startTransmission()` if you need
     *   real-time scheduling under the given policy/priority.
     *
     * @param[in] policy
     *   One of the standard POSIX policies (for example SCHED_FIFO,
     *   SCHED_RR, SCHED_OTHER).
     * @param[in] priority
     *   Thread priority (1-99) for real-time policies. Ignored under
     *   SCHED_OTHER.
     */
    void setThreadScheduling(int policy, int priority);

    /**
     * @brief Enable or disable one-shot scheduling for WSPR mode.
     *
     * When enabled, the internal timing scheduler will launch exactly one
     * already-committed WSPR request and then stop.
     */
    void setOneShot(bool enable) noexcept;

    /**
     * @brief Enable or disable immediate transmission for WSPR mode.
     *
     * When enabled, WSPR mode bypasses the next-window wait and starts the
     * already-committed request immediately.
     */
    void setTransmitNow(bool enable) noexcept;

    /**
     * @brief Request a "soft off".
     *
     * Prevents any new WSPR transmissions from being scheduled while allowing
     * any currently running transmission to continue or be stopped cleanly by
     * requestStopTx()/stopAndJoin().
     */
    void requestSoftOff() noexcept;

    /**
     * @brief Clear a previously requested "soft off".
     */
    void clearSoftOff() noexcept;

    /**
     * @brief Start transmission, either immediately or via the scheduler.
     *
     * @details
     *   If the active request is tone mode, this will spawn the transmit
     *   thread right away (bypassing the scheduler). Otherwise it launches
     *   the background scheduler, which will fire at the next WSPR window
     *   and then spawn the transmit thread.
     *
     * @note This call is non-blocking. In tone mode it returns immediately
     *       after spawning the thread; in WSPR mode it returns immediately
     *       after starting the scheduler thread.
     */
    void startAsync();

    /**
     * @brief Stop scheduler and transmission and release hardware.
     *
     * @details
     *   Cancels the scheduler (if running), requests any in-flight
     *   transmission to stop, waits for threads to exit, and performs
     *   hardware shutdown (DMA/PWM/clocks).
     */
    void shutdown();

    /**
     * @brief Request an in-flight transmission to stop.
     *
     * @details
     *   Sets the internal stop flag, wakes any interruptible waits, and
     *   waits for the transmit thread to exit. After this returns, it is
     *   safe to call configureExecution() with a new committed request and
     *   then restart with startAsync().
     */
    void requestStopTx();

    /**
     * @brief Request TX stop without joining the transmit thread.
     *
     * @details
     *   Used by the watchdog thread to avoid blocking inside join() if the
     *   transmit thread is slow to unwind. Recovery will later perform the
     *   full shutdown sequence.
     */
    void requestStopTxNoJoin() noexcept;

    /**
     * @brief Forcefully reset the DMA/PWM/clock hardware sequence.
     *
     * @details
     *   This is a best-effort, non-throwing path used for watchdog recovery.
     *   It can be called even when the transmitter state machine believes TX
     *   is stalled.
     */
    void force_dma_reset_sequence() noexcept;

    /**
     * @brief Stop and wait for the scheduler/transmit threads.
     *
     * @details
     *   Requests stop and joins threads as needed.
     */
    void stopAndJoin();

    /**
     * @brief Fully tear down transmitter-owned background threads for exit.
     *
     * @details
     *   Performs the normal stop/join path, releases backend ownership so
     *   backend-managed helper threads are joined, and stops the callback
     *   worker. Intended for final process shutdown only.
     */
    void shutdownForProcessExit();

    const wsprrypi::CleanupResult& lastCleanupResult() const noexcept
    {
        return last_cleanup_result_;
    }

    /**
     * @brief Returns true if the DMA watchdog detected a stalled DMA engine.
     */
    bool watchdogFaulted() const noexcept;

    /**
     * @brief Clears the DMA watchdog fault latch.
     */
    void clearWatchdogFault() noexcept;

    /**
     * @brief Enable or disable automatic recovery after a DMA watchdog stall.
     *
     * @details
     *   When enabled, the watchdog thread will request a full hardware reset
     *   (DMA/PWM/clock teardown and re-init) and will restart execution
     *   using the last committed request.
     *
     *   Recovery runs on a dedicated internal worker thread so the watchdog
     *   can exit promptly without risking deadlocks.
     *
     * @param enable True to enable automatic recovery.
     */
    void setWatchdogAutoRecover(bool enable) noexcept;

    /**
     * @brief Returns true if watchdog auto-recovery is enabled.
     */
    bool watchdogAutoRecoverEnabled() const noexcept;

    /**
     * @brief Attempt to recover from a latched watchdog fault immediately.
     *
     * @details
     *   This is a synchronous recovery helper. It stops the scheduler and
     *   transmit thread, resets DMA/PWM/clock state, reinitializes DMA state
     *   with the last committed request, clears the watchdog fault latch,
     *   and restarts execution via startAsync().
     *
     * @return True if recovery succeeded, false otherwise.
     */
    bool recoverFromWatchdogFault();

    /**
     * @brief Get the current transmission state.
     *
     * @details
     *   Returns a value indicating if the system is transmitting in any way.
     *
     * @return The current transmitter state.
     */
    State getState() const noexcept;

    /**
     * @brief Prints current transmission parameters and encoded WSPR symbols.
     *
     * @details
     *   Displays the configured WSPR parameters including frequency, power,
     *   mode, tone/test settings, and symbol timing. Also prints all WSPR
     *   symbols as integer values, grouped for readability.
     *
     *   This function is useful for debugging and verifying that all
     *   transmission settings and symbol sequences are correctly populated
     *   before transmission.
     */
    void dumpParameters();

    WsprTransmitState backendStateValue() const noexcept override;

    void backendSetStateValue(WsprTransmitState state) noexcept override;

    bool backendShouldStop() const noexcept override;

    void backendSignalStopRequest() noexcept override;

    void backendRequestStopTxNoJoin() noexcept override;

    bool backendWaitInterruptableFor(std::chrono::nanoseconds duration) override;

    void backendThrowIfStopRequested(const char *context) override;

    void backendReportExecutionProgress(std::size_t event_index) noexcept override;
    bool backendReportRasterProgress(
        std::uint64_t generation,
        std::size_t event_index,
        const wsprrypi::RfEvent::RasterProgress& progress) override;
    bool backendActivateRasterProgress(
        const wsprrypi::ExecutionPlan&, std::uint64_t) override;
    bool backendFinalizeRasterProgress(
        std::uint64_t, wsprrypi::RpiStandardFeldExecutionTerminal, bool) override;

    void backendFireTransmitCallback(WsprTransmissionCallbackEvent event,
                                     WsprTransmitLogLevel level,
                                     const std::string &msg,
                                     double value) override;

    bool backendRestartCurrentConfiguration() override;

private:
    std::unique_ptr<wsprrypi::ITransmissionBackend> createBackend(
        wsprrypi::BackendKind backend_kind,
        const Si5351RuntimeConfig& runtime_config,
        const SimulatedRuntimeConfig& simulated_config);
    void requireBackendCleanup(const char* context);
    bool observeBackendCleanup(const char* context);
    [[noreturn]] void rethrowWithCleanupResult(
        std::exception_ptr original,
        const char* context);
    struct PendingTransmitCallback
    {
        TransmissionCallbackEvent event;
        LogLevel level;
        std::string msg;
        double value;
    };

    void startFaultMonitoring();

    void stopFaultMonitoring();

    /**
     * @brief Request a recovery cycle after a watchdog fault.
     *
     * @details
     *   Safe to call from the watchdog thread. This only signals the recovery
     *   worker and returns immediately.
     */
    void request_watchdog_recovery() noexcept;

    /**
     * @brief Internal recovery worker loop.
     */
    void recovery_worker();

    /**
     * @brief Core recovery implementation guarded by recovery_mtx_.
     */
    bool recover_from_watchdog_fault_locked();

    /**
     * @brief CPU core affinity for the transmit thread.
     *
     * @details
     *   Used to bind the transmit thread to a specific CPU core in order to
     *   reduce scheduling jitter during tight timing loops.
     */
    int tx_cpu_{0};

    /**
     * @brief Busy-wait tail duration in nanoseconds.
     *
     * @details
     *   During tight absolute sleeps, the thread will switch to a spin-wait
     *   for the final portion of the delay to reduce wake-up latency and
     *   improve symbol boundary accuracy.
     */
    std::int64_t spin_ns_{200'000};

    /**
     * @brief Invoked when a transmission starts or completes.
     *
     * This callback is fired asynchronously from the transmit thread to
     * notify the user that a transmission has started or completed.
     * Users can assign a function via `setTransmissionCallbacks()` to
     * perform setup, logging, or cleanup work tied to these events.
     */
    TransmissionCallback on_transmit_cb_{};
    std::thread callback_thread_;
    std::mutex callback_mtx_;
    std::condition_variable callback_cv_;
    std::deque<PendingTransmitCallback> callback_queue_{};
    bool callback_stop_{false};

    /**
     * @brief Background thread for carrying out the transmission.
     *
     * Launched by startTransmission() and joined by
     * join_transmission().
     */
    std::thread tx_thread_;

    /**
     * @brief Guards tx_thread_ lifecycle against stop/scheduler races.
     *
     * @details
     *   The scheduler thread can finish a transmission and quickly attempt to
     *   start the next. The owning thread may call stopAndJoin() right after
     *   the end callback fires. This mutex ensures that joining and launching
     *   tx_thread_ cannot interleave in a way that creates an extra
     *   transmission or a stuck join.
     */
    std::mutex tx_thread_mtx_;

    /**
     * @brief POSIX scheduling policy for the transmission thread.
     *
     * One of SCHED_FIFO, SCHED_RR, or SCHED_OTHER.
     */
    int thread_policy_ = SCHED_OTHER;

    /**
     * @brief Scheduling priority for the transmission thread.
     *
     * Valid range is 1-99 for real-time policies; ignored by SCHED_OTHER.
     */
    int thread_priority_ = 0;

    /**
     * @brief Flag indicating that a stop request has been issued.
     *
     * When true, loops in transmit() and transmit_symbol() will
     * exit at the next interruption point.
     */
    std::atomic<bool> stop_requested_{false};

    /**
     * @brief Flag indicating that new transmissions must not be scheduled.
     *
     * When set, the scheduler loop will stop launching new transmissions.
     * This does not stop any currently running transmit thread.
     */
    std::atomic<bool> soft_off_{false};

    /**
     * @brief Flag indicating that the scheduler should run exactly one
     *        transmission and then stop.
     */
    std::atomic<bool> one_shot_{false};

    /**
     * @brief Flag indicating that the next transmission should start
     *        immediately.
     */
    std::atomic<bool> transmit_now_{false};

    /**
     * @brief Optional external stop flag.
     *
     * When set to a non-null pointer, shouldStop() will also consider the
     * external flag value.
     */
    const std::atomic<bool> *external_stop_flag_{nullptr};

    /**
     * @brief Aggregate internal and external stop requests.
     *
     * @return true if either stop_requested_ or the external termination
     *         flag (if provided) is set.
     */
    bool shouldStop() const noexcept;

    /**
     * @brief Wait for the given duration unless a stop is requested.
     *
     * @param duration Duration to wait.
     * @return true if the full duration elapsed, false if interrupted.
     */
    bool waitInterruptableFor(std::chrono::nanoseconds duration);

    /**
     * @brief Sleep to an absolute clock deadline unless a stop is requested.
     *
     * @param clk_id Clock used for the absolute deadline.
     * @param ts_target Absolute deadline for the sleep.
     * @param spin_ns Busy-wait tail in nanoseconds.
     * @return true if the deadline was reached, false if interrupted.
     */
    bool sleepUntilAbsTightInterruptible(
        clockid_t clk_id,
        const timespec &ts_target,
        int64_t spin_ns);

    /**
     * @brief Throw if a stop has been requested.
     *
     * @param context Short context string for diagnostics.
     */
    void throwIfStopRequested(const char *context);

    /**
     * @brief Condition variable used to wake the transmission thread.
     *
     * requestStopTx() calls notify_all() on this to unblock
     * any waits so the thread can observe stop_requested_.
     */
    std::condition_variable stop_cv_;

    /**
     * @brief Mutex paired with stop_cv_.
     *
     * Used to implement interruptible waits that can be woken immediately
     * when requestStopTx() is called.
     */
    mutable std::mutex stop_mtx_;

    /**
     * @brief Stores the current transmission state.
     *
     * True when transmit_on() is called, false when transmit_off() or
     * disable_clock() is called.
     */
    std::atomic<State> state_{State::DISABLED};

    /**
     * @brief Scheduled wall-clock start time for windowed WSPR transmissions.
     *
     * @details
     *   Stored as nanoseconds since the Unix epoch using CLOCK_REALTIME. The
     *   scheduler sets this just before spawning the TX thread so the TX thread
     *   can align the first symbol to the exact WSPR window boundary. A value
     *   of 0 means "start immediately".
     */
    std::atomic<std::int64_t> scheduled_start_rt_ns_{0};

    /**
     * @brief Mutex accompanying stop_cv_ for coordinated waits.
     */
    std::mutex stop_mutex_;

    /**
     * @brief Nominal symbol duration for WSPR transmissions.
     *
     * This constant represents the nominal time duration of a WSPR symbol,
     * calculated as 8192 samples divided by a sample rate of 12000 Hz.
     *
     * @details
     *   This duration is a key parameter in WSPR transmissions, ensuring the
     *   correct timing for symbol generation and encoding.
     *
     * @note Any deviation in sample rate or processing latency could affect
     *       the actual symbol duration.
     */
    static constexpr double WSPR_SYMTIME = 8192.0 / 12000.0;

    /**
     * @brief Active execution request.
     *
     * @details
     *   This instance holds the committed execution request used by the
     *   internal timing scheduler and transmit thread.
     */
    TransmissionRequest current_request_{};
    wsprrypi::ExecutionPlan current_execution_plan_{};
    wsprrypi::TransmissionMode current_execution_mode_{
        wsprrypi::TransmissionMode::WSPR};
    std::string current_cw_message_{};
    std::atomic<int> current_cw_active_char_index_{-1};
    // Internal-only completed-position stream. It is deliberately absent from
    // public callbacks, status serialization, and operator protocols.
    wsprrypi::RpiStandardFeldProgressBridge standard_feld_progress_{};

    /**
     * @brief Invoke the configured transmission callback.
     *
     * @details
     *   Calls the user-provided callback, if one is installed, passing
     *   the event type, an optional descriptive message, and an associated
     *   value.
     *
     * @param event Identifies whether this is a start, completion, skip, or
     *              logging notification.
     * @param level Log level for the callback.
     * @param msg   Message string describing the transmission.
     * @param value For STARTING, the transmit frequency in Hz.
     *              For COMPLETE or CANCELLED, the elapsed transmission time in seconds.
     *              For SKIPPED and LOGGING, this value is ignored.
     */
    void fire_transmit_cb(TransmissionCallbackEvent event,
                          LogLevel level,
                          const std::string &msg,
                          double value);
    void callback_worker_loop();
    void stop_callback_worker();

    /**
     * @brief Derive the backend-neutral execution plan for the active request.
     *
     * @details
     *   This converts the committed request into the reduced hardware-facing
     *   plan consumed by the backend. It does not perform planning policy.
     */
    WsprTransmissionPlan buildTransmissionPlan() const noexcept;
    /**
     * @brief Execute the transmission loop.
     *
     * @details
     *   Drives DMA and PWM hardware to emit either a continuous tone or a
     *   full WSPR symbol sequence. This function runs on the transmit
     *   worker thread.
     */
    void transmit();

    /**
     * @brief Join the active transmission thread.
     *
     * @details
     *   Waits for the transmit thread to exit if it is currently running.
     *   Safe to call multiple times.
     */
    void join_transmission();

    /**
     * @brief Clean up DMA-related resources.
     *
     * @details
     *   Tears down DMA state, restores hardware registers, and releases
     *   mailbox-allocated memory used during transmission.
     */
    wsprrypi::CleanupResult cleanupTransmissionBackend() noexcept;

    /**
     * @brief Convert a GPIO power level index to milliwatts.
     *
     * @details
     *   Maps a logical power level index to an approximate output power in
     *   milliwatts based on the configured GPIO drive characteristics.
     *
     * @param level Power level index.
     * @return Output power in milliwatts.
     */
    int getOutputPowerMilliwatts(int level);

    /**
     * @brief Convert milliwatts to dBm.
     *
     * @param mw Power in milliwatts.
     * @return Equivalent power in dBm.
     */
    inline double convert_mw_dbm(double mw);

    /**
     * @brief Access a memory-mapped peripheral register by bus address.
     *
     * @details
     *   Translates a bus address into a reference within the mapped peripheral
     *   region for direct register access.
     *
     * @param bus_addr Bus address of the register.
     * @return Reference to the mapped register value.
     */
    /**
     * @brief Enable RF output for transmission.
     *
     * @details
     *   Activates clocks and GPIO routing required to begin emitting RF.
     */
    void beginTransmissionOutput();

    /**
     * @brief Disable RF output after transmission.
     *
     * @details
     *   Turns off clocks and GPIO routing to stop RF emission cleanly.
     */
    void endTransmissionOutput();

    /**
     * @brief Transmit work corresponding to a single WSPR symbol.
     *
     * @details
     *   Advances the DMA control block ring to emit the waveform segment
     *   associated with one symbol interval. Frequency changes are applied
     *   by updating DMA control blocks in-place while maintaining precise
     *   symbol timing.
     *
     * @param sym_num Sequential symbol number within the transmission.
     * @param tsym Symbol duration in seconds.
     * @param symbol_index Optional symbol index override.
     */
    void emitSymbol(
        const std::uint32_t &sym_num,
        const double &tsym,
        int symbol_index = -1);

    /**
     * @brief Truncate a floating-point value to a given number of LSBs.
     *
     * @details
     *   Used to limit fractional precision when computing tuning words or
     *   divisors to match hardware resolution.
     *
     * @param d Input value.
     * @param lsb Number of least-significant bits to retain.
     * @return Truncated value.
     */
    double bit_trunc(const double &d, const int &lsb);

    /**
     * @brief Create DMA pages and instruction ring.
     *
     * @details
     *   Initializes the constant data page, instruction page, and DMA
     *   control block ring used for DMA-driven PWM output.
     *
     * @param const_page Reference to the constant data page mapping.
     * @param instr_page Reference to the instruction page mapping.
     * @param instructions Array of instruction page mappings.
     */
    void prepareTransmissionBackend();

    WsprTransmissionConfigureResult configureTransmissionBackend();

    /**
     * @brief Entry point for the transmit worker thread.
     *
     * @details
     *   Applies thread scheduling, fires start callbacks, executes the
     *   transmission loop, and performs post-transmit cleanup.
     */
    void thread_entry();

    /**
     * @brief Apply configured scheduling policy and priority to the thread.
     *
     * @details
     *   Uses the previously configured POSIX scheduling parameters to
     *   adjust the priority of the calling thread.
     */
    void set_thread_priority();

    /**
     * @class TransmissionScheduler
     * @brief Schedules WSPR message transmissions on time-aligned windows.
     *
     * @details
     *   Runs in a background thread and waits for the next valid WSPR
     *   transmission window. When triggered, it launches the transmit
     *   worker thread on the parent WsprTransmitter instance.
     *
     *   The scheduler supports one-shot operation, immediate transmission,
     *   and soft-off behavior.
     */
    class TransmissionScheduler
    {
    public:
        /**
         * @brief Construct a scheduler bound to a transmitter instance.
         *
         * @param parent Pointer to the owning WsprTransmitter.
         */
        explicit TransmissionScheduler(WsprTransmitter *parent);

        /**
         * @brief Destroy the scheduler.
         *
         * @details
         *   Requests the scheduler thread to stop and waits for it to exit.
         */
        ~TransmissionScheduler();

        /**
         * @brief Start the scheduler thread.
         */
        void start();

        /**
         * @brief Stop the scheduler thread.
         *
         * @details
         *   Requests termination and wakes the scheduler if it is sleeping.
         */
        void stop();

        /**
         * @brief Wake the scheduler from a wait.
         *
         * @details
         *   Used to interrupt the scheduler when configuration or control
         *   state changes.
         */
        void notify() noexcept;

    private:
        /**
         * @brief Parent transmitter instance.
         */
        WsprTransmitter *parent_;

        /**
         * @brief Scheduler worker thread.
         */
        std::thread thread_;

        /**
         * @brief Stop flag for the scheduler thread.
         */
        std::atomic<bool> stop_requested_{false};

        /**
         * @brief Mutex protecting scheduler wait state.
         */
        std::mutex mtx_;

        /**
         * @brief Condition variable used for scheduler waits.
         */
        std::condition_variable cv_;

        /**
         * @brief Compute the next WSPR transmission window.
         *
         * @return Time point of the next scheduler event.
         */
        std::chrono::system_clock::time_point nextEvent() const;

        /**
         * @brief Scheduler main loop.
         */
        void run();
    };

    /**
     * @brief Scheduler instance for this transmitter.
     *
     * @details
     *   Manages window-based transmission timing for message mode.
     */
    TransmissionScheduler scheduler_{this};

    /**
     * @brief Selected transmission backend and controller.
     */
    wsprrypi::ExecutionPlanCompiler execution_plan_compiler_{};
    std::unique_ptr<wsprrypi::ITransmissionBackend> backend_;
    WsprRpiBackend *rpi_backend_{nullptr};
    wsprrypi::BackendKind selected_backend_{wsprrypi::BackendKind::RPI_CLOCK_GPIO};
    Si5351RuntimeConfig selected_si5351_config_{};
    SimulatedRuntimeConfig selected_simulated_config_{};
    wsprrypi::CleanupResult last_cleanup_result_{true, {}};
    std::unique_ptr<wsprrypi::TransmissionController> transmission_controller_;
};

/**
 * @brief Global WSPR transmitter instance.
 *
 * @details
 *   Declares the project-wide WsprTransmitter object used to configure
 *   and control RF transmission. The instance is defined in a corresponding
 *   translation unit and is intended to be shared across modules.
 */
extern WsprTransmitter wsprTransmitter;

#endif // WSPR_TRANSMIT_HPP
