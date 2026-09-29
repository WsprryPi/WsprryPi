#include "wtp_endpoint/tone_engine.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

using namespace wsprrypico::wtp;
using namespace wsprrypi;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class TestClock final : public Clock {
public:
    ClockSnapshot snapshot() const override {
        const auto monotonic = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        return {ClockState::Synchronized, monotonic + 1'000'000'000'000ULL,
                monotonic, 1000, 0, LeapState::Normal, std::nullopt};
    }
};

class FakeBackend final : public ITransmissionBackend {
public:
    bool configured = false, executed = false, stopped = false;
    BackendInfo info() const override { return {BackendKind::SIMULATED, "fake", "test"}; }
    BackendCapabilities capabilities() const override { return {}; }
    BackendCompileResult configure(const ExecutionPlan& plan,
                                   const BackendExecutionInputs&) override {
        configured = plan.events.size() == 1 && plan.duration_was_explicit;
        return {configured, {}, {}};
    }
    ExecutionResult execute(const ExecutionPlan&) override {
        if (stopped) return {false, true, false, "context still stopped"};
        executed = true;
        return {true, false, false, {}};
    }
    StartupQuiesceResult quiesceForStartup() override { return {true, {}}; }
    void stop() noexcept override { stopped = true; }
    CleanupResult cleanup() noexcept override { return {true, {}}; }
};
} // namespace

int main() {
    TestClock clock;
    FakeBackend backend;
    WtpPiToneEngine engine(backend, {}, BackendKind::SIMULATED, clock,
                           [](std::uint64_t requested) {
                               return std::optional<std::uint64_t>(requested);
                           }, 0.0, [&] { backend.stopped = false; });
    check(engine.startup_safe(), "startup must quiesce output");
    Job job{std::string(32, '1'), "rf-events/1", "tone", 1'000'000'000ULL,
            {{0, 1'000'000'000ULL, true, 14'097'100'000'000'000ULL}}, false};
    check(engine.prepare(job).accepted && backend.configured,
          "finite one-event tone must prepare");
    auto malformed = job;
    malformed.events[0].offset_ns = 1;
    check(!engine.prepare(malformed).accepted,
          "nonzero first-event offset must be rejected");
    const auto now = clock.snapshot();
    const auto start = now.monotonic_now_ns + 30'000'000ULL;
    check(engine.schedule(job, start,
          {&clock, now.utc_now_ns + 30'000'000ULL, 1'000'000ULL, 0, 0}),
          "prepared tone must schedule locally");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const auto report = engine.poll(clock.snapshot().monotonic_now_ns);
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
    check(!engine.prepare(controller_tone).accepted,"a second tone is outside initial route capability");

    FakeBackend missing_enable;
    WtpPiToneEngine physical_adapter(missing_enable, {}, BackendKind::SI5351, clock,
        [](std::uint64_t value) { return std::optional<std::uint64_t>(value); },
        0.0, [&] { missing_enable.stopped = false; }, true);
    check(physical_adapter.prepare(job).accepted,"physical adapter prepares");
    const auto physical_now = clock.snapshot();
    check(physical_adapter.schedule(job, physical_now.monotonic_now_ns,
          {&clock, physical_now.utc_now_ns, 1'000'000ULL, 0, 0}),"physical adapter schedules");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(physical_adapter.poll(clock.snapshot().monotonic_now_ns).state == EngineState::Failed,
          "backend success without an observed output enable must never report a completed transmission");
}
