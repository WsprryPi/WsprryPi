/**
 * @file runtime_config_operations.hpp
 * @brief Narrow runtime-facing configuration operations.
 */

#ifndef RUNTIME_CONFIG_OPERATIONS_HPP
#define RUNTIME_CONFIG_OPERATIONS_HPP

#include "config_types.hpp"

#include <atomic>
#include <cstdint>
#include <string>

class BandLookup;

extern BandLookup lookup;
extern std::atomic<bool> ini_reload_pending;
extern std::atomic<std::uint64_t> ini_reload_generation;
extern std::atomic<bool> ppm_reload_pending;

void apply_runtime_config_side_effects();
void show_config_values(bool reload);
bool validate_config_data();
void start_runtime_config_monitor(const std::string &filename);
void stop_runtime_config_monitor() noexcept;
void set_startup_diagnostic_deferral(bool enabled) noexcept;
void emit_deferred_startup_diagnostics();
bool consume_startup_config_handoff() noexcept;
bool apply_managed_startup_policy_if_requested(bool startup_config_handoff);

bool has_direct_tone_startup_request() noexcept;
bool try_get_direct_tone_startup_request(
    WsprFrequencyEntry &entry_out,
    double &actual_rf_frequency_hz_out) noexcept;
bool has_qrss_startup_request() noexcept;
bool try_get_qrss_startup_request(
    std::string &message_out,
    double &frequency_hz_out,
    double &dot_seconds_out) noexcept;
bool has_fskcw_startup_request() noexcept;
bool try_get_fskcw_startup_request(
    std::string &message_out,
    double &mark_frequency_hz_out,
    double &space_frequency_hz_out,
    double &dot_seconds_out) noexcept;
bool has_dfcw_startup_request() noexcept;
bool try_get_dfcw_startup_request(
    std::string &message_out,
    double &dot_frequency_hz_out,
    double &dash_frequency_hz_out,
    double &dot_seconds_out) noexcept;

#endif // RUNTIME_CONFIG_OPERATIONS_HPP
