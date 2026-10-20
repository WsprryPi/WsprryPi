/**
 * @file signal_handler.hpp
 * @brief Signal handler interface for dedicated-thread signal processing.
 *
 * Licensed under the repository-root LICENSE.md.
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

#include "signal_wait.hpp"

// Standard Libraries
#include <atomic>
#include <csignal>
#include <functional>
#include <string_view>
#include <thread>
#include <unordered_map>

// System libraries
#include <termios.h>

/**
 * @brief Lifecycle state of the signal handler worker thread.
 *
 * This state tracks whether the dedicated signal-wait thread is active,
 * draining toward shutdown, or fully stopped.
 */
enum class SignalHandlerState
{
    RUNNING,        ///< The worker thread is active.
    STOP_REQUESTED, ///< Shutdown was requested and join is pending.
    FAILED,         ///< The worker exited after an internal wait/setup failure.
    STOPPED         ///< The worker thread has exited.
};

/**
 * @brief Block all signals registered in SignalHandler::signal_map.
 *
 * This helper is intended to be called before worker threads are created so
 * the blocked mask is inherited and signals are funneled to the dedicated
 * signal thread.
 *
 * @throws No exceptions are thrown. Debug builds may report system call
 *         failures to stderr.
 */
void block_signals();

/**
 * @brief Manages signal handling in a multi-threaded environment.
 *
 * The handler centralizes selected POSIX signals in one dedicated thread.
 * That keeps signal delivery predictable across the process and lets the
 * application react from normal thread context instead of async handlers.
 *
 * @warning The callback runs inline on the signal worker thread. It must stay
 *          minimal and return quickly so shutdown via stop() cannot be delayed.
 */
class SignalHandler
{
public:
    /**
     * @brief Placeholder status values for future expansion.
     */
    enum class Status
    {
        OK,   ///< Operation completed successfully.
        ERROR ///< Operation failed or was invalid.
    };

    /**
     * @brief Construct an inactive signal handler.
     *
     * Construction does not start the worker thread. Call start() after any
     * required callback registration is complete.
     */
    SignalHandler();

    /**
     * @brief Construct with an internal synchronous-wait test seam.
     *
     * Production code uses the default constructor. This overload permits
     * deterministic lifecycle and failure-path tests without sending signals.
     */
    explicit SignalHandler(SignalWaitFunction wait_function);

    /**
     * @brief Destroy the signal handler after stopping the worker thread.
     *
     * If the worker thread is still active, destruction requests shutdown and
     * waits for the worker to exit before object lifetime ends.
     *
     * @warning The object must not be destroyed from the worker thread itself,
     *          because stop() joins that thread during shutdown.
     */
    ~SignalHandler();

    /**
     * @brief Disable copying.
     *
     * A handler owns a single worker thread and cannot be copied safely.
     */
    SignalHandler(const SignalHandler &) = delete;

    /**
     * @brief Disable copy assignment.
     *
     * A handler owns a single worker thread and cannot be copied safely.
     */
    SignalHandler &operator=(const SignalHandler &) = delete;

    /**
     * @brief Starts the signal handling thread.
     *
     * This builds the wait set, blocks the configured signals for the current
     * thread, and launches the dedicated worker thread that waits for them.
     *
     * @warning This function is intended to be called once per active worker
     *          lifetime. Re-entry while already running is ignored.
     */
    void start();

    /**
     * @brief Set the callback invoked for handled signals.
     *
     * The callback executes inline on the worker thread after the synchronous
     * platform wait returns a handled signal.
     *
     * @param cb Function receiving the signal number and whether the signal is
     *           marked immediate in signal_map
     *
     * @warning The callback should perform only minimal non-blocking work, such
     *          as setting flags or notifying other threads.
     */
    void setCallback(const std::function<void(int, bool)> &cb);

    /**
     * @brief Stops the signal handling thread and restores terminal settings.
     *
     * Shutdown sets STOP_REQUESTED, wakes the blocked worker with SIGUSR1, and
     * joins the worker thread before returning. This guarantees the object is
     * no longer accessed by the worker after stop() completes.
     *
     * @return True if this call performed a clean stop, false if the worker was
     *         already stopped or shutdown was already in progress
     *
     * @warning Because stop() always joins, it can wait as long as the worker
     *          needs to finish any in-flight callback.
     */
    bool stop();

    /**
     * @brief Sets thread scheduling policy and priority for the signal thread.
     *
     * @param schedPolicy One of SCHED_FIFO, SCHED_RR, or SCHED_OTHER
     * @param priority Desired priority level for the thread
     * @return True if the change succeeded, false otherwise
     */
    bool setPriority(int schedPolicy, int priority);

    /**
     * @brief Converts a signal number to its string representation.
     *
     * @param signum Signal number to convert
     * @return Signal name, or "UNKNOWN" if the signal is not mapped
     */
    static std::string_view signalToString(int signum);

    /**
     * @brief Static map of signals to handle.
     *
     * Each entry maps a signal number to its display name and whether it is
     * considered immediate by the application.
     */
    static const std::unordered_map<int, std::pair<std::string_view, bool>> signal_map;

private:
    /**
     * @brief Worker thread that runs in the signal loop.
     */
    std::thread worker_thread;

    /**
     * @brief Current lifecycle state of the worker thread.
     */
    std::atomic<SignalHandlerState> state;

    /**
     * @brief User-provided callback executed on the worker thread.
     */
    std::function<void(int, bool)> callback;

    /**
     * @brief Original terminal settings for STDIN.
     *
     * Stored so control-character echo settings can be restored on shutdown.
     */
    termios original_termios;

    /**
     * @brief Whether original_termios contains a valid saved state.
     */
    bool termios_saved;

    /**
     * @brief Cached set of handled signals built from signal_map.
     */
    sigset_t signal_set;

    /** @brief Platform-normalized synchronous signal wait operation. */
    SignalWaitFunction wait_function;

    /**
     * @brief Worker entry point for the dedicated signal thread.
     *
     * The worker waits through the normalized platform seam, dispatches the
     * minimal callback, and exits promptly once shutdown is requested.
     */
    void run();
};

#endif // SIGNAL_HANDLER_HPP
