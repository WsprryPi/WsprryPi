/**
 * @file main.cpp
 * @brief Entry point for the PPM Manager demonstration program.
 *
 * This program initializes the PPMManager, starts worker threads,
 * and handles signal-based termination while ensuring clean shutdown.
 */

#include "ppm_manager.hpp"

#include <chrono>
#include <ctime>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>

/// Global instance of PPMManager
/// Variables to control program execution
std::mutex cv_mutex;
std::condition_variable cv;
bool stop_requested = false;

/**
 * @brief Signal handler for graceful shutdown.
 *
 * Captures termination signals (e.g., SIGINT) and updates the cv and running
 * flag.
 *
 * @param signum The signal number received.
 */
void signal_handler(int signum)
{
    std::cout << "\nCaught signal " << signum << ", stopping gracefully..." << std::endl;
    {
        std::lock_guard<std::mutex> lock(cv_mutex);
        stop_requested = true;
    }
    cv.notify_all(); // Wake up worker threads
}

/**
 * @brief Simulated worker thread function.
 *
 * Each worker runs a computational loop while the `running` flag is true.
 *
 * @param id Unique identifier for the worker thread.
 */
void worker_thread(int id)
{
    std::unique_lock<std::mutex> lock(cv_mutex);
    while (!stop_requested)
    {
        // Simulate some work
        for (volatile int i = 0; i < 1000000; ++i)
            ;

        // Wait for stop signal (non-busy waiting)
        cv.wait_for(lock, std::chrono::milliseconds(100), []
                    { return stop_requested; });
    }
}

/**
 * @brief Main function of the program.
 *
 * Initializes the PPMManager, starts worker threads, and waits for termination.
 *
 * @return 0 on successful execution, nonzero on failure.
 */
int main()
{
    // Register signal handler for graceful exit
    std::signal(SIGINT, signal_handler);

    // Initialize PPM Manager
    PPMStatus status = ppmManager.initialize();
    // Handle initialization errors
    if (status == PPMStatus::ERROR_UNSYNCHRONIZED_TIME)
    {
        std::cerr << "System time is not synchronized." << std::endl;
        return 1;
    }
    else if (status == PPMStatus::ERROR_CHRONY_NOT_FOUND)
    {
        std::cerr << "chrony frequency estimate unavailable." << std::endl;
    }
    ppmManager.setPriority(SCHED_RR, 10);

    // Register a callback function
    ppmManager.setPPMCallback([](double ppm) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_time = *std::localtime(&now_c);
        std::cout << "[" << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] "
                  << "PPM Updated: " << ppm << std::endl;
    });

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_time = *std::localtime(&now_c);
    std::cout << "[" << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] "
              << "Current PPM: " << ppmManager.getCurrentPPM() << std::endl;

    std::vector<std::thread> workers;
    const int num_workers = 4;

    // Launch worker threads
    for (int i = 0; i < num_workers; ++i)
    {
        workers.emplace_back(worker_thread, i);
    }

    // Main thread waits for termination signal
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
        cv.wait(lock, []
                { return stop_requested; });
    }

    // Graceful shutdown
    std::cout << "Stopping PPM Manager." << std::endl;
    ppmManager.stop();

    std::cout << "Waiting for worker threads to finish." << std::endl;
    for (auto &worker : workers)
    {
        worker.join();
    }

    std::cout << "All threads stopped. Exiting." << std::endl;
    return 0;
}
