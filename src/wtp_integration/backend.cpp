// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "backend.hpp"
#include "WSPR-Transmitter/src/gpio_band_policy.hpp"
#include <algorithm>
#include <limits>
#include <thread>

namespace wsprrypi {
namespace {
constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t grace_ms = 2000;
constexpr std::uint64_t monitor_status_interval_ms = 5000;
constexpr std::uint64_t completion_fallback_delay_ms = 1000;
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
  return b > maximum - a ? maximum : a + b;
}
std::uint64_t ceil_ms(std::uint64_t ns) {
  return ns / 1000000 + (ns % 1000000 != 0);
}
bool terminal(wtp::State s) {
  return s == wtp::State::Complete || s == wtp::State::Aborted ||
         s == wtp::State::Missed;
}
std::string job_bytes(const wtp::Job &job) {
  auto encoded = wtp::encode_request(
      {std::string(32, '0'), std::string(32, '0'), wtp::Operation::Load, job});
  return encoded ? *encoded.payload : std::string{};
}
} // namespace
std::uint64_t WtpSteadyClock::now_ms() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
void WtpSteadyClock::wait_ms(std::uint64_t duration) {
  std::this_thread::sleep_for(std::chrono::milliseconds(duration));
}
WtpTransmitBackend::WtpTransmitBackend(WtpHostClock &clock,
                                       wtp::SessionOptions options,
                                       std::function<bool()> arm_admission)
    : clock_(clock), options_(std::move(options)), session_(options_),
      arm_admission_(std::move(arm_admission)) {}
WtpTransmitBackend::~WtpTransmitBackend() { session_.disconnect(); }
void WtpTransmitBackend::observed() {
  if (observation_)
    observation_();
}
bool WtpTransmitBackend::fail(std::string error) {
  error_ = std::move(error);
  observed();
  return false;
}
std::uint64_t WtpTransmitBackend::deadline() const {
  return add(clock_.now_ms(), add(options_.transaction_timeout_ms, grace_ms));
}
bool WtpTransmitBackend::pause(std::uint64_t end, bool cancellable) {
  auto before = clock_.now_ms();
  if (before >= end)
    return fail("WTP operation deadline expired; output may be unknown");
  if (cancellable && stopped_)
    return fail("WTP cancellation requested");
  clock_.wait_ms(std::min<std::uint64_t>(10, end - before));
  if (clock_.now_ms() <= before) {
    session_.disconnect();
    return fail("WTP host monotonic clock stalled or regressed");
  }
  return true;
}
bool WtpTransmitBackend::settle(std::uint64_t end, bool cancellable) {
  for (;;) {
    if (clock_.now_ms() >= end)
      return fail("WTP operation deadline expired; output may be unknown");
    const bool needed_status = session_.needs_status();
    session_.poll(clock_.now_ms());
    if (needed_status && !session_.needs_status())
      status_observed_ms_ = clock_.now_ms();
    while (auto result = session_.take_result()) {
      if (result->operation == wtp::Operation::Status &&
          result->kind == wtp::ResultKind::Acknowledged &&
          !session_.needs_status())
        status_observed_ms_ = clock_.now_ms();
      result_ = std::move(result);
    }
    observed();
    auto phase = session_.phase();
    if (phase == wtp::SessionPhase::Disconnected ||
        phase == wtp::SessionPhase::Fault ||
        phase == wtp::SessionPhase::IdentityChanged)
      return fail(session_.diagnostic());
    if (cancellable && stopped_)
      return fail("WTP cancellation requested");
    if (phase == wtp::SessionPhase::Ready && !session_.busy() &&
        !session_.needs_status())
      return true;
    if (!pause(end, cancellable))
      return false;
  }
}
bool WtpTransmitBackend::transact(wtp::Operation op, wtp::RequestBody body,
                                  std::uint64_t end, bool cancellable) {
  if (!settle(end, cancellable))
    return false;
  result_.reset();
  if (op == wtp::Operation::Arm && arm_admission_ && !arm_admission_())
    return fail("WTP scheduler declined ARM admission");
  if (!session_.request(op, std::move(body), clock_.now_ms()))
    return fail(session_.diagnostic());
  observed();
  if (!settle(end, cancellable))
    return false;
  if (!result_ || result_->operation != op ||
      result_->kind != wtp::ResultKind::Acknowledged) {
    if (result_ && result_->response)
      if (auto e = std::get_if<wtp::Error>(&result_->response->body))
        return fail(e->message);
    return fail("WTP transaction unacknowledged; no automatic retry");
  }
  return true;
}
bool WtpTransmitBackend::fresh(std::uint64_t end, bool cancellable) {
  return transact(wtp::Operation::Status, wtp::Empty{}, end, cancellable) &&
         !session_.needs_status();
}
bool WtpTransmitBackend::connect(wtp::ByteStream &stream) {
  error_.clear();
  if (!session_.connect(stream, clock_.now_ms()))
    return fail(session_.diagnostic());
  status_observed_ms_.reset();
  observed();
  return settle(deadline(), false);
}
void WtpTransmitBackend::disconnect() {
  session_.disconnect();
  observed();
}
bool WtpTransmitBackend::schedule(WtpPlanOptions options) {
  if (prepared_ || session_.uncertain() || session_.safety_fault() ||
      session_.owns() || session_.busy() ||
      used_jobs_.contains(options.job_id) || used_jobs_.size() >= 4096)
    return fail("Previous WTP job must be resolved and cleaned up; job "
                "identities cannot be reused");
  schedule_ = std::move(options);
  stopped_ = false;
  executed_ = false;
  return true;
}
BackendInfo WtpTransmitBackend::info() const {
  return {BackendKind::WTP, "WTP",
          "Explicit remote finite-job backend; RF qualification is separate"};
}
BackendCapabilities WtpTransmitBackend::capabilities() const {
  BackendCapabilities c;
  c.output_class = BackendOutputClass::EXTERNAL_CLOCK_RF;
  c.supports_precomputed_execution = true;
  if (session_.phase() != wtp::SessionPhase::Ready ||
      !session_.capabilities() || session_.safety_fault())
    return c;
  for (auto mode : session_.capabilities()->modes) {
    switch (mode) {
    case wtp::Mode::Wspr:
      c.supported_modes |= transmission_mode_bit(TransmissionMode::WSPR);
      break;
    case wtp::Mode::Qrss:
      c.supported_modes |= transmission_mode_bit(TransmissionMode::QRSS);
      break;
    case wtp::Mode::Fskcw:
      c.supported_modes |= transmission_mode_bit(TransmissionMode::FSKCW);
      break;
    case wtp::Mode::Dfcw:
      c.supported_modes |= transmission_mode_bit(TransmissionMode::DFCW);
      break;
    case wtp::Mode::Tone:
      c.supported_modes |= transmission_mode_bit(TransmissionMode::TONE);
      break;
    case wtp::Mode::Cw:
      break;
    }
  }
  // Scalar fields are a bounding range only. Conversion checks every CAPS
  // range.
  for (const auto &r : session_.capabilities()->frequency_ranges) {
    auto low = static_cast<double>(r.minimum_nhz) / 1e9;
    if (!c.min_frequency_hz || low < c.min_frequency_hz)
      c.min_frequency_hz = low;
    c.max_frequency_hz =
        std::max(c.max_frequency_hz, static_cast<double>(r.maximum_nhz) / 1e9);
  }
  c.min_event_duration = std::chrono::nanoseconds(1);
  return c;
}
BackendCompileResult
WtpTransmitBackend::configure(const ExecutionPlan &plan,
                              const BackendExecutionInputs &inputs) {
  error_.clear();
  if (!schedule_ || prepared_ || plan.backend != BackendKind::WTP ||
      !session_.capabilities() || session_.uncertain() ||
      session_.safety_fault() || stopped_)
    return {false,
            {},
            "WTP requires an explicit schedule, negotiated healthy session and "
            "no outstanding job"};
  if (inputs.power_level || inputs.tx_gpio || inputs.configured_tx_gpio ||
      inputs.rp1_development.enabled)
    return {
        false,
        {},
        "WTP has no host GPIO, electrical power or RP1 development controls"};
  auto converted = prepare_wtp_plan(plan, *session_.capabilities(), *schedule_);
  if (!converted)
    return {false, {}, converted.explanation};
  auto policy = evaluate_gpio_band_policy(plan);
  if (!policy.allowed)
    return {false, {}, policy.error};
  if (!fresh(deadline(), true))
    return {false, {}, error_};
  const auto &status = *session_.status();
  if (status.owner_id || status.output_active ||
      (status.state != wtp::State::Empty && !terminal(status.state)))
    return {false, {}, "WTP endpoint is occupied or not safely idle"};
  used_jobs_.insert(schedule_->job_id);
  prepared_ = std::move(converted.prepared);
  adjustments_.clear();
  adjustments_job_id_.clear();
  if (!transact(wtp::Operation::Claim,
                wtp::LeaseRequest{options_.owner_id,
                                  session_.capabilities()->maximum_lease_ms},
                deadline(), true))
    return {false, {}, error_};
  loaded_ = true; // Keep pessimistic evidence even if a write or acknowledgment
                  // is lost.
  if (!transact(wtp::Operation::Load, prepared_->job, deadline(), true)) {
    if (result_ && result_->operation == wtp::Operation::Load &&
        (result_->kind == wtp::ResultKind::NotSent ||
         result_->kind == wtp::ResultKind::Rejected))
      loaded_ = false;
    return {false, {}, error_};
  }
  adjustments_ =
      std::get<wtp::LoadResponse>(result_->response->body).adjustments;
  adjustments_job_id_ = prepared_->job.job_id;
  for (const auto &a : adjustments_) {
    const auto hz = static_cast<double>(a.realized_frequency_nhz) / 1e9;
    const auto policy_adjusted =
        evaluate_frequency_policy(BackendKind::WTP, plan.mode, hz,
                                  plan.policy.allow_unqualified_frequency,
                                  plan.policy.allow_non_amateur_frequency);
    const bool in_range = std::any_of(
        session_.capabilities()->frequency_ranges.begin(),
        session_.capabilities()->frequency_ranges.end(), [&](const auto &r) {
          return a.realized_frequency_nhz >= r.minimum_nhz &&
                 a.realized_frequency_nhz <= r.maximum_nhz;
        });
    if (!policy_adjusted.allowed || !in_range)
      return {false, {}, "Adjusted WTP frequency violates host policy or CAPS"};
  }
  // Per-event nanohertz evidence is exposed by adjustments(), never passed to
  // the legacy controller's uniform-shift adjustment machinery.
  configured_ = true;
  return {true, {}, {}};
}
bool WtpTransmitBackend::renew(std::uint64_t end) {
  if (!session_.lease_valid(clock_.now_ms()))
    return fail("WTP ownership lease expired; job output is not inferred");
  if (!session_.renewal_due(clock_.now_ms()))
    return true;
  return transact(wtp::Operation::Renew,
                  wtp::LeaseRequest{options_.owner_id,
                                    session_.capabilities()->maximum_lease_ms},
                  end, true);
}
bool WtpTransmitBackend::clock_admits(const wtp::Clock &c,
                                      std::uint64_t since) {
  const auto now = clock_.now_ms();
  const auto &a = prepared_->arm;
  const auto &caps = *session_.capabilities();
  if (now < since || now - since > maximum / 1000000)
    return fail("WTP clock observation overflow");
  const auto elapsed = (now - since) * 1000000;
  if (c.state == wtp::ClockState::Unsynchronized ||
      c.leap == wtp::Leap::Unknown ||
      c.uncertainty_ns > a.max_start_uncertainty_ns ||
      c.uncertainty_ns > caps.maximum_arm_uncertainty_ns ||
      elapsed > maximum - c.utc_now_ns ||
      (c.state == wtp::ClockState::Holdover &&
       (!caps.maximum_holdover_age_ns ||
        c.sync_age_ns > caps.maximum_holdover_age_ns ||
        elapsed > caps.maximum_holdover_age_ns - c.sync_age_ns)))
    return fail("WTP device clock is not admissible");
  const auto latest = c.utc_now_ns + elapsed;
  if (a.start_utc_ns < latest ||
      a.start_utc_ns - latest < caps.minimum_arm_lead_ns ||
      a.start_utc_ns < c.utc_now_ns ||
      a.start_utc_ns - c.utc_now_ns > caps.maximum_arm_ahead_ns)
    return fail("WTP start misses device lead time or horizon");
  if (c.leap_transition_utc_ns) {
    auto t = *c.leap_transition_utc_ns;
    if (a.start_utc_ns <= add(t, 1000000000) &&
        a.start_utc_ns + prepared_->job.total_duration_ns >=
            (t > 1000000000 ? t - 1000000000 : 0))
      return fail("WTP job overlaps leap exclusion");
  }
  return true;
}
bool WtpTransmitBackend::terminal_safe() const {
  const auto &e = session_.job_evidence();
  return prepared_ && !session_.needs_status() && e &&
         e->job_id == prepared_->job.job_id && e->authoritative && e->state &&
         terminal(*e->state) && e->output_active == false &&
         e->device_output_active == false && !session_.safety_fault();
}
ExecutionResult WtpTransmitBackend::cancel_execution() {
  auto c = cleanup();
  const auto &e = session_.job_evidence();
  const bool complete = c.ok && e && e->completed();
  const bool cancelled = c.ok && e && e->cancelled();
  const bool fault = !complete && !cancelled;
  return {complete,
          cancelled,
          fault,
          !c.ok ? c.error
          : fault
              ? "WTP cancellation lacks complete or aborted inactive evidence"
              : "",
          true,
          c};
}
ExecutionResult WtpTransmitBackend::execute(const ExecutionPlan &plan) {
  auto failure = [&]() {
    if (stopped_)
      return cancel_execution();
    return ExecutionResult{false, false, true, error_};
  };
  if (!prepared_ || !loaded_ || !configured_ || executed_ || !schedule_ ||
      plan.backend != BackendKind::WTP) {
    fail("WTP has no executable prepared job, or execution was already "
         "attempted");
    return {false, false, true, error_};
  }
  auto converted = prepare_wtp_plan(plan, *session_.capabilities(), *schedule_);
  if (!converted ||
      converted.prepared->plan_id.value != prepared_->plan_id.value ||
      converted.prepared->request_id.value != prepared_->request_id.value ||
      job_bytes(converted.prepared->job) != job_bytes(prepared_->job)) {
    fail("WTP prepared plan changed");
    return failure();
  }
  executed_ = true; // Even a rejected or uncertain ARM is never retried here.
  if (stopped_)
    return cancel_execution();
  if (!renew(deadline()))
    return failure();
  const auto observed = clock_.now_ms();
  if (!transact(wtp::Operation::GetClock, wtp::Empty{}, deadline(), true))
    return failure();
  const auto clock = std::get<wtp::Clock>(result_->response->body);
  if (!clock_admits(clock, observed))
    return failure();
  const auto monitor_end =
      add(observed, add(ceil_ms(prepared_->arm.start_utc_ns - clock.utc_now_ns),
                        add(ceil_ms(prepared_->job.total_duration_ns),
                            add(options_.transaction_timeout_ms, grace_ms))));
  const auto expected_completion =
      add(observed, add(ceil_ms(prepared_->arm.start_utc_ns - clock.utc_now_ns),
                        ceil_ms(prepared_->job.total_duration_ns)));
  const auto quiet_window_ms = add(options_.transaction_timeout_ms, grace_ms);
  const auto quiet_window_start =
      expected_completion > quiet_window_ms
          ? expected_completion - quiet_window_ms
          : observed;
  auto next_monitor_status = add(observed, monitor_status_interval_ms);
  auto next_fallback_status =
      add(expected_completion, completion_fallback_delay_ms);
  if (!transact(wtp::Operation::Arm, prepared_->arm, deadline(), true)) {
    if (!stopped_)
      return failure();
  }
  for (;;) {
    if (stopped_)
      return cancel_execution();
    const auto end = std::min(deadline(), monitor_end);
    // JOB_STATE invalidates the last STATUS and Session immediately obtains a
    // new authoritative STATUS.  Poll the stream without continuously placing
    // another STATUS in flight: a request that straddles the terminal event
    // must be discarded as an older snapshot and otherwise delays RELEASE.
    // Paced observations stop for one full transaction allowance before the
    // predicted completion.  A bounded post-completion fallback retains
    // fail-closed completion when an event is absent.
    if (clock_.now_ms() >= next_fallback_status) {
      if (!fresh(end, true))
        return failure();
      next_fallback_status =
          add(clock_.now_ms(), completion_fallback_delay_ms);
    } else if (clock_.now_ms() >= next_monitor_status &&
               clock_.now_ms() < quiet_window_start) {
      if (!fresh(end, true))
        return failure();
      next_monitor_status =
          add(clock_.now_ms(), monitor_status_interval_ms);
    } else if (!settle(end, true))
      return failure();
    if (terminal_safe()) {
      const auto &e = *session_.job_evidence();
      if (e.completed())
        return {true, false, false, {}};
      if (e.cancelled())
        return {false, true, false, {}};
      fail("WTP device missed the scheduled start");
      return failure();
    }
    if (session_.safety_fault()) {
      fail("WTP remote safety fault; inactive output unconfirmed");
      return failure();
    }
    if (!renew(std::min(deadline(), monitor_end)) ||
        !pause(monitor_end, true)) {
      if (stopped_)
        continue;
      return failure();
    }
  }
}
StartupQuiesceResult WtpTransmitBackend::quiesceForStartup() {
  if (prepared_ || session_.uncertain() || !fresh(deadline(), false))
    return {false, "WTP startup state unresolved"};
  const auto &s = *session_.status();
  bool safe = !session_.safety_fault() && !s.owner_id && !s.output_active &&
              (s.state == wtp::State::Empty || terminal(s.state));
  return {safe,
          safe ? ""
               : "WTP endpoint occupied or unsafe; startup made no mutation"};
}
void WtpTransmitBackend::stop() noexcept { stopped_ = true; }
CleanupResult WtpTransmitBackend::clean() {
  const auto end =
      deadline(); // One budget across observation, ABORT and RELEASE.
  // execute() returns a successful result only after a fresh STATUS/event
  // reduction has established terminal_safe().  While this session still owns
  // that terminal job no other principal can mutate it, so repeating STATUS
  // here adds latency without strengthening the proof.  All other cleanup
  // entries still obtain a fresh observation before deciding whether to abort.
  if (!terminal_safe() && !fresh(end, false))
    return {false, error_};
  if (loaded_ && !terminal_safe()) {
    if (!prepared_ || !session_.owns() || !session_.status()->job_id ||
        *session_.status()->job_id != prepared_->job.job_id)
      return {false,
              "WTP tracked job/output unresolved; no foreign job was aborted"};
    // A completion race can reject ABORT; fresh STATUS still decides outcome.
    const bool acknowledged =
        transact(wtp::Operation::Abort,
                 wtp::AbortRequest{prepared_->job.job_id}, end, false);
    if (!acknowledged && !fresh(end, false))
      return {false, error_};
    if (!terminal_safe())
      return {false, "WTP cleanup lacks matching inactive terminal evidence"};
  }
  if (session_.uncertain() || session_.safety_fault() ||
      session_.status()->output_active ||
      (session_.status()->state != wtp::State::Empty &&
       !terminal(session_.status()->state)))
    return {false, "WTP output or transaction unresolved"};
  bool release_acknowledged = false;
  if (session_.owns()) {
    if (!transact(wtp::Operation::Release, wtp::Empty{}, end, false))
      return {false, error_};
    release_acknowledged = true;
  }
  // A different authenticated principal may legitimately claim the endpoint
  // immediately after our acknowledged RELEASE.  That successor must not make
  // our completed, explicitly inactive job look like a cleanup failure.  Never
  // tolerate active/faulted output, uncertainty, or missing retained evidence,
  // and never abort or release the successor's work.
  const bool inactive_successor =
      release_acknowledged && session_.status()->owner_id &&
      !session_.owns() && terminal_safe();
  if (session_.safety_fault() || session_.uncertain() ||
      session_.needs_status() || session_.status()->output_active ||
      ((session_.status()->owner_id ||
        (session_.status()->state != wtp::State::Empty &&
         !terminal(session_.status()->state))) &&
       !inactive_successor))
    return {false, "WTP endpoint is owned or active after cleanup"};
  prepared_.reset();
  schedule_.reset();
  loaded_ = false;
  configured_ = false;
  error_.clear();
  return {true, {}};
}
CleanupResult WtpTransmitBackend::cleanup() noexcept {
  try {
    return clean();
  } catch (...) {
    return {false, "Exception during WTP cleanup; output is not confirmed"};
  }
}
} // namespace wsprrypi
