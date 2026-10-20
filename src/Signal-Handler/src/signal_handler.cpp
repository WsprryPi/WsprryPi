/**
 * @file signal_handler.cpp
 * @brief Signal handler implementation for dedicated-thread signal processing.
 *
 * Licensed under the repository-root LICENSE.md.
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

// Project libraries
#include "signal_handler.hpp"

// Standard libraries
#include <cstdlib>
#include <iostream>
#include <utility>

// System Libraries
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef DEBUG_SIGNAL_HANDLER
#include <cstring>  // For strerror()
#endif

namespace
{
bool build_handled_signal_set(sigset_t &set)
{
    if (sigemptyset(&set) != 0)
    {
#ifdef DEBUG_SIGNAL_HANDLER
        perror("sigemptyset");
#endif
        return false;
    }

    for (const auto &entry : SignalHandler::signal_map)
    {
        if (sigaddset(&set, entry.first) != 0)
        {
#ifdef DEBUG_SIGNAL_HANDLER
            perror("sigaddset");
#endif
        }
    }

    return true;
}

bool apply_handled_signal_mask(const sigset_t &set)
{
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0)
    {
#ifdef DEBUG_SIGNAL_HANDLER
        perror("pthread_sigmask");
#endif
        return false;
    }

    return true;
}
} // namespace

/**
 * @brief Mapping of handled signals to display names and immediacy flags.
 *
 * SIGUSR1 is included so stop() can wake the synchronous waiter without a
 * separate shutdown primitive.
 */
const std::unordered_map<int, std::pair<std::string_view, bool>> SignalHandler::signal_map = {
    {SIGUSR1, {"SIGUSR1", false}},
    {SIGINT, {"SIGINT", false}},
    {SIGTERM, {"SIGTERM", false}},
    {SIGQUIT, {"SIGQUIT", false}},
    {SIGHUP, {"SIGHUP", false}},
    {SIGSEGV, {"SIGSEGV", true}},
    {SIGBUS, {"SIGBUS", true}},
    {SIGFPE, {"SIGFPE", true}},
    {SIGILL, {"SIGILL", true}},
    {SIGABRT, {"SIGABRT", true}}};

/**
 * @brief Block all signals registered in SignalHandler::signal_map.
 *
 * This keeps the handled signals out of arbitrary threads so the dedicated
 * signal worker can receive them through the platform's synchronous wait.
 *
 * @throws No exceptions are thrown. Debug builds may report system call
 *         failures to stderr.
 */
void block_signals()
{
    sigset_t blockset;

    if (!build_handled_signal_set(blockset))
    {
        return;
    }

    (void)apply_handled_signal_mask(blockset);
}

/**
 * @brief Construct an inactive signal handler.
 *
 * Construction leaves the worker stopped. start() performs the signal-set
 * setup and launches the worker when the surrounding process is ready.
 */
SignalHandler::SignalHandler()
    : SignalHandler(wait_for_blocked_signal)
{
}

SignalHandler::SignalHandler(SignalWaitFunction wait_function_value)
    : state(SignalHandlerState::STOPPED),
      termios_saved(false),
      wait_function(std::move(wait_function_value))
{
}

/**
 * @brief Starts the signal handling worker thread.
 *
 * The signal set is built from signal_map and blocked in the calling thread
 * before the worker is launched so the worker inherits the correct mask.
 *
 * @warning Repeated calls while the worker is already running are ignored.
 */
void SignalHandler::start()
{
    if (state.load() != SignalHandlerState::STOPPED)
    {
        return;
    }

    state.store(SignalHandlerState::RUNNING);

    if (tcgetattr(STDIN_FILENO, &original_termios) == 0)
    {
        termios_saved = true;

        termios new_termios = original_termios;
#ifdef ECHOCTL
        new_termios.c_lflag &= ~ECHOCTL;
#endif
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    }

    if (!build_handled_signal_set(signal_set))
    {
        state.store(SignalHandlerState::FAILED);
        return;
    }

    if (!apply_handled_signal_mask(signal_set))
    {
        state.store(SignalHandlerState::FAILED);
        return;
    }

    worker_thread = std::thread(&SignalHandler::run, this);
}

/**
 * @brief Destroy the signal handler after stopping the worker thread.
 *
 * Destruction enforces orderly shutdown so the worker cannot outlive the
 * object it accesses.
 *
 * @warning This must not run on the worker thread because stop() joins it.
 */
SignalHandler::~SignalHandler()
{
    const SignalHandlerState current = state.load();

    if (current == SignalHandlerState::RUNNING ||
        current == SignalHandlerState::STOP_REQUESTED ||
        current == SignalHandlerState::FAILED)
    {
        stop();
    }
}

/**
 * @brief Set the user-defined callback for handled signals.
 *
 * The callback is stored by value and later invoked inline on the signal
 * worker thread.
 *
 * @param cb Callback receiving the signal number and immediate flag
 */
void SignalHandler::setCallback(const std::function<void(int, bool)> &cb)
{
    callback = cb;
}

/**
 * @brief Converts a signal number to its corresponding name string.
 *
 * @param signum Signal number to look up
 * @return Signal name, or "UNKNOWN" if the signal is not mapped
 */
std::string_view SignalHandler::signalToString(int signum)
{
    auto it = signal_map.find(signum);
    if (it != signal_map.end())
    {
        return it->second.first;
    }

    return "UNKNOWN";
}

/**
 * @brief Stops the signal handling thread and restores terminal state.
 *
 * Shutdown always joins the worker before returning. This preserves object
 * lifetime safety because run() accesses this object directly while active.
 *
 * @return True if this call performed a clean stop, false if the worker was
 *         already stopped or shutdown was already in progress
 *
 * @warning stop() can wait for any in-flight callback to finish because the
 *          callback runs inline on the worker thread.
 */
bool SignalHandler::stop()
{
    const SignalHandlerState current = state.load();

    if (current == SignalHandlerState::STOPPED ||
        current == SignalHandlerState::STOP_REQUESTED)
    {
        return false;
    }

    const bool worker_needs_wake = current == SignalHandlerState::RUNNING;
    if (worker_needs_wake)
    {
        state.store(SignalHandlerState::STOP_REQUESTED);
    }

    if (worker_thread.joinable())
    {
        if (worker_needs_wake)
        {
            const int retval = pthread_kill(worker_thread.native_handle(), SIGUSR1);
            if (retval != 0)
            {
                std::cerr
                    << "[ERROR] Failed to wake signal handler thread with SIGUSR1. "
                    << "pthread_kill() returned "
                    << retval
                    << ". Joining anyway because returning before thread exit would "
                       "leave object lifetime unsafe."
                    << std::endl;
            }
        }

        worker_thread.join();
    }

    state.store(SignalHandlerState::STOPPED);

    if (termios_saved)
    {
        if (tcsetattr(STDIN_FILENO, TCSANOW, &original_termios) != 0)
        {
            std::cerr
                << "[WARN ] Failed to restore terminal settings."
                << std::endl;
        }
        termios_saved = false;
    }

    return true;
}

/**
 * @brief Sets the scheduling policy and priority of the signal handling thread.
 *
 * This is an optional tuning hook for applications that want signal handling
 * to remain responsive under load.
 *
 * @param schedPolicy Scheduling policy such as SCHED_FIFO or SCHED_RR
 * @param priority Priority value for the selected scheduling policy
 * @return True if the scheduling change succeeded, false otherwise
 */
bool SignalHandler::setPriority(int schedPolicy, int priority)
{
    if (state.load() != SignalHandlerState::RUNNING || !worker_thread.joinable())
    {
        return false;
    }

    sched_param sch_params;
    sch_params.sched_priority = priority;

    int ret = pthread_setschedparam(worker_thread.native_handle(), schedPolicy, &sch_params);

    return (ret == 0);
}

/**
 * @brief Main loop for the signal handling thread.
 *
 * The worker waits synchronously through the normalized platform seam, filters
 * shutdown wake signal, and invokes the callback inline for handled signals.
 * When STOP_REQUESTED is observed, the loop exits promptly so stop() can join.
 *
 * @warning Because the callback runs inline here, any blocking callback work
 *          directly delays shutdown completion.
 */
void SignalHandler::run()
{
#ifdef DEBUG_SIGNAL_HANDLER
    std::cout << "Signal thread running, waiting for signals." << std::endl;
#endif

    SignalHandler *local_this = this;

    sigset_t local_set;
    if (!build_handled_signal_set(local_set))
    {
        local_this->state.store(SignalHandlerState::FAILED);
        return;
    }

    // Re-apply the handled mask inside the worker itself so the signal
    // waiter stays correct even if thread creation or later library code
    // disturbed the inherited mask.
    if (!apply_handled_signal_mask(local_set))
    {
        local_this->state.store(SignalHandlerState::FAILED);
        return;
    }

    while (true)
    {
        const SignalHandlerState current = local_this->state.load();

        if (current != SignalHandlerState::RUNNING &&
            current != SignalHandlerState::STOP_REQUESTED)
        {
            break;
        }

        const SignalWaitResult wait_result = local_this->wait_function(local_set);

        const SignalHandlerState post_wait_state = local_this->state.load();
        if (post_wait_state == SignalHandlerState::STOP_REQUESTED)
        {
            break;
        }

        if (post_wait_state != SignalHandlerState::RUNNING)
        {
            break;
        }

        if (wait_result.status == SignalWaitStatus::Interrupted)
        {
            continue;
        }

        if (wait_result.status == SignalWaitStatus::Failed)
        {
            std::cerr << "[ERROR] Synchronous signal wait failed with error "
                      << wait_result.error_number << "." << std::endl;
            SignalHandlerState expected = SignalHandlerState::RUNNING;
            (void)local_this->state.compare_exchange_strong(
                expected,
                SignalHandlerState::FAILED);
            break;
        }

        const int sig = wait_result.signal_number;

        if (sig == SIGUSR1)
        {
            continue;
        }

        auto it = signal_map.find(sig);
        if (it == signal_map.end())
        {
            continue;
        }

        bool immediate = it->second.second;

        if (local_this->callback)
        {
#ifdef DEBUG_SIGNAL_HANDLER
            std::cout << "Callback requested for signal: "
                      << SignalHandler::signalToString(sig) << std::endl;
            std::cout << std::flush;
#endif
            local_this->callback(sig, immediate);
        }
        else
        {
            if (immediate)
            {
                std::exit(EXIT_FAILURE);
            }
        }
    }

    if (local_this->state.load() == SignalHandlerState::STOP_REQUESTED)
    {
        local_this->state.store(SignalHandlerState::STOPPED);
    }
}
