#include "wtp_endpoint/tone_engine.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wsprrypi {
namespace {
using namespace wsprrypico::wtp;
constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr std::uint64_t kMaximumToneDurationNs = 10'000'000'000;

std::chrono::steady_clock::time_point steady_at(std::uint64_t ns) {
    return std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(static_cast<std::int64_t>(ns)));
}
} // namespace

WtpPiToneEngine::WtpPiToneEngine(ITransmissionBackend& backend,
                                 BackendExecutionInputs inputs, BackendKind kind,
                                 Clock& clock, RealizeFrequency realize,
                                 double calibration_ppm,
                                 std::function<void()> reset_execution_context,
                                 bool backend_controls_enable,
                                 std::function<void()> stop_execution_context)
    : backend_(backend), inputs_(std::move(inputs)), kind_(kind),
      clock_(clock), realize_(std::move(realize)),
      calibration_ppm_(calibration_ppm),
      reset_execution_context_(std::move(reset_execution_context)),
      stop_execution_context_(std::move(stop_execution_context)),
      backend_controls_enable_(backend_controls_enable) {
    startup_safe_ = backend_.quiesceForStartup().ok;
    if (!startup_safe_) output_active_ = true;
}

WtpPiToneEngine::~WtpPiToneEngine() {
    const auto deadline = clock_.snapshot().monotonic_now_ns + 5'000'000'000ULL;
    (void)disable(deadline);
    if (worker_.joinable()) worker_.join();
}

void WtpPiToneEngine::join_finished() {
    if (worker_.joinable() && worker_done_) worker_.join();
}

PrepareResult WtpPiToneEngine::prepare(const Job& job) {
    std::lock_guard lock(mutex_);
    join_finished();
    if (!startup_safe_ || !worker_done_ || output_active_ ||
        !realize_ || job.mode != "tone" || job.events.empty() || job.events.size() > 2 ||
        job.total_duration_ns == 0 ||
        job.total_duration_ns > kMaximumToneDurationNs + 1)
        return {};
    const auto& event = job.events[0];
    const bool has_tail = job.events.size() == 2;
    if (has_tail) {
        const auto& tail = job.events[1];
        if (tail.rf_on || tail.frequency_nhz || tail.duration_ns != 1 ||
            tail.offset_ns != event.duration_ns) return {};
    }
    if (event.offset_ns != 0 || !event.rf_on || !event.frequency_nhz ||
        event.duration_ns == 0 || event.duration_ns > kMaximumToneDurationNs ||
        event.duration_ns + (has_tail ? 1ULL : 0ULL) != job.total_duration_ns ||
        event.duration_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return {};
    const auto realized = realize_(*event.frequency_nhz);
    if (!realized || *realized == 0 ||
        (!job.allow_frequency_adjustment && *realized != *event.frequency_nhz))
        return {};
    const double frequency_hz = static_cast<double>(
        static_cast<long double>(*event.frequency_nhz) / kNanosecondsPerSecond);
    if (!std::isfinite(frequency_hz) || frequency_hz <= 0) return {};
    ExecutionPlan candidate;
    candidate.id = {1};
    candidate.request_id = {1};
    candidate.mode = TransmissionMode::TONE;
    candidate.backend = kind_;
    candidate.reference_frequency_hz = frequency_hz;
    candidate.calibration.ppm = calibration_ppm_;
    candidate.duration_was_explicit = true;
    candidate.events.push_back({std::chrono::nanoseconds{0},
                                std::chrono::nanoseconds{
                                    static_cast<std::int64_t>(event.duration_ns)},
                                RfEventType::HOLD, frequency_hz, true});
    candidate.summary = {std::chrono::nanoseconds{
                             static_cast<std::int64_t>(event.duration_ns)},
                         1, frequency_hz, frequency_hz};
    if (reset_execution_context_) reset_execution_context_();
    const auto configured = backend_.configure(candidate, inputs_);
    if (!configured.ok || !configured.adjustments.empty()) {
        (void)backend_.cleanup();
        return {};
    }
    plan_ = std::move(candidate);
    report_ = {};
    PrepareResult result{true, {}};
    if (*realized != *event.frequency_nhz)
        result.adjustments.push_back({0, *event.frequency_nhz, *realized});
    return result;
}

bool WtpPiToneEngine::schedule(const Job& job, std::uint64_t start_monotonic_ns,
                               const LocalStartConditions& conditions) {
    std::lock_guard lock(mutex_);
    join_finished();
    if (!plan_ || !startup_safe_ || !worker_done_ ||
        job.events.empty() || job.events.size() > 2 || conditions.clock != &clock_ ||
        start_monotonic_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return false;
    stop_requested_ = false;
    launch_missed_ = false;
    start_monotonic_ns_ = start_monotonic_ns;
    start_conditions_ = conditions;
    worker_done_ = false;
    report_.state = EngineState::Armed;
    try {
        worker_ = std::thread(&WtpPiToneEngine::run, this,
                              start_monotonic_ns, conditions);
    } catch (...) {
        worker_done_ = true;
        report_.state = EngineState::Failed;
        return false;
    }
    return true;
}

bool WtpPiToneEngine::admit_output_enable() {
    std::unique_lock lock(mutex_);
    cv_.wait_until(lock, steady_at(start_monotonic_ns_),
                   [this] { return stop_requested_; });
    if (stop_requested_) return false;
    auto now = clock_.snapshot();
    if (now.utc_now_ns < start_conditions_.start_utc_ns &&
        start_conditions_.start_utc_ns - now.utc_now_ns <=
            start_conditions_.maximum_uncertainty_ns) {
        cv_.wait_for(lock, std::chrono::nanoseconds(static_cast<std::int64_t>(
            start_conditions_.start_utc_ns - now.utc_now_ns)),
            [this] { return stop_requested_; });
        now = clock_.snapshot();
    }
    const auto window = start_window_ns(start_conditions_.start_utc_ns);
    const bool clock_usable = !stop_requested_ &&
        now.state == ClockState::Synchronized && now.leap == LeapState::Normal &&
        now.uncertainty_ns <= start_conditions_.maximum_uncertainty_ns &&
        now.utc_now_ns >= start_conditions_.start_utc_ns &&
        now.utc_now_ns - start_conditions_.start_utc_ns < window &&
        now.monotonic_now_ns < start_monotonic_ns_ + window;
    if (!clock_usable) {
        launch_missed_ = !stop_requested_;
        return false;
    }
    output_active_ = true; // The enable transaction may now be in flight.
    return true;
}

void WtpPiToneEngine::observe_output_enable() {
    std::lock_guard lock(mutex_);
    const auto now = clock_.snapshot();
    report_.state = EngineState::Running;
    report_.launch_monotonic_ns = now.monotonic_now_ns;
    if (now.monotonic_now_ns >= start_monotonic_ns_ +
        start_window_ns(start_conditions_.start_utc_ns))
        throw std::runtime_error("Output enable exceeded the admitted start window");
}

WtpPiToneEngine::LaunchObservation WtpPiToneEngine::launch_observation() const {
    std::lock_guard lock(mutex_);
    return {start_monotonic_ns_, report_.launch_monotonic_ns};
}

void WtpPiToneEngine::run(std::uint64_t, LocalStartConditions) noexcept {
    ExecutionResult result;
    bool cleaned = false;
    try {
        if (backend_controls_enable_ || admit_output_enable()) {
            if (!backend_controls_enable_) observe_output_enable();
            result = backend_.execute(*plan_);
        }
        cleaned = backend_.cleanup().ok;
    } catch (...) {
        cleaned = backend_.cleanup().ok;
    }
    {
        std::lock_guard lock(mutex_);
        if (cleaned) output_active_ = false;
        report_.output_active = output_active_;
        report_.state = cleaned && launch_missed_ ? EngineState::Missed :
                        cleaned && result.ok && !result.stopped &&
                            report_.launch_monotonic_ns ? EngineState::Complete :
                        EngineState::Failed;
        worker_done_ = true;
    }
    cv_.notify_all();
}

EngineReport WtpPiToneEngine::poll(std::uint64_t) {
    std::lock_guard lock(mutex_);
    auto result = report_;
    result.output_active = output_active_;
    join_finished();
    return result;
}

bool WtpPiToneEngine::disable(std::uint64_t deadline_monotonic_ns) {
    {
        std::lock_guard lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (stop_execution_context_) stop_execution_context_();
    backend_.stop();
    {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_until(lock, steady_at(deadline_monotonic_ns),
                            [this] { return worker_done_; }))
            return false;
        join_finished();
    }
    const bool cleaned = output_active_ ? backend_.quiesceForStartup().ok :
                                         backend_.cleanup().ok;
    if (cleaned) output_active_ = false;
    return startup_safe_ && cleaned && !output_active_ &&
           clock_.snapshot().monotonic_now_ns <= deadline_monotonic_ns;
}
} // namespace wsprrypi
