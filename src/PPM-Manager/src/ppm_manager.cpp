/**
 * @file ppm_manager.cpp
 * @brief Implementation of the PPMManager class for managing periodic PPM
 * calculations.
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

#include "ppm_manager.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <sstream>
#include <vector>

#ifdef DEBUG_PPMMANAGER
#include <iomanip>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#endif

/**
 * @brief Global instance of the PPMManager class.
 *
 * Responsible for measuring and managing frequency drift (PPM)
 * through the currently configured provider adapter.
 */
PPMManager ppmManager;

/**
 * @brief Constructs a PPMManager instance.
 *
 * Initializes internal values but does not start the update loop.
 */
PPMManager::PPMManager() : ppm_value_(0.0), running_(false) {}

namespace
{
std::vector<std::string> split_csv_line(const std::string &line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ','))
        fields.push_back(field);
    return fields;
}

std::optional<double> parse_finite_double(const std::string &value)
{
    try
    {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed == value.size() && std::isfinite(parsed))
            return parsed;
    }
    catch (...) {}
    return std::nullopt;
}

std::string run_command(const char *command)
{
    FILE *pipe = popen(command, "r");
    if (!pipe)
        return {};
    std::string output;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        output += buffer;
    if (pclose(pipe) != 0)
        return {};
    return output;
}
}

/**
 * @brief Initializes the PPMManager.
 *
 * Attempts to retrieve the initial estimate and quality snapshot from chrony.
 *
 * @return A PPMStatus indicating success or failure reason.
 */
PPMStatus PPMManager::initialize()
{
    PPMProviderSnapshot snapshot = get_chrony_snapshot();
    std::optional<double> chrony_ppm_opt = snapshot.frequency_ppm;

    // Check if Chrony is available
    if (!chrony_ppm_opt.has_value())
    {
        return PPMStatus::ERROR_CHRONY_NOT_FOUND;
    }

    // Safely extract the Chrony PPM value
    ppm_value_.store(*chrony_ppm_opt);
    {
        std::lock_guard<std::mutex> lock(ppm_mutex_);
        provider_snapshot_ = std::move(snapshot);
    }

    if (ppm_callback_)
        ppm_callback_(ppm_value_.load());

#ifdef DEBUG_PPMMANAGER
    std::cerr << "[DEBUG] :init() initial PPM = " << ppm_value_.load() << " ppm\n";
#endif

    // Start the loop
    start_ppm_update_loop();

    return PPMStatus::SUCCESS;
}

/**
 * @brief Registers a callback to be invoked when the PPM value changes.
 *
 * The callback function receives the new PPM value.
 *
 * @param callback A function or lambda that takes a double PPM value.
 */
void PPMManager::setPPMCallback(std::function<void(double)> callback)
{
    std::lock_guard<std::mutex> lock(ppm_mutex_);
    ppm_callback_ = std::move(callback);
}

/**
 * @brief Destroys the PPMManager instance.
 *
 * Ensures the PPM update loop stops before the object is destroyed.
 */
PPMManager::~PPMManager()
{
    stop(); // Ensure the thread stops on destruction
}

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
 *         `false` otherwise (e.g., thread not running_ or `pthread_setschedparam()` failed).
 *
 * @note
 * The caller may require elevated privileges (e.g., CAP_SYS_NICE) to apply real-time priorities.
 * It is the caller's responsibility to ensure the priority value is valid for the given policy.
 */
bool PPMManager::setPriority(int schedPolicy, int priority)
{
    // Ensure that the worker thread is active and joinable
    if (!ppm_thread_.joinable())
    {
        return false;
    }

    // Set up the scheduling parameters
    sched_param sch_params;
    sch_params.sched_priority = priority;

    // Attempt to apply the scheduling policy and priority
    int ret = pthread_setschedparam(ppm_thread_.native_handle(), schedPolicy, &sch_params);

    return (ret == 0);
}

/**
 * @brief Checks if the system time is synchronized.
 *
 * Uses `timedatectl` to determine whether NTP synchronization is active.
 *
 * @return True if the system time is synchronized, false otherwise.
 */
bool PPMManager::isTimeSynchronized()
{
    // Ask chrony for its sources, silencing any errors
    FILE *pipe = popen("chronyc sources -n 2>/dev/null", "r");
    if (!pipe)
    {
        return false;
    }

    char buffer[128];
    bool synced = false;

    // Read each line; a leading '*' means we have a valid sync source
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        if (buffer[0] == '*')
        {
            synced = true;
            break;
        }
    }

    pclose(pipe);
    return synced;
}

/**
 * @brief Check if the Chrony daemon is active under systemd.
 *
 * Invokes “systemctl is-active --quiet chronyd” and returns true
 * only if the Chrony service is running. This is the most direct
 * way to verify that your NTP client is alive when you have systemd.
 *
 * @return true if the Chrony service is active; false otherwise.
 */
bool PPMManager::isChronyAlive()
{
    // This returns 0 if the service is active, non-zero otherwise
    int ret = std::system("systemctl is-active --quiet chronyd");
    // On most Linuxes, system() returns the child’s exit status directly,
    // so zero means “active”.
    return (ret == 0);
}

/**
 * @brief Retrieves the initial PPM value from Chrony.
 *
 * Uses `chronyc tracking` to get the frequency offset in PPM.
 * Returns `-9999.0` if Chrony is unavailable.
 *
 * @return The PPM value from Chrony, or `-9999.0` on failure.
 */
PPMProviderSnapshot PPMManager::get_chrony_snapshot()
{
    // Anchor before querying: time spent collecting the reports must not
    // make their selected-source sample look newer than it is.
    const auto acquired_at = std::chrono::steady_clock::now();
    return parseChronyReports(
        run_command("chronyc -c tracking 2>/dev/null"),
        run_command("chronyc -c sources -n 2>/dev/null"),
        run_command("chronyc -c sourcestats -n 2>/dev/null"),
        acquired_at);
}

PPMProviderSnapshot PPMManager::parseChronyReports(
    const std::string &tracking_csv,
    const std::string &sources_csv,
    const std::string &sourcestats_csv,
    std::chrono::steady_clock::time_point acquired_at)
{
    PPMProviderSnapshot result;
    result.age_reference_time = acquired_at;
    std::string tracking_line;
    std::stringstream tracking_stream(tracking_csv);
    std::getline(tracking_stream, tracking_line);
    const auto tracking = split_csv_line(tracking_line);
    if (tracking.size() < 14)
    {
        result.error_reason = "chrony tracking data is unavailable or incomplete";
        return result;
    }

    result.frequency_ppm = parse_finite_double(tracking[7]);
    result.residual_frequency_ppm = parse_finite_double(tracking[8]);
    result.skew_ppm = parse_finite_double(tracking[9]);
    result.leap_normal = tracking[13] == "Normal";

    std::vector<std::string> selected_names;
    std::vector<std::string> combined_names;
    bool has_local_reference = false;
    bool has_network = false;
    bool has_unknown_source = false;
    std::string line;
    std::stringstream sources_stream(sources_csv);
    while (std::getline(sources_stream, line))
    {
        const auto fields = split_csv_line(line);
        if (fields.size() < 3)
            continue;
        if (fields[1] == "*" || fields[1] == "+")
        {
            (fields[1] == "*" ? selected_names : combined_names).push_back(fields[2]);
            // Chrony's source mode identifies the kind of source. Names and
            // unselected rows cannot establish the selected estimate's origin.
            if (fields[0] == "#")
                has_local_reference = true;
            else if (fields[0] == "^" || fields[0] == "=")
                has_network = true;
            else
                has_unknown_source = true;
            if (fields[1] == "*" && fields.size() > 6)
                result.age_seconds = parse_finite_double(fields[6]).value_or(
                    std::numeric_limits<double>::quiet_NaN());
        }
    }
    result.selected_source = selected_names.size() == 1 && !selected_names.front().empty();
    if (!result.selected_source)
        result.age_seconds = std::numeric_limits<double>::quiet_NaN();
    result.combined_sources = !combined_names.empty();
    result.synchronized = result.selected_source && result.leap_normal;

    std::sort(selected_names.begin(), selected_names.end());
    std::sort(combined_names.begin(), combined_names.end());
    for (const auto &name : selected_names)
    {
        if (!result.source_signature.empty())
            result.source_signature += ",";
        result.source_signature += "*:" + name;
    }
    for (const auto &name : combined_names)
    {
        if (!result.source_signature.empty())
            result.source_signature += ",";
        result.source_signature += "+:" + name;
    }

    std::vector<std::string> all_names = selected_names;
    all_names.insert(all_names.end(), combined_names.begin(), combined_names.end());
    std::sort(all_names.begin(), all_names.end());

    if (has_unknown_source || !result.selected_source)
        result.source_provenance = "Unknown";
    else if (has_local_reference && has_network)
        result.source_provenance = "Mixed";
    else if (has_local_reference)
        result.source_provenance = "GNSS/PPS or other local reference clock";
    else if (has_network)
        result.source_provenance = "Network NTP";
    else
        result.source_provenance = "Unknown";

    if (!result.frequency_ppm.has_value())
        result.error_reason = "chrony did not report a finite frequency estimate";
    else if (!result.selected_source)
        result.error_reason = "chrony has no unique selected source";
    else if (!result.leap_normal)
        result.error_reason = "chrony leap status is not normal";
    else if (!std::isfinite(result.age_seconds) || result.age_seconds < 0.0)
        result.error_reason = "chrony selected-source age is unavailable or malformed";

    std::stringstream stats_stream(sourcestats_csv);
    bool first_selected_stat = true;
    while (std::getline(stats_stream, line))
    {
        const auto fields = split_csv_line(line);
        if (fields.size() < 4 ||
            std::find(all_names.begin(), all_names.end(), fields[0]) == all_names.end())
            continue;
        const auto retained = parse_finite_double(fields[1]);
        const auto span = parse_finite_double(fields[3]);
        if (!retained.has_value() || !span.has_value())
            continue;
        const std::size_t retained_count = static_cast<std::size_t>(std::max(0.0, *retained));
        if (first_selected_stat)
        {
            result.retained_source_samples = retained_count;
            result.source_stability_span_seconds = *span;
            first_selected_stat = false;
        }
        else
        {
            result.retained_source_samples = std::min(result.retained_source_samples, retained_count);
            result.source_stability_span_seconds = std::min(result.source_stability_span_seconds, *span);
        }
    }
    return result;
}

/**
 * @brief The internal update loop for recalculating PPM.
 *
 * Periodically refreshes the provider estimate and quality snapshot.
 * The loop continues running_ while `running_` is true.
 *
 * @param interval_seconds The interval in seconds between PPM updates.
 */
PPMStatus PPMManager::ppm_update_loop(int interval_seconds)
{
    const int check_interval = 1;

    while (running_)
    {
        // Fetch Chrony PPM
        PPMProviderSnapshot snapshot = get_chrony_snapshot();
        std::optional<double> chrony_ppm_opt = snapshot.frequency_ppm;
        bool chrony_available = chrony_ppm_opt.has_value();
        double chrony_ppm = chrony_available ? *chrony_ppm_opt : 0.0;

        double final_ppm = chrony_available ? chrony_ppm : 0.0;

        // Update ppm_value_ if there's a significant change
        {
            std::lock_guard<std::mutex> lock(ppm_mutex_);
            provider_snapshot_ = snapshot;
            const bool ppm_changed =
                std::abs(final_ppm - ppm_value_.load()) > 0.01;
            if (ppm_changed)
            {
                ppm_value_.store(final_ppm);
            }
            if (ppm_callback_)
            {
                // Capture the callback and value by copy, then detach.
                std::thread([cb = ppm_callback_, val = final_ppm]()
                            { cb(val); })
                    .detach();
            }
        }

#ifdef DEBUG_PPMMANAGER
        // Print debug information
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::cout << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] "
                  << "chrony PPM: " << chrony_ppm << " | Published PPM: " << final_ppm << std::endl;
#endif

        // Sleep for interval_seconds, checking `running_` status every second
        for (int i = 0; i < interval_seconds; i += check_interval)
        {
            if (!running_)
                return PPMStatus::SUCCESS;
            std::this_thread::sleep_for(std::chrono::seconds(check_interval));
        }
    }

    return PPMStatus::SUCCESS;
}

/**
 * @brief Starts the PPM update loop in a background thread.
 *
 * Ensures that only one instance of the loop runs at a time.
 */
PPMStatus PPMManager::start_ppm_update_loop()
{
    if (running_)
    {
        return PPMStatus::SUCCESS;
    }
    running_ = true;
    ppm_thread_ = std::thread(&PPMManager::ppm_update_loop, this, ppm_update_interval_);

    // Define the desired scheduling policy and use the provided priority.
    int policy = SCHED_RR; // Round-robin scheduling; alternatives include SCHED_FIFO
    sched_param sch_params;
    sch_params.sched_priority = ppm_loop_priority_; // Use the optional parameter (defaults to 10)

    // Set the thread's scheduling policy and priority.
    int ret = pthread_setschedparam(ppm_thread_.native_handle(), policy, &sch_params);

    if (ret != 0)
    {
#ifdef DEBUG_PPMMANAGER
        std::cerr << "Failed to set thread priority: " << ::strerror(ret) << std::endl;
#endif
        ;
        ;
    }

    return PPMStatus::SUCCESS;
}

/**
 * @brief Stops the PPM update loop safely.
 *
 * Ensures the thread is properly terminated before stopping.
 */
PPMStatus PPMManager::stop()
{
    if (running_)
    {
        running_ = false;
        if (ppm_thread_.joinable())
        {
            ppm_thread_.join();
        }
    }
    return PPMStatus::SUCCESS;
}

/**
 * @brief Retrieves the latest calculated PPM value.
 *
 * Ensures thread-safe access to the PPM value.
 *
 * @return The most recent PPM measurement.
 */
double PPMManager::getCurrentPPM()
{
    std::lock_guard<std::mutex> lock(ppm_mutex_);
#ifdef DEBUG_PPMMANAGER
    std::cout << "[DEBUG] :getCurrentPPM() PPM Value: " << ppm_value_ << std::endl;
#endif
    return ppm_value_.load(std::memory_order_acquire);
}

PPMProviderSnapshot PPMManager::getProviderSnapshot()
{
    std::lock_guard<std::mutex> lock(ppm_mutex_);
    return provider_snapshot_.withAgeAt(std::chrono::steady_clock::now());
}

bool PPMManager::isRunning() const
{
    return running_.load(std::memory_order_acquire);
}
