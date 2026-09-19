/**
 * @file scheduling.cpp
 * @brief Orchestration layer for planning and committing transmissions.
 *
 * This file owns planning policy for the current architecture. It is the
 * only layer that decides:
 * - Auto versus RequirePaired WSPR planning.
 * - WSPR versus direct-tone execution mode.
 * - Random WSPR RF offset application.
 * - Per-band selector GPIO preparation.
 * - When a built request is committed to the transmitter.
 *
 * The transmitter only consumes committed `TransmissionRequest`
 * snapshots. The backend only realizes hardware for the backend-neutral
 * execution plan derived from that committed request.
 *
 * This project is is licensed under the MIT License. See LICENSE.md
 * for more information.
 *
 * Copyright © 2023-2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Primary header for this source file
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "chipset_offsets.hpp"
#include "rp1_route_bridge.hpp"
#include "WSPR-Transmitter/src/rp1_gpclk_development_policy.hpp"
#include "WSPR-Transmitter/src/rp1_gpclk_planner.hpp"

// Project headers
#include "band_gpio_selector.hpp"
#include "band_lookup.hpp"
#include "runtime_config_bridge.hpp"
#include "runtime_config_operations.hpp"
#include "frequency_semantics.hpp"
#include "gpio_band_policy.hpp"
#include "gpio_output.hpp"
#include "logging.hpp"
#include "ppm_manager.hpp"
#include "system_clock_frequency_estimate.hpp"
#include "execution_plan_compiler.hpp"
#include "wspr_reference_adapter.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_runtime_bridge.hpp"
#include "version.hpp"
#include "non_wspr_request_builder.hpp"

// Standard library headers
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <mutex>

// System headers

/**
 * @brief Selects and controls the GPIO assigned to the active amateur band.
 *
 * This object is used by the transmission callback path to assert the
 * correct GPIO when transmission begins and to release it when the
 * transmission completes, is skipped, or is canceled.
 */
struct SelectorGPIOReservation
{
    BandGPIOConfig config{};
    std::unique_ptr<GPIOOutput> gpio;
};

BandGPIOSelector bandGPIOSelector;

namespace
{
    std::mutex frequency_estimate_mutex;
    FrequencyEstimateQualifier frequency_estimate_qualifier;
    SystemClockFrequencyEstimate current_frequency_estimate{};
    GpioFrequencyCorrection current_gpio_correction{};
    WsprRuntimeStatusSnapshot::GpioCorrectionProvenance
        current_gpio_candidate_provenance{};
    WsprRuntimeStatusSnapshot::GpioCorrectionProvenance
        committed_gpio_correction_provenance{};
    std::uint64_t committed_gpio_execution_sequence = 0;

    const char *legacy_processor_name(
        wsprrypi::LegacyGpioProcessorProfile profile) noexcept
    {
        switch (profile)
        {
        case wsprrypi::LegacyGpioProcessorProfile::Bcm2835: return "BCM2835";
        case wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837:
            return "BCM2836/BCM2837";
        case wsprrypi::LegacyGpioProcessorProfile::Bcm2711: return "BCM2711";
        }
        return "unavailable";
    }

    std::string utc_timestamp(std::chrono::system_clock::time_point value)
    {
        if (value.time_since_epoch().count() == 0)
            return {};
        const std::time_t raw = std::chrono::system_clock::to_time_t(value);
        std::tm utc{};
        gmtime_r(&raw, &utc);
        std::ostringstream out;
        out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
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

    SystemClockFrequencyEstimate qualify_provider_snapshot(
        const PPMProviderSnapshot &provider)
    {
        SystemClockFrequencyEstimate sample;
        sample.provider_name = provider.provider_name;
        sample.frequency_ppm = provider.frequency_ppm;
        sample.synchronized = provider.synchronized;
        sample.age_seconds = provider.age_seconds;
        sample.residual_frequency_ppm = provider.residual_frequency_ppm;
        sample.skew_ppm = provider.skew_ppm;
        sample.selected_source = provider.selected_source;
        sample.combined_sources = provider.combined_sources;
        sample.leap_normal = provider.leap_normal;
        sample.source_provenance = provider.source_provenance;
        sample.source_signature = provider.source_signature;
        if (std::isfinite(provider.age_seconds) && provider.age_seconds >= 0.0)
        {
            sample.snapshot_time = std::chrono::system_clock::now() -
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::duration<double>(provider.age_seconds));
        }
        sample.retained_source_samples = provider.retained_source_samples;
        sample.source_stability_span_seconds = provider.source_stability_span_seconds;
        sample.reason = provider.error_reason;
        return frequency_estimate_qualifier.evaluate(std::move(sample));
    }

    GpioFrequencyCorrection select_and_publish_gpio_correction(
        const ArgParserConfig &cfg)
    {
        std::lock_guard<std::mutex> lock(frequency_estimate_mutex);
        GpioFrequencyCorrection selected = select_gpio_frequency_correction(
            cfg.use_system_clock_frequency_estimate,
            cfg.gpio_frequency_residual_ppm,
            cfg.gpio_manual_ppm,
            current_frequency_estimate);
        if (selected.valid && transmit_backend_uses_gpio_output(
                cfg.transmit_backend))
        {
            double intrinsic_ppm =
                wsprrypi::chipsetIntrinsicOffsetPpm(wsprrypi::ClockChipset::Rp1);
            if (cfg.transmit_backend == TransmitBackendKind::GPIO)
            {
                const auto processor = resolve_legacy_gpio_processor_profile();
                if (!processor.has_value())
                {
                    selected.valid = false;
                    selected.reason =
                        "Unable to resolve an exact legacy GPIO processor profile.";
                    current_gpio_correction = selected;
                    current_gpio_candidate_provenance = {};
                    return current_gpio_correction;
                }
                const auto model = wsprrypi::legacyGpioClockModel(
                    *processor,
                    wsprrypi::LegacyGpioClockParent::PllD);
                intrinsic_ppm = model.intrinsic_system_to_rf_difference_ppm;
            }
            const auto composition = wsprrypi::composeLegacyGpioCorrection(
                intrinsic_ppm, selected.effective_ppm);
            if (!composition.valid)
            {
                selected.valid = false;
                selected.reason =
                    "Intrinsic plus additional GPIO correction is invalid.";
            }
            else
            {
                selected.intrinsic_ppm = composition.intrinsic_ppm;
                selected.additional_ppm = composition.additional_ppm;
                selected.effective_ppm = composition.effective_ppm;
            }
        }
        else if (selected.valid)
        {
            selected.additional_ppm = selected.effective_ppm;
        }
        current_gpio_correction = selected;
        current_gpio_candidate_provenance = {};
        if (selected.valid && transmit_backend_uses_gpio_output(
                cfg.transmit_backend))
        {
            auto &candidate = current_gpio_candidate_provenance;
            candidate.available = true;
            candidate.intrinsic_ppm = selected.intrinsic_ppm;
            candidate.selected_component_ppm =
                selected.estimate_ppm.value_or(selected.additional_ppm);
            candidate.conducted_residual_ppm = selected.residual_ppm;
            candidate.final_ppm = selected.effective_ppm;
            candidate.additional_ppm = selected.additional_ppm;
            candidate.correction_mode = to_string(selected.mode);
            candidate.provider_name = selected.provider_name;
            candidate.provider_source_signature = selected.source_signature;
            candidate.provider_snapshot_time = utc_timestamp(selected.snapshot_time);
            if (cfg.transmit_backend == TransmitBackendKind::RP1_GPCLK)
            {
                candidate.processor_profile = "RP1";
                candidate.selected_parent = "PLL_SYS";
                candidate.nominal_rate_hz =
                    wsprrypi::kRp1GpclkNominalParentFrequencyHz;
            }
            else
            {
                const auto processor = resolve_legacy_gpio_processor_profile();
                candidate.processor_profile = legacy_processor_name(*processor);
                candidate.selected_parent = "pending execution plan";
            }
        }
        return current_gpio_correction;
    }

    void refresh_frequency_estimate()
    {
        const PPMProviderSnapshot provider = ppmManager.getProviderSnapshot();
        std::lock_guard<std::mutex> lock(frequency_estimate_mutex);
        current_frequency_estimate = qualify_provider_snapshot(provider);
    }

    std::string get_active_gpio_suffix()
    {
        const BandGPIOConfig *cfg = bandGPIOSelector.currentConfig();

        if (cfg == nullptr || !cfg->enabled || cfg->gpio < 0)
        {
            return "";
        }

        return " (GPIO" +
               std::to_string(cfg->gpio) +
               (cfg->active_high ? "H)" : "L)");
    }
}

std::string active_gpio_log_suffix()
{
    return get_active_gpio_suffix();
}

void refresh_frequency_estimate_for_config()
{
    refresh_frequency_estimate();
}

GpioFrequencyCorrection select_and_publish_gpio_correction_for_config(
    const ArgParserConfig &cfg)
{
    return select_and_publish_gpio_correction(cfg);
}

BandGPIOPrepareStatus prepare_band_gpio_for_frequency_or_log(
    double source_frequency_hz,
    const WsprFrequencyEntry &entry,
    const ArgParserConfig &cfg,
    int frequency_entry_index,
    BandGPIOResolution *resolution_out);
bool sync_configured_selector_gpio_idle_state(
    const ArgParserConfig &cfg,
    bool keep_initialized,
    std::string *error_message);
static BandGPIOPrepareStatus apply_band_gpio_resolution(
    const BandGPIOResolution &resolution) noexcept;
void commit_band_gpio_snapshot_to_request(
    TransmissionRequest &request,
    const BandGPIOResolution &resolution,
    BandGPIOPrepareStatus prepare_status) noexcept;
static void commit_execution_request(
    const wsprrypi::TransmissionRequest &controller_request,
    const TransmissionRequest &legacy_request);
void clear_committed_execution_request() noexcept;
static bool refresh_committed_band_gpio_selection() noexcept;
void assert_transmit_gpio_outputs(const char *context) noexcept;
void deassert_transmit_gpio_outputs(
    const ArgParserConfig *selector_config,
    bool keep_selector_gpio_initialized,
    const char *context) noexcept;
bool runtime_transmit_enabled(const ArgParserConfig &cfg) noexcept;
bool runtime_transmit_requested(const ArgParserConfig &cfg) noexcept;
bool runtime_transmit_preparation_enabled(
    const ArgParserConfig &cfg) noexcept;
static void log_startup_quiesce_inhibited_skip();
static bool apply_direct_rp1_development_confirmation(
    const ArgParserConfig &cfg,
    TransmissionRequest &request,
    std::string *error_message);

/**
 * @brief Mutex to protect access to the shutdown flag for the WSPR loop.
 *
 * This mutex must be locked before reading or writing \c exitwspr_ready
 * to ensure thread-safe coordination between the signal handler callback
 * and the WSPR loop.
 */
std::mutex exitwspr_mtx;

/**
 * @brief Condition variable used to signal the WSPR loop to exit.
 *
 * The signal handler callback will notify this condition variable after
 * setting \c exitwspr_ready to \c true, causing the waiting WSPR loop
 * to wake up and perform shutdown.
 */
std::condition_variable exitwspr_cv;

/**
 * @brief Atomic bool used to signal other functions that we are shutting down.
 */
std::atomic<bool> exiting_wspr = false;
std::mutex set_config_mtx;

/**
 * @brief Flag indicating whether the WSPR loop should terminate.
 *
 * Set to \c true by the signal handler callback under protection of
 * \c exitwspr_mtx, then \c exitwspr_cv is notified so that the WSPR
 * loop can break out of its wait and begin shutdown.
 */
bool exitwspr_ready = false;

/**
 * @brief Round‐robin index into the configured WSPR dial-frequency list.
 *
 * Tracks which entry in the `config.wspr_dial_freq_set` vector will be
 * used for the next WSPR transmission.  Wraps via modulo on each use.
 */
int freq_iterator = 0;

/**
 * @brief Currently active WSPR dial frequency (in Hz).
 *
 * Holds the last dial frequency selected by the scheduler.
 * A zero value indicates that no frequency is configured or the list was empty.
 */
double current_dial_frequency = 0.0;
WsprFrequencyEntry current_frequency_entry{};
TransmissionRequest current_transmission_request{};
static std::optional<wsprrypi::TransmissionRequest>
    current_controller_request_for_test_storage{};
static CommittedExecutionRouteForTest
    committed_execution_route_for_test_storage =
        CommittedExecutionRouteForTest::NONE;
static std::atomic<std::size_t> tx_led_assert_request_count_for_test_storage{0U};
static std::atomic<std::size_t> tx_led_deassert_request_count_for_test_storage{0U};
static std::atomic<std::size_t> tx_led_failure_count_for_test_storage{0U};
static std::mutex tx_led_state_mtx;
static std::condition_variable tx_led_state_cv;
static bool tx_led_active = false;
static std::mutex transmit_gpio_lifecycle_mtx;
static std::atomic<bool> startup_quiesce_inhibited{false};
static std::atomic<bool> rp1_route_transaction_inhibited{false};
static std::mutex startup_quiesce_error_mtx;
static std::string startup_quiesce_error;
static StartupQuiesceInvokerForTest startup_quiesce_invoker_for_test{};

/**
 * @brief File-scope self-pipe descriptors for signal notifications.
 *
 * @details Declared `extern` here so that any TU (like scheduling.cpp)
 *          can refer to the same pipe ends.  The *definition* (no `extern`)
 *          remains in exactly one .cpp (main.cpp).
 */
extern int sig_pipe_fds[2];

/**
 * @brief Global mutex for coordinating shutdown and thread safety.
 *
 * Used to protect shared data during shutdown, ensuring only one thread
 * initiates and executes the shutdown procedure.
 */
std::mutex shutdown_mtx;

/**
 * @brief Flag indicating if a system reboot is in progress.
 *
 * @details
 * This atomic flag is used throughout the application to signal when a
 * full system reboot has been initiated. It is typically set from one
 * of the control points (REST or websockets).
 *
 * Other threads can poll or wait on this flag to terminate safely.
 */
std::atomic<bool> reboot_flag{false};

/**
 * @brief Atomic flag indicating that a shutdown sequence has begun.
 *
 * Set by GPIO or system-triggered shutdown paths to initiate coordinated
 * shutdown across all subsystems.
 */
std::atomic<bool> shutdown_flag{false};

/**
 * @brief Stores the previous transmission mode.
 *
 * This variable saves the last value of config.mode before entering
 * test tone mode so that the original mode can be restored later.
 */
ModeType lastMode;

TestToneRestorationOwner test_tone_restoration_owner =
    TestToneRestorationOwner::Unknown;

/**
 * @brief Flag indicating if a web-triggered test tone is active.
 *
 * An atomic bool that is true while a test tone transmission is in
 * progress via web controls, and false otherwise.
 */
std::atomic<bool> web_test_tone{false};
std::atomic<bool> shutdown_after_current_transmission{false};
std::atomic<bool> shutdown_after_wspr_plan{false};
static bool managed_reload_tx_inhibited = false;
bool suppress_scheduler_execution_for_test = false;
static TestToneCommitInvokerForTest test_tone_commit_invoker_for_test{};
static DirectToneStartInvokerForTest direct_tone_start_invoker_for_test{};
static std::mutex direct_tone_confirmation_mtx;
static std::optional<std::string> claimed_direct_tone_confirmation;
std::atomic<std::uint64_t> non_wspr_schedule_generation{0};

std::uint64_t non_wspr_schedule_generation_for_test() noexcept
{
    return non_wspr_schedule_generation.load(std::memory_order_acquire);
}

static std::atomic<BandGPIOPrepareStatus> active_band_gpio_prepare_status{
    BandGPIOPrepareStatus::Inactive};
std::atomic<bool> suppress_cancelled_ws_event_for_user_stop{false};
static std::vector<SelectorGPIOReservation> idle_selector_gpio_reservations{};
bool selector_gpio_control_enabled = false;
bool selector_gpio_drive_enabled = false;
static std::vector<BandGPIOConfig> last_selector_shutdown_cleanup_targets{};
static std::atomic<std::size_t> band_gpio_prepare_call_counter_for_test{0U};
/**
 * @brief Scheduler-owned paired WSPR plan being continued across slots.
 *
 * When a paired plan is selected, the scheduler saves the full prepared
 * plan and the frequency entry that produced it. The second slot reuses
 * this saved scheduler state instead of asking the planner for a new
 * policy decision.
 */
PreparedWsprTransmission active_wspr_plan{};
std::size_t active_wspr_frame_index = 0;
double active_wspr_plan_dial_frequency = 0.0;
WsprFrequencyEntry active_wspr_plan_frequency_entry{};
bool active_wspr_plan_in_progress = false;

/**
 * @brief Tear down the selector prepared for the active committed request.
 *
 * This is the single teardown path for scheduler-owned band-selection GPIO
 * state. Any code that needs to release the active selector must call this
 * helper rather than stopping the selector directly.
 */
static bool selector_gpio_config_matches(
    const BandGPIOConfig &lhs,
    const BandGPIOConfig &rhs) noexcept
{
    return lhs.enabled == rhs.enabled &&
           lhs.gpio == rhs.gpio &&
           lhs.active_high == rhs.active_high;
}

static bool configured_selector_gpio_contains(
    const ArgParserConfig &cfg,
    const BandGPIOConfig &target) noexcept
{
    if (!target.enabled || target.gpio < 0)
    {
        return false;
    }

    for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
    {
        if (selector_gpio_config_matches(cfg.band_gpio[band_index], target))
        {
            return true;
        }
    }

    for (const WsprFrequencyEntry &entry : cfg.wspr_frequency_entries)
    {
        if (entry.selector_gpio == kSelectorGpioUnset)
        {
            continue;
        }

        BandGPIOConfig config;
        config.gpio = entry.selector_gpio;
        config.enabled = true;
        config.active_high = entry.selector_gpio_active_high;
        if (selector_gpio_config_matches(config, target))
        {
            return true;
        }
    }

    return false;
}

bool runtime_should_hold_selector_gpios_initialized(
    const ArgParserConfig &cfg) noexcept
{
    return selector_gpio_control_enabled &&
           cfg.transmit &&
           runtime_transmit_enabled(cfg);
}

static std::unique_ptr<GPIOOutput> take_idle_selector_gpio_reservation(
    int gpio,
    BandGPIOConfig *config_out = nullptr) noexcept
{
    const auto it = std::find_if(
        idle_selector_gpio_reservations.begin(),
        idle_selector_gpio_reservations.end(),
        [gpio](const SelectorGPIOReservation &reservation)
        {
            return reservation.config.gpio == gpio;
        });
    if (it == idle_selector_gpio_reservations.end())
    {
        return nullptr;
    }

    if (config_out != nullptr)
    {
        *config_out = it->config;
    }

    std::unique_ptr<GPIOOutput> gpio_handle = std::move(it->gpio);
    idle_selector_gpio_reservations.erase(it);
    return gpio_handle;
}

static bool append_idle_selector_gpio_reservation(
    const BandGPIOConfig &selector_config,
    std::unique_ptr<GPIOOutput> gpio_handle,
    std::string *error_message = nullptr)
{
    SelectorGPIOReservation reservation;
    reservation.config = selector_config;
    reservation.gpio = std::move(gpio_handle);

    if (selector_gpio_drive_enabled)
    {
        if (reservation.gpio == nullptr)
        {
            reservation.gpio = std::make_unique<GPIOOutput>();
            if (!reservation.gpio->enableGPIOPin(
                    selector_config.gpio,
                    selector_config.active_high))
            {
                if (error_message != nullptr)
                {
                    *error_message =
                        "Unable to initialize LPF selector GPIO " +
                        std::to_string(selector_config.gpio) +
                        " inactive: " +
                        reservation.gpio->lastError();
                }
                return false;
            }
        }

        if (!reservation.gpio->toggleGPIO(false))
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Unable to drive LPF selector GPIO " +
                    std::to_string(selector_config.gpio) +
                    " inactive.";
            }
            return false;
        }
    }

    llog.logS(
        DEBUG,
        "[BandGPIO]",
        "Idle selector pool now holds GPIO ",
        selector_config.gpio,
        " inactive with polarity ",
        (selector_config.active_high ? "active high" : "active low"),
        ".");

    idle_selector_gpio_reservations.push_back(std::move(reservation));
    return true;
}

bool stop_active_transmission_selectors(
    const ArgParserConfig *runtime_cfg,
    bool keep_initialized,
    std::string *error_message) noexcept
{
    const BandGPIOConfig *active_config_ptr = bandGPIOSelector.currentConfig();
    if (active_config_ptr == nullptr)
    {
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Inactive,
            std::memory_order_release);
        return true;
    }

    const BandGPIOConfig active_config = *active_config_ptr;
    if (!bandGPIOSelector.setBandState(false))
    {
        llog.logS(WARN,
                  "Band GPIO deassert request did not complete during selector teardown.");
    }

    std::unique_ptr<GPIOOutput> active_gpio =
        bandGPIOSelector.releaseGPIOReservation();
    active_band_gpio_prepare_status.store(
        BandGPIOPrepareStatus::Inactive,
        std::memory_order_release);

    const bool keep_active_gpio_inactive =
        keep_initialized &&
        runtime_cfg != nullptr &&
        configured_selector_gpio_contains(*runtime_cfg, active_config);
    if (!keep_active_gpio_inactive)
    {
        return true;
    }

    return append_idle_selector_gpio_reservation(
        active_config,
        std::move(active_gpio),
        error_message);
}

void release_idle_selector_gpio_reservations() noexcept
{
    idle_selector_gpio_reservations.clear();
}

static void append_unique_selector_gpio_config(
    std::vector<BandGPIOConfig> &configs,
    const BandGPIOConfig &config) noexcept
{
    if (!config.enabled || config.gpio < 0)
    {
        return;
    }

    const auto existing = std::find_if(
        configs.begin(),
        configs.end(),
        [&config](const BandGPIOConfig &candidate)
        {
            return candidate.gpio == config.gpio;
        });
    if (existing == configs.end())
    {
        configs.push_back(config);
        return;
    }

    if (existing->active_high != config.active_high)
    {
        llog.logS(
            WARN,
            "Selector shutdown cleanup saw conflicting polarity for GPIO ",
            config.gpio,
            "; keeping the first configured polarity.");
    }
}

static std::vector<BandGPIOConfig> collect_selector_gpio_shutdown_targets(
    const ArgParserConfig &cfg) noexcept
{
    std::vector<BandGPIOConfig> targets;

    for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
    {
        append_unique_selector_gpio_config(targets, cfg.band_gpio[band_index]);
    }

    for (const WsprFrequencyEntry &entry : cfg.wspr_frequency_entries)
    {
        if (entry.selector_gpio == kSelectorGpioUnset)
        {
            continue;
        }

        BandGPIOConfig selector_config;
        selector_config.gpio = entry.selector_gpio;
        selector_config.enabled = true;
        selector_config.active_high = entry.selector_gpio_active_high;
        append_unique_selector_gpio_config(targets, selector_config);
    }

    const BandGPIOConfig *active_config = bandGPIOSelector.currentConfig();
    if (active_config != nullptr)
    {
        append_unique_selector_gpio_config(targets, *active_config);
    }

    for (const SelectorGPIOReservation &reservation : idle_selector_gpio_reservations)
    {
        append_unique_selector_gpio_config(targets, reservation.config);
    }

    return targets;
}

void shutdown_all_configured_selector_gpios(
    const ArgParserConfig &cfg) noexcept
{
    std::vector<BandGPIOConfig> targets =
        collect_selector_gpio_shutdown_targets(cfg);
    last_selector_shutdown_cleanup_targets = targets;

    stop_active_transmission_selectors();
    release_idle_selector_gpio_reservations();

    if (!selector_gpio_drive_enabled)
    {
        return;
    }

    for (const BandGPIOConfig &selector_config : targets)
    {
        GPIOOutput gpio;
        if (!gpio.enableGPIOPin(
                selector_config.gpio,
                selector_config.active_high))
        {
            llog.logS(
                WARN,
                "Selector shutdown cleanup could not request GPIO ",
                selector_config.gpio,
                ": ",
                gpio.lastError());
            continue;
        }

        if (!gpio.toggleGPIO(false))
        {
            llog.logS(
                WARN,
                "Selector shutdown cleanup could not drive GPIO ",
                selector_config.gpio,
                " inactive before release.");
        }

        gpio.stop();
    }
}

static bool collect_configured_selector_gpios(
    const ArgParserConfig &cfg,
    std::vector<BandGPIOConfig> &configs_out,
    std::string *error_message = nullptr)
{
    std::unordered_map<int, bool> polarity_by_gpio;

    auto append_config = [&](const BandGPIOConfig &config,
                             std::string_view source_label) -> bool
    {
        if (!config.enabled || config.gpio < 0)
        {
            return true;
        }

        const auto existing = polarity_by_gpio.find(config.gpio);
        if (existing != polarity_by_gpio.end())
        {
            if (existing->second != config.active_high)
            {
                if (error_message != nullptr)
                {
                    *error_message =
                        "Conflicting LPF selector polarity configured for GPIO " +
                        std::to_string(config.gpio) +
                        " while collecting scheduler selector GPIOs from " +
                        std::string(source_label) + ".";
                }
                return false;
            }
            return true;
        }

        polarity_by_gpio.emplace(config.gpio, config.active_high);
        configs_out.push_back(config);
        return true;
    };

    configs_out.clear();

    for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
    {
        if (!append_config(cfg.band_gpio[band_index], "[Band GPIO]"))
        {
            return false;
        }
    }

    for (const WsprFrequencyEntry &entry : cfg.wspr_frequency_entries)
    {
        if (entry.selector_gpio == kSelectorGpioUnset)
        {
            continue;
        }

        BandGPIOConfig config;
        config.gpio = entry.selector_gpio;
        config.enabled = true;
        config.active_high = entry.selector_gpio_active_high;
        if (!append_config(config, "frequency entries"))
        {
            return false;
        }
    }

    return true;
}

bool has_configured_selector_gpios(const ArgParserConfig &cfg) noexcept
{
    for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
    {
        const BandGPIOConfig &config = cfg.band_gpio[band_index];
        if (config.enabled && config.gpio >= 0)
        {
            return true;
        }
    }

    for (const WsprFrequencyEntry &entry : cfg.wspr_frequency_entries)
    {
        if (entry.selector_gpio != kSelectorGpioUnset)
        {
            return true;
        }
    }

    return false;
}

bool sync_configured_selector_gpio_idle_state(
    const ArgParserConfig &cfg,
    bool keep_initialized,
    std::string *error_message)
{
    if (!stop_active_transmission_selectors(&cfg, keep_initialized, error_message))
    {
        return false;
    }

    if (!keep_initialized || !selector_gpio_control_enabled)
    {
        release_idle_selector_gpio_reservations();
        return true;
    }

    std::vector<BandGPIOConfig> selector_configs;
    if (!collect_configured_selector_gpios(cfg, selector_configs, error_message))
    {
        return false;
    }

    std::vector<SelectorGPIOReservation> previous_reservations =
        std::move(idle_selector_gpio_reservations);
    idle_selector_gpio_reservations.clear();

    for (const BandGPIOConfig &selector_config : selector_configs)
    {
        std::unique_ptr<GPIOOutput> gpio_handle;
        const auto existing = std::find_if(
            previous_reservations.begin(),
            previous_reservations.end(),
            [&selector_config](const SelectorGPIOReservation &reservation)
            {
                return reservation.config.gpio == selector_config.gpio;
            });
        if (existing != previous_reservations.end())
        {
            gpio_handle = std::move(existing->gpio);
            previous_reservations.erase(existing);
        }

        if (!append_idle_selector_gpio_reservation(
                selector_config,
                std::move(gpio_handle),
                error_message))
        {
            release_idle_selector_gpio_reservations();
            return false;
        }
    }

    return true;
}

static BandGPIOPrepareStatus apply_band_gpio_resolution(
    const BandGPIOResolution &resolution) noexcept
{
    if (suppress_scheduler_execution_for_test)
    {
        selector_gpio_drive_enabled = GPIOOutput::testModeEnabled();
    }
    bandGPIOSelector.setEnabled(selector_gpio_control_enabled);
    bandGPIOSelector.setDriveGPIO(selector_gpio_drive_enabled);

    if (!resolution.selector_enabled)
    {
        stop_active_transmission_selectors();
        return BandGPIOPrepareStatus::Inactive;
    }

    const BandGPIOConfig *current_config = bandGPIOSelector.currentConfig();
    const HamBand *current_band = bandGPIOSelector.currentBand();
    if (current_config != nullptr &&
        current_band != nullptr &&
        *current_band == resolution.band &&
        selector_gpio_config_matches(*current_config, resolution.config))
    {
        if (bandGPIOSelector.isBandStateActive() &&
            !bandGPIOSelector.setBandState(false))
        {
            active_band_gpio_prepare_status.store(
                BandGPIOPrepareStatus::Failed,
                std::memory_order_release);
            return BandGPIOPrepareStatus::Failed;
        }

        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Prepared,
            std::memory_order_release);
        return BandGPIOPrepareStatus::Prepared;
    }

    if (current_config != nullptr)
    {
        const bool keep_initialized =
            runtime_should_hold_selector_gpios_initialized(config);
        if (!stop_active_transmission_selectors(&config, keep_initialized))
        {
            active_band_gpio_prepare_status.store(
                BandGPIOPrepareStatus::Failed,
                std::memory_order_release);
            return BandGPIOPrepareStatus::Failed;
        }
    }

    std::unique_ptr<GPIOOutput> reserved_gpio =
        take_idle_selector_gpio_reservation(resolution.config.gpio);

    if (!bandGPIOSelector.prepareBand(
            resolution.band,
            resolution.config,
            std::move(reserved_gpio)))
    {
        llog.logS(
            WARN,
            "Unable to prepare unified scheduler band GPIO for band ",
            ham_band_to_string(resolution.band),
            ", GPIO ",
            resolution.config.gpio,
            ".");
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Failed,
            std::memory_order_release);
        return BandGPIOPrepareStatus::Failed;
    }

    active_band_gpio_prepare_status.store(
        BandGPIOPrepareStatus::Prepared,
        std::memory_order_release);
    return BandGPIOPrepareStatus::Prepared;
}

static bool refresh_committed_band_gpio_selection() noexcept
{
    if (!current_transmission_request.hasSelectorGPIO())
    {
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Inactive,
            std::memory_order_release);
        return true;
    }
    return apply_band_gpio_resolution(BandGPIOResolution{
               current_transmission_request.selector_band,
               current_transmission_request.selector_gpio_config,
               true,
               false,
               "committed request"}) == BandGPIOPrepareStatus::Prepared;
}

static bool tx_led_configured(const ArgParserConfig &cfg) noexcept
{
    return cfg.use_led && cfg.led_pin >= 0 && cfg.led_pin <= 27;
}

static bool should_control_tx_led() noexcept
{
    return tx_led_configured(config);
}

static bool amp_gpio_configured(const ArgParserConfig &cfg) noexcept
{
    return cfg.use_amp && cfg.amp_pin >= 0 && cfg.amp_pin <= 27;
}

static bool should_control_amp_gpio() noexcept
{
    return amp_gpio_configured(config);
}

static void mark_tx_led_active_state(bool active) noexcept
{
    {
        std::lock_guard<std::mutex> lk(tx_led_state_mtx);
        tx_led_active = active;
    }
    tx_led_state_cv.notify_all();
}

void set_tx_led_state(bool state, const char *context) noexcept
{
    if (!should_control_tx_led())
    {
        return;
    }

    if (state)
    {
        tx_led_assert_request_count_for_test_storage.fetch_add(
            1U,
            std::memory_order_relaxed);
    }
    else
    {
        tx_led_deassert_request_count_for_test_storage.fetch_add(
            1U,
            std::memory_order_relaxed);
    }

    if (!ledControl.toggleGPIO(state))
    {
        tx_led_failure_count_for_test_storage.fetch_add(
            1U,
            std::memory_order_relaxed);
        llog.logS(WARN,
                  "TX LED ",
                  (state ? "assert" : "deassert"),
                  " request did not complete during ",
                  context,
                  ".");
        return;
    }

    mark_tx_led_active_state(state);
}

static void set_amp_gpio_state(bool state, const char *context) noexcept
{
    if (!should_control_amp_gpio())
    {
        return;
    }

    if (!ampControl.toggleGPIO(state))
    {
        llog.logS(WARN,
                  "Amp Control ",
                  (state ? "assert" : "deassert"),
                  " request did not complete during ",
                  context,
                  ".");
    }
}

void assert_transmit_gpio_outputs(const char *context) noexcept
{
    std::lock_guard<std::mutex> lk(transmit_gpio_lifecycle_mtx);

    if (!refresh_committed_band_gpio_selection())
    {
        llog.logS(DEBUG,
                  "Band GPIO refresh from committed request did not complete.");
    }

    if (current_transmission_request.hasSelectorGPIO() &&
        !bandGPIOSelector.setBandState(true))
    {
        llog.logS(DEBUG,
                  "Band GPIO assert request was issued but did not complete.");
    }

    set_amp_gpio_state(true, context);
    set_tx_led_state(true, context);
}

void deassert_transmit_gpio_outputs(
    const ArgParserConfig *selector_config,
    bool keep_selector_gpio_initialized,
    const char *context) noexcept
{
    std::lock_guard<std::mutex> lk(transmit_gpio_lifecycle_mtx);

    set_amp_gpio_state(false, context);
    set_tx_led_state(false, context);
    stop_active_transmission_selectors(
        selector_config,
        keep_selector_gpio_initialized);
}

static bool reconcile_transmit_gpio_after_transmitter_stop(const char *context) noexcept
{
    bool fallback_used = false;

    if (should_control_amp_gpio())
    {
        std::lock_guard<std::mutex> lifecycle_lk(transmit_gpio_lifecycle_mtx);
        set_amp_gpio_state(false, context);
        fallback_used = true;
    }

    if (!should_control_tx_led())
    {
        return fallback_used;
    }

    {
        std::unique_lock<std::mutex> lk(tx_led_state_mtx);
        tx_led_state_cv.wait_for(
            lk,
            std::chrono::milliseconds(100),
            []
            {
                return !tx_led_active;
            });

        if (!tx_led_active)
        {
            return fallback_used;
        }
    }

    llog.logS(
        DEBUG,
        "TX LED remained asserted after transmitter stop; applying shutdown fallback during ",
        context,
        ".");
    set_tx_led_state(false, context);
    fallback_used = true;
    return fallback_used;
}

bool reconcile_tx_led_after_transmitter_stop(const char *context) noexcept
{
    if (!should_control_tx_led())
    {
        return false;
    }

    bool was_active = false;
    {
        std::lock_guard<std::mutex> lk(tx_led_state_mtx);
        was_active = tx_led_active;
    }
    (void)reconcile_transmit_gpio_after_transmitter_stop(context);
    if (!was_active)
    {
        return false;
    }
    return true;
}

static wsprrypi::BackendKind to_controller_backend(
    TransmitBackendKind backend) noexcept;

static wsprrypi::ClockSource to_controller_clock_source(
    const ArgParserConfig &cfg) noexcept;

static wsprrypi::TransmissionRequest build_controller_request_from_legacy(
    const TransmissionRequest &legacy_request,
    wsprrypi::TransmissionMode mode)
{
    wsprrypi::TransmissionRequest controller_request;
    controller_request.mode = mode;
    controller_request.output.backend =
        to_controller_backend(config.transmit_backend);
    controller_request.output.output = to_controller_clock_source(config);
    controller_request.output.gpio = legacy_request.tx_gpio;
    controller_request.calibration.ppm = legacy_request.ppm;
    controller_request.policy.allow_unqualified_frequency =
        legacy_request.allow_unqualified_frequency;
    controller_request.policy.allow_non_amateur_frequency =
        legacy_request.allow_non_amateur_frequency;
    controller_request.policy.hardware_profile = legacy_request.hardware_profile;
    controller_request.id.value = 1;
    controller_request.metadata.label = legacy_request.frequency_entry_label;
    controller_request.metadata.origin = "scheduler";
    return controller_request;
}

static void freeze_gpio_correction_provenance(
    TransmissionRequest &request,
    std::optional<std::pair<double, double>> exact_frequency_range = std::nullopt)
{
    committed_gpio_correction_provenance = {};
    if (!transmit_backend_uses_gpio_output(config.transmit_backend) ||
        request.isSkipWindow() || request.actual_rf_frequency_hz <= 0.0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(frequency_estimate_mutex);
    if (!current_gpio_candidate_provenance.available ||
        !current_gpio_correction.valid)
    {
        return;
    }
    if (config.transmit_backend == TransmitBackendKind::RP1_GPCLK)
    {
        if (request.hardware_profile != wsprrypi::HardwareProfile::RP1_GPCLK)
            return;

        // The backend owns RF application; freeze only the additional value
        // into its input, while reporting the complete correction separately.
        request.ppm = current_gpio_correction.additional_ppm;
        auto frozen = current_gpio_candidate_provenance;
        frozen.active = false;
        frozen.final_ppm = current_gpio_correction.effective_ppm;
        frozen.execution_identity =
            "gpio-execution-" +
            std::to_string(++committed_gpio_execution_sequence);
        committed_gpio_correction_provenance = frozen;
        current_gpio_candidate_provenance.final_ppm = frozen.final_ppm;
        return;
    }

    const auto processor = resolve_legacy_gpio_processor_profile();
    if (!processor.has_value() ||
        !wsprrypi::legacyHardwareProfileMatches(
            request.hardware_profile, *processor))
        return;

    double minimum_hz = exact_frequency_range.has_value()
        ? exact_frequency_range->first
        : request.actual_rf_frequency_hz;
    double maximum_hz = exact_frequency_range.has_value()
        ? exact_frequency_range->second
        : request.actual_rf_frequency_hz;
    if (!exact_frequency_range.has_value() && !request.payload.empty())
        maximum_hz += 1.5 * (12000.0 / 8192.0);
    if (!exact_frequency_range.has_value() && request.applied_offset_hz != 0.0)
    {
        minimum_hz = std::min(minimum_hz,
            request.actual_rf_frequency_hz + request.applied_offset_hz);
        maximum_hz = std::max(maximum_hz,
            request.actual_rf_frequency_hz + request.applied_offset_hz);
    }

    const auto selection =
        wsprrypi::selectLegacyGpioClockForAdditionalCorrection(
            *processor,
            minimum_hz,
            maximum_hz,
            current_gpio_correction.additional_ppm);
    request.ppm = selection.correction.additional_ppm;
    current_gpio_correction.intrinsic_ppm =
        selection.correction.intrinsic_ppm;
    current_gpio_correction.effective_ppm =
        selection.correction.effective_ppm;
    auto frozen = current_gpio_candidate_provenance;
    frozen.active = false;
    frozen.selected_parent =
        selection.model.parent == wsprrypi::LegacyGpioClockParent::PllD
            ? "PLLD"
            : "oscillator";
    frozen.nominal_rate_hz = selection.model.nominal_rate_hz;
    frozen.intrinsic_ppm = selection.correction.intrinsic_ppm;
    frozen.selected_component_ppm =
        current_gpio_correction.estimate_ppm.value_or(
            selection.correction.additional_ppm);
    frozen.final_ppm = selection.correction.effective_ppm;
    frozen.additional_ppm = selection.correction.additional_ppm;
    frozen.execution_identity =
        "gpio-execution-" + std::to_string(++committed_gpio_execution_sequence);
    committed_gpio_correction_provenance = frozen;
    current_gpio_candidate_provenance.selected_parent = frozen.selected_parent;
    current_gpio_candidate_provenance.nominal_rate_hz = frozen.nominal_rate_hz;
    current_gpio_candidate_provenance.intrinsic_ppm = frozen.intrinsic_ppm;
    current_gpio_candidate_provenance.final_ppm = frozen.final_ppm;
}

/**
 * @brief Commit the single execution request consumed by the transmitter.
 *
 * This is the execution boundary between orchestration and transmitter
 * layers. All WSPR and tone execution must pass through this helper so the
 * transmitter only ever sees a complete, scheduler-owned request snapshot.
 *
 * @param request Fully built execution request for one transmitter run.
 */
void commit_execution_request(
    const TransmissionRequest &request)
{
    current_transmission_request = request;
    freeze_gpio_correction_provenance(current_transmission_request);
    current_controller_request_for_test_storage.reset();
    committed_execution_route_for_test_storage =
        CommittedExecutionRouteForTest::NONE;

    if (current_transmission_request.isTone() &&
        (config.transmit_backend == TransmitBackendKind::SI5351 ||
         config.transmit_backend == TransmitBackendKind::RP1_GPCLK ||
         config.transmit_backend == TransmitBackendKind::WTP))
    {
        wsprrypi::TransmissionRequest controller_request =
            build_controller_request_from_legacy(
                current_transmission_request,
                wsprrypi::TransmissionMode::TONE);
        wsprrypi::TonePayload payload;
        payload.frequency_hz =
            current_transmission_request.actual_rf_frequency_hz;
        payload.duration = current_transmission_request.tone_duration;
        controller_request.payload = payload;
        current_controller_request_for_test_storage = controller_request;
        committed_execution_route_for_test_storage =
            CommittedExecutionRouteForTest::CONTROLLER_TONE;

        if (test_tone_commit_invoker_for_test)
        {
            test_tone_commit_invoker_for_test();
        }

        if (suppress_scheduler_execution_for_test)
        {
            return;
        }

        transmitter_configure_execution(
            controller_request,
            current_transmission_request);
        return;
    }

    if (current_transmission_request.isTone())
    {
        committed_execution_route_for_test_storage =
            CommittedExecutionRouteForTest::LEGACY;

        if (suppress_scheduler_execution_for_test)
        {
            return;
        }

        transmitter_configure_execution(current_transmission_request);
        return;
    }

    if (suppress_scheduler_execution_for_test)
    {
        return;
    }

    if (!current_transmission_request.isSkipWindow())
    {
        wsprrypi::TransmissionRequest controller_request =
            build_controller_request_from_legacy(
                current_transmission_request,
                wsprrypi::TransmissionMode::WSPR);

        wsprrypi::WsprPayload payload;
        payload.prepared = current_transmission_request.payload;
        payload.base_frequency_hz =
            current_transmission_request.actual_rf_frequency_hz;
        controller_request.payload = payload;
        current_controller_request_for_test_storage = controller_request;
        committed_execution_route_for_test_storage =
            CommittedExecutionRouteForTest::CONTROLLER_WSPR;

        transmitter_configure_execution(
            controller_request,
            current_transmission_request);
        return;
    }

    committed_execution_route_for_test_storage =
        CommittedExecutionRouteForTest::LEGACY;
    transmitter_configure_execution(current_transmission_request);
}

static wsprrypi::TransmissionMode to_controller_mode(ModeType mode) noexcept
{
    switch (mode)
    {
    case ModeType::TONE: return wsprrypi::TransmissionMode::TONE;
    case ModeType::QRSS: return wsprrypi::TransmissionMode::QRSS;
    case ModeType::FSKCW: return wsprrypi::TransmissionMode::FSKCW;
    case ModeType::DFCW: return wsprrypi::TransmissionMode::DFCW;
    case ModeType::WSPR: return wsprrypi::TransmissionMode::WSPR;
    }
    return wsprrypi::TransmissionMode::WSPR;
}

static wsprrypi::HardwareProfile to_controller_profile(
    TransmitBackendKind backend) noexcept
{
    if (backend == TransmitBackendKind::SI5351)
        return wsprrypi::HardwareProfile::SI5351;
    if (backend == TransmitBackendKind::SIMULATED || backend == TransmitBackendKind::WTP)
        return wsprrypi::HardwareProfile::UNSPECIFIED;
    if (backend == TransmitBackendKind::RP1_GPCLK)
        return wsprrypi::HardwareProfile::RP1_GPCLK;
    const auto processor = resolve_legacy_gpio_processor_profile();
    if (processor.has_value())
        return wsprrypi::legacyHardwareProfile(*processor);
    return wsprrypi::HardwareProfile::UNSPECIFIED;
}

static void commit_execution_request(
    const wsprrypi::TransmissionRequest &controller_request,
    const TransmissionRequest &legacy_request)
{
    current_transmission_request = legacy_request;
    current_transmission_request.hardware_profile =
        controller_request.policy.hardware_profile;
    std::pair<double, double> exact_range{
        current_transmission_request.actual_rf_frequency_hz,
        current_transmission_request.actual_rf_frequency_hz};
    switch (controller_request.mode)
    {
    case wsprrypi::TransmissionMode::FSKCW:
    {
        const auto &payload = std::get<wsprrypi::FskcwPayload>(
            controller_request.payload);
        exact_range = std::minmax(
            payload.mark_frequency_hz, payload.space_frequency_hz);
        break;
    }
    case wsprrypi::TransmissionMode::DFCW:
    {
        const auto &payload = std::get<wsprrypi::DfcwPayload>(
            controller_request.payload);
        exact_range = std::minmax(
            payload.dot_frequency_hz, payload.dash_frequency_hz);
        break;
    }
    default:
        break;
    }
    freeze_gpio_correction_provenance(
        current_transmission_request, exact_range);
    current_controller_request_for_test_storage = controller_request;
    if (suppress_scheduler_execution_for_test)
    {
        return;
    }

    transmitter_configure_execution(controller_request, current_transmission_request);
}

/**
 * @brief Clear scheduler-owned execution snapshots after a completed stop.
 *
 * The transmitter owns a separate execution snapshot.  It is cleared by
 * transmitter_clear_execution_state_after_stop(); the scheduler must also
 * discard its committed request so a completed transient Test Tone cannot be
 * reported or reused as live work.
 */
void clear_committed_execution_request() noexcept
{
    current_transmission_request = TransmissionRequest{};
    current_controller_request_for_test_storage.reset();
    committed_execution_route_for_test_storage =
        CommittedExecutionRouteForTest::NONE;
    committed_gpio_correction_provenance = {};
}

static bool resolve_qrss_runtime_request(
    const ArgParserConfig &cfg,
    std::string &message_out,
    double &frequency_hz_out,
    double &dot_seconds_out) noexcept
{
    if (try_get_qrss_startup_request(message_out, frequency_hz_out, dot_seconds_out))
    {
        return true;
    }

    if (cfg.qrss.message.empty() ||
        cfg.qrss.frequency_hz <= 0.0 ||
        cfg.qrss.dot_seconds <= 0.0)
    {
        return false;
    }

    message_out = cfg.qrss.message;
    frequency_hz_out = cfg.qrss.frequency_hz;
    dot_seconds_out = cfg.qrss.dot_seconds;
    return true;
}

static bool resolve_fskcw_runtime_request(
    const ArgParserConfig &cfg,
    std::string &message_out,
    double &mark_frequency_hz_out,
    double &space_frequency_hz_out,
    double &dot_seconds_out) noexcept
{
    if (try_get_fskcw_startup_request(
            message_out,
            mark_frequency_hz_out,
            space_frequency_hz_out,
            dot_seconds_out))
    {
        return true;
    }

    if (cfg.fskcw.message.empty() ||
        cfg.fskcw.mark_frequency_hz <= 0.0 ||
        cfg.fskcw.space_frequency_hz <= 0.0 ||
        cfg.fskcw.mark_frequency_hz <= cfg.fskcw.space_frequency_hz ||
        cfg.fskcw.dot_seconds <= 0.0)
    {
        return false;
    }

    message_out = cfg.fskcw.message;
    mark_frequency_hz_out = cfg.fskcw.mark_frequency_hz;
    space_frequency_hz_out = cfg.fskcw.space_frequency_hz;
    dot_seconds_out = cfg.fskcw.dot_seconds;
    return true;
}

static bool resolve_dfcw_runtime_request(
    const ArgParserConfig &cfg,
    std::string &message_out,
    double &dot_frequency_hz_out,
    double &dash_frequency_hz_out,
    double &dot_seconds_out) noexcept
{
    if (try_get_dfcw_startup_request(
            message_out,
            dot_frequency_hz_out,
            dash_frequency_hz_out,
            dot_seconds_out))
    {
        return true;
    }

    if (cfg.dfcw.message.empty() ||
        cfg.dfcw.dot_frequency_hz <= 0.0 ||
        cfg.dfcw.dash_frequency_hz <= 0.0 ||
        cfg.dfcw.dot_frequency_hz == cfg.dfcw.dash_frequency_hz ||
        cfg.dfcw.dot_seconds <= 0.0)
    {
        return false;
    }

    message_out = cfg.dfcw.message;
    dot_frequency_hz_out = cfg.dfcw.dot_frequency_hz;
    dash_frequency_hz_out = cfg.dfcw.dash_frequency_hz;
    dot_seconds_out = cfg.dfcw.dot_seconds;
    return true;
}

bool has_non_wspr_cli_startup_request(ModeType mode) noexcept
{
    switch (mode)
    {
    case ModeType::QRSS:
        return has_qrss_startup_request();
    case ModeType::FSKCW:
        return has_fskcw_startup_request();
    case ModeType::DFCW:
        return has_dfcw_startup_request();
    default:
        return false;
    }
}

static const char *mode_type_name(ModeType mode) noexcept
{
    switch (mode)
    {
    case ModeType::QRSS:
        return "QRSS";
    case ModeType::FSKCW:
        return "FSKCW";
    case ModeType::DFCW:
        return "DFCW";
    case ModeType::WSPR:
        return "WSPR";
    case ModeType::TONE:
        return "TONE";
    }

    return "UNKNOWN";
}

bool is_non_wspr_runtime_mode(ModeType mode) noexcept
{
    return mode == ModeType::QRSS ||
           mode == ModeType::FSKCW ||
           mode == ModeType::DFCW;
}

void log_scheduler_path_selection(ModeType mode)
{
    llog.logS(INFO, "Scheduling path selected: ", mode_type_name(mode), ".");
}

std::chrono::system_clock::time_point next_non_wspr_schedule_time_for_test(
    const ArgParserConfig &cfg,
    const std::chrono::system_clock::time_point &now)
{
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
    localtime_r(&now_time_t, &local_tm);
    local_tm.tm_min = cfg.schedule_start_minute;
    local_tm.tm_sec = cfg.schedule_start_second;
    std::time_t candidate_time_t = std::mktime(&local_tm);
    auto candidate = std::chrono::system_clock::from_time_t(candidate_time_t);
    const auto repeat =
        std::chrono::minutes(cfg.schedule_repeat_minutes);

    while (candidate <= now)
    {
        candidate += repeat;
    }

    return candidate;
}

static std::chrono::system_clock::time_point next_non_wspr_schedule_time(
    const ArgParserConfig &cfg)
{
    return next_non_wspr_schedule_time_for_test(
        cfg,
        std::chrono::system_clock::now());
}

static std::string format_local_schedule_time(
    const std::chrono::system_clock::time_point &tp)
{
    std::time_t time_t_value = std::chrono::system_clock::to_time_t(tp);
    std::tm local_tm{};
    localtime_r(&time_t_value, &local_tm);
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static std::string format_utc_schedule_time(
    const std::chrono::system_clock::time_point &tp)
{
    std::time_t time_t_value = std::chrono::system_clock::to_time_t(tp);
    std::tm utc_tm{};
    gmtime_r(&time_t_value, &utc_tm);

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%S") << " UTC";
    return oss.str();
}

void reset_active_wspr_plan_state()
{
    active_wspr_plan = PreparedWsprTransmission{};
    active_wspr_frame_index = 0;
    active_wspr_plan_dial_frequency = 0.0;
    active_wspr_plan_frequency_entry = WsprFrequencyEntry{};
    active_wspr_plan_in_progress = false;
}

bool active_wspr_plan_has_more_frames_after_current() noexcept
{
    return active_wspr_plan_in_progress &&
           (active_wspr_frame_index + 1U) < active_wspr_plan.frameCount();
}

bool is_managed_persistent_mode() noexcept
{
    return config.use_ini;
}

void log_transmit_disabled_skip(const ArgParserConfig &cfg)
{
    if (!runtime_transmit_requested(cfg))
    {
        llog.logS(
            INFO,
            "No transmission requested; skipping transmission and scheduling.");
    }
    else if (managed_reload_tx_inhibited)
    {
        llog.logS(
            ERROR,
            "Transmission requested but inhibited during managed configuration reload.");
    }
    else if (startup_quiesce_inhibited.load(std::memory_order_acquire))
    {
        log_startup_quiesce_inhibited_skip();
    }
    else if (rp1_route_transaction_inhibited.load(std::memory_order_acquire))
    {
        llog.logS(
            ERROR,
            "Transmission requested but inhibited because the RP1 route transaction is unresolved.");
    }
    else
    {
        llog.logS(
            ERROR,
            "Transmission requested but inhibited by an unknown runtime gate.");
    }
}

static void log_startup_quiesce_inhibited_skip()
{
    std::lock_guard<std::mutex> lock(startup_quiesce_error_mtx);
    llog.logS(
        ERROR,
        "Transmit is configured on but inhibited because startup hardware could not be quiesced: ",
        startup_quiesce_error);
}

static wsprrypi::StartupQuiesceResult invoke_startup_quiesce()
{
    if (startup_quiesce_invoker_for_test)
        return startup_quiesce_invoker_for_test();
    return transmitter_quiesce_for_startup();
}

bool run_startup_quiesce_gate(const ArgParserConfig &cfg)
{
    const wsprrypi::StartupQuiesceResult result = invoke_startup_quiesce();
    if (cfg.transmit_backend == TransmitBackendKind::WTP) return result.ok;
    if (!result.ok)
    {
        std::lock_guard<std::mutex> lock(startup_quiesce_error_mtx);
        startup_quiesce_error = result.error.empty()
            ? "The selected transmission backend did not report a reason."
            : result.error;
        startup_quiesce_inhibited.store(true, std::memory_order_release);
    }

    deassert_transmit_gpio_outputs(&cfg, false, "startup hardware quiesce");
    shutdown_all_configured_selector_gpios(cfg);
    return result.ok;
}

bool runtime_transmit_requested(const ArgParserConfig &cfg) noexcept
{
    if (!cfg.use_ini)
    {
        if (cfg.mode == ModeType::TONE &&
            has_direct_tone_startup_request())
        {
            return true;
        }

        if (has_non_wspr_cli_startup_request(cfg.mode))
        {
            return true;
        }

        if (cfg.mode == ModeType::WSPR && !cfg.loop_tx)
        {
            return true;
        }
    }

    return cfg.transmit;
}

bool runtime_transmit_enabled(const ArgParserConfig &cfg) noexcept
{
    return runtime_transmit_requested(cfg) &&
           (cfg.transmit_backend != TransmitBackendKind::WTP || wtp_runtime_ready()) &&
           !managed_reload_tx_inhibited &&
           !startup_quiesce_inhibited.load(std::memory_order_acquire) &&
           !rp1_route_transaction_inhibited.load(std::memory_order_acquire);
}

bool runtime_transmit_preparation_enabled(
    const ArgParserConfig &cfg) noexcept
{
    if (runtime_transmit_enabled(cfg))
        return true;

    // A transient positional WSPR request must first bind its supplied
    // confirmation to the concrete frame request. That reconciliation is what
    // resolves the RP1 route transaction; all other runtime gates remain
    // authoritative before request preparation.
    return runtime_transmit_requested(cfg) &&
           !cfg.use_ini &&
           cfg.mode == ModeType::WSPR &&
           cfg.transmit_backend == TransmitBackendKind::RP1_GPCLK &&
           !managed_reload_tx_inhibited &&
           !startup_quiesce_inhibited.load(std::memory_order_acquire) &&
           rp1_route_transaction_inhibited.load(std::memory_order_acquire);
}

static std::string direct_tone_inhibition_reason(const ArgParserConfig &cfg)
{
    if (cfg.use_ini)
        return "Direct CLI tone request is unavailable in managed INI mode.";
    if (managed_reload_tx_inhibited)
        return "Direct CLI tone request is inhibited during managed configuration reload.";
    if (startup_quiesce_inhibited.load(std::memory_order_acquire))
    {
        std::lock_guard<std::mutex> lock(startup_quiesce_error_mtx);
        return "Direct CLI tone request is inhibited because startup hardware could not be quiesced: " +
            startup_quiesce_error;
    }
    if (rp1_route_transaction_inhibited.load(std::memory_order_acquire))
        return "Direct CLI tone request is inhibited because the RP1 route transaction is unresolved.";
    return {};
}

bool web_server_start_enabled(const ArgParserConfig &cfg) noexcept
{
    return cfg.enable_web &&
           cfg.enable_http &&
           cfg.web_port >= 1024 &&
           cfg.web_port <= 49151;
}

bool websocket_server_start_enabled(const ArgParserConfig &cfg) noexcept
{
    return cfg.enable_web &&
           cfg.socket_port >= 1024 &&
           cfg.socket_port <= 49151;
}

static wsprrypi::BackendKind to_controller_backend(
    TransmitBackendKind backend) noexcept
{
    if (backend == TransmitBackendKind::WTP)
        return wsprrypi::BackendKind::WTP;
    if (backend == TransmitBackendKind::SI5351)
        return wsprrypi::BackendKind::SI5351;
    if (backend == TransmitBackendKind::SIMULATED)
        return wsprrypi::BackendKind::SIMULATED;
    if (backend == TransmitBackendKind::RP1_GPCLK)
        return wsprrypi::BackendKind::RP1_GPCLK;
    return wsprrypi::BackendKind::RPI_CLOCK_GPIO;
}

static wsprrypi::ClockSource to_controller_clock_source(
    const ArgParserConfig &cfg) noexcept
{
    if (cfg.transmit_backend == TransmitBackendKind::WTP)
        return wsprrypi::ClockSource::UNSPECIFIED;
    if (cfg.transmit_backend != TransmitBackendKind::SI5351)
    {
        return wsprrypi::ClockSource::GPIO_CLK;
    }

    switch (cfg.si5351_tx_output)
    {
    case 1:
        return wsprrypi::ClockSource::SI5351_CLK1;
    case 2:
        return wsprrypi::ClockSource::SI5351_CLK2;
    default:
        return wsprrypi::ClockSource::SI5351_CLK0;
    }
}

bool managed_reload_generation_changed(
    std::uint64_t generation_snapshot) noexcept
{
    return ini_reload_generation.load(std::memory_order_acquire) != generation_snapshot;
}

void set_managed_reload_tx_inhibited(
    bool inhibited,
    std::string_view reason)
{
    managed_reload_tx_inhibited = inhibited;

    if (!reason.empty())
    {
        llog.logS(ERROR, reason);
    }
}

bool transmitter_reload_should_defer() noexcept
{
    if (wtp_runtime_selected()) return wtp_runtime_defers_reload();
    const WsprTransmitState state = transmitter_state();

    if (state == WsprTransmitState::TRANSMITTING ||
        state == WsprTransmitState::RECOVERING)
    {
        return true;
    }

    // Direct-tone modes start immediately and can still be in the launch
    // handoff while the controller remains ENABLED. Treat that window as
    // active for reload purposes so INI edits do not cancel the live run.
    return state == WsprTransmitState::ENABLED &&
           transmitter_active_execution_is_tone();
}

std::string transmitter_reload_defer_debug_snapshot()
{
    auto mode_name =
        [](wsprrypi::TransmissionMode mode) noexcept
    {
        switch (mode)
        {
        case wsprrypi::TransmissionMode::WSPR:
            return "WSPR";
        case wsprrypi::TransmissionMode::QRSS:
            return "QRSS";
        case wsprrypi::TransmissionMode::FSKCW:
            return "FSKCW";
        case wsprrypi::TransmissionMode::DFCW:
            return "DFCW";
        case wsprrypi::TransmissionMode::CW:
            return "CW";
        case wsprrypi::TransmissionMode::TONE:
            return "TONE";
        default:
            return "UNKNOWN";
        }
    };

    auto committed_mode_name =
        [](TransmissionMode mode) noexcept
    {
        switch (mode)
        {
        case TransmissionMode::WSPR:
            return "WSPR";
        case TransmissionMode::TONE:
            return "TONE";
        default:
            return "UNKNOWN";
        }
    };

    auto backend_name =
        [](wsprrypi::BackendKind backend) noexcept
    {
        switch (backend)
        {
        case wsprrypi::BackendKind::RPI_CLOCK_GPIO:
            return "GPIO";
        case wsprrypi::BackendKind::RP1_GPCLK:
            return "RP1 GPCLK";
        case wsprrypi::BackendKind::SI5351:
            return "SI5351";
        case wsprrypi::BackendKind::SIMULATED:
            return "simulated";
        default:
            return "UNKNOWN";
        }
    };

    std::ostringstream oss;
    const WsprTransmitState state = transmitter_state();
    const bool state_transmitting =
        state == WsprTransmitState::TRANSMITTING;
    const bool state_recovering =
        state == WsprTransmitState::RECOVERING;
    const bool state_enabled =
        state == WsprTransmitState::ENABLED;
    const bool active_execution_is_tone =
        transmitter_active_execution_is_tone();
    const bool defer =
        state_transmitting ||
        state_recovering ||
        (state_enabled && active_execution_is_tone);
    const auto runtime_status = transmitter_runtime_status();
    const TransmissionRequest committed_request =
        current_transmission_request_for_test();
    const std::optional<wsprrypi::TransmissionRequest> controller_request =
        current_controller_request_for_test();
    const WsprRuntimeStatusSnapshot runtime_snapshot =
        current_tx_runtime_status_snapshot();

    oss << "defer=" << (defer ? "true" : "false")
        << ", state_transmitting=" << (state_transmitting ? "true" : "false")
        << ", state_recovering=" << (state_recovering ? "true" : "false")
        << ", state_enabled=" << (state_enabled ? "true" : "false")
        << ", web_test_tone=" << (web_test_tone.load(std::memory_order_acquire) ? "true" : "false")
        << ", managed_reload_tx_inhibited=" << (managed_reload_tx_inhibited_state() ? "true" : "false")
        << ", shutdown_after_current_transmission=" << (shutdown_after_current_transmission.load(std::memory_order_acquire) ? "true" : "false")
        << ", shutdown_after_wspr_plan=" << (shutdown_after_wspr_plan.load(std::memory_order_acquire) ? "true" : "false")
        << ", active_wspr_plan_in_progress=" << (active_wspr_plan_in_progress ? "true" : "false")
        << ", runtime_tx_state=" << runtime_snapshot.tx_state
        << ", runtime_mode=" << runtime_snapshot.runtime_mode
        << ", runtime_frequency_hz=" << runtime_snapshot.frequency_hz
        << ", runtime_frequency_is_skip=" << (runtime_snapshot.frequency_is_skip ? "true" : "false")
        << ", runtime_snapshot_mode=" << mode_name(runtime_status.mode)
        << ", committed_request_mode=" << committed_mode_name(committed_request.mode)
        << ", committed_request_rf_hz=" << committed_request.actual_rf_frequency_hz
        << ", committed_request_dial_hz=" << committed_request.dial_frequency_hz
        << ", committed_request_skip=" << (committed_request.isSkipWindow() ? "true" : "false")
        << ", committed_request_token=" << committed_request.frequency_entry_label
        << ", current_dial_frequency=" << current_dial_frequency
        << ", current_frequency_token=" << current_frequency_entry.token
        << ", controller_request_active=" << (controller_request.has_value() ? "true" : "false")
        << ", controller_request_mode=";

    if (controller_request.has_value())
    {
        oss << mode_name(controller_request->mode);
    }
    else
    {
        oss << "NONE";
    }

    oss << ", controller_request_backend=";
    if (controller_request.has_value())
    {
        oss << backend_name(controller_request->output.backend);
    }
    else
    {
        oss << "NONE";
    }

    oss << ", transmitter_snapshot={" << transmitter_reload_defer_debug_state() << "}";
    return oss.str();
}

bool scheduler_managed_transmission_active_for_test_tone() noexcept
{
    const WsprTransmitState state = transmitter_state();
    if (state != WsprTransmitState::TRANSMITTING &&
        state != WsprTransmitState::RECOVERING)
    {
        return false;
    }

    return !transmitter_active_execution_is_tone();
}

bool scheduler_managed_transmission_enabled_for_test_tone() noexcept
{
    return runtime_transmit_enabled(config);
}

void finalize_transmission_stop_cleanup(
    const ArgParserConfig *selector_config,
    bool keep_selector_gpio_initialized,
    const char *led_reason,
    bool clear_scheduler_latches,
    bool emit_debug_log)
{
    if (emit_debug_log)
    {
        llog.logS(
            DEBUG,
            "Finalizing scheduler cleanup after transmitter stop.");
    }

    deassert_transmit_gpio_outputs(
        selector_config,
        keep_selector_gpio_initialized,
        led_reason);
    if (clear_scheduler_latches)
    {
        shutdown_after_current_transmission.store(false, std::memory_order_release);
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();
    }
}

WsprFrequencyEntry next_frequency_entry_from(
    const std::vector<WsprFrequencyEntry> &entries,
    int &iterator,
    bool reset)
{
    if (reset)
    {
        iterator = 0;
    }

    if (entries.empty())
    {
        return WsprFrequencyEntry{};
    }

    const auto idx =
        static_cast<std::size_t>(iterator % static_cast<int>(entries.size()));
    const WsprFrequencyEntry entry = entries[idx];
    ++iterator;
    return entry;
}

WsprFrequencyEntry next_frequency_entry(bool reset);

/**
 * @brief Return the prepared plan for a single frame from a saved plan.
 *
 * The scheduler uses this when continuing a paired transmission so each
 * committed request still represents exactly one execution slot even when
 * the planner originally returned multiple frames.
 */
static PreparedWsprTransmission slot_plan_for_frame(
    const PreparedWsprTransmission &plan,
    std::size_t frame_index)
{
    PreparedWsprTransmission slot_plan;
    slot_plan.plan_type = plan.plan_type;
    slot_plan.callsign = plan.callsign;
    slot_plan.locator = plan.locator;
    slot_plan.callsign_raw = plan.callsign_raw;
    slot_plan.locator_raw = plan.locator_raw;
    slot_plan.callsign_normalized = plan.callsign_normalized;
    slot_plan.locator_normalized = plan.locator_normalized;
    slot_plan.total_frame_count = 1U;
    slot_plan.current_frame = 1U;
    if (frame_index < plan.frame_callsigns.size())
    {
        slot_plan.frame_callsign = plan.frame_callsigns.at(frame_index);
    }
    else
    {
        slot_plan.frame_callsign = plan.callsign_normalized;
    }
    if (frame_index < plan.frame_locators.size())
    {
        slot_plan.frame_locator = plan.frame_locators.at(frame_index);
    }
    else
    {
        slot_plan.frame_locator = plan.locator_normalized;
    }
    slot_plan.power_dbm = plan.power_dbm;
    slot_plan.frames.push_back(plan.frames.at(frame_index));
    return slot_plan;
}

static bool is_auto_paired_upgrade_eligible(const ArgParserConfig &cfg) noexcept
{
    return cfg.mode == ModeType::WSPR &&
           (cfg.wspr.callsign.find('/') != std::string::npos) &&
           cfg.wspr.grid_square.size() == 6U;
}

void consume_tx_iteration_if_needed()
{
    if (config.loop_tx)
    {
        return;
    }

    if (config.tx_iterations.load(std::memory_order_acquire) <= 0)
    {
        return;
    }

    int remaining = --config.tx_iterations;

    if (remaining <= 0)
    {
        if (active_wspr_plan_has_more_frames_after_current())
        {
            shutdown_after_wspr_plan.store(true, std::memory_order_release);
            llog.logS(
                INFO,
                "Parsed last of TX iterations, signaling shutdown "
                "after paired transmission.");
        }
        else
        {
            shutdown_after_current_transmission.store(
                true,
                std::memory_order_release);
            llog.logS(
                INFO,
                "Parsed last of TX iterations, signaling shutdown "
                "after current transmission.");
        }
    }
    else
    {
        llog.logS(INFO, "WSPR transmissions remaining: ", remaining);
    }
}

BandGPIOPrepareStatus prepare_band_gpio_for_frequency_or_log(
    double source_frequency_hz,
    const WsprFrequencyEntry &entry,
    const ArgParserConfig &cfg,
    int frequency_entry_index,
    BandGPIOResolution *resolution_out)
{
    band_gpio_prepare_call_counter_for_test.fetch_add(1U, std::memory_order_relaxed);

    if (resolution_out != nullptr)
    {
        *resolution_out = BandGPIOResolution{};
    }

    const auto gpio_policy = wsprrypi::evaluate_gpio_band_policy(
        to_controller_backend(cfg.transmit_backend),
        source_frequency_hz,
        to_controller_mode(cfg.mode),
        cfg.allow_unqualified_frequency,
        cfg.allow_non_amateur_frequency,
        to_controller_profile(cfg.transmit_backend));
    if (!gpio_policy.allowed)
    {
        llog.logS(WARN, gpio_policy.error);
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Failed,
            std::memory_order_release);
        return BandGPIOPrepareStatus::Failed;
    }

    const auto band = lookup.lookup_ham_band(source_frequency_hz);
    if (!band.has_value())
    {
        if (entry.selector_gpio != kSelectorGpioUnset)
        {
            BandGPIOResolution explicit_resolution;
            explicit_resolution.config.gpio = entry.selector_gpio;
            explicit_resolution.config.enabled = true;
            explicit_resolution.config.active_high = entry.selector_gpio_active_high;
            explicit_resolution.selector_enabled = true;
            explicit_resolution.selector_source = "explicit frequency entry";
            explicit_resolution.band_known = false;
            if (resolution_out != nullptr)
                *resolution_out = explicit_resolution;
            llog.logS(
                DEBUG, "[BandGPIO]", "Frequency entry ", entry.token,
                " uses explicit GPIO ", entry.selector_gpio,
                " without inferring an amateur band.");
            return apply_band_gpio_resolution(explicit_resolution);
        }
        llog.logS(
            WARN,
            "Unable to map source frequency ",
            lookup.freq_display_string(source_frequency_hz),
            " to a ham band for band-selector GPIO preparation.");
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Failed,
            std::memory_order_release);
        return BandGPIOPrepareStatus::Failed;
    }

    BandGPIOResolution resolution;
    resolution.band = *band;
    resolution.selector_source = "frequency entry";
    if (entry.selector_gpio != kSelectorGpioUnset)
    {
        resolution.config.gpio = entry.selector_gpio;
        resolution.config.enabled = true;
        resolution.config.active_high = entry.selector_gpio_active_high;
        resolution.selector_enabled = true;
    }
    else if (entry.allow_band_gpio_fallback)
    {
        resolution.config = cfg.band_gpio[ham_band_index(*band)];
        resolution.from_band_config = true;
        resolution.selector_source = "band configuration";
        resolution.selector_enabled =
            resolution.config.enabled && resolution.config.gpio >= 0;
        if (!resolution.selector_enabled)
        {
            if (resolution_out != nullptr)
            {
                *resolution_out = resolution;
            }
            stop_active_transmission_selectors();
            llog.logS(
                DEBUG,
                "[BandGPIO]",
                "Frequency entry index ",
                frequency_entry_index,
                " token ",
                entry.token,
                " resolved band ",
                ham_band_to_string(*band),
                "; GPIO switching enabled false; fallback path band configuration had no enabled GPIO.");
            llog.logS(
                DEBUG,
                "[BandGPIO]",
                "No selector GPIO requested for frequency entry ",
                entry.token,
                "; no configured band GPIO for band ",
                ham_band_to_string(*band),
                "; leaving LPF selection inactive.");
            return BandGPIOPrepareStatus::Inactive;
        }
    }
    else
    {
        if (resolution_out != nullptr)
        {
            *resolution_out = resolution;
        }
        stop_active_transmission_selectors();
        llog.logS(
            DEBUG,
            "[BandGPIO]",
            "Frequency entry index ",
            frequency_entry_index,
            " token ",
            entry.token,
            " resolved band ",
            ham_band_to_string(*band),
            "; GPIO switching enabled false; no per-entry selector and band fallback disabled.");
        llog.logS(
            DEBUG,
            "[BandGPIO]",
            "No selector GPIO requested for frequency entry ",
            entry.token,
            "; leaving LPF selection inactive.");
        return BandGPIOPrepareStatus::Inactive;
    }

    llog.logS(
        DEBUG,
        "[BandGPIO]",
        "Frequency entry index ",
        frequency_entry_index,
        " token ",
        entry.token,
        "; ",
        "Unified scheduler selector derived band ",
        ham_band_to_string(*band),
        " from source frequency ",
        lookup.freq_display_string(source_frequency_hz),
        "; selected GPIO ",
        resolution.config.gpio,
        " (",
        (resolution.config.active_high ? "active high" : "active low"),
        ")",
        " from ",
        resolution.selector_source,
        ", enabled ",
        (resolution.selector_enabled ? "true" : "false"),
        "; committed request token ",
        entry.token,
        ".");
    if (resolution_out != nullptr)
    {
        *resolution_out = resolution;
    }
    return apply_band_gpio_resolution(resolution);
}

double maybe_apply_wspr_random_offset(
    double actual_rf_frequency_hz,
    const ArgParserConfig &cfg)
{
    if (!cfg.use_offset || actual_rf_frequency_hz == 0.0)
    {
        return actual_rf_frequency_hz;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-1.0, 1.0);
    return actual_rf_frequency_hz + dis(gen) * kWsprRandomOffsetHz;
}

/**
 * @brief Build the scheduler-side request for a direct tone execution.
 *
 * This request is fully committed at the orchestration layer. The
 * transmitter must not infer any additional policy from tone mode.
 */
TransmissionRequest make_tone_request(
    const ArgParserConfig &cfg,
    double committed_ppm,
    double actual_rf_frequency_hz,
    double dial_frequency_hz,
    const WsprFrequencyEntry &entry)
{
    TransmissionRequest request;
    request.mode = TransmissionMode::TONE;
    request.dial_frequency_hz = dial_frequency_hz;
    request.actual_rf_frequency_hz = actual_rf_frequency_hz;
    request.ppm = committed_ppm;
    request.power_level = cfg.power_level;
    request.tx_gpio = cfg.tx_pin;
    request.frequency_entry_label = entry.token;
    request.allow_unqualified_frequency = cfg.allow_unqualified_frequency;
    request.allow_non_amateur_frequency = cfg.allow_non_amateur_frequency;
    request.hardware_profile = to_controller_profile(cfg.transmit_backend);
    return request;
}

/**
 * @brief Prepare, commit, and start one transient direct-CLI tone request.
 *
 * All fallible band-policy and selector work completes before the execution
 * request is committed or startAsync() can be reached.  Callers therefore
 * leave both RF and selector outputs inactive when preparation fails.
 */
bool start_direct_tone_execution(
    const ArgParserConfig &cfg,
    const WsprFrequencyEntry &entry,
    double actual_rf_frequency_hz,
    std::string *error_message)
{
    const auto gpio_policy = wsprrypi::evaluate_gpio_band_policy(
        to_controller_backend(cfg.transmit_backend),
        actual_rf_frequency_hz,
        wsprrypi::TransmissionMode::TONE,
        cfg.allow_unqualified_frequency,
        cfg.allow_non_amateur_frequency,
        to_controller_profile(cfg.transmit_backend));
    if (!gpio_policy.allowed)
    {
        if (error_message != nullptr)
        {
            *error_message = gpio_policy.error;
        }
        return false;
    }

    TransmissionRequest request = make_tone_request(
        cfg,
        cfg.ppm,
        actual_rf_frequency_hz,
        actual_rf_frequency_hz,
        entry);
    std::string development_error;
    if (!apply_direct_rp1_development_confirmation(
            cfg, request, &development_error))
    {
        if (error_message != nullptr)
        {
            *error_message =
                "RP1 direct-operation confirmation rejected: " +
                development_error;
        }
        return false;
    }

    if (request.rp1_development.enabled)
    {
        std::lock_guard<std::mutex> lock(direct_tone_confirmation_mtx);
        if (claimed_direct_tone_confirmation.has_value())
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "RP1 direct-operation confirmation rejected: confirmation replay is not permitted.";
            }
            return false;
        }
        claimed_direct_tone_confirmation =
            request.rp1_development.operation_id;
    }

    const std::string inhibition_reason = direct_tone_inhibition_reason(cfg);
    if (!inhibition_reason.empty())
    {
        if (error_message != nullptr)
            *error_message = inhibition_reason;
        return false;
    }

    BandGPIOResolution selector_resolution;
    const BandGPIOPrepareStatus selector_status =
        prepare_band_gpio_for_frequency_or_log(
            entry.dial_frequency_hz,
            entry,
            cfg,
            -1,
            &selector_resolution);
    if (selector_status == BandGPIOPrepareStatus::Failed)
    {
        (void)stop_active_transmission_selectors();
        if (error_message != nullptr)
        {
            *error_message =
                "Unable to resolve or prepare the direct test-tone band selector.";
        }
        return false;
    }

    commit_band_gpio_snapshot_to_request(
        request,
        selector_resolution,
        selector_status);
    commit_execution_request(request);

    llog.logS(
        INFO,
        "Direct tone request committed; transmission start initiated.");

    if (direct_tone_start_invoker_for_test)
    {
        direct_tone_start_invoker_for_test();
    }
    else if (!suppress_scheduler_execution_for_test)
    {
        transmitter_start_async();
    }

    return true;
}


static WsprFrequencyEntry make_non_wspr_band_gpio_frequency_entry(
    std::string token,
    double frequency_hz)
{
    WsprFrequencyEntry entry;
    entry.token = std::move(token);
    entry.dial_frequency_hz = frequency_hz;
    entry.selector_gpio = kSelectorGpioUnset;
    entry.selector_gpio_active_high = false;
    entry.allow_band_gpio_fallback = true;
    return entry;
}

static bool apply_direct_rp1_development_confirmation(
    const ArgParserConfig &cfg,
    TransmissionRequest &request,
    std::string *error_message)
{
    return apply_direct_rp1_development_confirmation_bridge(
        cfg, request, error_message);
}

static bool prepare_and_commit_non_wspr_request(
    const ArgParserConfig &cfg,
    const wsprrypi::TransmissionRequest &controller_request,
    TransmissionRequest legacy_request,
    const WsprFrequencyEntry &frequency_entry)
{
    BandGPIOResolution selector_resolution;
    const BandGPIOPrepareStatus selector_status =
        prepare_band_gpio_for_frequency_or_log(
            frequency_entry.dial_frequency_hz,
            frequency_entry,
            cfg,
            -1,
            &selector_resolution);
    if (selector_status == BandGPIOPrepareStatus::Failed)
    {
        return false;
    }

    commit_band_gpio_snapshot_to_request(
        legacy_request,
        selector_resolution,
        selector_status);
    commit_execution_request(controller_request, legacy_request);
    return true;
}

bool start_non_wspr_transmission_now(const ArgParserConfig &cfg,
    std::optional<std::chrono::system_clock::time_point> scheduled_start)
{
    const double committed_ppm = cfg.ppm;
    std::string policy_error;
    if (!validate_non_wspr_repeat_interval_policy(cfg, &policy_error))
    {
        llog.logE(ERROR, policy_error);
        return false;
    }

    if (cfg.mode == ModeType::QRSS)
    {
        auto controller_request =
            scheduling_detail::make_qrss_controller_request(cfg, committed_ppm);
        if (scheduled_start) controller_request.slot.start_time = *scheduled_start;
        auto legacy_request = scheduling_detail::make_qrss_legacy_request(cfg, committed_ppm);
        std::string development_error;
        if (!apply_direct_rp1_development_confirmation(
                cfg, legacy_request, &development_error))
        {
            llog.logE(ERROR, development_error);
            return false;
        }
        if (!runtime_transmit_enabled(cfg))
        {
            if (startup_quiesce_inhibited.load(std::memory_order_acquire))
                log_startup_quiesce_inhibited_skip();
            else
                log_transmit_disabled_skip(cfg);
            return true;
        }
        std::string message;
        double frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!resolve_qrss_runtime_request(cfg, message, frequency_hz, dot_seconds))
        {
            llog.logE(ERROR, "QRSS mode requested without a valid QRSS configuration.");
            return false;
        }

        const WsprFrequencyEntry selector_entry =
            make_non_wspr_band_gpio_frequency_entry(
                "qrss-cli-test",
                frequency_hz);
        if (!prepare_and_commit_non_wspr_request(
                cfg,
                controller_request,
                std::move(legacy_request),
                selector_entry))
        {
            llog.logE(ERROR, "QRSS mode could not prepare band GPIO selector.");
            return false;
        }
        if (!suppress_scheduler_execution_for_test)
        {
            transmitter_start_async();
        }
        llog.logS(DEBUG, "Transmitting QRSS message.");

        llog.logS(DEBUG,
                  "- Message: ",
                  message);

        llog.logS(DEBUG,
                  "- Frequency (Hz): ",
                  frequency_hz);

        llog.logS(DEBUG,
                  "- Frequency (MHz): ",
                  transmitter_format_frequency_mhz(frequency_hz));

        llog.logS(DEBUG,
                  "- Dot length (s): ",
                  dot_seconds);
        return true;
    }

    if (cfg.mode == ModeType::FSKCW)
    {
        auto controller_request =
            scheduling_detail::make_fskcw_controller_request(cfg, committed_ppm);
        if (scheduled_start) controller_request.slot.start_time = *scheduled_start;
        auto legacy_request = scheduling_detail::make_fskcw_legacy_request(cfg, committed_ppm);
        std::string development_error;
        if (!apply_direct_rp1_development_confirmation(
                cfg, legacy_request, &development_error))
        {
            llog.logE(ERROR, development_error);
            return false;
        }
        if (!runtime_transmit_enabled(cfg))
        {
            if (startup_quiesce_inhibited.load(std::memory_order_acquire))
                log_startup_quiesce_inhibited_skip();
            else
                log_transmit_disabled_skip(cfg);
            return true;
        }
        std::string message;
        double mark_frequency_hz = 0.0;
        double space_frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!resolve_fskcw_runtime_request(
                cfg,
                message,
                mark_frequency_hz,
                space_frequency_hz,
                dot_seconds))
        {
            llog.logE(ERROR, "FSKCW mode requested without a valid FSKCW configuration.");
            return false;
        }

        const WsprFrequencyEntry selector_entry =
            make_non_wspr_band_gpio_frequency_entry(
                "fskcw-cli-test",
                mark_frequency_hz);
        if (!prepare_and_commit_non_wspr_request(
                cfg,
                controller_request,
                std::move(legacy_request),
                selector_entry))
        {
            llog.logE(ERROR, "FSKCW mode could not prepare band GPIO selector.");
            return false;
        }
        if (!suppress_scheduler_execution_for_test)
        {
            transmitter_start_async();
        }
        llog.logS(DEBUG, "Transmitting FSKCW message.");

        llog.logS(DEBUG,
                  "- Message: ",
                  message);

        llog.logS(DEBUG,
                  "- Mark frequency (Hz): ",
                  mark_frequency_hz);

        llog.logS(DEBUG,
                  "- Mark frequency (MHz): ",
                  transmitter_format_frequency_mhz(mark_frequency_hz));

        llog.logS(DEBUG,
                  "- Space frequency (Hz): ",
                  space_frequency_hz);

        llog.logS(DEBUG,
                  "- Space frequency (MHz): ",
                  transmitter_format_frequency_mhz(space_frequency_hz));

        llog.logS(DEBUG,
                  "- Dot length (s): ",
                  dot_seconds);
        return true;
    }

    if (cfg.mode == ModeType::DFCW)
    {
        auto controller_request =
            scheduling_detail::make_dfcw_controller_request(cfg, committed_ppm);
        if (scheduled_start) controller_request.slot.start_time = *scheduled_start;
        auto legacy_request = scheduling_detail::make_dfcw_legacy_request(cfg, committed_ppm);
        std::string development_error;
        if (!apply_direct_rp1_development_confirmation(
                cfg, legacy_request, &development_error))
        {
            llog.logE(ERROR, development_error);
            return false;
        }
        if (!runtime_transmit_enabled(cfg))
        {
            if (startup_quiesce_inhibited.load(std::memory_order_acquire))
                log_startup_quiesce_inhibited_skip();
            else
                log_transmit_disabled_skip(cfg);
            return true;
        }
        std::string message;
        double dot_frequency_hz = 0.0;
        double dash_frequency_hz = 0.0;
        double dot_seconds = 0.0;
        if (!resolve_dfcw_runtime_request(
                cfg,
                message,
                dot_frequency_hz,
                dash_frequency_hz,
                dot_seconds))
        {
            llog.logE(ERROR, "DFCW mode requested without a valid DFCW configuration.");
            return false;
        }

        const WsprFrequencyEntry selector_entry =
            make_non_wspr_band_gpio_frequency_entry(
                "dfcw-cli-test",
                dot_frequency_hz);
        if (!prepare_and_commit_non_wspr_request(
                cfg,
                controller_request,
                std::move(legacy_request),
                selector_entry))
        {
            llog.logE(ERROR, "DFCW mode could not prepare band GPIO selector.");
            return false;
        }
        if (!suppress_scheduler_execution_for_test)
        {
            transmitter_start_async();
        }
        llog.logS(DEBUG, "Transmitting DFCW message.");

        llog.logS(DEBUG,
                  "- Message: ",
                  message);

        llog.logS(DEBUG,
                  "- Dot frequency (Hz): ",
                  dot_frequency_hz);

        llog.logS(DEBUG,
                  "- Dot frequency (MHz): ",
                  transmitter_format_frequency_mhz(dot_frequency_hz));

        llog.logS(DEBUG,
                  "- Dash frequency (Hz): ",
                  dash_frequency_hz);

        llog.logS(DEBUG,
                  "- Dash frequency (MHz): ",
                  transmitter_format_frequency_mhz(dash_frequency_hz));

        llog.logS(DEBUG,
                  "- Dot length (s): ",
                  dot_seconds);
        return true;
    }

    return false;
}

bool start_non_wspr_transmission_now_for_test(const ArgParserConfig &cfg)
{
    return start_non_wspr_transmission_now(cfg);
}

void schedule_next_non_wspr_launch(const ArgParserConfig &cfg)
{
    if (cfg.mode != ModeType::QRSS &&
        cfg.mode != ModeType::FSKCW &&
        cfg.mode != ModeType::DFCW)
    {
        return;
    }

    if (!runtime_transmit_enabled(cfg))
    {
        non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel);
        if (startup_quiesce_inhibited.load(std::memory_order_acquire))
            log_startup_quiesce_inhibited_skip();
        else
            log_transmit_disabled_skip(cfg);
        return;
    }

    auto next_launch = next_non_wspr_schedule_time(cfg);
    if (cfg.transmit_backend == TransmitBackendKind::WTP) {
        if (cfg.schedule_repeat_minutes <= 0) throw std::runtime_error("Pico repeat interval must be positive");
        const auto earliest = std::chrono::system_clock::now() + wtp_runtime_preparation_lead();
        while (next_launch < earliest)
            next_launch += std::chrono::minutes(cfg.schedule_repeat_minutes);
        non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel);
        if (!start_non_wspr_transmission_now(cfg, next_launch))
            request_wspr_shutdown("Pico scheduled request preparation failed");
        return;
    }
    const std::uint64_t generation =
        non_wspr_schedule_generation.fetch_add(1, std::memory_order_acq_rel) + 1U;

    llog.logS(INFO,
              "Next ",
              mode_type_name(cfg.mode),
              " launch at: ",
              format_utc_schedule_time(next_launch));

    llog.logS(DEBUG,
              "- Start minute: ",
              cfg.schedule_start_minute);

    llog.logS(DEBUG,
              "- Start second: ",
              cfg.schedule_start_second);

    llog.logS(DEBUG,
              "- Repeat interval (minutes): ",
              cfg.schedule_repeat_minutes);

    llog.logS(DEBUG,
              "- Mode type: ",
              mode_type_name(cfg.mode));

    std::thread(
        [generation, next_launch]()
        {
            std::this_thread::sleep_until(next_launch);

            if (exiting_wspr.load(std::memory_order_acquire) ||
                generation != non_wspr_schedule_generation.load(std::memory_order_acquire))
            {
                return;
            }

            const ArgParserConfig scheduled_config = config;
            if (!start_non_wspr_transmission_now(scheduled_config))
            {
                request_wspr_shutdown("non-WSPR scheduled transmission setup failed");
            }
        })
        .detach();
}

/**
 * @brief Build the scheduler-side request for one WSPR execution slot.
 *
 * The request captures all execution-time state, including the prepared
 * WSPR frame for this slot, the committed RF frequency, and the original
 * scheduler frequency-entry label used for diagnostics.
 */
static TransmissionRequest make_wspr_request(
    const ArgParserConfig &cfg,
    double committed_ppm,
    const PreparedWsprTransmission &slot_plan,
    double dial_frequency_hz,
    double actual_rf_frequency_hz,
    const WsprFrequencyEntry &entry,
    double applied_offset_hz)
{
    TransmissionRequest request;
    request.mode = TransmissionMode::WSPR;
    request.payload = slot_plan;
    request.dial_frequency_hz = dial_frequency_hz;
    request.actual_rf_frequency_hz = actual_rf_frequency_hz;
    request.ppm = committed_ppm;
    request.power_level = cfg.power_level;
    request.tx_gpio = cfg.tx_pin;
    request.use_offset = cfg.use_offset;
    request.applied_offset_hz = applied_offset_hz;
    request.frequency_entry_label = entry.token;
    request.allow_unqualified_frequency = cfg.allow_unqualified_frequency;
    request.allow_non_amateur_frequency = cfg.allow_non_amateur_frequency;
    request.hardware_profile = to_controller_profile(cfg.transmit_backend);
    return request;
}

TransmissionRequest make_skip_window_request(
    const ArgParserConfig &cfg,
    double committed_ppm,
    double dial_frequency_hz,
    const WsprFrequencyEntry &entry)
{
    TransmissionRequest request;
    request.mode = TransmissionMode::WSPR;
    request.dial_frequency_hz = dial_frequency_hz;
    request.actual_rf_frequency_hz = 0.0;
    request.ppm = committed_ppm;
    request.power_level = cfg.power_level;
    request.tx_gpio = cfg.tx_pin;
    request.frequency_entry_label = entry.token;
    request.skip_window = true;
    return request;
}

void commit_band_gpio_snapshot_to_request(
    TransmissionRequest &request,
    const BandGPIOResolution &resolution,
    BandGPIOPrepareStatus prepare_status) noexcept
{
    request.selector_gpio_enabled = false;
    request.selector_band = HamBand::BAND_2200M;
    request.selector_gpio_config = BandGPIOConfig{};

    if (prepare_status != BandGPIOPrepareStatus::Prepared)
    {
        return;
    }

    if (!resolution.selector_enabled)
    {
        return;
    }

    request.selector_gpio_enabled =
        resolution.config.enabled && resolution.config.gpio >= 0;
    request.selector_band = resolution.band;
    request.selector_gpio_config = resolution.config;
}

/**
 * @brief Build the next committed WSPR request for the current slot.
 *
 * This is scheduler policy code. It chooses Auto versus RequirePaired,
 * records paired continuation state, and builds exactly one slot-scoped
 * execution request. If a paired plan spans multiple slots, later slots
 * reuse the saved scheduler plan instead of making a new planning choice.
 *
 * @param actual_rf_frequency_hz RF frequency already chosen by the scheduler.
 * @param request_out Receives the committed request snapshot for one slot.
 * @return `true` if the request was built successfully.
 */
bool configure_current_wspr_transmission(
    const ArgParserConfig &cfg,
    double committed_ppm,
    double dial_frequency_hz,
    const WsprFrequencyEntry &frequency_entry,
    PreparedWsprTransmission &active_plan,
    std::size_t &active_frame_index,
    double &active_plan_dial_frequency,
    WsprFrequencyEntry &active_plan_frequency_entry,
    bool &active_plan_in_progress,
    double actual_rf_frequency_hz,
    TransmissionRequest &request_out)
{
    try
    {
        PreparedWsprTransmission plan;
        PreparedWsprTransmission slot_plan;
        const wspr::TransmissionPlanPreference preference =
            wspr_planner_preference_to_plan_preference(
                cfg.wspr.planner_preference);
        bool auto_upgraded = false;

        if (active_plan_in_progress)
        {
            // Continue the already selected paired plan. This reuses saved
            // scheduler state and does not invoke a new planning policy
            // decision for the second slot.
            plan = active_plan;
            slot_plan = slot_plan_for_frame(plan, active_frame_index);

            llog.logS(INFO,
                      "Scheduling paired WSPR frame ",
                      static_cast<int>(active_frame_index + 1U),
                      " of ",
                      static_cast<int>(plan.frameCount()),
                      " for the next WSPR slot.");
        }
        else
        {
            if (cfg.wspr.planner_preference == WsprPlannerPreference::RequirePaired)
            {
                llog.logS(INFO,
                          "Paired WSPR planning explicitly requested.");

                plan = build_prepared_wspr_transmission(
                    cfg.wspr.callsign,
                    cfg.wspr.grid_square,
                    cfg.wspr.power_dbm,
                    wspr::TransmissionPlanPreference::RequirePaired);
            }
            else
            {
                const bool paired_upgrade_eligible =
                    is_auto_paired_upgrade_eligible(cfg);

                if (cfg.wspr.planner_preference == WsprPlannerPreference::PreferPaired)
                {
                    llog.logS(INFO,
                              "Paired WSPR planning preferred when available.");
                }

                try
                {
                    plan = build_prepared_wspr_transmission(
                        cfg.wspr.callsign,
                        cfg.wspr.grid_square,
                        cfg.wspr.power_dbm,
                        preference);
                }
                catch (const std::exception &)
                {
                    if (!paired_upgrade_eligible)
                        throw;

                    llog.logS(
                        INFO,
                        "Auto-upgrading to paired WSPR plan because "
                        "callsign is compound and locator is 6 characters.");

                    PreparedWsprTransmission paired_plan =
                        build_prepared_wspr_transmission(
                            cfg.wspr.callsign,
                            cfg.wspr.grid_square,
                            cfg.wspr.power_dbm,
                            wspr::TransmissionPlanPreference::RequirePaired);

                    plan = std::move(paired_plan);
                    auto_upgraded = true;
                }

                if (cfg.wspr.planner_preference == WsprPlannerPreference::Auto &&
                    !auto_upgraded &&
                    plan.frameCount() <= 1U &&
                    paired_upgrade_eligible)
                {
                    llog.logS(
                        INFO,
                        "Auto-upgrading to paired WSPR plan because "
                        "callsign is compound and locator is 6 characters.");

                    PreparedWsprTransmission paired_plan =
                        build_prepared_wspr_transmission(
                            cfg.wspr.callsign,
                            cfg.wspr.grid_square,
                            cfg.wspr.power_dbm,
                            wspr::TransmissionPlanPreference::RequirePaired);

                    if (paired_plan.frameCount() > 1U)
                    {
                        plan = std::move(paired_plan);
                        auto_upgraded = true;
                    }
                }
            }

            if (plan.frameCount() > 1U)
            {
                active_plan = plan;
                active_frame_index = 0;
                active_plan_dial_frequency = dial_frequency_hz;
                active_plan_frequency_entry = frequency_entry;
                active_plan_in_progress = true;
            }
            else
            {
                active_plan = PreparedWsprTransmission{};
                active_frame_index = 0;
                active_plan_dial_frequency = 0.0;
                active_plan_frequency_entry = WsprFrequencyEntry{};
                active_plan_in_progress = false;
            }

            slot_plan = slot_plan_for_frame(plan, active_frame_index);
        }

        llog.logS(DEBUG, "Selected WSPR plan.");

        llog.logS(DEBUG,
                  "- Plan type: ",
                  plan.plan_type);

        llog.logS(DEBUG,
                  "- Frames: ",
                  static_cast<int>(plan.frames.size()));

        llog.logS(DEBUG,
                  "- Preference: ",
                  wspr_planner_preference_to_string(cfg.wspr.planner_preference));

        llog.logS(DEBUG,
                  "- Auto-upgraded: ",
                  auto_upgraded ? "true" : "false");

        request_out = make_wspr_request(
            cfg,
            committed_ppm,
            slot_plan,
            dial_frequency_hz,
            actual_rf_frequency_hz,
            frequency_entry,
            0.0);

        std::string development_error;
        if (!apply_direct_rp1_development_confirmation(
                cfg, request_out, &development_error))
        {
            throw std::runtime_error(development_error);
        }

        if (request_out.rp1_development.enabled)
        {
            llog.logS(
                INFO,
                "RP1 confirmation accepted and bounded positional WSPR frame request prepared for operation ",
                request_out.rp1_development.operation_id,
                ".");
        }

        if (plan.frameCount() > 1U)
        {
            llog.logS(
                DEBUG,
                "Prepared paired WSPR transmission with ",
                static_cast<int>(plan.frameCount()),
                " frames using plan ",
                plan.plan_type,
                ".");
        }

        return true;
    }
    catch (const std::exception &e)
    {
        active_plan = PreparedWsprTransmission{};
        active_frame_index = 0;
        active_plan_dial_frequency = 0.0;
        active_plan_frequency_entry = WsprFrequencyEntry{};
        active_plan_in_progress = false;
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        llog.logE(ERROR, "WSPR encoding/configuration failed: ", e.what());
        return false;
    }
}

bool request_wspr_shutdown(std::string_view reason)
{
    const bool already_requested =
        exiting_wspr.exchange(true, std::memory_order_seq_cst);

    if (!reason.empty())
    {
        if (already_requested)
        {
            llog.logS(INFO,
                      "Shutdown already in progress; duplicate request: ",
                      reason);
        }
        else
        {
            llog.logS(INFO, "Shutdown requested: ", reason);
        }
    }

    {
        std::lock_guard<std::mutex> lk(exitwspr_mtx);
        exitwspr_ready = true;
    }
    exitwspr_cv.notify_one();

    return !already_requested;
}

static std::string runtime_mode_to_string(
    wsprrypi::TransmissionMode mode)
{
    switch (mode)
    {
    case wsprrypi::TransmissionMode::WSPR:
        return "WSPR";
    case wsprrypi::TransmissionMode::QRSS:
        return "QRSS";
    case wsprrypi::TransmissionMode::FSKCW:
        return "FSKCW";
    case wsprrypi::TransmissionMode::DFCW:
        return "DFCW";
    case wsprrypi::TransmissionMode::CW:
        return "CW";
    case wsprrypi::TransmissionMode::TONE:
        return "TONE";
    default:
        return "";
    }
}

static double first_configured_wspr_dial_frequency_hz() noexcept
{
    for (const WsprFrequencyEntry &entry : config.wspr_frequency_entries)
    {
        if (entry.dial_frequency_hz > 0.0)
        {
            return entry.dial_frequency_hz;
        }
    }

    return 0.0;
}

WsprRuntimeStatusSnapshot current_tx_runtime_status_snapshot()
{
    std::lock_guard<std::mutex> lk(set_config_mtx);

    WsprRuntimeStatusSnapshot snapshot;
    snapshot.transmit_backend = transmit_backend_kind_to_string(config.transmit_backend);
    const std::string configured_rp1_route =
        config.gpio_tx_pin == 4 ? "GPIO4" :
        config.gpio_tx_pin == 20 ? "GPIO20" : "unavailable";
    snapshot.rp1_route_requested = configured_rp1_route;
    snapshot.rp1_route_persisted = configured_rp1_route;
    snapshot.rp1_route_configured = configured_rp1_route;
    // Active provider identity and cleanup are populated only by the bounded
    // route API. Do not infer them from configuration or package presence.
    snapshot.rp1_route_active = "unavailable";
    snapshot.rp1_eligibility = "unknown";
    snapshot.rp1_cleanup_state = "unknown";
    snapshot.rp1_journal_state = rp1_route_transaction_inhibited_state()
        ? "unresolved"
        : "none-reported";
    if (config.transmit_backend == TransmitBackendKind::RP1_GPCLK)
    {
        const auto route = query_rp1_route_status();
        snapshot.rp1_route_requested = route.requested;
        snapshot.rp1_route_persisted = route.persisted;
        snapshot.rp1_route_configured = route.configured;
        snapshot.rp1_route_active = route.active;
        snapshot.rp1_eligibility = route.eligible ? "eligible" : "unavailable";
        snapshot.rp1_journal_state = route.journal;
    }
    {
        std::lock_guard<std::mutex> correction_lock(frequency_estimate_mutex);
        snapshot.frequency_estimate_qualification = to_string(current_gpio_correction.qualification);
        snapshot.frequency_estimate_provider = current_gpio_correction.provider_name;
        snapshot.frequency_estimate_provenance = current_gpio_correction.source_provenance;
        snapshot.frequency_correction_mode = to_string(current_gpio_correction.mode);
        snapshot.frequency_estimate_reason = current_gpio_correction.reason;
        snapshot.frequency_estimate_ppm_available = current_gpio_correction.estimate_ppm.has_value();
        snapshot.frequency_estimate_ppm = current_gpio_correction.estimate_ppm.value_or(0.0);
        snapshot.gpio_frequency_residual_ppm = current_gpio_correction.residual_ppm;
        snapshot.effective_gpio_ppm = current_gpio_correction.effective_ppm;
        snapshot.additional_gpio_ppm = current_gpio_correction.additional_ppm;
        snapshot.frequency_estimate_age_seconds = current_gpio_correction.estimate_age_seconds;
        snapshot.gpio_correction_candidate = current_gpio_candidate_provenance;
        snapshot.gpio_correction_committed = committed_gpio_correction_provenance;
    }
    snapshot.tx_state = transmitter_state_string_lower(
        transmitter_state());
    snapshot.gpio_correction_committed.active =
        snapshot.gpio_correction_committed.available &&
        snapshot.tx_state == "transmitting";
    if (current_transmission_request.actual_rf_frequency_hz <= 0.0 ||
        current_transmission_request.isSkipWindow())
    {
        snapshot.gpio_correction_committed = {};
    }
    const auto runtime_status = transmitter_runtime_status();
    if (snapshot.tx_state == "transmitting")
    {
        snapshot.runtime_mode = runtime_mode_to_string(runtime_status.mode);
    }
    else
    {
        snapshot.runtime_mode = mode_type_name(config.mode);
    }
    snapshot.cw_message = runtime_status.cw_message;
    snapshot.cw_active_char_index = runtime_status.cw_active_char_index;
    snapshot.selector_gpio_enabled =
        current_transmission_request.hasSelectorGPIO();
    if (snapshot.selector_gpio_enabled)
    {
        snapshot.selector_gpio = current_transmission_request.selector_gpio_config.gpio;
        snapshot.selector_gpio_active_high =
            current_transmission_request.selector_gpio_config.active_high;
    }

    if (config.mode == ModeType::WSPR)
    {
        snapshot.power_dbm = config.wspr.power_dbm;
        snapshot.frequency_is_skip =
            current_transmission_request.isSkipWindow() ||
            (current_dial_frequency == 0.0 &&
             !current_frequency_entry.token.empty());
        if (snapshot.frequency_is_skip)
        {
            snapshot.frequency_hz = 0.0;
            snapshot.offset_hz = 0.0;
        }
        else
        {
            snapshot.frequency_hz = current_transmission_request.dial_frequency_hz;
            if (snapshot.frequency_hz <= 0.0)
            {
                snapshot.frequency_hz = current_dial_frequency;
            }
            if (snapshot.frequency_hz <= 0.0)
            {
                snapshot.frequency_hz = first_configured_wspr_dial_frequency_hz();
            }
            snapshot.offset_hz = current_transmission_request.applied_offset_hz;
        }
    }
    else if (config.mode == ModeType::QRSS)
    {
        snapshot.frequency_hz = config.qrss.frequency_hz;
    }
    else if (config.mode == ModeType::FSKCW)
    {
        snapshot.frequency_hz = config.fskcw.space_frequency_hz;
        snapshot.offset_hz =
            config.fskcw.mark_frequency_hz - config.fskcw.space_frequency_hz;
    }
    else if (config.mode == ModeType::DFCW)
    {
        snapshot.frequency_hz = config.dfcw.dot_frequency_hz;
        snapshot.offset_hz =
            config.dfcw.dash_frequency_hz - config.dfcw.dot_frequency_hz;
    }

    if (is_non_wspr_runtime_mode(config.mode) &&
        runtime_transmit_enabled(config) &&
        config.schedule_repeat_minutes > 0)
    {
        snapshot.next_transmission_at =
            format_local_schedule_time(next_non_wspr_schedule_time(config));
    }

    if (config.mode != ModeType::WSPR ||
        current_transmission_request.mode != TransmissionMode::WSPR ||
        current_transmission_request.payload.empty())
    {
        return snapshot;
    }

    const PreparedWsprTransmission &plan = current_transmission_request.payload;
    snapshot.plan_type = plan.plan_type;
    snapshot.power_dbm = plan.power_dbm;
    snapshot.callsign_raw = plan.callsign_raw;
    snapshot.callsign_normalized =
        !plan.callsign_normalized.empty() ? plan.callsign_normalized : plan.callsign;
    snapshot.locator_raw = plan.locator_raw;
    snapshot.locator_normalized =
        !plan.locator_normalized.empty() ? plan.locator_normalized : plan.locator;

    if (active_wspr_plan_in_progress && !active_wspr_plan.empty())
    {
        const PreparedWsprTransmission &active_plan = active_wspr_plan;
        snapshot.frame_count =
            active_plan.total_frame_count != 0U
                ? active_plan.total_frame_count
                : active_plan.frameCount();
        snapshot.current_frame = active_wspr_frame_index + 1U;
        if (active_wspr_frame_index < active_plan.frame_callsigns.size())
        {
            snapshot.frame_callsign =
                active_plan.frame_callsigns.at(active_wspr_frame_index);
        }
        else
        {
            snapshot.frame_callsign = snapshot.callsign_normalized;
        }
        if (active_wspr_frame_index < active_plan.frame_locators.size())
        {
            snapshot.frame_locator =
                active_plan.frame_locators.at(active_wspr_frame_index);
        }
        else
        {
            snapshot.frame_locator = snapshot.locator_normalized;
        }
        return snapshot;
    }

    snapshot.frame_count =
        plan.total_frame_count != 0U ? plan.total_frame_count : plan.frameCount();
    snapshot.current_frame = plan.current_frame;
    snapshot.frame_callsign =
        !plan.frame_callsign.empty() ? plan.frame_callsign : snapshot.callsign_normalized;
    snapshot.frame_locator =
        !plan.frame_locator.empty() ? plan.frame_locator : snapshot.locator_normalized;
    return snapshot;
}

/**
 * @brief Return the next configured scheduler frequency entry.
 *
 * The scheduler owns round-robin traversal of configured frequency entries,
 * using the returned source frequency for band-selector policy. When `reset`
 * is true, the next returned entry is the first configured slot.
 *
 * @param reset True to restart from the first configured entry.
 * @return The next configured entry, or a default-constructed entry if none
 *         are configured.
 */
WsprFrequencyEntry next_frequency_entry(bool reset)
{
    return next_frequency_entry_from(
        config.wspr_frequency_entries,
        freq_iterator,
        reset);
}

/**
 * @brief Reload scheduler state and commit the next execution request.
 *
 * This function is the central orchestration path for startup, reload, PPM
 * updates, random WSPR offset application, paired-slot continuation, GPIO
 * selector preparation, and request commit. The transmitter receives only
 * the final committed request built here.
 */

bool managed_reload_tx_inhibited_for_test() noexcept
{
    return managed_reload_tx_inhibited;
}

bool managed_reload_tx_inhibited_state() noexcept
{
    return managed_reload_tx_inhibited;
}

void set_rp1_route_transaction_inhibited(bool inhibited) noexcept
{
    rp1_route_transaction_inhibited.store(inhibited, std::memory_order_release);
}

bool rp1_route_transaction_inhibited_state() noexcept
{
    return rp1_route_transaction_inhibited.load(std::memory_order_acquire);
}

wsprrypi::Rp1GpclkApplicationIdleState rp1_gpclk_application_idle_state() noexcept
{
    wsprrypi::Rp1GpclkApplicationIdleState state;
    const auto current_transmitter_state = transmitter_state();
    state.controller_prepared = config.transmit;
    const bool controller_quiescent =
        current_transmitter_state == WsprTransmitState::DISABLED ||
        current_transmitter_state == WsprTransmitState::COMPLETE ||
        current_transmitter_state == WsprTransmitState::CANCELLED;
    state.execution_active = !controller_quiescent || web_test_tone.load(std::memory_order_acquire);
    state.schedule_committed = config.transmit || !active_wspr_plan.frames.empty();
    state.stop_or_drain_active = exiting_wspr.load(std::memory_order_acquire);
    state.cancellation_or_cleanup_active = false;
    state.provider_lease_active = !controller_quiescent;
    state.backend_transaction_active = !controller_quiescent;
    state.shutdown_or_restart_active = shutdown_flag.load(std::memory_order_acquire) ||
        reboot_flag.load(std::memory_order_acquire);
    return state;
}

CommittedExecutionRouteForTest committed_execution_route_for_test() noexcept
{
    return committed_execution_route_for_test_storage;
}

void reset_committed_execution_route_for_test() noexcept
{
    committed_execution_route_for_test_storage =
        CommittedExecutionRouteForTest::NONE;
}

std::size_t tx_led_assert_request_count_for_test() noexcept
{
    return tx_led_assert_request_count_for_test_storage.load(
        std::memory_order_relaxed);
}

std::size_t tx_led_deassert_request_count_for_test() noexcept
{
    return tx_led_deassert_request_count_for_test_storage.load(
        std::memory_order_relaxed);
}

std::size_t tx_led_failure_count_for_test() noexcept
{
    return tx_led_failure_count_for_test_storage.load(
        std::memory_order_relaxed);
}

void reset_tx_led_request_counts_for_test() noexcept
{
    tx_led_assert_request_count_for_test_storage.store(
        0U,
        std::memory_order_relaxed);
    tx_led_deassert_request_count_for_test_storage.store(
        0U,
        std::memory_order_relaxed);
    tx_led_failure_count_for_test_storage.store(
        0U,
        std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(tx_led_state_mtx);
        tx_led_active = false;
    }
    tx_led_state_cv.notify_all();
}

bool tx_led_active_for_test() noexcept
{
    std::lock_guard<std::mutex> lk(tx_led_state_mtx);
    return tx_led_active;
}

bool reconcile_tx_led_after_transmitter_stop_for_test(const char *context) noexcept
{
    return reconcile_tx_led_after_transmitter_stop(context);
}

void reset_managed_reload_runtime_for_test() noexcept
{
    managed_reload_tx_inhibited = false;
}

void set_scheduler_execution_suppressed_for_test(bool suppressed) noexcept
{
    suppress_scheduler_execution_for_test = suppressed;
    if (suppressed)
    {
        selector_gpio_control_enabled = false;
        selector_gpio_drive_enabled = false;
        bandGPIOSelector.setEnabled(false);
        bandGPIOSelector.setDriveGPIO(false);
        stop_active_transmission_selectors();
        release_idle_selector_gpio_reservations();
    }
}

void reset_band_gpio_prepare_call_count_for_test() noexcept
{
    band_gpio_prepare_call_counter_for_test.store(0U, std::memory_order_relaxed);
}

std::size_t band_gpio_prepare_call_count_for_test() noexcept
{
    return band_gpio_prepare_call_counter_for_test.load(std::memory_order_relaxed);
}

void set_band_gpio_selector_for_test(bool enabled, bool drive_gpio) noexcept
{
    selector_gpio_control_enabled = enabled;
    selector_gpio_drive_enabled = drive_gpio;
    bandGPIOSelector.setEnabled(enabled);
    bandGPIOSelector.setDriveGPIO(drive_gpio);
    if (!enabled)
    {
        stop_active_transmission_selectors();
        release_idle_selector_gpio_reservations();
    }
}

bool current_band_gpio_selection_for_test(
    BandGPIOConfig &config_out,
    std::string &band_label_out) noexcept
{
    const BandGPIOConfig *current_config = bandGPIOSelector.currentConfig();
    const HamBand *current_band = bandGPIOSelector.currentBand();
    if (current_config == nullptr || current_band == nullptr)
    {
        config_out = BandGPIOConfig{};
        band_label_out.clear();
        return false;
    }

    config_out = *current_config;
    band_label_out = ham_band_to_string(*current_band);
    return true;
}

std::vector<BandGPIOConfig> initialized_selector_gpios_for_test()
{
    std::vector<BandGPIOConfig> configs;
    configs.reserve(idle_selector_gpio_reservations.size());
    for (const SelectorGPIOReservation &reservation :
         idle_selector_gpio_reservations)
    {
        configs.push_back(reservation.config);
    }
    return configs;
}

bool selector_gpio_logical_state_for_test(
    int gpio,
    bool &logical_state_out) noexcept
{
    const BandGPIOConfig *current_config = bandGPIOSelector.currentConfig();
    if (current_config != nullptr && current_config->gpio == gpio)
    {
        logical_state_out = bandGPIOSelector.isBandStateActive();
        return true;
    }

    const auto it = std::find_if(
        idle_selector_gpio_reservations.begin(),
        idle_selector_gpio_reservations.end(),
        [gpio](const SelectorGPIOReservation &reservation)
        {
            return reservation.config.gpio == gpio;
        });
    if (it != idle_selector_gpio_reservations.end())
    {
        logical_state_out = false;
        return true;
    }

    return false;
}

void stop_active_transmission_selectors_for_test() noexcept
{
    stop_active_transmission_selectors();
}

bool park_active_transmission_selectors_for_test() noexcept
{
    const BandGPIOConfig *active_config_ptr = bandGPIOSelector.currentConfig();
    if (active_config_ptr == nullptr)
    {
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Inactive,
            std::memory_order_release);
        return true;
    }

    const BandGPIOConfig active_config = *active_config_ptr;
    if (!bandGPIOSelector.setBandState(false))
    {
        return false;
    }

    std::unique_ptr<GPIOOutput> active_gpio =
        bandGPIOSelector.releaseGPIOReservation();
    active_band_gpio_prepare_status.store(
        BandGPIOPrepareStatus::Inactive,
        std::memory_order_release);

    if (!selector_gpio_control_enabled)
    {
        return true;
    }

    return append_idle_selector_gpio_reservation(
        active_config,
        std::move(active_gpio));
}

bool restore_committed_band_gpio_selection_for_test(bool assert_state) noexcept
{
    if (!refresh_committed_band_gpio_selection())
    {
        return false;
    }

    if (!assert_state || !current_transmission_request.hasSelectorGPIO())
    {
        return true;
    }

    return bandGPIOSelector.setBandState(true);
}

TransmissionRequest current_transmission_request_for_test()
{
    return current_transmission_request;
}

void set_current_transmission_request_for_test(
    const TransmissionRequest &request) noexcept
{
    current_transmission_request = request;
}

std::optional<wsprrypi::TransmissionRequest> current_controller_request_for_test()
{
    return current_controller_request_for_test_storage;
}

wsprrypi::TransmissionRequest controller_request_from_legacy_for_test(
    const TransmissionRequest &request,
    wsprrypi::TransmissionMode mode)
{
    return build_controller_request_from_legacy(request, mode);
}

std::vector<BandGPIOConfig> selector_shutdown_cleanup_targets_for_test()
{
    return last_selector_shutdown_cleanup_targets;
}

void seed_selector_shutdown_state_for_test(
    const BandGPIOConfig &active_config,
    const std::vector<BandGPIOConfig> &idle_configs) noexcept
{
    stop_active_transmission_selectors();
    release_idle_selector_gpio_reservations();

    bandGPIOSelector.setEnabled(true);
    bandGPIOSelector.setDriveGPIO(false);
    selector_gpio_control_enabled = true;
    selector_gpio_drive_enabled = false;

    if (active_config.enabled && active_config.gpio >= 0)
    {
        (void)bandGPIOSelector.prepareBand(HamBand::BAND_20M, active_config);
        active_band_gpio_prepare_status.store(
            BandGPIOPrepareStatus::Prepared,
            std::memory_order_release);
    }

    for (const BandGPIOConfig &idle_config : idle_configs)
    {
        if (!idle_config.enabled || idle_config.gpio < 0)
        {
            continue;
        }

        SelectorGPIOReservation reservation;
        reservation.config = idle_config;
        idle_selector_gpio_reservations.push_back(std::move(reservation));
    }
}

void run_final_selector_gpio_shutdown_cleanup_for_test() noexcept
{
    shutdown_all_configured_selector_gpios(config);
}

void clear_current_wspr_runtime_state_for_test() noexcept
{
    current_transmission_request = TransmissionRequest{};
    current_dial_frequency = 0.0;
    current_frequency_entry = WsprFrequencyEntry{};
}

void reset_current_transmission_request_for_test() noexcept
{
    current_transmission_request = TransmissionRequest{};
}

void set_current_frequency_estimate_for_test(
    const SystemClockFrequencyEstimate &estimate)
{
    std::lock_guard<std::mutex> lock(frequency_estimate_mutex);
    current_frequency_estimate = estimate;
    current_gpio_correction = GpioFrequencyCorrection{};
}

void reset_current_controller_request_for_test() noexcept
{
    current_controller_request_for_test_storage.reset();
}

void set_test_tone_commit_invoker_for_test(
    TestToneCommitInvokerForTest invoker)
{
    test_tone_commit_invoker_for_test = std::move(invoker);
}

void reset_test_tone_commit_invoker_for_test() noexcept
{
    test_tone_commit_invoker_for_test = {};
}

void set_direct_tone_start_invoker_for_test(
    DirectToneStartInvokerForTest invoker)
{
    direct_tone_start_invoker_for_test = std::move(invoker);
}

void reset_direct_tone_start_invoker_for_test() noexcept
{
    direct_tone_start_invoker_for_test = {};
}

void reset_rp1_development_reconcile_invoker_for_test() noexcept
{
    reset_rp1_development_reconcile_invoker_bridge();
    std::lock_guard<std::mutex> lock(direct_tone_confirmation_mtx);
    claimed_direct_tone_confirmation.reset();
}

bool start_direct_tone_execution_for_test(
    const ArgParserConfig &cfg,
    const WsprFrequencyEntry &entry,
    double actual_rf_frequency_hz,
    std::string *error_message)
{
    return start_direct_tone_execution(
        cfg,
        entry,
        actual_rf_frequency_hz,
        error_message);
}

void set_startup_quiesce_invoker_for_test(StartupQuiesceInvokerForTest invoker)
{
    startup_quiesce_invoker_for_test = std::move(invoker);
}

void reset_startup_quiesce_for_test() noexcept
{
    startup_quiesce_invoker_for_test = {};
    startup_quiesce_inhibited.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(startup_quiesce_error_mtx);
    startup_quiesce_error.clear();
}

bool run_startup_quiesce_gate_for_test(const ArgParserConfig &cfg)
{
    return run_startup_quiesce_gate(cfg);
}

bool startup_quiesce_inhibited_for_test() noexcept
{
    return startup_quiesce_inhibited_state();
}

std::string startup_quiesce_error_for_test()
{
    return startup_quiesce_error_state();
}

bool startup_quiesce_inhibited_state() noexcept
{
    return startup_quiesce_inhibited.load(std::memory_order_acquire);
}

std::string startup_quiesce_error_state()
{
    std::lock_guard<std::mutex> lock(startup_quiesce_error_mtx);
    return startup_quiesce_error;
}
