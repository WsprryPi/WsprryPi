/**
 * @file main.cpp
 * @brief Demonstration program for the SignalHandler class.
 *
 * Licensed under the repository-root LICENSE.md.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * @details
 * This example sets up a global signal handler to catch POSIX signals and
 * coordinates a clean shutdown of multiple worker threads upon receiving
 * a termination signal (e.g., SIGINT or SIGTERM).
 *
 * Features:
 * - Blocks signals in the main thread and all spawned worker threads.
 * - Starts a dedicated signal-handling thread to catch and respond to signals.
 * - Demonstrates thread synchronization using condition variables.
 */

#include "signal_handler.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

// -----------------------------------------------------------------------------
// Global Resources
// -----------------------------------------------------------------------------

/// Global unique instance of SignalHandler.
SignalHandler signalHandler;

/// Controls thread synchronization and shutdown.
std::mutex cv_mutex;
std::condition_variable cv;
std::atomic<bool> stop_requested{false};

// -----------------------------------------------------------------------------
// Signal Callback
// -----------------------------------------------------------------------------

/**
 * @brief Signal handler callback registered with SignalHandler.
 *
 * @details
 * Triggered when a signal is received. Logs the signal and sets the
 * `stop_requested` flag, then notifies all waiting threads.
 *
 * @param signum The signal number received.
 * @param critical Whether the signal is marked as critical (unused here).
 */
void signal_handler(int signum, bool critical = false)
{
    std::cout << "Caught signal " << SignalHandler::signalToString(signum)
              << ", stopping gracefully." << std::endl;

    {
        std::lock_guard<std::mutex> lock(cv_mutex);
        stop_requested.store(true);
    }

    cv.notify_all();
}

// -----------------------------------------------------------------------------
// Worker Thread
// -----------------------------------------------------------------------------

/**
 * @brief Simulated worker thread that waits until stop is requested.
 *
 * @param id The thread's unique ID (unused in this example).
 */
void worker_thread(int id)
{
    (void)id;

    std::unique_lock<std::mutex> lock(cv_mutex);

    while (!stop_requested.load())
    {
        lock.unlock();

        // Simulate computation or I/O without a deprecated volatile loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        lock.lock();

        cv.wait_for(lock, std::chrono::milliseconds(100), []
                    { return stop_requested.load(); });
    }
}

// -----------------------------------------------------------------------------
// Main Entry Point
// -----------------------------------------------------------------------------

/**
 * @brief Main function demonstrating signal-safe multithreading.
 *
 * @return Exit status code.
 */
int main()
{
    block_signals();

    signalHandler.setCallback(signal_handler);
    signalHandler.start();
    signalHandler.setPriority(SCHED_RR, 10);

    std::vector<std::thread> workers;
    const int num_workers = 4;
    for (int i = 0; i < num_workers; ++i)
    {
        workers.emplace_back(worker_thread, i);
    }

    {
        std::unique_lock<std::mutex> lock(cv_mutex);
        cv.wait(lock, []
                { return stop_requested.load(); });
    }

    signalHandler.stop();

    std::cout << "Waiting for worker threads to finish." << std::endl;

    for (auto &worker : workers)
    {
        worker.join();
    }

    std::cout << "All threads stopped. Exiting." << std::endl;
    return 0;
}
