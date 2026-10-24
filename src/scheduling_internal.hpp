/**
 * @file scheduling_internal.hpp
 * @brief Private cross-unit scheduler state and helper contracts.
 */

#ifndef SCHEDULING_INTERNAL_HPP
#define SCHEDULING_INTERNAL_HPP

#include "scheduling.hpp"
#include "band_gpio_selector.hpp"
#include "system_clock_frequency_estimate.hpp"

struct BandGPIOResolution
{
    HamBand band = HamBand::BAND_2200M;
    BandGPIOConfig config{};
    bool selector_enabled = false;
    bool from_band_config = false;
    const char *selector_source = "none";
    bool band_known = true;
};

enum class BandGPIOPrepareStatus
{
    Inactive,
    Prepared,
    Failed
};

enum class TestToneRestorationOwner
{
    Unknown,
    WsprScheduler,
    DirectToneStartup,
    ManagedIdleNonWspr,
};

extern BandGPIOSelector bandGPIOSelector;
extern std::mutex set_config_mtx;
extern int freq_iterator;
extern double current_dial_frequency;
extern WsprFrequencyEntry current_frequency_entry;
extern TransmissionRequest current_transmission_request;
extern bool suppress_scheduler_execution_for_test;
extern std::atomic<bool> web_test_tone;
extern std::atomic<bool> suppress_cancelled_ws_event_for_user_stop;
extern ModeType lastMode;
extern TestToneRestorationOwner test_tone_restoration_owner;
extern bool selector_gpio_control_enabled;
extern bool selector_gpio_drive_enabled;
extern std::atomic<bool> shutdown_after_wspr_plan;
extern std::atomic<bool> shutdown_after_current_transmission;
extern std::atomic<std::uint64_t> non_wspr_schedule_generation;
// Nominal placement belongs to a prepared plan, independently of calibration.
// Presence is explicit: a randomized 0 Hz placement is valid.
struct WsprPlanPlacement
{
    double rf_center_hz = 0.0;
    double applied_offset_hz = 0.0;
};
extern std::optional<WsprPlanPlacement> active_wspr_plan_placement;
extern PreparedWsprTransmission active_wspr_plan;
extern std::size_t active_wspr_frame_index;
extern double active_wspr_plan_dial_frequency;
extern WsprFrequencyEntry active_wspr_plan_frequency_entry;
extern bool active_wspr_plan_in_progress;

BandGPIOPrepareStatus prepare_band_gpio_for_frequency_or_log(
    double, const WsprFrequencyEntry &, const ArgParserConfig &, int = -1,
    BandGPIOResolution * = nullptr);
bool sync_configured_selector_gpio_idle_state(
    const ArgParserConfig &, bool, std::string * = nullptr);
void commit_band_gpio_snapshot_to_request(
    TransmissionRequest &, const BandGPIOResolution &,
    BandGPIOPrepareStatus) noexcept;
bool stop_active_transmission_selectors(
    const ArgParserConfig * = nullptr, bool = false,
    std::string * = nullptr) noexcept;
void release_idle_selector_gpio_reservations() noexcept;
bool runtime_should_hold_selector_gpios_initialized(
    const ArgParserConfig &) noexcept;
bool has_configured_selector_gpios(const ArgParserConfig &) noexcept;
void deassert_transmit_gpio_outputs(
    const ArgParserConfig *, bool, const char *) noexcept;
void assert_transmit_gpio_outputs(const char *) noexcept;
void set_tx_led_state(bool, const char *) noexcept;
bool reconcile_tx_led_after_transmitter_stop(const char *) noexcept;
void shutdown_all_configured_selector_gpios(
    const ArgParserConfig &) noexcept;
void reset_active_wspr_plan_state();
bool active_wspr_plan_has_more_frames_after_current() noexcept;
bool is_managed_persistent_mode() noexcept;
bool is_non_wspr_runtime_mode(ModeType) noexcept;
bool has_non_wspr_cli_startup_request(ModeType) noexcept;
void log_scheduler_path_selection(ModeType);
void log_transmit_disabled_skip(const ArgParserConfig &);
bool runtime_transmit_requested(const ArgParserConfig &) noexcept;
bool runtime_transmit_enabled(const ArgParserConfig &) noexcept;
bool runtime_transmit_preparation_enabled(const ArgParserConfig &) noexcept;
bool managed_reload_generation_changed(std::uint64_t) noexcept;
void set_managed_reload_tx_inhibited(bool, std::string_view = {});
WsprFrequencyEntry next_frequency_entry_from(
    const std::vector<WsprFrequencyEntry> &, int &, bool);
WsprFrequencyEntry next_frequency_entry(bool reset);
double maybe_apply_wspr_random_offset(double, const ArgParserConfig &);
void schedule_next_non_wspr_launch(const ArgParserConfig &);
bool launch_scheduled_non_wspr_if_current(std::uint64_t);
bool start_non_wspr_transmission_now(const ArgParserConfig &,
    std::optional<std::chrono::system_clock::time_point> = std::nullopt);
TransmissionRequest make_skip_window_request(
    const ArgParserConfig &, double, double, const WsprFrequencyEntry &);
bool configure_current_wspr_transmission(
    const ArgParserConfig &, double, double, const WsprFrequencyEntry &,
    PreparedWsprTransmission &, std::size_t &, double &,
    WsprFrequencyEntry &, bool &, std::optional<WsprPlanPlacement> &,
    TransmissionRequest &);
void refresh_frequency_estimate_for_config();
GpioFrequencyCorrection select_and_publish_gpio_correction_for_config(
    const ArgParserConfig &);
void commit_execution_request(const TransmissionRequest &);
void clear_committed_execution_request() noexcept;
TransmissionRequest make_tone_request(
    const ArgParserConfig &, double, double, double,
    const WsprFrequencyEntry &);
bool scheduler_managed_transmission_active_for_test_tone() noexcept;
bool scheduler_managed_transmission_enabled_for_test_tone() noexcept;
void finalize_transmission_stop_cleanup(
    const ArgParserConfig *, bool, const char *, bool = false,
    bool = false);
bool start_direct_tone_execution(
    const ArgParserConfig &, const WsprFrequencyEntry &, double,
    std::string *);
bool run_startup_quiesce_gate(const ArgParserConfig &);
bool startup_quiesce_inhibited_state() noexcept;
std::string startup_quiesce_error_state();
void stop_runtime_components_for_process_exit() noexcept;
std::string active_gpio_log_suffix();
void consume_tx_iteration_if_needed();

#endif // SCHEDULING_INTERNAL_HPP
