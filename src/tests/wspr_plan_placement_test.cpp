#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "execution_plan_compiler.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wspr_transmit.hpp"
#include "wtp_integration/execution_plan.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace wtp = wsprrypi::wtp;

namespace {
unsigned checks = 0;
void check(bool condition, const std::string &message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void equal(double actual, double expected, const std::string &message) {
    check(std::abs(actual - expected) < 0.0000001, message + ": " + std::to_string(actual));
}
void event(WsprTransmissionCallbackEvent kind) {
    transmitter_cb(kind, WsprTransmitLogLevel::INFO, "placement test", 110.6);
}
void identity(bool paired) {
    config.callsign = config.wspr.callsign = paired ? "AA0NT/12" : "AA0NT";
    config.grid_square = config.wspr.grid_square = paired ? "EM18IG" : "EM18";
    config.wspr_planner_preference = config.wspr.planner_preference =
        paired ? WsprPlannerPreference::RequirePaired : WsprPlannerPreference::Auto;
}
struct Fixture {
    Fixture() {
        init_default_config();
        clear_current_wspr_runtime_state_for_test();
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_managed_reload_runtime_for_test();
        ini_reload_pending.store(false);
        ppm_reload_pending.store(false);
        exiting_wspr.store(false);
        shutdown_after_current_transmission.store(false);
        shutdown_after_wspr_plan.store(false);
        set_scheduler_execution_suppressed_for_test(true);
        set_band_gpio_selector_for_test(false, false);
        config.use_ini = false;
        config.mode = ModeType::WSPR;
        config.transmit = true;
        config.use_offset = true;
        config.use_system_clock_frequency_estimate = false;
        config.gpio_use_system_clock_frequency_estimate = false;
        config.transmit_backend = TransmitBackendKind::SIMULATED;
        config.simulated_backend_override = true;
        config.si5351_ppm = 0;
        config.use_led = config.use_amp = config.use_shutdown = false;
        config.power_dbm = config.wspr.power_dbm = 20;
        config.frequencies = config.wspr.frequencies = "20m";
        identity(true);
        resolve_backend_specific_config(config);
        set_frequencies(config);
        wsprTransmitter.selectBackend(wsprrypi::BackendKind::SIMULATED);
        config_to_json();
    }
    ~Fixture() {
        transmitter_stop_and_join();
        clear_current_wspr_runtime_state_for_test();
        reset_current_transmission_request_for_test();
        reset_current_controller_request_for_test();
        reset_managed_reload_runtime_for_test();
        set_scheduler_execution_suppressed_for_test(false);
        exiting_wspr.store(false);
        ini_reload_pending.store(false);
        ppm_reload_pending.store(false);
    }
};
struct Draws {
    std::vector<double> values;
    std::size_t calls = 0;
    double next() {
        check(calls < values.size(), "unexpected random draw");
        return values.at(calls++);
    }
};
wtp::Capabilities capabilities() {
    wtp::Capabilities c;
    c.profiles = {"rf-events/1"}; c.modes = {wtp::Mode::Wspr};
    c.frequency_ranges = {{1, std::numeric_limits<std::uint64_t>::max()}};
    c.max_events = 512; c.max_payload_bytes = 65536;
    c.max_job_duration_ns = 86'400'000'000'000ULL;
    c.maximum_arm_uncertainty_ns = 500'000'000;
    return c;
}
constexpr std::array<double, 4> plus37 = {
    14097134.802734375, 14097136.267578125, 14097137.732421875, 14097139.197265625};
constexpr std::array<double, 4> minus21 = {
    14097076.802734375, 14097078.267578125, 14097079.732421875, 14097081.197265625};
void verify(double offset, unsigned frame, const std::array<double, 4> &tones,
            bool convert = true) {
    const auto request = current_transmission_request_for_test();
    equal(request.dial_frequency_hz, 14095600, "committed dial");
    equal(request.applied_offset_hz, offset, "committed placement");
    equal(request.actual_rf_frequency_hz, 14097100 + offset, "committed center");
    check(request.payload.frameCount() == 1 && request.payload.total_frame_count == 1 &&
          request.payload.current_frame == 1, "slot request has exactly one executable frame");
    const auto status = current_tx_runtime_status_snapshot();
    equal(status.frequency_hz, 14095600, "status dial");
    equal(status.offset_hz, offset, "status placement");
    check(status.current_frame == frame, "overall frame identity");
    check(status.frame_callsign == (frame == 2 ? "<AA0NT/12>" : config.wspr.callsign),
          "encoded frame callsign identity");
    const auto controller = current_controller_request_for_test();
    check(controller.has_value(), "production commit retains controller request");
    check(controller->metadata.origin == "scheduler" &&
          controller->output.backend == (config.transmit_backend == TransmitBackendKind::GPIO
              ? wsprrypi::BackendKind::RPI_CLOCK_GPIO : wsprrypi::BackendKind::SIMULATED),
          "backend and origin metadata retained");
    const auto plan = wsprrypi::ExecutionPlanCompiler{}.compile(*controller);
    check(plan.events.size() == 162 && plan.summary.total_duration.count() == 110591999892LL,
          "compiler retains one slot and exact duration");
    equal(plan.reference_frequency_hz, 14097100 + offset, "nominal compiler center");
    check(plan.request_id.value == controller->id.value && plan.backend == controller->output.backend,
          "compiler identities and backend preserved");
    for (std::size_t i = 0; i < plan.events.size(); ++i) {
        const auto &e = plan.events[i];
        equal(e.frequency_hz, tones.at(request.payload.frames[0].symbols[i]),
              "symbol selects independent expected tone grid");
        check(e.rf_on && e.offset_from_start.count() == static_cast<long long>(i) * 682666666 &&
              e.duration.count() == 682666666, "RF gate and symbol timeline preserved");
    }
    if (!convert) return;
    const auto wire = wsprrypi::prepare_wtp_plan(plan, capabilities(),
        {std::string(32, 'a'), 1'800'000'000'000'000'000ULL, 1000});
    check(bool(wire), "actual scheduler slot converts to WTP: " + wire.explanation);
    check(wire.prepared->job.events.size() == 162 &&
          wire.prepared->job.total_duration_ns == 110591999892ULL &&
          wire.prepared->request_id.value == plan.request_id.value,
          "WTP identity, frame count and duration retained");
    // Independent integer constants; no second dial-to-center/random conversion.
    const std::array<std::uint64_t, 4> nhz = offset == 37
        ? std::array<std::uint64_t, 4>{14097134802734375ULL,14097136267578125ULL,14097137732421875ULL,14097139197265625ULL}
        : std::array<std::uint64_t, 4>{14097076802734375ULL,14097078267578125ULL,14097079732421875ULL,14097081197265625ULL};
    for (std::size_t i = 0; i < 162; ++i) {
        const auto &e = wire.prepared->job.events[i];
        check(e.rf_on && e.frequency_nhz == nhz.at(request.payload.frames[0].symbols[i]) &&
              e.offset_ns == i * 682666666ULL && e.duration_ns == 682666666ULL,
              "WTP events remain absolute with exact timing");
    }
}
void pair_and_independent() {
    Fixture f; Draws draws{{37, -21, -21, 0}};
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    check(set_config(true), "prepare pair"); verify(37, 1, plus37);
    check(set_config(), "re-enter first-frame preparation"); verify(37, 1, plus37);
    for (int i = 0; i < 3; ++i) (void)current_tx_runtime_status_snapshot();
    check(draws.calls == 1, "first frame re-entry/status consumes no draw");
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(37, 2, plus37);
    check(set_config(), "re-enter second-frame preparation"); verify(37, 2, plus37);
    check(draws.calls == 1, "pair consumes one draw");
    identity(false);
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(-21, 1, minus21);
    check(draws.calls == 2 && !active_wspr_plan_in_progress, "next independent single draws once");
    identity(true);
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(-21, 1, minus21);
    check(draws.calls == 3 && active_wspr_plan_in_progress, "fresh pair may repeat numerical draw");
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(-21, 2, minus21);
    shutdown_after_wspr_plan.store(true);
    event(WsprTransmissionCallbackEvent::COMPLETE);
    check(!active_wspr_plan_placement && !active_wspr_plan_in_progress,
          "pair completion releases placement before shutdown");
    exiting_wspr.store(false);
    check(set_config(), "prepare valid zero-placement pair");
    equal(current_transmission_request_for_test().applied_offset_hz, 0, "zero placement is valid");
    event(WsprTransmissionCallbackEvent::COMPLETE);
    equal(current_transmission_request_for_test().actual_rf_frequency_hz, 14097100, "zero retained for frame two");
    check(draws.calls == 4 && active_wspr_plan_placement.has_value(), "zero is not absence sentinel");
    std::cout << "Pair +37 Hz: center 14097137, both grids verified; next -21 Hz: center 14097079; one draw per plan\n";
}
void disabled_single_and_rotation() {
    Fixture f; Draws draws{{37, -21}};
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    config.use_offset = false;
    check(set_config(true), "disabled pair");
    event(WsprTransmissionCallbackEvent::COMPLETE);
    equal(current_transmission_request_for_test().actual_rf_frequency_hz, 14097100, "disabled center");
    identity(false); event(WsprTransmissionCallbackEvent::COMPLETE);
    event(WsprTransmissionCallbackEvent::COMPLETE);
    check(draws.calls == 0, "disabled pair and independent singles never call provider");
    config.use_offset = true;
    config.frequencies = config.wspr.frequencies = "20m 40m"; set_frequencies(config);
    identity(true); check(set_config(true), "rotating pair");
    check(set_config(), "first-frame re-entry retains dial with rotation");
    equal(current_transmission_request_for_test().dial_frequency_hz, 14095600, "re-entry cannot rotate active plan");
    event(WsprTransmissionCallbackEvent::COMPLETE);
    equal(current_transmission_request_for_test().dial_frequency_hz, 14095600, "pair retains band");
    identity(false); event(WsprTransmissionCallbackEvent::COMPLETE);
    equal(current_transmission_request_for_test().dial_frequency_hz, 7038600, "next plan rotates normally");
    check(draws.calls == 2, "one draw per rotating plan");
}
void cancellation_skips_and_modes() {
    Fixture f; Draws draws{{37,-21,37,-21,37,-21,37}};
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    check(set_config(true), "prepare before cancellation");
    event(WsprTransmissionCallbackEvent::CANCELLED);
    check(!active_wspr_plan_placement && !active_wspr_plan_in_progress, "cancel releases pair; no orphan frame");
    check(set_config(), "prepare after cancellation"); verify(-21, 1, minus21);
    config.frequencies = config.wspr.frequencies = "0 20m"; set_frequencies(config);
    check(set_config(true), "forced skip entry");
    check(!active_wspr_plan_placement && current_transmission_request_for_test().isSkipWindow(),
          "zero entry releases placement");
    event(WsprTransmissionCallbackEvent::SKIPPED); verify(37, 1, plus37);
    // A failed execution follows the existing inhibition/cleanup path.
    event(WsprTransmissionCallbackEvent::FAILED);
    check(!active_wspr_plan_placement, "execution failure releases snapshot");
    exiting_wspr.store(false); config.transmit = true;
    config.frequencies = config.wspr.frequencies = "20m"; set_frequencies(config);
    check(set_config(true), "recovery fresh pair"); verify(-21, 1, minus21);
    config.mode = ModeType::QRSS; config.transmit = false;
    config.qrss = {"E", 14096900, 1};
    check(set_config(true), "mode change");
    check(!active_wspr_plan_placement, "mode change abandons snapshot");
    config.mode = ModeType::WSPR; config.transmit = true;
    check(set_config(true), "pair before Test Tone"); verify(37, 1, plus37);
    const auto count = draws.calls;
    config.use_ini = true; // The web Test Tone workflow belongs to managed operation.
    check(stop_transmission_by_user_request(false).transmit_disabled, "stop before Test Tone");
    const auto tone = start_test_tone({TestToneRequestSource::WsprBand, "20m", {}});
    check(tone.started, "web band Test Tone starts");
    equal(current_transmission_request_for_test().actual_rf_frequency_hz, 14097100, "Test Tone no placement");
    check(draws.calls == count, "Test Tone consumes no random draw");
    check(end_test_tone().stopped, "Test Tone restoration");
    check(!active_wspr_plan_placement && draws.calls == count,
          "Test Tone restoration cannot revive a stopped pair");
    config.use_ini = false;
    reset_managed_reload_runtime_for_test();
    config.transmit = true;
    check(set_config(true), "enable after Test Tone prepares a fresh plan");
    verify(-21, 1, minus21);
    check(draws.calls == count + 1, "enable prepares a fresh plan");
    stop_runtime_components_for_process_exit();
    check(!active_wspr_plan_placement, "shutdown releases placement");
    exiting_wspr.store(false);
    check(set_config(true), "restart prepares a fresh pair"); verify(37, 1, plus37);
}
void failures_and_scoped_provider() {
    Fixture f; Draws draws{{37,-21,37,-21}};
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    check(set_config(true), "committed baseline"); verify(37, 1, plus37);
    config.wspr.callsign = "!invalid!";
    check(!set_config(true), "planning failure rejected");
    check(draws.calls == 1, "failed encoding draws no placement");
    equal(active_wspr_plan_placement->applied_offset_hz, 37, "planning candidate cannot corrupt committed placement");
    identity(true); config.transmit = true;
    WsprTransmitter::SimulatedRuntimeConfig simulation; simulation.fail_configure = true;
    wsprTransmitter.selectBackend(wsprrypi::BackendKind::SIMULATED, {}, simulation);
    set_scheduler_execution_suppressed_for_test(false);
    bool failed = false;
    try { (void)set_config(true); } catch (const std::runtime_error &) { failed = true; }
    set_scheduler_execution_suppressed_for_test(true);
    check(failed, "first-frame backend preparation failure injected");
    verify(37, 1, plus37);
    equal(active_wspr_plan_placement->applied_offset_hz, 37, "failed first candidate not published");
    // The first committed plan still owns +37; failure on its continuation must
    // publish neither pair progress nor the rejected backend request.
    set_scheduler_execution_suppressed_for_test(false); failed = false;
    try { event(WsprTransmissionCallbackEvent::COMPLETE); }
    catch (const std::runtime_error &) { failed = true; }
    set_scheduler_execution_suppressed_for_test(true);
    check(failed && draws.calls == 2, "second-frame preparation fails without another draw");
    verify(37, 1, plus37);
    equal(active_wspr_plan_placement->applied_offset_hz, 37, "second-frame failure preserves request metadata");
    event(WsprTransmissionCallbackEvent::CANCELLED);
    wsprTransmitter.selectBackend(wsprrypi::BackendKind::SIMULATED);
    check(set_config(true), "explicit retry after cancellation"); verify(37, 1, plus37);
    // Exception unwinding restores a nested override even when a test fails.
    try {
        ScopedWsprRandomPlacementForTest nested([]() -> double { throw std::runtime_error("test failure"); });
        check(!set_config(true), "provider exception is a preparation failure");
        throw std::runtime_error("unwind case");
    } catch (const std::runtime_error &) {}
    config.transmit = true;
    check(set_config(true), "outer provider restored after failure"); verify(-21, 1, minus21);
}
void reloads_and_superseded_candidate() {
    Fixture f; Draws draws{{37,-21,37,-21,37}};
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    char name[] = "/tmp/wsprrypi-placement-XXXXXX";
    const int fd = mkstemp(name); check(fd >= 0, "temporary INI"); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } remove{name};
    config.ini_filename = name; iniFile.set_filename(name);
    config.use_ini = true;
    config_to_json(); json_to_ini();
    check(set_config(true), "pair before managed reload");
    // Existing policy defers during a transmitting frame, then replaces the
    // remaining pair at completion rather than resuming its second frame.
    wsprTransmitter.backendSetStateValue(WsprTransmitState::TRANSMITTING);
    ini_reload_generation.fetch_add(1); ini_reload_pending.store(true);
    check(set_config(), "reload deferred");
    verify(37, 1, plus37); check(draws.calls == 1, "deferred candidate does not draw");
    wsprTransmitter.backendSetStateValue(WsprTransmitState::DISABLED);
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(-21, 1, minus21);
    check(draws.calls == 2 && !ini_reload_pending.load(), "deferred reload replaces plan with a fresh draw");
    const auto valid_data = iniFile.getData();
    // Force replaces the plan. Supersede the first candidate after its draw;
    // only the next candidate's placement may commit.
    {
        ScopedWsprRandomPlacementForTest supersede([&] {
            const auto value = draws.next();
            equal(active_wspr_plan_placement->applied_offset_hz, -21, "candidate has not published placement");
            if (draws.calls == 3) ini_reload_generation.fetch_add(1);
            return value;
        });
        ini_reload_generation.fetch_add(1); ini_reload_pending.store(true);
        check(set_config(true), "superseded reload retries transaction");
    }
    verify(-21, 1, minus21);
    check(draws.calls == 4, "superseded draw discarded; replacement commits");
    auto invalid = valid_data; invalid["WSPR"]["Call Sign"] = "!invalid!";
    iniFile.setData(invalid); iniFile.commit_changes();
    ini_reload_generation.fetch_add(1); ini_reload_pending.store(true);
    check(set_config(), "invalid managed reload recoverable");
    check(managed_reload_tx_inhibited_for_test() && draws.calls == 4,
          "invalid reload inhibits without drawing");
    equal(active_wspr_plan_placement->applied_offset_hz, -21, "invalid candidate preserves committed placement");
    iniFile.setData(valid_data); iniFile.commit_changes();
    ini_reload_generation.fetch_add(1); ini_reload_pending.store(true);
    check(set_config(true), "valid recovery configuration accepted");
    check(!managed_reload_tx_inhibited_for_test() && !active_wspr_plan_placement && draws.calls == 4,
          "existing inhibited reload recovery clears plan before later preparation");
    check(set_config(true), "prepare after recovery admission"); verify(37, 1, plus37);
    check(draws.calls == 5, "recovery draws for a fresh plan");
    config.use_ini = false;
}
void normal_skip_and_provider_lifetime() {
    Fixture f;
    Draws draws{{37,-21}};
    {
        ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
        config.frequencies = config.wspr.frequencies = "20m 0"; set_frequencies(config);
        check(set_config(true), "prepare rotating pair before normal skip");
        event(WsprTransmissionCallbackEvent::COMPLETE); verify(37, 2, plus37);
        event(WsprTransmissionCallbackEvent::COMPLETE);
        check(!active_wspr_plan_placement && current_transmission_request_for_test().isSkipWindow() && draws.calls == 1,
              "pair completion then zero entry releases placement without a draw");
        event(WsprTransmissionCallbackEvent::SKIPPED); verify(-21, 1, minus21);
        check(draws.calls == 2, "skipped window advances to a fresh plan");
    }
    // Concurrent override scopes must serialize their entire lifetime: an
    // older scope cannot restore a callback captured by an expired scope.
    std::promise<void> attempting, acquired;
    auto entered = attempting.get_future(); auto installed = acquired.get_future();
    std::future<void> other;
    std::atomic<unsigned> outer_calls{0}, other_calls{0};
    {
        ScopedWsprRandomPlacementForTest outer([&] { ++outer_calls; return 0; });
        other = std::async(std::launch::async, [&] {
            attempting.set_value();
            ScopedWsprRandomPlacementForTest concurrent([&] { ++other_calls; return 0; });
            acquired.set_value();
            check(set_config(true), "serialized concurrent override prepares normally");
        });
        check(entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
              "concurrent installer starts");
        check(installed.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
              "concurrent scope waits for prior override lifetime");
        check(set_config(true), "outer provider remains selected");
    }
    check(other.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
          "concurrent scope completes after release"); other.get();
    check(outer_calls == 1 && other_calls == 1, "each scoped provider called once");
    check(set_config(true), "production provider restored");
    check(outer_calls == 1 && other_calls == 1 &&
          std::abs(current_transmission_request_for_test().applied_offset_hz) <= 80,
          "expired callbacks cannot leak into production RNG");
}
void independent_calibration() {
    Fixture f; Draws draws{{37}};
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    config.si5351_ppm = 2.5;
    check(set_config(true), "nonzero correction first frame"); verify(37, 1, plus37, false);
    equal(current_transmission_request_for_test().ppm, 2.5, "correction belongs to request");
    config.si5351_ppm = -3.25;
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(37, 2, plus37, false);
    equal(current_transmission_request_for_test().ppm, -3.25, "second frame refreshes correction independently");
    check(draws.calls == 1, "calibration refresh cannot redraw placement");
}
void gpio_correction_lifetime() {
#if WSPRRYPI_BACKEND_RPI_GPIO
    Fixture f; Draws draws{{37}};
    struct ResetGeneration { ~ResetGeneration() { clear_raspberry_pi_generation_override_for_test(); } } reset;
    set_raspberry_pi_generation_override_for_test(4);
    ScopedWsprRandomPlacementForTest provider([&] { return draws.next(); });
    config.transmit_backend = TransmitBackendKind::GPIO;
    config.gpio_manual_ppm = 2.5;
    resolve_backend_specific_config(config);
    check(set_config(true), "GPIO correction first frame"); verify(37, 1, plus37, false);
    equal(current_controller_request_for_test()->calibration.ppm, 2.5, "controller receives frozen first correction");
    equal(current_tx_runtime_status_snapshot().gpio_correction_committed.additional_ppm, 2.5,
          "first correction provenance agrees with controller");
    config.gpio_manual_ppm = -3.25;
    event(WsprTransmissionCallbackEvent::COMPLETE); verify(37, 2, plus37, false);
    equal(current_transmission_request_for_test().ppm, -3.25, "second legacy request refreshes GPIO correction");
    equal(current_controller_request_for_test()->calibration.ppm, -3.25,
          "second controller agrees with independently refreshed correction");
    equal(current_tx_runtime_status_snapshot().gpio_correction_committed.additional_ppm, -3.25,
          "second correction provenance agrees with controller");
    check(draws.calls == 1, "GPIO correction updates do not redraw nominal placement");
#endif
}
} // namespace
int main() {
    try {
        pair_and_independent();
        disabled_single_and_rotation();
        cancellation_skips_and_modes();
        failures_and_scoped_provider();
        reloads_and_superseded_candidate();
        normal_skip_and_provider_lifetime();
        independent_calibration();
        gpio_correction_lifetime();
        std::cout << "wspr_plan_placement_test passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
