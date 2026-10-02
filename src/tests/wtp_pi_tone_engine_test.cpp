#include "wtp_endpoint/tone_engine.hpp"
#include "wtp_endpoint/capabilities.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

using namespace wsprrypico::wtp;
using namespace wsprrypi;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

EngineReport await_terminal(WtpPiToneEngine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (true) {
        const auto report = engine.poll(0);
        if (report.state == EngineState::Complete || report.state == EngineState::Failed ||
            report.state == EngineState::Missed) return report;
        check(std::chrono::steady_clock::now() < deadline, "engine did not reach a terminal state");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

class TestClock final : public Clock {
public:
    std::atomic<ClockState> state{ClockState::Synchronized};
    const std::uint64_t origin = monotonic_now();
    static std::uint64_t monotonic_now() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    ClockSnapshot snapshot() const override {
        const auto monotonic = monotonic_now();
        // Start this fixture at a full UTC second, rather than an arbitrary
        // fractional second with potentially almost no admission window left.
        return {state.load(), 1'000'000'000'000ULL + monotonic - origin,
                monotonic, 1000, 0, LeapState::Normal, std::nullopt};
    }
};

class GateClock final : public Clock {
    const std::uint64_t origin = TestClock::monotonic_now();
public:
    std::atomic<std::uint64_t> elapsed{0};
    ClockSnapshot snapshot() const override {
        const auto delta = elapsed.load();
        return {ClockState::Synchronized, 1'000'900'000'000ULL + delta,
                origin + delta, 1000, 0, LeapState::Normal, std::nullopt};
    }
};

class FakeBackend final : public ITransmissionBackend {
public:
    bool configured = false, executed = false;
    std::atomic<bool> stopped{false};
    BackendInfo info() const override { return {BackendKind::SIMULATED, "fake", "test"}; }
    std::function<void()> execute_hook;
    ExecutionPlan recorded;
    BackendCapabilities caps;
    bool fail_configure = false, fail_cleanup = false;
    FakeBackend() { caps.supported_modes = 0xffffffff; caps.min_frequency_hz = 7812.5;
        caps.max_frequency_hz = 200000000; }
    BackendCapabilities capabilities() const override { return caps; }
    BackendCompileResult configure(const ExecutionPlan& plan,
                                   const BackendExecutionInputs&) override {
        recorded = plan;
        configured = !fail_configure && !plan.events.empty() && plan.duration_was_explicit;
        return {configured, {}, {}};
    }
    ExecutionResult execute(const ExecutionPlan&) override {
        if (stopped) return {false, true, false, "context still stopped"};
        executed = true;
        if (execute_hook) execute_hook();
        return {true, false, false, {}};
    }
    StartupQuiesceResult quiesceForStartup() override { return {true, {}}; }
    void stop() noexcept override { stopped = true; }
    CleanupResult cleanup() noexcept override { return {!fail_cleanup, {}}; }
};
} // namespace

int main() {
    TestClock clock;
    FakeBackend backend;
    auto exact = [](const Job& job) -> std::optional<std::vector<std::uint64_t>> {
        std::vector<std::uint64_t> values;
        for (const auto& e : job.events) values.push_back(e.frequency_nhz.value_or(0));
        return values;
    };
    WtpPiToneEngine engine(backend, {}, BackendKind::SIMULATED, clock,
                           exact, 0.0, [&] { backend.stopped = false; });
    const auto caps = wtp_backend_caps(backend);
    check(caps.supported_modes.size() == wtp_modes.size() && caps.max_events == 512 &&
          caps.max_job_duration_ns == 86'400'000'000'000ULL &&
          caps.minimum_frequency_nhz == 7'812'500'000'000ULL &&
          caps.maximum_frequency_nhz == 200'000'000'000'000'000ULL,
          "CAPS must come from backend metadata and bounded protocol resources");
    check(engine.startup_safe(), "startup must quiesce output");
    {
        FakeBackend native;
        WtpPiToneEngine gpio(native, {}, BackendKind::RPI_CLOCK_GPIO, clock, exact,
            0, {}, false, {}, false, false, HardwareProfile::BCM2711);
        constexpr std::uint64_t low = 14'097'100'000'000'000ULL;
        Job frame{std::string(32, '3'), "rf-events/1", "wspr", 4'000'000'000ULL,
            {{0, 1'000'000'000ULL, true, low},
             {1'000'000'000ULL, 1'000'000'000ULL, true, low + 1'464'843'750ULL},
             {2'000'000'000ULL, 1'000'000'000ULL, true, low + 2'929'687'500ULL},
             {3'000'000'000ULL, 1'000'000'000ULL, true, low + 4'394'531'250ULL}}, false};
        check(gpio.prepare(frame).accepted &&
            native.recorded.policy.hardware_profile == HardwareProfile::BCM2711 &&
            native.recorded.reference_frequency_hz == 14'097'102.197265625,
            "GPIO receives its exact processor policy and native WSPR center");
        frame.events[0].frequency_nhz = 144'490'000'000'000'000ULL;
        check(!gpio.prepare(frame).accepted, "GPIO qualification remains processor-specific");
    }
    Job job{std::string(32, '1'), "rf-events/1", "tone", 1'000'000'000ULL,
            {{0, 1'000'000'000ULL, true, 14'097'100'000'000'000ULL}}, false};
    check(engine.prepare(job).accepted && backend.configured,
          "finite one-event tone must prepare");
    auto malformed = job;
    malformed.events[0].offset_ns = 1;
    check(!engine.prepare(malformed).accepted,
          "nonzero first-event offset must be rejected");
    check(engine.prepare(job).accepted, "reprepare after rejected job");
    const auto now = clock.snapshot();
    const auto start = now.monotonic_now_ns + 30'000'000ULL;
    check(engine.schedule(job, start,
          {&clock, now.utc_now_ns + 30'000'000ULL, 1'000'000ULL, 0, 0}),
          "prepared tone must schedule locally");
    const auto report = await_terminal(engine);
    check(report.state == EngineState::Complete && backend.executed &&
              !report.output_active, "tone must complete with output off");
    check(engine.disable(clock.snapshot().monotonic_now_ns + 1'000'000'000ULL),
          "terminal cleanup must be bounded and idempotent");
    check(backend.stopped, "disable must stop the execution context");
    check(engine.prepare(job).accepted && !backend.stopped,
          "a new accepted job must reset the previous stop context");
    auto controller_tone = job;
    controller_tone.events.push_back({job.total_duration_ns,1,false,std::nullopt});
    ++controller_tone.total_duration_ns;
    check(engine.prepare(controller_tone).accepted,
          "native controller tone with terminal RF-off event must prepare");
    controller_tone.events.back().rf_on=true;
    controller_tone.events.back().frequency_nhz=14'097'200'000'000'000ULL;
    check(engine.prepare(controller_tone).accepted, "multiple backend-supported events prepare");
    for (const auto& [mode, wire] : wtp_modes) {
        for (auto frequency : {137500ULL, 475700ULL, 1838100ULL, 14097100ULL, 144490000ULL}) {
            Job multi{std::string(32, '2'), "rf-events/1", std::string(wire), 12'000'000'001ULL,
                {{0, 4'000'000'000ULL, true, frequency * 1'000'000'000ULL},
                 {4'000'000'000ULL, 4'000'000'000ULL, false, std::nullopt},
                 {8'000'000'000ULL, 4'000'000'000ULL, true, frequency * 1'000'000'000ULL},
                 {12'000'000'000ULL, 1, false, std::nullopt}}, false};
            check(engine.prepare(multi).accepted, "all backend modes and bands accept finite event plans");
            check(backend.recorded.mode == mode && backend.recorded.events.size() == 4 &&
                  backend.recorded.events[2].offset_from_start.count() == 8'000'000'000ULL &&
                  !backend.recorded.events[1].rf_on &&
                  backend.recorded.summary.total_duration.count() == 12'000'000'001ULL,
                  "wire timing and RF gating preserved");
        }
    }
    auto silent = job;
    silent.events = {{0, 1000, false, std::nullopt},
                     {1000, job.total_duration_ns - 1000, true, job.events[0].frequency_nhz}};
    check(engine.prepare(silent).accepted && !backend.recorded.events.front().rf_on,
          "leading RF-off interval is preserved");
    auto bad = job; bad.mode = "unknown";
    check(!engine.prepare(bad).accepted, "unknown mode refused");
    check(!engine.schedule(job, start, {&clock, now.utc_now_ns, 1000, 0, 0}),
          "rejected preparation cannot execute stale plan");
    backend.caps.supported_modes = transmission_mode_bit(TransmissionMode::TONE);
    bad.mode = "wspr";
    check(!engine.prepare(bad).accepted, "backend mode mask is authoritative");
    check(wtp_backend_caps(backend).supported_modes == std::vector<std::string>{"tone"},
          "CAPS follows changed backend mask");
    bad = job; bad.events[0].frequency_nhz = 201'000'000'000'000'000ULL;
    check(!engine.prepare(bad).accepted, "backend frequency bounds enforced");
    bad = job; bad.total_duration_ns = 86'400'000'000'001ULL;
    check(!engine.prepare(bad).accepted, "protocol duration bounded");
    backend.fail_configure = true;
    check(!engine.prepare(job).accepted, "configure failure rejected");
    backend.fail_configure = false;
    {
        FakeBackend preparation;
        preparation.fail_cleanup = true;
        WtpPiToneEngine failed_inputs(preparation, {}, BackendKind::SIMULATED, clock,
            exact, 0, {}, false, {}, false, false, HardwareProfile::UNSPECIFIED,
            [](const Job&, ExecutionPlan&, BackendExecutionInputs&) -> bool {
                throw std::runtime_error("preparation failed");
            });
        check(!failed_inputs.prepare(job).accepted && failed_inputs.output_active(),
            "input preparation exceptions must reject and retain unconfirmed cleanup");
        preparation.fail_cleanup = false;
    }
    FakeBackend rounded;
    WtpPiToneEngine rounding(rounded, {}, BackendKind::SIMULATED, clock,
        [&](const Job& j) { auto values = exact(j); ++(*values)[0]; return values; });
    check(!rounding.prepare(job).accepted, "rounding requires consent");
    auto consent = job; consent.allow_frequency_adjustment = true;
    const auto adjusted = rounding.prepare(consent);
    check(adjusted.accepted && adjusted.adjustments.size() == 1 &&
          adjusted.adjustments[0].event_index == 0, "per-event realization reported");
    rounded.fail_cleanup = true;
    check(!rounding.prepare(job).accepted && rounding.output_active(),
          "unconfirmed rejected-plan cleanup inhibits later work");
    rounded.fail_cleanup = false;


    FakeBackend missing_enable;
    WtpPiToneEngine physical_adapter(missing_enable, {}, BackendKind::SI5351, clock,
        exact,
        0.0, [&] { missing_enable.stopped = false; }, true);
    check(physical_adapter.prepare(job).accepted,"physical adapter prepares");
    const auto physical_now = clock.snapshot();
    check(physical_adapter.schedule(job, physical_now.monotonic_now_ns,
          {&clock, physical_now.utc_now_ns, 1'000'000ULL, 0, 0}),"physical adapter schedules");
    check(await_terminal(physical_adapter).state == EngineState::Failed,
          "backend success without an observed output enable must never report a completed transmission");
    // Later gated elements must not be rejected as a late initial launch.
    FakeBackend gates;
    GateClock gate_clock;
    WtpPiToneEngine gated(gates, {}, BackendKind::SIMULATED, gate_clock, exact,
        0.0, [&] { gates.stopped = false; }, true);
    gates.execute_hook = [&] {
        check(gated.admit_output_enable(), "first physical enable admitted");
        gated.observe_output_enable();
        gate_clock.elapsed = 150'000'000ULL;
        check(gate_clock.snapshot().utc_now_ns >= 1'001'000'000'000ULL,
              "later gate fixture must be beyond the first UTC-second boundary");
        check(gated.admit_output_enable(), "later RF-on is not another ARM deadline");
    };
    check(gated.prepare(job).accepted, "gated sequence prepares");
    auto gate_now = gate_clock.snapshot();
    check(gated.schedule(job, gate_now.monotonic_now_ns,
          {&gate_clock, gate_now.utc_now_ns, 1'000'000ULL, 0, 0}), "gated sequence schedules");
    check(await_terminal(gated).state == EngineState::Complete, "later RF-on completes");
    check(gated.disable(gate_clock.snapshot().monotonic_now_ns + 1'000'000'000ULL), "gated stop");
    check(!gated.admit_output_enable(), "stop prevents any subsequent RF-on");
    FakeBackend late;
    WtpPiToneEngine missed(late, {}, BackendKind::SIMULATED, clock, exact);
    check(missed.prepare(job).accepted, "late sequence prepares");
    auto late_now = clock.snapshot();
    check(missed.schedule(job, late_now.monotonic_now_ns - 1'000'000'000ULL,
          {&clock, late_now.utc_now_ns - 1'000'000'000ULL, 1'000'000ULL, 0, 0}), "late sequence schedules");
    check(await_terminal(missed).state == EngineState::Missed && !late.executed,
          "missed initial start never executes backend");

    FakeBackend cancelled;
    WtpPiToneEngine cancelled_engine(cancelled, {}, BackendKind::SIMULATED, clock, exact);
    check(cancelled_engine.prepare(job).accepted, "cancellable future job prepares");
    auto future = clock.snapshot();
    check(cancelled_engine.schedule(job, future.monotonic_now_ns + 2'000'000'000ULL,
          {&clock, future.utc_now_ns + 2'000'000'000ULL, 1000, 0, 0}), "future job arms");
    check(cancelled_engine.disable(clock.snapshot().monotonic_now_ns + 500'000'000ULL) &&
          !cancelled.executed, "cancel while armed wakes scheduler without RF");
    FakeBackend unsynced;
    WtpPiToneEngine unsynced_engine(unsynced, {}, BackendKind::SIMULATED, clock, exact);
    check(unsynced_engine.prepare(job).accepted, "clock-loss fixture prepares");
    auto sync_now = clock.snapshot();
    clock.state = ClockState::Unsynchronized;
    check(unsynced_engine.schedule(job, sync_now.monotonic_now_ns,
          {&clock, sync_now.utc_now_ns, 1000, 0, 0}), "clock-loss fixture arms");
    check(await_terminal(unsynced_engine).state == EngineState::Missed && !unsynced.executed,
          "clock loss before first enable prevents RF");
    clock.state = ClockState::Synchronized;

}
