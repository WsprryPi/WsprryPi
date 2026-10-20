/**
 * @file monitorfile.cpp
 * @brief Implementation file for MonitorFile class.
 *
 * Licensed under the repository-root LICENSE.md.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "monitorfile.hpp"

#include <chrono>
#include <iostream>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace
{
    struct FileSnapshot
    {
        bool exists = false;
        fs::file_time_type write_time{};
        std::uintmax_t size = 0;
#if defined(__linux__) || defined(__APPLE__)
        std::uintmax_t device = 0;
        std::uintmax_t inode = 0;
#endif
    };

    bool read_file_snapshot(
        const std::string &file_name,
        FileSnapshot &snapshot) noexcept
    {
        snapshot = FileSnapshot{};

        std::error_code ec;
        if (!fs::exists(file_name, ec) || ec)
        {
            return false;
        }

        snapshot.exists = true;
        snapshot.write_time = fs::last_write_time(file_name, ec);
        if (ec)
        {
            snapshot = FileSnapshot{};
            return false;
        }

        snapshot.size = fs::file_size(file_name, ec);
        if (ec)
        {
            snapshot = FileSnapshot{};
            return false;
        }

#if defined(__linux__) || defined(__APPLE__)
        struct stat st
        {
        };
        if (::stat(file_name.c_str(), &st) != 0)
        {
            snapshot = FileSnapshot{};
            return false;
        }

        snapshot.device = static_cast<std::uintmax_t>(st.st_dev);
        snapshot.inode = static_cast<std::uintmax_t>(st.st_ino);
#endif

        return true;
    }

    bool snapshots_equal(
        const FileSnapshot &lhs,
        const FileSnapshot &rhs) noexcept
    {
        if (lhs.exists != rhs.exists)
        {
            return false;
        }

        if (!lhs.exists)
        {
            return true;
        }

        if (lhs.write_time != rhs.write_time || lhs.size != rhs.size)
        {
            return false;
        }

#if defined(__linux__) || defined(__APPLE__)
        if (lhs.device != rhs.device || lhs.inode != rhs.inode)
        {
            return false;
        }
#endif

        return true;
    }

    void sync_monitor_baseline(
        const FileSnapshot &snapshot,
        std::optional<fs::file_time_type> &org_time,
        std::optional<std::uintmax_t> &org_size
#if defined(__linux__) || defined(__APPLE__)
        ,
        std::optional<std::uintmax_t> &org_device,
        std::optional<std::uintmax_t> &org_inode
#endif
    ) noexcept
    {
        if (!snapshot.exists)
        {
            org_time.reset();
            org_size.reset();
#if defined(__linux__) || defined(__APPLE__)
            org_device.reset();
            org_inode.reset();
#endif
            return;
        }

        org_time = snapshot.write_time;
        org_size = snapshot.size;
#if defined(__linux__) || defined(__APPLE__)
        org_device = snapshot.device;
        org_inode = snapshot.inode;
#endif
    }
} // namespace

/**
 * @brief Constructs the MonitorFile object.
 *
 * Initializes monitoring flags and polling interval.
 */
MonitorFile::MonitorFile()
    : stop_monitoring(false),
      polling_interval(std::chrono::seconds(1)),
      monitoring_state(MonitorState::NOT_MONITORING)
{
}

/**
 * @brief Destroys the MonitorFile object.
 *
 * Ensures monitoring stops cleanly before destruction.
 */
MonitorFile::~MonitorFile()
{
    stop();
}

/**
 * @brief Starts monitoring a specified file.
 *
 * @param fileName Name of the file to monitor.
 * @param cb Optional callback function to invoke when the file changes.
 * @return MonitorState::MONITORING if monitoring starts successfully,
 *         MonitorState::FILE_NOT_FOUND if the file does not exist.
 */
MonitorState MonitorFile::filemon(
    const std::string &fileName,
    std::function<void()> cb)
{
    std::unique_lock<std::shared_mutex> lock(mutex);

    if (monitoring_thread.joinable())
    {
        lock.unlock();
        stop();
        lock.lock();
    }

    file_name = fileName;
    stable_checks = 0;

    if (cb)
    {
        callback = std::move(cb);
    }

    FileSnapshot snapshot;
    if (read_file_snapshot(file_name, snapshot))
    {
        sync_monitor_baseline(
            snapshot,
            org_time,
            org_size
#if defined(__linux__) || defined(__APPLE__)
            ,
            org_device,
            org_inode
#endif
        );
        monitoring_state.store(MonitorState::MONITORING);
    }
    else
    {
        sync_monitor_baseline(
            FileSnapshot{},
            org_time,
            org_size
#if defined(__linux__) || defined(__APPLE__)
            ,
            org_device,
            org_inode
#endif
        );
        monitoring_state.store(MonitorState::FILE_NOT_FOUND);
    }

    stop_monitoring.store(false);
    monitoring_thread = std::thread(&MonitorFile::monitor_loop, this);

    return monitoring_state.load();
}

/**
 * @brief Sets the scheduling policy and priority for the monitoring thread.
 *
 * @details
 * Uses `pthread_setschedparam()` to configure the monitoring thread’s
 * scheduling behavior. This is useful when monitoring must respond quickly
 * under real-time or high-priority conditions.
 *
 * @param schedPolicy The desired scheduling policy (e.g., `SCHED_FIFO`, `SCHED_RR`, `SCHED_OTHER`).
 * @param priority The priority level associated with the policy.
 *
 * @return `true` if the scheduling parameters were successfully applied.
 * @return `false` if the thread is not running or if `pthread_setschedparam()` fails.
 *
 * @note
 * - Requires appropriate system privileges (e.g., `CAP_SYS_NICE`) to apply real-time policies.
 * - This function should be called after the monitoring thread has started.
 */
bool MonitorFile::setPriority(int schedPolicy, int priority)
{
    if (!monitoring_thread.joinable())
    {
        return false;
    }

    sched_param sch_params;
    sch_params.sched_priority = priority;
    const int ret = pthread_setschedparam(
        monitoring_thread.native_handle(),
        schedPolicy,
        &sch_params);

    return (ret == 0);
}

/**
 * @brief Stops the file monitoring thread.
 */
void MonitorFile::stop()
{
    bool expected = false;
    if (!stop_monitoring.compare_exchange_strong(expected, true))
    {
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        monitoring_state.store(MonitorState::NOT_MONITORING);
    }

    cv.notify_all();

    if (monitoring_thread.joinable())
    {
        monitoring_thread.join();
    }
}

/**
 * @brief Sets the polling interval for monitoring.
 *
 * @param interval The new polling interval in milliseconds.
 */
void MonitorFile::set_polling_interval(std::chrono::milliseconds interval)
{
    std::unique_lock<std::shared_mutex> lock(mutex);
    polling_interval = interval;
    cv.notify_all();
}

/**
 * @brief Gets the current state of the file monitor.
 *
 * @return The current monitoring state.
 */
MonitorState MonitorFile::get_state()
{
    std::shared_lock<std::shared_mutex> lock(mutex);
    return monitoring_state;
}

/**
 * @brief Sets a callback function to be called when the file changes.
 *
 * @param func The callback function.
 */
void MonitorFile::set_callback(std::function<void()> func)
{
    std::unique_lock<std::shared_mutex> lock(mutex);
    callback = std::move(func);
}

/**
 * @brief Runs the monitoring loop to detect file changes.
 */
void MonitorFile::monitor_loop()
{
    std::unique_lock<std::shared_mutex> lock(mutex);

    FileSnapshot last_reported_snapshot{};
    if (org_time.has_value())
    {
        last_reported_snapshot.exists = true;
        last_reported_snapshot.write_time = org_time.value();
        last_reported_snapshot.size = org_size.value_or(0);
#if defined(__linux__) || defined(__APPLE__)
        last_reported_snapshot.device = org_device.value_or(0);
        last_reported_snapshot.inode = org_inode.value_or(0);
#endif
    }

    FileSnapshot pending_snapshot{};
    bool change_detected = false;
    stable_checks = 0;

    while (!stop_monitoring.load())
    {
        cv.wait_for(lock, polling_interval, [this] {
            return stop_monitoring.load();
        });

        if (stop_monitoring.load())
        {
            return;
        }

        FileSnapshot current_snapshot{};
        if (!read_file_snapshot(file_name, current_snapshot))
        {
            current_snapshot = FileSnapshot{};
        }

        if (!change_detected && snapshots_equal(current_snapshot, last_reported_snapshot))
        {
            sync_monitor_baseline(
                current_snapshot,
                org_time,
                org_size
#if defined(__linux__) || defined(__APPLE__)
                ,
                org_device,
                org_inode
#endif
            );
            monitoring_state.store(
                current_snapshot.exists ? MonitorState::MONITORING
                                        : MonitorState::FILE_NOT_FOUND);
            continue;
        }

        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lock.lock();

        current_snapshot = FileSnapshot{};
        if (!read_file_snapshot(file_name, current_snapshot))
        {
            current_snapshot = FileSnapshot{};
        }

        sync_monitor_baseline(
            current_snapshot,
            org_time,
            org_size
#if defined(__linux__) || defined(__APPLE__)
            ,
            org_device,
            org_inode
#endif
        );

        if (!change_detected)
        {
            if (snapshots_equal(current_snapshot, last_reported_snapshot))
            {
                monitoring_state.store(
                    current_snapshot.exists ? MonitorState::MONITORING
                                            : MonitorState::FILE_NOT_FOUND);
                continue;
            }

            pending_snapshot = current_snapshot;
            change_detected = true;
            stable_checks = 0;
            continue;
        }

        if (!snapshots_equal(current_snapshot, pending_snapshot))
        {
            if (snapshots_equal(current_snapshot, last_reported_snapshot))
            {
                change_detected = false;
                stable_checks = 0;
                monitoring_state.store(
                    current_snapshot.exists ? MonitorState::MONITORING
                                            : MonitorState::FILE_NOT_FOUND);
                continue;
            }

            pending_snapshot = current_snapshot;
            stable_checks = 0;
            continue;
        }

        if (++stable_checks < 3)
        {
            monitoring_state.store(
                current_snapshot.exists ? MonitorState::MONITORING
                                        : MonitorState::FILE_NOT_FOUND);
            continue;
        }

        last_reported_snapshot = pending_snapshot;
        change_detected = false;
        stable_checks = 0;

        if (pending_snapshot.exists)
        {
            monitoring_state.store(MonitorState::FILE_CHANGED);
        }
        else
        {
            monitoring_state.store(MonitorState::FILE_NOT_FOUND);
        }

        if (callback)
        {
            lock.unlock();
            callback();
            lock.lock();
        }

        monitoring_state.store(
            pending_snapshot.exists ? MonitorState::MONITORING
                                    : MonitorState::FILE_NOT_FOUND);
    }
}
