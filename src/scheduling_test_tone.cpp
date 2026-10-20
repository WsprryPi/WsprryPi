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
#include "wtp_pi_control.hpp"
#include "wtp_endpoint/runtime.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace
{
std::recursive_mutex lifecycle_mutex;
std::mutex observation_mutex;
TestToneStatus tone_status;
TestToneLifecycleHook lifecycle_hook;
void checkpoint(std::string_view point) { if (lifecycle_hook) lifecycle_hook(point); }
void publish_status(std::string state, bool owned, std::string message,
                    std::string restoration = "none")
{
    std::lock_guard lock(observation_mutex);
    ++tone_status.revision;
    tone_status.state = std::move(state);
    tone_status.owned = owned;
    tone_status.message = std::move(message);
    tone_status.restoration = std::move(restoration);
}
bool stop_tone_output()
{
    const auto cleanup = transmitter_stop_and_join();
    // Pico owns remote RF cleanup. The generic query intentionally cannot
    // prove its inactivity; use this transaction's WTP stop result instead.
    return wtp_runtime_selected() ? cleanup.ok : transmitter_output_inactive_confirmed();
}
class LocalToneReservation
{
public:
    explicit LocalToneReservation(const bool &cleanup_confirmed)
        : cleanup_confirmed_(cleanup_confirmed) {}
    ~LocalToneReservation()
    {
        if (held_)
            wtp_pi_end_local(cleanup_confirmed_);
    }
    void transfer_to_running_tone() noexcept { held_ = false; }
private:
    bool held_ = true;
    const bool &cleanup_confirmed_;
};

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

std::recursive_mutex &test_tone_lifecycle_mutex() { return lifecycle_mutex; }
TestToneStatus current_test_tone_status()
{
    std::lock_guard lock(observation_mutex);
    return tone_status;
}
void set_test_tone_lifecycle_hook_for_test(TestToneLifecycleHook hook)
{
    std::lock_guard lock(lifecycle_mutex);
    lifecycle_hook = std::move(hook);
}
void record_test_tone_event(std::uint64_t generation,
                           WsprTransmissionCallbackEvent event,
                           const std::string &message)
{
    std::lock_guard lock(observation_mutex);
    if (!tone_status.owned || tone_status.generation != generation) return;
    // Terminal observations cannot be overwritten by a delayed STARTING event.
    if (tone_status.state == "completed" || tone_status.state == "failed" ||
        tone_status.state == "cancelled") return;
    const char *state = event == WsprTransmissionCallbackEvent::STARTING ? "active" :
        event == WsprTransmissionCallbackEvent::COMPLETE ? "completed" :
        event == WsprTransmissionCallbackEvent::FAILED ? "failed" :
        event == WsprTransmissionCallbackEvent::CANCELLED ? "cancelled" : nullptr;
    if (!state) return;
    ++tone_status.revision;
    tone_status.state = state;
    tone_status.message = message;
}

void reconcile_test_tone_runtime()
{
    std::unique_lock lock(lifecycle_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    auto status = current_test_tone_status();
    // WTP reports execution through polled status rather than a STARTING
    // callback. Confirm only the currently owned tone's observed runtime.
    if (status.owned && status.state == "starting" &&
        transmitter_state() == WsprTransmitState::TRANSMITTING &&
        transmitter_active_execution_is_tone()) {
        record_test_tone_event(status.generation, WsprTransmissionCallbackEvent::STARTING, "");
        status = current_test_tone_status();
    }
    if (!status.owned || (status.state != "completed" && status.state != "failed" &&
                          status.state != "cancelled")) return;
    const auto ended = end_test_tone(status.generation,
        status.state == "failed" ? TestToneEndPolicy::Quiesce :
                                  TestToneEndPolicy::RestorePrevious);
    publish_status(ended.restoration_failed ? ended.lifecycle.state : status.state, ended.lifecycle.owned,
        (status.message.empty() ? std::string("Test Tone ") + status.state + "." : status.message) +
        " " + ended.message, ended.lifecycle.restoration);
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
static bool rollback_failed_test_tone_start(ModeType previous_mode) noexcept
{
    bool cleanup_confirmed = false;
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    try
    {
        cleanup_confirmed = stop_tone_output();
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
    transmitter_set_callbacks(transmitter_cb);
    const bool inactive = cleanup_confirmed;
    if (!inactive) config.transmit = false;
    publish_status(inactive ? "failed" : "unknown", false,
        inactive ? "Test Tone startup failed; output cleanup confirmed."
                 : "Test Tone startup failed; output cleanup is unconfirmed. Use End or local recovery.");
    return cleanup_confirmed;
}

static TestToneStartResult start_test_tone_locked(const TestToneRequest &tone_request)
{
    std::unique_lock config_lock(set_config_mtx);
    TestToneStartResult result;
    if (exiting_wspr.load()) { result.message = "Application is stopping."; return result; }
    result.source = tone_request.source;
    if (!web_test_tone.load() && current_test_tone_status().state == "unknown") {
        result.message = "Test Tone output cleanup is unconfirmed. Use End or local recovery before starting another tone.";
        return result;
    }
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

    if (!wtp_pi_begin_test_tone())
    {
        result.message = "The local output is owned by a remote WTP job or is not confirmed idle.";
        return result;
    }
    bool rollback_cleanup_confirmed = false;
    LocalToneReservation local_tone_reservation(rollback_cleanup_confirmed);
    web_test_tone.store(true);

    // Save previous mode so we can restore it later.
    lastMode = config.mode;
    test_tone_restoration_owner = restoration_owner;

    try
    {
    config_lock.unlock();
    transmitter_stop_and_join();
    config_lock.lock();
    // Any delayed launch armed by a drained predecessor callback is stale.
    non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard lock(observation_mutex);
        ++tone_status.generation;
    }
    publish_status("starting", true, "Test Tone accepted; waiting for runtime confirmation.");
    const auto generation = current_test_tone_status().generation;
    transmitter_set_callbacks([generation](WsprTransmissionCallbackEvent event,
        WsprTransmitLogLevel, const std::string &message, double) {
        record_test_tone_event(generation, event, message);
    });

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
        rollback_cleanup_confirmed = rollback_failed_test_tone_start(lastMode);
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

    checkpoint("before_start_async");
    if (exiting_wspr.load()) throw std::runtime_error("Application is stopping.");
    if (!suppress_scheduler_execution_for_test)
    {
        if (config.transmit_backend != TransmitBackendKind::WTP)
            assert_transmit_gpio_outputs("Test Tone start");
        transmitter_start_async();
    }

    llog.logS(INFO,
              "WSPR-band test tone using dial frequency: ",
              lookup.freq_display_string(dial_freq));
    result.started = true;
    result.message = "Test Tone accepted; waiting for runtime confirmation.";
    local_tone_reservation.transfer_to_running_tone();
    return result;
    }
    catch (const std::exception &error)
    {
        const std::string detail = error.what();
        llog.logE(
            ERROR,
            "Test tone start rejected during configuration: " + detail);
        rollback_cleanup_confirmed = rollback_failed_test_tone_start(lastMode);
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
        rollback_cleanup_confirmed = rollback_failed_test_tone_start(lastMode);
        result.message = "Unable to configure the requested test tone.";
        return result;
    }
}

TestToneStartResult start_test_tone(const TestToneRequest &request)
{
    std::lock_guard transaction(lifecycle_mutex);
    checkpoint("start_locked");
    auto result = start_test_tone_locked(request);
    result.lifecycle = current_test_tone_status();
    return result;
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
static TestToneStopResult end_test_tone_locked(std::uint64_t expected_generation, TestToneEndPolicy policy)
{
    std::unique_lock config_lock(set_config_mtx);
    TestToneStopResult result;
    const auto status = current_test_tone_status();
    if (expected_generation && (!status.owned || status.generation != expected_generation))
    {
        // Natural finite completion can reconcile before the host watchdog.
        // Replay that owner's outcome without cleanup/restoration a second time.
        if (status.generation == expected_generation && !status.owned) {
            result.stopped = status.state != "unknown";
            result.restoration_failed = status.restoration == "failed";
            result.scheduler_restored = status.restoration == "scheduler";
            result.output_inactive = transmitter_output_inactive_confirmed();
            result.message = status.message;
        } else result.message = "Test Tone transaction already ended or superseded.";
        return result;
    }
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    result.tone_was_active = web_test_tone.load();

    if (!result.tone_was_active)
    {
        result.output_inactive = transmitter_output_inactive_confirmed();
        if (status.state == "unknown") {
            config_lock.unlock();
            const bool inactive = stop_tone_output();
            config_lock.lock();
            finalize_transmission_stop_cleanup(&config, false, "Test Tone recovery", true, true);
            wtp_pi_end_local(inactive);
            result.output_inactive = inactive;
            config.transmit = false;
            result.stopped = inactive;
            result.message = inactive
                ? "Test Tone recovery cleanup confirmed; previous work was not resumed."
                : "Output cleanup remains unconfirmed; use local recovery.";
            publish_status(inactive ? "ended" : "unknown", false,
                           result.message, "not_resumed");
            return result;
        }
        result.message = "No active Test Tone transaction; prior output state is unchanged.";
        return result;
    }

    llog.logS(INFO, "Ending test tone requested by Web UI.");

    config_lock.unlock();
    const bool cleanup_confirmed = stop_tone_output();
    config_lock.lock();
    transmitter_clear_execution_state_after_stop();
    finalize_transmission_stop_cleanup(
        &config,
        runtime_should_hold_selector_gpios_initialized(config),
        "test tone stop",
        true,
        true);
    clear_committed_execution_request();
    (void)reconcile_tx_led_after_transmitter_stop("test tone stop");
    wtp_pi_end_local(cleanup_confirmed);

    result.stopped = true;
    result.output_inactive = cleanup_confirmed;
    transmitter_set_callbacks(transmitter_cb);
    const bool deferred_reload_pending =
        ini_reload_pending.load(std::memory_order_acquire);

    const TestToneRestorationOwner restoration_owner = test_tone_restoration_owner;
    test_tone_restoration_owner = TestToneRestorationOwner::Unknown;
    web_test_tone.store(false);
    config.mode = lastMode;
    publish_status("ended", false, "Test Tone ended.");
    if (!result.output_inactive || policy == TestToneEndPolicy::Quiesce || exiting_wspr.load())
    {
        config.transmit = false;
        result.restoration_failed = !result.output_inactive;
        result.message = result.output_inactive
            ? "Test Tone ended; previous work was not resumed."
            : "Test Tone cleanup is unconfirmed; output remains inhibited. Use local recovery.";
        publish_status(result.output_inactive ? "ended" : "unknown", false,
                       result.message, "not_resumed");
        return result;
    }
    config_lock.unlock();
    checkpoint("before_restore");

    if (restoration_owner == TestToneRestorationOwner::WsprScheduler)
    {
        if (config.mode != ModeType::WSPR)
        {
            result.restoration_failed = true;
            result.message = "Inconsistent Test Tone scheduler restoration state.";
            return result;
        }
        if (!set_config(true))
        {
            result.restoration_failed = true;
            result.message = "Unable to restore scheduler state after test tone stop.";
            return result;
        }

        if (managed_reload_tx_inhibited_state()) {
            result.restoration_failed = true;
            result.message = "Test Tone ended; configuration restoration was rejected. Transmit remains inhibited.";
            return result;
        }
        if (config.mode == ModeType::WSPR && runtime_transmit_enabled(config) &&
            !suppress_scheduler_execution_for_test)
        {
            // Re-arm the committed WSPR wait loop even if the tone stop
            // interrupted a scheduler thread that had already been torn down.
            transmitter_clear_soft_off();
            transmitter_start_async();
        }

        result.stopped = true;
        result.scheduler_restored = true;
        if (runtime_transmit_enabled(config)) result.output_inactive = false;
        result.deferred_reload_reconciled =
            deferred_reload_pending &&
            !ini_reload_pending.load(std::memory_order_acquire);
        result.message = "Test Tone ended; scheduler configuration restored.";
        publish_status("ended", false, result.message, "scheduler");
        return result;
    }

    if (restoration_owner == TestToneRestorationOwner::ManagedIdleNonWspr)
    {
        if (!(config.use_ini && !runtime_transmit_enabled(config) &&
              (config.mode == ModeType::FSKCW || config.mode == ModeType::QRSS ||
               config.mode == ModeType::DFCW)))
        {
            result.restoration_failed = true;
            result.message = "Inconsistent managed Test Tone restoration state.";
            return result;
        }

        result.stopped = true;
        if (deferred_reload_pending) {
            result.deferred_reload_reconciled = set_config(true) && !ini_reload_pending.load();
        }
        if (managed_reload_tx_inhibited_state()) {
            result.restoration_failed = true;
            result.message = "Test Tone ended; configuration restoration was rejected. Transmit remains inhibited.";
            return result;
        }
        const bool enabled = runtime_transmit_enabled(config);
        if (enabled) result.output_inactive = false;
        result.message = enabled ? "Test Tone ended; updated managed scheduling restored."
                                 : "Test Tone ended; managed mode restored inactive.";
        publish_status("ended", false, result.message, enabled ? "scheduler" : "managed_idle");
        return result;
    }

    if (restoration_owner != TestToneRestorationOwner::DirectToneStartup)
    {
        result.restoration_failed = true;
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
        result.restoration_failed = true;
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
        result.restoration_failed = true;
        result.message = "Test Tone ended; direct tone restoration failed: " + restoration_error;
        publish_status("failed", false, result.message, "failed");
        return result;
    }

    llog.logS(INFO,
              "Transmitting tone, hit Ctrl-C to terminate tone.");
    result.stopped = true;
    result.output_inactive = false;
    result.message = "Test Tone ended; previous CLI direct tone restored (startup requested).";
    publish_status("ended", false, result.message, "direct_tone");
    return result;
}

TestToneStopResult end_test_tone(std::uint64_t expected_generation, TestToneEndPolicy policy)
{
    std::lock_guard transaction(lifecycle_mutex);
    checkpoint("end_locked");
    TestToneStopResult result;
    const bool owned_before = web_test_tone.load();
    try { result = end_test_tone_locked(expected_generation, policy); }
    catch (const std::exception &error) {
        result.restoration_failed = true;
        result.message = std::string("Test Tone cleanup or restoration failed: ") + error.what();
        if (!web_test_tone.load()) {
            result.output_inactive = rollback_failed_test_tone_start(lastMode);
            wtp_pi_end_local(result.output_inactive);
        }
    }
    catch (...) {
        result.restoration_failed = true;
        result.message = "Test Tone cleanup or restoration failed; use End or local recovery.";
        if (!web_test_tone.load()) {
            result.output_inactive = rollback_failed_test_tone_start(lastMode);
            wtp_pi_end_local(result.output_inactive);
        }
    }
    if (result.restoration_failed) {
        result.tone_was_active = owned_before;
        result.stopped = owned_before && !web_test_tone.load();
        if (!wtp_runtime_selected()) result.output_inactive = transmitter_output_inactive_confirmed();
        publish_status(result.output_inactive ? "failed" : "unknown", web_test_tone.load(), result.message, "failed");
    }
    result.lifecycle = current_test_tone_status();
    return result;
}

StopTransmissionResult stop_transmission_by_user_request(bool persist_transmit)
{
    std::lock_guard transaction(lifecycle_mutex);
    checkpoint("stop_locked");
    StopTransmissionResult result;
    const bool was_test_tone = web_test_tone.load();
    test_tone_restoration_owner = TestToneRestorationOwner::Unknown;
    publish_status("unknown", false, "Operator Stop requested; awaiting cleanup.", "not_resumed");
    const bool remote_stopped = wsprrypi::wtp_pi_stop_remote_work();
    bool persist_to_ini = false;
    suppress_cancelled_ws_event_for_user_stop.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(set_config_mtx);

        if (was_test_tone) config.mode = lastMode;
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

    const bool output_inactive = stop_tone_output();
    transmitter_set_callbacks(transmitter_cb);
    transmitter_clear_execution_state_after_stop();
    clear_committed_execution_request();
    deassert_transmit_gpio_outputs(
        &config,
        false,
        "scheduler shutdown");
    release_idle_selector_gpio_reservations();
    wtp_pi_end_local(output_inactive);
    publish_status(output_inactive && remote_stopped ? "ended" : "unknown", false,
        output_inactive && remote_stopped
            ? "Operator Stop completed; previous work will not resume."
            : "Operator Stop requested; output cleanup is unconfirmed. Use local recovery.", "not_resumed");

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
        result.message = !remote_stopped
                             ? "Remote output state is unknown; local output remains inhibited. Use local recovery."
                             : persist_transmit
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

    result.message = !remote_stopped
                         ? "Remote output state is unknown; local output remains inhibited. Use local recovery."
                         : result.transmission_active
                         ? "Active transmission stopped and transmit disabled."
                         : "Transmit disabled; no active transmission was running.";
    return result;
}
