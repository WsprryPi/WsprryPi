#include "wtp_endpoint/tone_engine.hpp"
#include "wtp_endpoint/capabilities.hpp"
#include "WSPR-Transmitter/src/gpio_band_policy.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wsprrypi {
namespace {
using namespace wsprrypico::wtp;
constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;

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
                                 std::function<void()> stop_execution_context,
                                 bool allow_unqualified_frequency,
                                 bool allow_non_amateur_frequency,
                                 HardwareProfile hardware_profile,
                                 std::function<bool(const Job&, ExecutionPlan&, BackendExecutionInputs&)> prepare_inputs)
    : backend_(backend), inputs_(std::move(inputs)), kind_(kind),
      clock_(clock), realize_(std::move(realize)),
      calibration_ppm_(calibration_ppm),
      allow_unqualified_frequency_(allow_unqualified_frequency),
      allow_non_amateur_frequency_(allow_non_amateur_frequency),
      hardware_profile_(hardware_profile), prepare_inputs_(std::move(prepare_inputs)),
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
    if (!worker_done_ || output_active_) return {};
    plan_.reset();
    prepared_job_id_.clear();
    const auto mode = wtp_transmission_mode(job.mode);
    const auto caps = backend_.capabilities();
    if (!startup_safe_ || !realize_ || !mode || !supports_mode(caps, *mode) ||
        job.profile != "rf-events/1" || !job.events.valid() || job.events.empty() ||
        job.events.size() > EventList::maximum_events ||
        job.total_duration_ns == 0 ||
        job.total_duration_ns > wtp_max_job_duration_ns)
        return {};

    ExecutionPlan candidate;
    candidate.id = {1};
    candidate.request_id = {1};
    candidate.mode = *mode;
    candidate.backend = kind_;
    candidate.policy.hardware_profile = hardware_profile_;
    candidate.policy.allow_unqualified_frequency = allow_unqualified_frequency_;
    candidate.policy.allow_non_amateur_frequency = allow_non_amateur_frequency_;
    candidate.calibration.ppm = calibration_ppm_;
    candidate.duration_was_explicit = true;
    std::uint64_t offset = 0;
    for (const auto& event : job.events) {
        if (event.offset_ns != offset || event.duration_ns == 0 ||
            event.duration_ns > wtp_max_job_duration_ns - offset ||
            event.rf_on != event.frequency_nhz.has_value()) return {};
        double hz = 0;
        if (event.frequency_nhz) {
            hz = static_cast<double>(static_cast<long double>(*event.frequency_nhz) /
                                     kNanosecondsPerSecond);
            if (!std::isfinite(hz) || hz <= 0 ||
                (caps.min_frequency_hz > 0 && hz < caps.min_frequency_hz) ||
                (caps.max_frequency_hz > 0 && hz > caps.max_frequency_hz)) return {};
            if (caps.output_class != BackendOutputClass::NON_RF_SIMULATION &&
                !evaluate_frequency_policy(kind_, *mode, hz,
                    allow_unqualified_frequency_, allow_non_amateur_frequency_, hardware_profile_).allowed)
                return {};
            if (!candidate.reference_frequency_hz) candidate.reference_frequency_hz = hz;
            if (!candidate.summary.min_frequency_hz || hz < candidate.summary.min_frequency_hz)
                candidate.summary.min_frequency_hz = hz;
            candidate.summary.max_frequency_hz = std::max(candidate.summary.max_frequency_hz, hz);
        }
        candidate.events.push_back({std::chrono::nanoseconds{static_cast<std::int64_t>(offset)},
            std::chrono::nanoseconds{static_cast<std::int64_t>(event.duration_ns)},
            event.rf_on ? RfEventType::HOLD : RfEventType::RF_OFF, hz, event.rf_on});
        offset += event.duration_ns;
    }
    if (offset != job.total_duration_ns) return {};
    candidate.summary.total_duration = std::chrono::nanoseconds{static_cast<std::int64_t>(offset)};
    candidate.summary.event_count = candidate.events.size();
    // Native GPIO clock tables use a center frequency; WTP events carry the
    // actual tone frequencies. Keep normalization in the parent adapter.
    if (kind_ == BackendKind::RPI_CLOCK_GPIO || kind_ == BackendKind::RP1_GPCLK) {
        constexpr double spacing = 12000.0 / 8192.0;
        if (*mode == TransmissionMode::WSPR || *mode == TransmissionMode::QRSS)
            candidate.reference_frequency_hz = candidate.summary.min_frequency_hz + 1.5 * spacing;
        else if (*mode == TransmissionMode::FSKCW || *mode == TransmissionMode::DFCW)
            candidate.reference_frequency_hz = candidate.summary.min_frequency_hz +
                1.5 * (candidate.summary.max_frequency_hz - candidate.summary.min_frequency_hz);
    }
    if (reset_execution_context_) reset_execution_context_();
    auto reject = [this]() -> PrepareResult {
        if (!backend_.cleanup().ok) output_active_ = true;
        return {};
    };
    BackendCompileResult configured;
    std::optional<std::vector<std::uint64_t>> realized;
    try {
        if (prepare_inputs_ && !prepare_inputs_(job, candidate, inputs_)) return reject();
        configured = backend_.configure(candidate, inputs_);
        if (configured.ok) realized = realize_(job);
    } catch (...) { return reject(); }
    if (!configured.ok || !configured.adjustments.empty() || !realized ||
        realized->size() != job.events.size()) return reject();
    PrepareResult result{true, {}};
    for (std::size_t i = 0; i < job.events.size(); ++i) {
        const auto& event = job.events[i];
        const auto actual = (*realized)[i];
        if (!event.rf_on) { if (actual != 0) return reject(); continue; }
        const double actual_hz = static_cast<double>(static_cast<long double>(actual) /
                                                     kNanosecondsPerSecond);
        if (!actual || (caps.min_frequency_hz > 0 && actual_hz < caps.min_frequency_hz) ||
            (caps.max_frequency_hz > 0 && actual_hz > caps.max_frequency_hz) ||
            (caps.output_class != BackendOutputClass::NON_RF_SIMULATION &&
             !evaluate_frequency_policy(kind_, *mode, actual_hz,
                 allow_unqualified_frequency_, allow_non_amateur_frequency_, hardware_profile_).allowed))
            return reject();
        if (actual != *event.frequency_nhz) {
            if (!job.allow_frequency_adjustment) return reject();
            result.adjustments.push_back({i, *event.frequency_nhz, actual});
        }
        candidate.events[i].frequency_hz = actual_hz;
    }
    plan_ = std::move(candidate);
    prepared_job_id_ = job.job_id;
    prepared_duration_ns_ = job.total_duration_ns;
    report_ = {};
    return result;
}

bool WtpPiToneEngine::schedule(const Job& job, std::uint64_t start_monotonic_ns,
                               const LocalStartConditions& conditions) {
    std::lock_guard lock(mutex_);
    join_finished();
    if (!plan_ || !startup_safe_ || !worker_done_ ||
        job.job_id != prepared_job_id_ || job.total_duration_ns != prepared_duration_ns_ ||
        conditions.clock != &clock_ ||
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
    if (report_.launch_monotonic_ns) return !stop_requested_;
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
    if (report_.launch_monotonic_ns) return;
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
