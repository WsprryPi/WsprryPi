/**
 * @file ppm_manager.hpp
 * @brief Header file for the PPMManager class.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * This file declares the PPMManager class, which manages periodic PPM (Parts
 * Per Million) calculations to track clock drift. 1 PPM = 1 microsecond of
 * drift every second.
 *
 * It retrieves PPM values from Chrony (if available) and periodically updates
 * them using system timing functions.
 *
 * Licensed under the repository-root LICENSE.md.
 */

#ifndef PPM_MANAGER_HPP
#define PPM_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <string>
#include <vector>

struct PPMProviderSnapshot
{
    std::string provider_name = "chrony";
    std::optional<double> frequency_ppm;
    bool synchronized = false;
    double age_seconds = std::numeric_limits<double>::quiet_NaN();
    std::optional<std::chrono::steady_clock::time_point> age_reference_time;
    std::optional<double> residual_frequency_ppm;
    std::optional<double> skew_ppm;
    bool selected_source = false;
    bool combined_sources = false;
    bool leap_normal = false;
    std::string source_provenance;
    std::string source_signature;
    std::size_t retained_source_samples = 0;
    double source_stability_span_seconds = 0.0;
    std::string error_reason;

    // Age a copy, rebasing its age anchor so repeated consumption cannot
    // double-count elapsed time. An unknown age/anchor must remain unknown.
    PPMProviderSnapshot withAgeAt(std::chrono::steady_clock::time_point now) const
    {
        auto result = *this;
        if (!age_reference_time || now < *age_reference_time ||
            !std::isfinite(age_seconds) || age_seconds < 0.0)
        {
            result.age_seconds = std::numeric_limits<double>::quiet_NaN();
            return result;
        }
        result.age_seconds += std::chrono::duration<double>(now - *age_reference_time).count();
        result.age_reference_time = now;
        return result;
    }
};

/**
 * @enum PPMStatus
 * @brief Defines possible return statuses for PPMManager operations.
 */
enum class PPMStatus
{
    SUCCESS,                  ///< Operation completed successfully.
    WARNING_HIGH_PPM,         ///< Measured PPM exceeds a safe threshold.
    ERROR_CHRONY_NOT_FOUND,   ///< The chrony provider is unavailable.
    ERROR_UNSYNCHRONIZED_TIME ///< System time is not synchronized.
};

/**
 * @class PPMManager
 * @brief Manages periodic PPM (Parts Per Million) calculations to track clock drift.
 *
 * This class retrieves provider observations and periodically refreshes the
 * published frequency estimate and quality metadata.
 */
class PPMManager
{
public:
    /**
     * @brief Constructs a PPMManager instance.
     *
     * Initializes internal values but does not start the update loop.
     */
    PPMManager();

    /**
     * @brief Destroys the PPMManager instance.
     *
     * Ensures the update loop is properly stopped before destruction.
     */
    ~PPMManager();

    /**
     * @brief Initializes the PPMManager by checking synchronization and obtaining initial PPM.
     *
     * @return A PPMStatus indicating success or failure reason.
     */
    PPMStatus initialize();

    /**
     * @brief Stops the PPM update loop.
     *
     * Ensures the background thread terminates properly before stopping.
     *
     * @return A PPMStatus indicating whether the loop stopped successfully.
     */
    PPMStatus stop();

    /**
     * @brief Sets the scheduling policy and priority of the signal handling thread.
     *
     * @details
     * Uses `pthread_setschedparam()` to adjust the real-time scheduling policy and
     * priority of the signal handling worker thread.
     *
     * This function is useful for raising the importance of the signal handling
     * thread under high system load, especially when using `SCHED_FIFO` or
     * `SCHED_RR`.
     *
     * @param schedPolicy The scheduling policy (e.g., `SCHED_FIFO`, `SCHED_RR`, `SCHED_OTHER`).
     * @param priority The thread priority value to assign (depends on policy).
     *
     * @return `true` if the scheduling parameters were successfully applied,
     *         `false` otherwise.
     */
    bool setPriority(int schedPolicy, int priority);

    /**
     * @brief Retrieves the latest calculated PPM value.
     *
     * Ensures thread-safe access to the PPM value.
     *
     * @return The current PPM value.
     */
    double getCurrentPPM();

    PPMProviderSnapshot getProviderSnapshot();

    static PPMProviderSnapshot parseChronyReports(
        const std::string &tracking_csv,
        const std::string &sources_csv,
        const std::string &sourcestats_csv,
        std::chrono::steady_clock::time_point acquired_at = std::chrono::steady_clock::now());

    /**
     * @brief Checks whether the system time is synchronized.
     *
     * Uses system utilities to determine if the clock is synchronized.
     *
     * @return True if time is synchronized, false otherwise.
     */
    bool isTimeSynchronized();

    /**
     * @brief Check if the Chrony daemon is active under systemd.
     *
     * Invokes “systemctl is-active --quiet chronyd” and returns true
     * only if the Chrony service is running. This is the most direct
     * way to verify that your NTP client is alive when you have systemd.
     *
     * @return true if the Chrony service is active; false otherwise.
     */
    bool isChronyAlive();

    /**
     * @brief Registers a callback to be invoked when the PPM value changes.
     *
     * The callback function receives the new PPM value.
     *
     * @param callback A function or lambda that takes a double PPM value.
     */
    void setPPMCallback(std::function<void(double)> callback);

    /**
     * @brief Returns whether the PPM update loop is currently running.
     *
     * @return True if the background PPM thread is active, false otherwise.
     */
    bool isRunning() const;

private:
    std::atomic<double> ppm_value_;            ///< Stores the current PPM value.
    std::mutex ppm_mutex_;                     ///< Ensures thread-safe access to the PPM value.
    std::atomic<bool> running_;                ///< Indicates whether the update loop is running.
    std::thread ppm_thread_;                   ///< Background thread for PPM updates.
    std::function<void(double)> ppm_callback_; ///< Callback function for PPM updates.
    PPMProviderSnapshot provider_snapshot_;

    static constexpr int ppm_update_interval_ = 120;  ///< Interval in seconds between PPM updates.
    static constexpr int ppm_loop_priority_ = 10;     ///< Default scheduling priority.

    PPMProviderSnapshot get_chrony_snapshot();

    /**
     * @brief Starts the PPM update loop in a background thread.
     *
     * Ensures that only one instance of the loop runs at a time.
     *
     * @return A PPMStatus indicating success.
     */
    PPMStatus start_ppm_update_loop();

    /**
     * @brief The internal update loop for recalculating PPM.
     *
     * Runs in a background thread and periodically updates the PPM value.
     *
     * @param interval_seconds The interval in seconds between PPM updates.
     * @return A PPMStatus indicating success or a warning if an anomaly is detected.
     */
    PPMStatus ppm_update_loop(int interval_seconds);
};

/**
 * @brief Global instance of the PPMManager class.
 *
 * Responsible for measuring and managing frequency drift (PPM)
 * through the currently configured provider adapter.
 */
extern PPMManager ppmManager;

#endif // PPM_MANAGER_HPP
