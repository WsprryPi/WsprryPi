/**
 * @file scheduling_config.cpp
 * @brief Applies coherent runtime configuration updates to the scheduler.
 */

#include "scheduling.hpp"
#include "scheduling_internal.hpp"

#include "arg_parser.hpp"
#include "band_gpio_selector.hpp"
#include "frequency_semantics.hpp"
#include "logging.hpp"
#include "ppm_manager.hpp"
#include "runtime_config_bridge.hpp"
#include "wspr_transmit.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_pi_control.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace {
class WtpLocalConfigurationReservation {
public:
    WtpLocalConfigurationReservation() : held_(wtp_pi_begin_configuration()) {}
    ~WtpLocalConfigurationReservation() { if (held_) wtp_pi_end_configuration(); }
    explicit operator bool() const noexcept { return held_; }
private:
    bool held_;
};
}

bool set_config(bool force)
{
    std::lock_guard<std::mutex> lk(set_config_mtx);

    // Exit if we are shutting down
    if (exiting_wspr.load())
    {
        llog.logS(DEBUG, "Exiting set_config() early.");
        ini_reload_pending.store(false, std::memory_order_relaxed);
        return true;
    }
    else
    {
        llog.logS(DEBUG, "Processing set_config().");
    }

    for (;;)
    {
        const bool reload_requested =
            ini_reload_pending.load(std::memory_order_acquire);
        const std::uint64_t reload_generation_snapshot =
            reload_requested
                ? ini_reload_generation.load(std::memory_order_acquire)
                : 0U;
        const bool managed_candidate_requested =
            config.use_ini && (force || reload_requested);
        const bool ppm_update_requested =
            ppm_reload_pending.load(std::memory_order_acquire);

        if ((managed_candidate_requested || ppm_update_requested) &&
            (wtp_runtime_selected() ? wtp_runtime_invalidate_for_reload() : transmitter_reload_should_defer()))
        {
            if (managed_candidate_requested)
            {
                ini_reload_pending.store(true, std::memory_order_release);
            }
            return true;
        }

        auto newer_reload_arrived =
            [&]() noexcept
        {
            return reload_requested &&
                   managed_reload_generation_changed(reload_generation_snapshot);
        };

        auto finalize_reload_pending =
            [&]() noexcept
        {
            if (newer_reload_arrived())
            {
                ini_reload_pending.store(true, std::memory_order_release);
                return false;
            }

            ini_reload_pending.store(false, std::memory_order_release);
            return true;
        };

        RuntimeConfigCandidate prepared_candidate{};
        bool candidate_ready_to_commit = false;
        ArgParserConfig working_config = config;

        if (managed_candidate_requested)
        {
            prepare_runtime_config_candidate(config.ini_filename, prepared_candidate);

            for (const auto &warning_message : prepared_candidate.warnings)
            {
                llog.logS(WARN, warning_message);
            }

            if (newer_reload_arrived())
            {
                continue;
            }

            if (!prepared_candidate.valid)
            {
                WtpLocalConfigurationReservation local_configuration;
                llog.logS(ERROR,
                          "Invalid configuration reload rejected; previous valid configuration remains loaded: ",
                          prepared_candidate.error_reason);
                send_ws_message(
                    "configuration",
                    "reload_failed",
                    prepared_candidate.error_reason);
                set_managed_reload_tx_inhibited(
                    true,
                    "Transmit is blocked until a valid configuration is loaded.");

                if (local_configuration &&
                    transmitter_state() != WsprTransmitter::State::TRANSMITTING)
                {
                    transmitter_stop_and_join();
                    deassert_transmit_gpio_outputs(
                        &config,
                        false,
                        "invalid configuration reload");
                    release_idle_selector_gpio_reservations();
                    current_transmission_request = TransmissionRequest{};
                }

                if (!finalize_reload_pending())
                {
                    continue;
                }
                return true;
            }

            working_config = prepared_candidate.normalized_config;
            candidate_ready_to_commit = true;
        }

        // A valid noninteractive Enable takes local priority before any local
        // backend reconfiguration. Other changes wait while a WTP owner uses
        // the same output, including idle time within that ownership lease.
        if (working_config.transmit && managed_candidate_requested &&
            !prepared_candidate.preserve_interactive_takeover)
            wtp_pi_config_transmit_committed(true);
        WtpLocalConfigurationReservation local_configuration;
        if (!local_configuration)
        {
            if (managed_candidate_requested)
                ini_reload_pending.store(true, std::memory_order_release);
            return true;
        }

        bool do_config = force;
        bool do_random = false;

        std::string backend_runtime_error;
        const bool backend_runtime_ready =
            !(working_config.mode == ModeType::TONE ||
              runtime_transmit_requested(working_config)) ||
            backend_ready_for_transmission(
                working_config,
                &backend_runtime_error);

        if (!backend_runtime_ready)
        {
            llog.logS(ERROR, backend_runtime_error);

            if (working_config.use_ini)
            {
                send_ws_message(
                    "configuration",
                    "reload_failed",
                    backend_runtime_error);
                set_managed_reload_tx_inhibited(
                    true,
                    backend_runtime_error);
                transmitter_stop_and_join();
                deassert_transmit_gpio_outputs(
                    &config,
                    false,
                    "backend unavailable");
                release_idle_selector_gpio_reservations();
                current_transmission_request = TransmissionRequest{};
                current_dial_frequency = 0.0;
                current_frequency_entry = WsprFrequencyEntry{};

                if (!finalize_reload_pending())
                {
                    continue;
                }
                return true;
            }

            if (!finalize_reload_pending())
            {
                continue;
            }
            return false;
        }

        bool ppm_running = ppmManager.isRunning();
        bool should_start_ppm = working_config.use_system_clock_frequency_estimate && !ppm_running;
        if (should_start_ppm)
        {
            ppm_init();
            ppm_reload_pending.store(true, std::memory_order_seq_cst);
            ppm_running = ppmManager.isRunning();
            should_start_ppm = false;
        }
        const bool should_stop_ppm = !working_config.use_system_clock_frequency_estimate && ppm_running;
        const bool should_log_ppm_disabled =
            force && !working_config.use_system_clock_frequency_estimate && !ppm_running;

        if (reload_requested)
        {
            do_config = true;
        }

        const bool ppm_update_pending =
            ppm_reload_pending.load(std::memory_order_acquire);
        const bool ppm_manager_authoritative =
            working_config.use_system_clock_frequency_estimate && ppm_running;
        bool runtime_ppm_changed = false;
        double committed_ppm = working_config.ppm;
        if (transmit_backend_uses_gpio_output(working_config.transmit_backend))
        {
            if (ppm_update_pending || ppm_manager_authoritative)
            {
                refresh_frequency_estimate_for_config();
            }
            const GpioFrequencyCorrection selected_correction =
                select_and_publish_gpio_correction_for_config(working_config);
            if (!selected_correction.valid)
            {
                const std::string correction_error =
                    selected_correction.reason.empty()
                        ? "GPIO frequency correction is invalid."
                        : selected_correction.reason;
                llog.logS(ERROR, correction_error);
                if (working_config.use_ini)
                {
                    send_ws_message(
                        "configuration",
                        "reload_failed",
                        correction_error);
                    set_managed_reload_tx_inhibited(true, correction_error);
                    if (transmitter_state() !=
                        WsprTransmitter::State::TRANSMITTING)
                    {
                        transmitter_stop_and_join();
                        deassert_transmit_gpio_outputs(
                            &config,
                            false,
                            "invalid GPIO frequency correction");
                        release_idle_selector_gpio_reservations();
                        current_transmission_request = TransmissionRequest{};
                    }
                }
                if (!finalize_reload_pending())
                {
                    continue;
                }
                return working_config.use_ini;
            }
            committed_ppm = selected_correction.additional_ppm;
            working_config.ppm = committed_ppm;
            if (ppm_update_pending)
            {
                llog.logS(
                    INFO,
                    "GPIO frequency correction updated: mode=",
                    to_string(selected_correction.mode),
                    ", provider=",
                    selected_correction.provider_name,
                    ", estimate_ppm=",
                    selected_correction.estimate_ppm.value_or(0.0),
                    ", residual_ppm=",
                    selected_correction.residual_ppm,
                    ", effective_ppm=",
                    selected_correction.additional_ppm,
                    ", qualification=",
                    to_string(selected_correction.qualification));
                runtime_ppm_changed = true;
                do_config = true;
            }
        }
        else
        {
            committed_ppm = working_config.si5351_ppm;
            working_config.ppm = committed_ppm;
        }

        if (!suppress_scheduler_execution_for_test)
        {
            const bool any_selector_gpio_configured =
                has_configured_selector_gpios(working_config);
            selector_gpio_control_enabled = any_selector_gpio_configured;
            selector_gpio_drive_enabled = any_selector_gpio_configured;
            bandGPIOSelector.setEnabled(selector_gpio_control_enabled);
            bandGPIOSelector.setDriveGPIO(selector_gpio_drive_enabled);
        }
        else
        {
            selector_gpio_drive_enabled = GPIOOutput::testModeEnabled();
            bandGPIOSelector.setDriveGPIO(selector_gpio_drive_enabled);
        }

        const bool keep_selector_gpio_initialized =
            working_config.transmit &&
            runtime_transmit_enabled(working_config);

        std::string selector_gpio_error;
        if (!sync_configured_selector_gpio_idle_state(
                working_config,
                keep_selector_gpio_initialized,
                &selector_gpio_error))
        {
            llog.logS(ERROR, selector_gpio_error);

            if (working_config.use_ini)
            {
                send_ws_message(
                    "configuration",
                    "reload_failed",
                    selector_gpio_error);
                set_managed_reload_tx_inhibited(
                    true,
                    selector_gpio_error);
                transmitter_stop_and_join();
                deassert_transmit_gpio_outputs(
                    &config,
                    false,
                    "selector GPIO synchronization failure");
                release_idle_selector_gpio_reservations();
                current_transmission_request = TransmissionRequest{};
                current_dial_frequency = 0.0;
                current_frequency_entry = WsprFrequencyEntry{};

                if (!finalize_reload_pending())
                {
                    continue;
                }
                return true;
            }

            ini_reload_pending.store(false, std::memory_order_relaxed);
            config.transmit = false;
            config_to_json();
            return false;
        }

        if (is_non_wspr_runtime_mode(working_config.mode))
        {
            std::string policy_error;
            if (!validate_non_wspr_repeat_interval_policy(
                    working_config,
                    &policy_error))
            {
                llog.logS(ERROR, policy_error);

                if (working_config.use_ini)
                {
                    send_ws_message(
                        "configuration",
                        "reload_failed",
                        policy_error);
                    set_managed_reload_tx_inhibited(
                        true,
                        policy_error);
                    transmitter_stop_and_join();
                    deassert_transmit_gpio_outputs(
                        &config,
                        false,
                        "non-WSPR policy failure");
                    release_idle_selector_gpio_reservations();
                    current_transmission_request = TransmissionRequest{};
                    current_dial_frequency = 0.0;
                    current_frequency_entry = WsprFrequencyEntry{};
                }

                if (!finalize_reload_pending())
                {
                    continue;
                }
                return working_config.use_ini;
            }

            if (candidate_ready_to_commit)
            {
                prepared_candidate.normalized_config.ppm = working_config.ppm;
                commit_runtime_config_candidate(prepared_candidate);
                apply_runtime_config_side_effects();
                set_managed_reload_tx_inhibited(false);
                if (reload_requested)
                {
                    send_ws_message("configuration", "reload");
                }
            }
            else if (runtime_ppm_changed)
            {
                config.ppm = working_config.ppm;
            }

            if (should_start_ppm)
            {
                ppm_init();
                ppm_reload_pending.store(true, std::memory_order_seq_cst);
            }
            else if (should_stop_ppm)
            {
                ppmManager.stop();
                llog.logS(INFO, "PPM Manager disabled.");
                ppm_reload_pending.store(false, std::memory_order_seq_cst);
            }
            else if (should_log_ppm_disabled)
            {
                llog.logS(INFO, "PPM Manager disabled.");
            }

            if (ppm_update_pending)
            {
                ppm_reload_pending.store(false, std::memory_order_relaxed);
            }

            transmitter_stop_and_join();
            deassert_transmit_gpio_outputs(
                &config,
                false,
                "non-WSPR reconfiguration");
            release_idle_selector_gpio_reservations();
            current_transmission_request = TransmissionRequest{};
            current_dial_frequency = 0.0;
            current_frequency_entry = WsprFrequencyEntry{};
            freq_iterator = 0;
            reset_active_wspr_plan_state();
            non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel);

            log_scheduler_path_selection(working_config.mode);

            if (!runtime_transmit_preparation_enabled(working_config))
            {
                log_transmit_disabled_skip(working_config);
                if (!finalize_reload_pending())
                {
                    continue;
                }
                return true;
            }

            if (!has_non_wspr_cli_startup_request(working_config.mode))
            {
                schedule_next_non_wspr_launch(working_config);
            }

            if (!finalize_reload_pending())
            {
                continue;
            }
            return true;
        }

        int next_freq_iterator = force ? 0 : freq_iterator;
        double next_current_dial_frequency =
            force ? 0.0 : current_dial_frequency;
        WsprFrequencyEntry next_current_frequency_entry =
            force ? WsprFrequencyEntry{} : current_frequency_entry;
        TransmissionRequest next_transmission_request =
            force ? TransmissionRequest{} : current_transmission_request;
        PreparedWsprTransmission next_active_wspr_plan =
            force ? PreparedWsprTransmission{} : active_wspr_plan;
        std::size_t next_active_wspr_frame_index =
            force ? 0U : active_wspr_frame_index;
        double next_active_wspr_plan_dial_frequency =
            force ? 0.0 : active_wspr_plan_dial_frequency;
        WsprFrequencyEntry next_active_wspr_plan_frequency_entry =
            force ? WsprFrequencyEntry{} : active_wspr_plan_frequency_entry;
        bool next_active_wspr_plan_in_progress =
            force ? false : active_wspr_plan_in_progress;
        if (force)
        {
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
        }

        static double last_freq = 0.0;
        static WsprFrequencyEntry last_frequency_entry{};
        int next_frequency_entry_index = -1;
        if (next_active_wspr_plan_in_progress && next_active_wspr_frame_index > 0U)
        {
            next_current_dial_frequency = next_active_wspr_plan_dial_frequency;
            next_current_frequency_entry = next_active_wspr_plan_frequency_entry;
            do_config = true;
        }
        else
        {
            if (!working_config.wspr_frequency_entries.empty())
            {
                next_frequency_entry_index =
                    next_freq_iterator %
                    static_cast<int>(working_config.wspr_frequency_entries.size());
            }
            next_current_frequency_entry = next_frequency_entry_from(
                working_config.wspr_frequency_entries,
                next_freq_iterator,
                force);
            next_current_dial_frequency =
                next_current_frequency_entry.dial_frequency_hz;
        }

        const bool frequency_entry_changed =
            next_current_frequency_entry.token != last_frequency_entry.token ||
            next_current_frequency_entry.selector_gpio != last_frequency_entry.selector_gpio ||
            next_current_frequency_entry.selector_gpio_active_high != last_frequency_entry.selector_gpio_active_high;
        const bool advanced_to_new_frequency_entry =
            !working_config.wspr_frequency_entries.empty() &&
            next_freq_iterator != freq_iterator;
        const bool advanced_to_new_wspr_slot =
            (next_active_wspr_plan_in_progress &&
             next_active_wspr_frame_index != active_wspr_frame_index) ||
            advanced_to_new_frequency_entry;

        if (advanced_to_new_wspr_slot ||
            next_current_dial_frequency != last_freq ||
            frequency_entry_changed)
        {
            do_config = true;
        }
        else if (working_config.use_offset && next_current_dial_frequency != 0.0)
        {
            do_random = true;
        }

        if (do_config || do_random)
        {
            if (!suppress_scheduler_execution_for_test)
            {
                const bool any_selector_gpio_configured =
                    has_configured_selector_gpios(working_config);
                selector_gpio_control_enabled = any_selector_gpio_configured;
                selector_gpio_drive_enabled = any_selector_gpio_configured;
                bandGPIOSelector.setEnabled(selector_gpio_control_enabled);
                bandGPIOSelector.setDriveGPIO(selector_gpio_drive_enabled);
            }
            else
            {
                selector_gpio_drive_enabled = GPIOOutput::testModeEnabled();
                bandGPIOSelector.setDriveGPIO(selector_gpio_drive_enabled);
            }

            const bool keep_selector_gpio_initialized =
                working_config.transmit &&
                runtime_transmit_enabled(working_config);
            std::string selector_idle_error;
            if (!sync_configured_selector_gpio_idle_state(
                    working_config,
                    keep_selector_gpio_initialized,
                    &selector_idle_error))
            {
                llog.logS(ERROR, "Failed to synchronize selector GPIO idle state: ",
                          selector_idle_error);
                if (is_managed_persistent_mode())
                {
                    set_managed_reload_tx_inhibited(
                        true,
                        "Managed reload could not initialize selector GPIO idle state; previous valid configuration remains loaded. Transmit is blocked until a valid configuration is loaded.");
                    send_ws_message(
                        "configuration",
                        "reload_failed",
                        "Managed reload could not initialize selector GPIO idle state; previous valid configuration remains loaded. Transmit is blocked until a valid configuration is loaded.");
                    if (!finalize_reload_pending())
                    {
                        continue;
                    }
                    return true;
                }

                ini_reload_pending.store(false, std::memory_order_relaxed);
                config.transmit = false;
                config_to_json();
                return false;
            }

            if (working_config.mode == ModeType::WSPR && do_config)
            {
                non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel);
            }

            if (!runtime_transmit_preparation_enabled(working_config))
            {
                if (newer_reload_arrived())
                {
                    continue;
                }

                if (candidate_ready_to_commit)
                {
                    prepared_candidate.normalized_config.ppm = working_config.ppm;
                    // Managed reloads are transactional; only fully validated candidates may replace live state. Invalid reloads result in TX being disabled after the current transmission completes.
                    // For managed -i reloads, once a deferred reload is consumed after TX completion, the freshly prepared valid INI candidate must become the sole source of truth for the next scheduling decision; previously committed live config must not override it.
                    commit_runtime_config_candidate(prepared_candidate);
                    apply_runtime_config_side_effects();
                    set_managed_reload_tx_inhibited(false);
                    if (reload_requested)
                    {
                        send_ws_message("configuration", "reload");
                    }
                }
                else if (runtime_ppm_changed)
                {
                    config.ppm = working_config.ppm;
                }

                if (should_start_ppm)
                {
                    ppm_init();
                    ppm_reload_pending.store(true, std::memory_order_seq_cst);
                }
                else if (should_stop_ppm)
                {
                    ppmManager.stop();
                    llog.logS(INFO, "PPM Manager disabled.");
                    ppm_reload_pending.store(false, std::memory_order_seq_cst);
                }
                else if (should_log_ppm_disabled)
                {
                    llog.logS(INFO, "PPM Manager disabled.");
                }
                else if (ppm_update_pending)
                {
                    ppm_reload_pending.store(false, std::memory_order_relaxed);
                }

                transmitter_stop_and_join();
                deassert_transmit_gpio_outputs(
                    &config,
                    false,
                    "transmit disabled reconfiguration");
                release_idle_selector_gpio_reservations();
                current_transmission_request = TransmissionRequest{};
                current_dial_frequency = 0.0;
                current_frequency_entry = WsprFrequencyEntry{};
                freq_iterator = next_freq_iterator;
                active_wspr_plan = next_active_wspr_plan;
                active_wspr_frame_index = next_active_wspr_frame_index;
                active_wspr_plan_dial_frequency = next_active_wspr_plan_dial_frequency;
                active_wspr_plan_frequency_entry = next_active_wspr_plan_frequency_entry;
                active_wspr_plan_in_progress = next_active_wspr_plan_in_progress;
                last_freq = next_current_dial_frequency;
                last_frequency_entry = next_current_frequency_entry;
                if (!runtime_transmit_requested(working_config))
                {
                    log_transmit_disabled_skip(working_config);
                }
                else
                {
                    llog.logS(INFO, "Transmissions disabled.");
                }

                if (!finalize_reload_pending())
                {
                    continue;
                }
                return true;
            }

            if (exiting_wspr.load(std::memory_order_acquire))
            {
                llog.logS(DEBUG, "Aborting reconfiguration because shutdown is in progress.");
                if (!finalize_reload_pending())
                {
                    continue;
                }
                return true;
            }

            if (next_current_dial_frequency == 0.0)
            {
                llog.logS(
                    INFO,
                    "Skipping transmission period because the planned frequency is 0 Hz.");

                next_active_wspr_plan = PreparedWsprTransmission{};
                next_active_wspr_frame_index = 0U;
                next_active_wspr_plan_dial_frequency = 0.0;
                next_active_wspr_plan_frequency_entry = WsprFrequencyEntry{};
                next_active_wspr_plan_in_progress = false;

                next_transmission_request = make_skip_window_request(
                    working_config,
                    committed_ppm,
                    next_current_dial_frequency,
                    next_current_frequency_entry);
                stop_active_transmission_selectors();
                commit_band_gpio_snapshot_to_request(
                    next_transmission_request,
                    BandGPIOResolution{},
                    BandGPIOPrepareStatus::Inactive);
            }
            else
            {
                const double base_actual_rf_frequency_hz = resolve_actual_rf_frequency_hz(
                    next_current_dial_frequency,
                    working_config.wspr.audio_offset_hz,
                    FrequencyPath::WsprDial);
                const double actual_rf_frequency_hz =
                    maybe_apply_wspr_random_offset(base_actual_rf_frequency_hz,
                                                   working_config);
                const double applied_offset_hz =
                    actual_rf_frequency_hz - base_actual_rf_frequency_hz;

                llog.logS(
                    DEBUG,
                    "Resolved WSPR dial frequency ",
                    lookup.freq_display_string(next_current_dial_frequency),
                    " to actual RF ",
                    lookup.freq_display_string(actual_rf_frequency_hz),
                    " using audio offset ",
                    working_config.wspr.audio_offset_hz,
                    " Hz.");
                if (!configure_current_wspr_transmission(
                        working_config,
                        committed_ppm,
                        next_current_dial_frequency,
                        next_current_frequency_entry,
                        next_active_wspr_plan,
                        next_active_wspr_frame_index,
                        next_active_wspr_plan_dial_frequency,
                        next_active_wspr_plan_frequency_entry,
                        next_active_wspr_plan_in_progress,
                        actual_rf_frequency_hz,
                        next_transmission_request))
                {
                    if (newer_reload_arrived())
                    {
                        continue;
                    }

                    if (is_managed_persistent_mode())
                    {
                        set_managed_reload_tx_inhibited(
                            true,
                            "Managed reload planning failed; previous valid configuration remains loaded. Transmit is blocked until a valid configuration is loaded.");
                        send_ws_message(
                            "configuration",
                            "reload_failed",
                            "Managed reload planning failed; previous valid configuration remains loaded. Transmit is blocked until a valid configuration is loaded.");
                        transmitter_stop_and_join();
                        deassert_transmit_gpio_outputs(
                            &config,
                            false,
                            "managed reload planning failure");
                        release_idle_selector_gpio_reservations();
                        current_transmission_request = TransmissionRequest{};
                        if (!finalize_reload_pending())
                        {
                            continue;
                        }
                        return true;
                    }

                    ini_reload_pending.store(false, std::memory_order_relaxed);
                    config.transmit = false;
                    config_to_json();
                    return false;
                }

                if (!runtime_transmit_enabled(working_config))
                {
                    log_transmit_disabled_skip(working_config);
                    transmitter_stop_and_join();
                    deassert_transmit_gpio_outputs(
                        &config,
                        false,
                        "WSPR request authorization failure");
                    release_idle_selector_gpio_reservations();
                    current_transmission_request = TransmissionRequest{};
                    ini_reload_pending.store(false, std::memory_order_relaxed);
                    return false;
                }

                next_transmission_request.applied_offset_hz = applied_offset_hz;

                BandGPIOResolution selector_resolution;
                const BandGPIOPrepareStatus selector_status =
                    prepare_band_gpio_for_frequency_or_log(
                        next_current_dial_frequency,
                        next_current_frequency_entry,
                        working_config,
                        next_frequency_entry_index,
                        &selector_resolution);
                if (selector_status == BandGPIOPrepareStatus::Failed)
                {
                    deassert_transmit_gpio_outputs(
                        &config,
                        false,
                        "band GPIO preparation failure");

                    if (newer_reload_arrived())
                    {
                        continue;
                    }

                    if (is_managed_persistent_mode())
                    {
                        set_managed_reload_tx_inhibited(
                            true,
                            "Managed reload could not prepare band GPIO; previous valid configuration remains loaded. Transmit is blocked until a valid configuration is loaded.");
                        send_ws_message(
                            "configuration",
                            "reload_failed",
                            "Managed reload could not prepare band GPIO; previous valid configuration remains loaded. Transmit is blocked until a valid configuration is loaded.");
                        if (!finalize_reload_pending())
                        {
                            continue;
                        }
                        return true;
                    }

                    ini_reload_pending.store(false, std::memory_order_relaxed);
                    config.transmit = false;
                    config_to_json();
                    return false;
                }

                commit_band_gpio_snapshot_to_request(
                    next_transmission_request,
                    selector_resolution,
                    selector_status);
            }

            if (newer_reload_arrived())
            {
                deassert_transmit_gpio_outputs(
                    &config,
                    false,
                    "newer reload superseded reconfiguration");
                release_idle_selector_gpio_reservations();
                continue;
            }

            if (candidate_ready_to_commit)
            {
                prepared_candidate.normalized_config.ppm = working_config.ppm;
                // Managed reloads are transactional; only fully validated candidates may replace live state. Invalid reloads result in TX being disabled after the current transmission completes.
                // For managed -i reloads, once a deferred reload is consumed after TX completion, the freshly prepared valid INI candidate must become the sole source of truth for the next scheduling decision; previously committed live config must not override it.
                commit_runtime_config_candidate(prepared_candidate);
                apply_runtime_config_side_effects();
                set_managed_reload_tx_inhibited(false);
                if (reload_requested)
                {
                    send_ws_message("configuration", "reload");
                }
            }
            else if (runtime_ppm_changed)
            {
                config.ppm = working_config.ppm;
            }

            if (should_start_ppm)
            {
                ppm_init();
                ppm_reload_pending.store(true, std::memory_order_seq_cst);
            }
            else if (should_stop_ppm)
            {
                ppmManager.stop();
                llog.logS(INFO, "PPM Manager disabled.");
                ppm_reload_pending.store(false, std::memory_order_seq_cst);
            }
            else if (should_log_ppm_disabled)
            {
                llog.logS(INFO, "PPM Manager disabled.");
            }

            if (ppm_update_pending)
            {
                ppm_reload_pending.store(false, std::memory_order_relaxed);
            }

            current_dial_frequency = next_current_dial_frequency;
            current_frequency_entry = next_current_frequency_entry;
            freq_iterator = next_freq_iterator;
            active_wspr_plan = next_active_wspr_plan;
            active_wspr_frame_index = next_active_wspr_frame_index;
            active_wspr_plan_dial_frequency = next_active_wspr_plan_dial_frequency;
            active_wspr_plan_frequency_entry = next_active_wspr_plan_frequency_entry;
            active_wspr_plan_in_progress = next_active_wspr_plan_in_progress;
            last_freq = next_current_dial_frequency;
            last_frequency_entry = next_current_frequency_entry;
            log_scheduler_path_selection(working_config.mode);
            commit_execution_request(next_transmission_request);
            if (next_transmission_request.rp1_development.enabled)
            {
                llog.logS(
                    INFO,
                    "Bounded RP1 WSPR frame request committed for operation ",
                    next_transmission_request.rp1_development.operation_id,
                    ".");
            }

            if (suppress_scheduler_execution_for_test)
            {
                if (!finalize_reload_pending())
                {
                    deassert_transmit_gpio_outputs(
                        &config,
                        false,
                        "suppressed scheduler execution reload");
                    release_idle_selector_gpio_reservations();
                    continue;
                }
                return true;
            }
        }

        if (runtime_transmit_enabled(config) && (do_config || do_random))
        {
            if (do_random)
            {
                llog.logS(DEBUG, "New random frequency.");
            }
            else
            {
                llog.logS(DEBUG, "Setup complete.");
            }
            llog.logS(INFO, "Waiting for next transmission window.");
            transmitter_start_async();
        }
#ifdef DEBUG_WSPR_TRANSMIT
        wsprTransmitter.dumpParameters();
#endif
        if (!finalize_reload_pending())
        {
            continue;
        }
        return true;
    }
}
