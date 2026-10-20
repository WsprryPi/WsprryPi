/**
 * @file monitorfile.hpp
 * @brief Header file for MonitorFile class - Monitors file changes.
 *
 * @details
 * Provides a class to monitor file changes in the background using a polling loop.
 * Allows optional callback execution upon detection of a file modification.
 *
 * Licensed under the repository-root LICENSE.md.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#ifndef MONITORFILE_HPP
#define MONITORFILE_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <thread>

namespace fs = std::filesystem;

/**
 * @enum MonitorState
 * @brief Represents the possible states of the file monitoring process.
 */
enum class MonitorState
{
    NOT_MONITORING, ///< Monitoring has been stopped.
    MONITORING,     ///< Monitoring is active, file exists, no recent changes.
    FILE_NOT_FOUND, ///< The file does not exist.
    FILE_CHANGED    ///< The file was modified and has stabilized.
};

/**
 * @class MonitorFile
 * @brief Monitors a file for changes in a background thread.
 *
 * @details
 * Periodically checks the modification timestamp of a given file and reports
 * changes after a stable period. Can trigger a callback when a change is detected.
 */
class MonitorFile
{
public:
    /**
     * @brief Constructs a MonitorFile object.
     */
    MonitorFile();

    /**
     * @brief Destroys the MonitorFile object.
     *
     * @details
     * Ensures monitoring is stopped and resources are cleaned up safely.
     */
    ~MonitorFile();

    /**
     * @brief Starts monitoring a specified file.
     *
     * @param fileName The full path of the file to monitor.
     * @param cb Optional callback function to invoke when the file changes.
     * @return MonitorState::MONITORING if monitoring starts successfully.
     * @return MonitorState::FILE_NOT_FOUND if the file does not exist.
     *
     * @note
     * If a callback is not provided here, it can be set later using set_callback().
     */
    MonitorState filemon(const std::string &fileName, std::function<void()> cb = nullptr);

    /**
     * @brief Sets the scheduling policy and priority of the monitor thread.
     *
     * @param schedPolicy The thread scheduling policy (e.g., SCHED_FIFO).
     * @param priority The priority value for the thread.
     * @return true if the scheduling parameters were successfully applied.
     * @return false if the thread is not running or setting priority failed.
     *
     * @note Requires CAP_SYS_NICE privileges for real-time policies.
     */
    bool setPriority(int schedPolicy, int priority);

    /**
     * @brief Stops the file monitoring thread.
     *
     * @details
     * Signals the monitoring loop to exit and joins the background thread.
     */
    void stop();

    /**
     * @brief Sets the polling interval for file change checks.
     *
     * @param interval Polling interval in milliseconds.
     *
     * @note Default polling interval is implementation-defined.
     */
    void set_polling_interval(std::chrono::milliseconds interval);

    /**
     * @brief Retrieves the current monitoring state.
     *
     * @return The current MonitorState.
     */
    MonitorState get_state();

    /**
     * @brief Sets a callback function to be called when the file changes.
     *
     * @param func Callback function taking no arguments.
     */
    void set_callback(std::function<void()> func);

private:
    /**
     * @brief Internal thread function that runs the monitoring loop.
     *
     * @details
     * Compares the file's last modification time periodically and detects
     * stabilized changes. Executes callback if change is confirmed.
     */
    void monitor_loop();

    std::string file_name;                      ///< Path of the file being monitored.
    std::optional<fs::file_time_type> org_time; ///< Last known modification timestamp.
    std::optional<std::uintmax_t> org_size;     ///< Last known file size.
#if defined(__linux__) || defined(__APPLE__)
    std::optional<std::uintmax_t> org_device;   ///< Last known device id.
    std::optional<std::uintmax_t> org_inode;    ///< Last known inode id.
#endif
    std::thread monitoring_thread;              ///< Thread for running monitor loop.
    std::atomic<bool> stop_monitoring;          ///< Signals monitoring loop to terminate.
    std::chrono::milliseconds polling_interval; ///< Interval between file checks.
    std::atomic<MonitorState> monitoring_state; ///< Tracks current monitor state.
    std::function<void()> callback;             ///< Optional callback on file change.
    mutable std::shared_mutex mutex;            ///< Protects shared access to file state.
    std::condition_variable_any cv;             ///< Condition variable for timing/sleep.
    int stable_checks = 0;                      ///< Counts stable intervals before confirming change.
};

#endif // MONITORFILE_HPP
