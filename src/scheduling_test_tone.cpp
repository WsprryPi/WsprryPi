/**
 * @file scheduling_test_tone.cpp
 * @brief Owns transient test-tone and operator stop orchestration.
 */

#include "scheduling.hpp"
#include "scheduling_internal.hpp"

#include "band_lookup.hpp"
#include "frequency_semantics.hpp"
#include "gpio_band_policy.hpp"
#include "legacy_gpio_clock_model.hpp"
#include "logging.hpp"
#include "rp1_gpclk_development_policy.hpp"
#include "rp1_route_bridge.hpp"
#include "runtime_config_bridge.hpp"
#include "runtime_config_operations.hpp"
#include "test_tone_frequency_plan.hpp"
#include "test_tone_selector_plan.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "version.hpp"
#include "wtp_runtime_bridge.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <string>

namespace
{
wsprrypi::BackendKind to_controller_backend(
    TransmitBackendKind backend) noexcept
{
    if (backend == TransmitBackendKind::WTP) return wsprrypi::BackendKind::WTP;
    if (backend == TransmitBackendKind::SI5351)
        return wsprrypi::BackendKind::SI5351;
    if (backend == TransmitBackendKind::SIMULATED)
        return wsprrypi::BackendKind::SIMULATED;
    if (backend == TransmitBackendKind::RP1_GPCLK)
        return wsprrypi::BackendKind::RP1_GPCLK;
    return wsprrypi::BackendKind::RPI_CLOCK_GPIO;
}

std::optional<wsprrypi::LegacyGpioProcessorProfile>
resolve_legacy_gpio_processor_profile() noexcept
{
    const int generation = get_raspberry_pi_generation();
    if (generation == 1)
        return wsprrypi::LegacyGpioProcessorProfile::Bcm2835;
    if (generation == 2 || generation == 3)
        return wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837;
    if (generation == 4)
        return wsprrypi::LegacyGpioProcessorProfile::Bcm2711;
    return std::nullopt;
}

wsprrypi::HardwareProfile to_controller_profile(
    TransmitBackendKind backend) noexcept
{
    if (backend == TransmitBackendKind::SI5351)
        return wsprrypi::HardwareProfile::SI5351;
    if (backend == TransmitBackendKind::SIMULATED || backend == TransmitBackendKind::WTP)
        return wsprrypi::HardwareProfile::UNSPECIFIED;
    if (backend == TransmitBackendKind::RP1_GPCLK)
        return wsprrypi::HardwareProfile::RP1_GPCLK;
    const auto processor = resolve_legacy_gpio_processor_profile();
    return processor.has_value()
        ? wsprrypi::legacyHardwareProfile(*processor)
        : wsprrypi::HardwareProfile::UNSPECIFIED;
}
}

/**
 * @brief Start a transient runtime tone using scheduler-owned setup.
 *
 * The scheduler stops any active run, reuses the first configured
 * frequency entry, prepares band-selector GPIO state, commits a tone request,
 * and starts the transmitter. When provided, the override is the final RF
 * frequency and does not receive WSPR dial-frequency offset. Tone mode here is
 * runtime-only behavior.
 */
static void rollback_failed_test_tone_start(ModeType previous_mode) noexcept
{
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    try
    {
        transmitter_stop_and_join();
        transmitter_clear_execution_state_after_stop();
        finalize_transmission_stop_cleanup(
            &config,
            false,
            "test tone start rejection",
            true,
            true);
    }
    catch (const std::exception &error)
    {
        llog.logE(
            ERROR,
            "Test tone rejection cleanup failed: " +
                std::string(error.what()));
    }
    catch (...)
    {
        llog.logE(ERROR, "Test tone rejection cleanup failed.");
    }

    (void)reconcile_tx_led_after_transmitter_stop(
        "test tone start rejection");
    current_transmission_request = TransmissionRequest{};
    reset_current_controller_request_for_test();
    reset_committed_execution_route_for_test();
    current_dial_frequency = 0.0;
    current_frequency_entry = WsprFrequencyEntry{};
    web_test_tone.store(false);
    config.mode = previous_mode;
    test_tone_restoration_owner = TestToneRestorationOwner::Unknown;
}

TestToneStartResult start_test_tone(const TestToneRequest &tone_request)
{
    TestToneStartResult result;
    result.source = tone_request.source;
    if (config.transmit_backend == TransmitBackendKind::WTP &&
        (!tone_request.duration || *tone_request.duration <= std::chrono::nanoseconds::zero())) {
        result.message = "Pico requires a finite tone duration; continuous Test Tone is unavailable.";
        return result;
    }

    if (web_test_tone.load())
    {
        result.already_active = true;
        result.message = "Test tone is already active.";
        return result;
    }

    if (scheduler_managed_transmission_active_for_test_tone())
    {
        result.blocked_by_active_transmission = true;
        result.message =
            "Stop transmissions before starting a test tone. Disable transmissions after the active transmission stops.";
        llog.logS(WARN, result.message);
        return result;
    }

    if (scheduler_managed_transmission_enabled_for_test_tone())
    {
        result.blocked_by_enabled_transmission = true;
        result.message =
            "Disable transmissions before starting a test tone.";
        llog.logS(WARN, result.message);
        return result;
    }

    // Explicit requests are completely planned from one accepted configuration
    // snapshot before any runtime mutation.
    const bool has_explicit_frequency_source =
        tone_request.source == TestToneFrequencySource::WsprBand ||
        tone_request.source == TestToneFrequencySource::CustomRf;
    std::optional<TestTonePlanningConfigSnapshot> explicit_planning_snapshot;
    std::optional<TestToneFrequencyPlan> explicit_frequency_plan;
    std::optional<TestToneSelectorPlan> explicit_selector_plan;
    if (has_explicit_frequency_source)
    {
        const TestTonePlanningConfigSnapshot planning_snapshot =
            current_test_tone_planning_config_snapshot();
        const auto frequency_plan = plan_explicit_test_tone_frequency(
            tone_request,
            planning_snapshot.wspr_audio_offset_hz,
            planning_snapshot.wspr_frequency_profile,
            planning_snapshot.wspr_band_preferences);
        if (!frequency_plan)
        {
            result.message = frequency_plan.error;
            return result;
        }
        const auto resolved_band = lookup.lookup_ham_band(
            static_cast<double>(frequency_plan.plan->actual_rf_frequency_hz));
        if (!resolved_band.has_value())
        {
            result.message = "Unable to resolve the planned RF band.";
            return result;
        }
        const auto selector_plan = plan_test_tone_selector(
            *resolved_band, planning_snapshot.wspr_frequency_entries,
            planning_snapshot.band_gpio);
        if (!selector_plan)
        {
            result.message = selector_plan.error;
            return result;
        }
        explicit_planning_snapshot = planning_snapshot;
        explicit_frequency_plan = *frequency_plan.plan;
        explicit_selector_plan = selector_plan.plan;

        const auto gpio_policy = wsprrypi::evaluate_gpio_band_policy(
            to_controller_backend(planning_snapshot.transmit_backend),
            static_cast<double>(frequency_plan.plan->actual_rf_frequency_hz),
            wsprrypi::TransmissionMode::TONE,
            planning_snapshot.allow_unqualified_frequency,
            planning_snapshot.allow_non_amateur_frequency,
            to_controller_profile(planning_snapshot.transmit_backend));
        if (!gpio_policy.allowed)
        {
            result.message = gpio_policy.error;
            llog.logS(WARN, result.message);
            return result;
        }
    }

    // Capture restoration ownership before Test Tone changes config.mode.  A
    // managed, transmit-disabled non-WSPR mode is idle configuration, not a
    // transient direct-tone startup request.
    TestToneRestorationOwner restoration_owner = TestToneRestorationOwner::Unknown;
    if (config.mode == ModeType::WSPR)
    {
        restoration_owner = TestToneRestorationOwner::WsprScheduler;
    }
    else
    {
        WsprFrequencyEntry startup_entry;
        double startup_rf_frequency_hz = 0.0;
        if (try_get_direct_tone_startup_request(startup_entry, startup_rf_frequency_hz))
        {
            restoration_owner = TestToneRestorationOwner::DirectToneStartup;
        }
        else if (config.use_ini && !runtime_transmit_enabled(config) &&
                 (config.mode == ModeType::FSKCW || config.mode == ModeType::QRSS ||
                  config.mode == ModeType::DFCW))
        {
            restoration_owner = TestToneRestorationOwner::ManagedIdleNonWspr;
        }
    }

    web_test_tone.store(true);

    // Save previous mode so we can restore it later.
    lastMode = config.mode;
    test_tone_restoration_owner = restoration_owner;

    try
    {
    transmitter_stop_and_join();

    ArgParserConfig selector_preparation_cfg;
    WsprFrequencyEntry entry;
    double dial_freq = 0.0;
    double actual_rf_freq = 0.0;
    if (has_explicit_frequency_source)
    {
        const TestToneFrequencyPlan &frequency_plan = *explicit_frequency_plan;
        const TestToneSelectorPlan &selector_plan = *explicit_selector_plan;
        dial_freq = frequency_plan.dial_frequency_hz.has_value()
            ? static_cast<double>(*frequency_plan.dial_frequency_hz)
            : static_cast<double>(frequency_plan.actual_rf_frequency_hz);
        actual_rf_freq = static_cast<double>(frequency_plan.actual_rf_frequency_hz);
        entry = WsprFrequencyEntry{};
        entry.dial_frequency_hz = dial_freq;
        entry.token = frequency_plan.band;
        if (selector_plan.enabled)
        {
            entry.selector_gpio = selector_plan.config.gpio;
            entry.selector_gpio_active_high = selector_plan.config.active_high;
        }
        selector_preparation_cfg.band_gpio = explicit_planning_snapshot->band_gpio;
        selector_preparation_cfg.transmit_backend =
            explicit_planning_snapshot->transmit_backend;
        selector_preparation_cfg.allow_unqualified_frequency =
            explicit_planning_snapshot->allow_unqualified_frequency;
        selector_preparation_cfg.allow_non_amateur_frequency =
            explicit_planning_snapshot->allow_non_amateur_frequency;
        result.band = frequency_plan.band;
        result.dial_frequency_hz = frequency_plan.dial_frequency_hz.value_or(0);
        result.audio_offset_hz = frequency_plan.audio_offset_hz.value_or(0);
        result.resolution_source = frequency_plan.resolution_source;
        result.preset = frequency_plan.preset;
    }
    else
    {
        // Legacy requests retain their established scheduler behavior.
        selector_preparation_cfg = config;
        entry = next_frequency_entry(/*restart=*/true);
        dial_freq = entry.dial_frequency_hz;
        const double configured_actual_rf_freq = resolve_actual_rf_frequency_hz(
            dial_freq,
            selector_preparation_cfg.wspr.audio_offset_hz,
            FrequencyPath::WsprDial);
        actual_rf_freq = tone_request.frequency_hz.has_value()
            ? static_cast<double>(*tone_request.frequency_hz)
            : configured_actual_rf_freq;
    }
    current_frequency_entry = entry;

    llog.logS(INFO, "Beginning test tone requested by web UI.");

    // Switch into tone mode.
    config.mode = ModeType::TONE;

    llog.logS(
        DEBUG,
        "Resolved WSPR dial frequency ",
        lookup.freq_display_string(dial_freq),
        " to actual RF ",
        lookup.freq_display_string(actual_rf_freq),
        " using audio offset ",
        explicit_frequency_plan.has_value()
            ? static_cast<double>(explicit_frequency_plan->audio_offset_hz.value_or(0))
            : selector_preparation_cfg.wspr.audio_offset_hz,
        " Hz.");
    if (tone_request.frequency_hz.has_value())
    {
        llog.logS(
            INFO,
            "Using web UI test tone RF frequency override: ",
            lookup.freq_display_string(actual_rf_freq));
    }
    double committed_ppm = config.ppm;
    if (transmit_backend_uses_gpio_output(config.transmit_backend))
    {
        const GpioFrequencyCorrection selected_correction =
            select_and_publish_gpio_correction_for_config(config);
        if (!selected_correction.valid)
        {
            throw std::runtime_error(selected_correction.reason);
        }
        committed_ppm = selected_correction.additional_ppm;
        config.ppm = committed_ppm;
    }
    TransmissionRequest request =
        make_tone_request(config, committed_ppm, actual_rf_freq, dial_freq, entry);
    request.tone_duration = tone_request.duration;
    if (explicit_frequency_plan.has_value())
    {
        request.applied_offset_hz = explicit_frequency_plan->audio_offset_hz.has_value()
            ? static_cast<double>(*explicit_frequency_plan->audio_offset_hz)
            : 0.0;
    }
    selector_preparation_cfg.mode = ModeType::TONE;
    BandGPIOResolution selector_resolution;
    const BandGPIOPrepareStatus selector_status =
        prepare_band_gpio_for_frequency_or_log(
            dial_freq,
            entry,
            selector_preparation_cfg,
            -1,
            &selector_resolution);
    commit_band_gpio_snapshot_to_request(
        request,
        selector_resolution,
        selector_status);
    if (selector_status == BandGPIOPrepareStatus::Failed)
    {
        web_test_tone.store(false);
        config.mode = lastMode;
        test_tone_restoration_owner = TestToneRestorationOwner::Unknown;
        const auto gpio_policy = wsprrypi::evaluate_gpio_band_policy(
            to_controller_backend(selector_preparation_cfg.transmit_backend),
            actual_rf_freq,
            wsprrypi::TransmissionMode::TONE,
            selector_preparation_cfg.allow_unqualified_frequency,
            selector_preparation_cfg.allow_non_amateur_frequency,
            to_controller_profile(selector_preparation_cfg.transmit_backend));
        result.message = gpio_policy.allowed
            ? "Unable to prepare the requested band selector."
            : gpio_policy.error;
        return result;
    }
    if (tone_request.rp1_development.enabled)
    {
        apply_test_tone_rp1_development_confirmation_bridge(
            tone_request.rp1_development, config, request);
    }
    if (config.transmit_backend == TransmitBackendKind::WTP)
    {
        // WTP's RF duration starts at the future device slot. The WebSocket
        // cleanup timer starts only after this function returns. Include the
        // preparation lead and the nominal five-second status/cleanup allowance
        // so a finite job can complete before host cleanup requests a stop.
        const auto allowance = wtp_runtime_preparation_lead() + std::chrono::seconds(5);
        if (*tone_request.duration > std::chrono::nanoseconds::max() - allowance)
            throw std::runtime_error("Bounded Pico tone cleanup deadline overflows");
        result.bounded_cleanup_delay = *tone_request.duration + allowance;
    }
    commit_execution_request(request);
    result.actual_rf_frequency_hz = static_cast<std::uint64_t>(actual_rf_freq);
    result.selector_gpio_enabled = request.selector_gpio_enabled;
    result.selector_gpio = request.selector_gpio_enabled
        ? request.selector_gpio_config.gpio
        : -1;
    result.selector_gpio_active_high = request.selector_gpio_enabled
        ? request.selector_gpio_config.active_high
        : false;
    if (result.band.empty()) result.band = ham_band_to_string(*lookup.lookup_ham_band(dial_freq));

    if (!suppress_scheduler_execution_for_test)
    {
        transmitter_start_async();
    }

    llog.logS(INFO,
              "WSPR-band test tone using dial frequency: ",
              lookup.freq_display_string(dial_freq));
    result.started = true;
    result.message = "Test tone started.";
    return result;
    }
    catch (const std::exception &error)
    {
        const std::string detail = error.what();
        llog.logE(
            ERROR,
            "Test tone start rejected during configuration: " + detail);
        rollback_failed_test_tone_start(lastMode);
        result.message = detail.empty()
            ? "Unable to configure the requested test tone."
            : detail;
        return result;
    }
    catch (...)
    {
        llog.logE(
            ERROR,
            "Test tone start rejected by an unknown configuration error.");
        rollback_failed_test_tone_start(lastMode);
        result.message = "Unable to configure the requested test tone.";
        return result;
    }
}

TestToneStartResult start_test_tone(
    std::optional<std::uint64_t> frequency_hz_override)
{
    TestToneRequest request;
    request.source = frequency_hz_override.has_value()
        ? TestToneFrequencySource::LegacyExactRf
        : TestToneFrequencySource::LegacyDefault;
    request.frequency_hz = frequency_hz_override;
    return start_test_tone(request);
}

/**
 * @brief End the transient runtime tone and restore prior orchestration.
 *
 * This stops the current tone, tears down selector lifecycle state through
 * the scheduler helper, and then resumes the pre-tone runtime mode.
 */
TestToneStopResult end_test_tone()
{
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    TestToneStopResult result;
    result.tone_was_active = web_test_tone.load();

    if (!result.tone_was_active)
    {
        result.message = "No active test tone.";
        return result;
    }

    llog.logS(INFO, "Ending test tone requested by Web UI.");

    transmitter_stop_and_join();
    transmitter_clear_execution_state_after_stop();
    finalize_transmission_stop_cleanup(
        &config,
        runtime_should_hold_selector_gpios_initialized(config),
        "test tone stop",
        true,
        true);
    clear_committed_execution_request();
    llog.logS(
        DEBUG,
        "Post-test-tone stop transmitter snapshot: ",
        transmitter_reload_defer_debug_snapshot());
    (void)reconcile_tx_led_after_transmitter_stop("test tone stop");

    const bool deferred_reload_pending =
        ini_reload_pending.load(std::memory_order_acquire);

    const TestToneRestorationOwner restoration_owner = test_tone_restoration_owner;
    test_tone_restoration_owner = TestToneRestorationOwner::Unknown;
    web_test_tone.store(false);
    config.mode = lastMode;

    if (restoration_owner == TestToneRestorationOwner::WsprScheduler)
    {
        if (config.mode != ModeType::WSPR)
        {
            result.message = "Inconsistent Test Tone scheduler restoration state.";
            return result;
        }
        if (!set_config(true))
        {
            result.message = "Unable to restore scheduler state after test tone stop.";
            return result;
        }

        if (runtime_transmit_enabled(config) &&
            !suppress_scheduler_execution_for_test)
        {
            // Re-arm the committed WSPR wait loop even if the tone stop
            // interrupted a scheduler thread that had already been torn down.
            transmitter_clear_soft_off();
            transmitter_start_async();
        }

        result.stopped = true;
        result.scheduler_restored = true;
        result.deferred_reload_reconciled =
            deferred_reload_pending &&
            !ini_reload_pending.load(std::memory_order_acquire);
        result.message = "Test tone stopped and scheduler restored.";
        return result;
    }

    if (restoration_owner == TestToneRestorationOwner::ManagedIdleNonWspr)
    {
        if (!(config.use_ini && !runtime_transmit_enabled(config) &&
              (config.mode == ModeType::FSKCW || config.mode == ModeType::QRSS ||
               config.mode == ModeType::DFCW)))
        {
            result.message = "Inconsistent managed Test Tone restoration state.";
            return result;
        }

        result.stopped = true;
        result.message = "Test tone stopped; managed mode restored inactive.";
        return result;
    }

    if (restoration_owner != TestToneRestorationOwner::DirectToneStartup)
    {
        result.message = "Unknown Test Tone restoration state after safe cleanup.";
        return result;
    }

    WsprFrequencyEntry entry;
    double actual_rf_frequency_hz = 0.0;
    if (!try_get_direct_tone_startup_request(
            entry,
            actual_rf_frequency_hz))
    {
        llog.logE(ERROR,
                  "Unable to restore direct test tone; no "
                  "transient tone request is active.");
        result.message =
            "Unable to restore direct test tone; no transient tone request is active.";
        return result;
    }

    validate_config_data();
    std::string restoration_error;
    if (!start_direct_tone_execution(
            config,
            entry,
            actual_rf_frequency_hz,
            &restoration_error))
    {
        result.stopped = true;
        result.message = restoration_error;
        return result;
    }

    llog.logS(INFO,
              "Transmitting tone, hit Ctrl-C to terminate tone.");
    result.stopped = true;
    result.message = "Test tone stopped and direct tone restored.";
    return result;
}

StopTransmissionResult stop_transmission_by_user_request(bool persist_transmit)
{
    StopTransmissionResult result;
    bool persist_to_ini = false;
    suppress_cancelled_ws_event_for_user_stop.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(set_config_mtx);

        const WsprTransmitState state = transmitter_state();
        result.transmission_active =
            state == WsprTransmitState::TRANSMITTING;

        llog.logS(
            INFO,
            result.transmission_active
                ? "Stop transmission requested by user; stopping active transmission."
                : "Stop transmission requested by user; no active transmission.");

        // Invalidate delayed launches before releasing the lock so no pending
        // scheduler thread can start another transmission during stop handling.
        non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel);
        shutdown_after_current_transmission.store(false, std::memory_order_release);
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();

        config.transmit = false;
        result.transmit_disabled = true;
        config_to_json();
        persist_to_ini = config.use_ini && persist_transmit;
    }

    if (result.transmission_active)
    {
        suppress_cancelled_ws_event_for_user_stop.store(true, std::memory_order_release);
    }

    transmitter_stop_and_join();
    deassert_transmit_gpio_outputs(
        &config,
        false,
        "scheduler shutdown");
    release_idle_selector_gpio_reservations();

    {
        std::lock_guard<std::mutex> lk(set_config_mtx);

        current_transmission_request = TransmissionRequest{};
        current_dial_frequency = 0.0;
        current_frequency_entry = WsprFrequencyEntry{};
        freq_iterator = 0;
        web_test_tone.store(false);

        result.stop_performed = result.transmission_active;
    }

    if (persist_to_ini)
    {
        try
        {
            persist_runtime_transmit_disabled();
            result.persisted = true;
            llog.logS(INFO, "Transmit disabled due to user stop request.");
        }
        catch (const std::exception &e)
        {
            result.persisted = false;
            result.message =
                std::string("Transmission stopped but failed to persist Operation.Transmit=false: ") +
                e.what();
            llog.logS(ERROR, result.message);
            return result;
        }
    }
    else
    {
        result.persisted = false;
        result.message = persist_transmit
                             ? "Transmission stopped and runtime transmit disabled; no INI file is active."
                             : "Transmission stopped and runtime transmit disabled without persisting.";
        llog.logS(INFO, result.message);
        send_ws_message("transmit", "stopped");
        return result;
    }

    {
        std::lock_guard<std::mutex> lk(set_config_mtx);
        set_managed_reload_tx_inhibited(false);
    }
    send_ws_message("transmit", "stopped");

    result.message = result.transmission_active
                         ? "Active transmission stopped and transmit disabled."
                         : "Transmit disabled; no active transmission was running.";
    return result;
}
