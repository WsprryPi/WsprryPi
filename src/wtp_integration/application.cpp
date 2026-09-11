// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "application.hpp"
#include <random>
#include <stdexcept>
#if defined(__linux__)
#include <sys/timex.h>
#endif

namespace wsprrypi {
std::string wtp_random_identity() {
  std::random_device random;
  static constexpr char hex[] = "0123456789abcdef";
  std::string id(32, '0');
  for (auto &c : id)
    c = hex[random() & 15];
  return id;
}
bool wtp_host_utc_valid() noexcept {
#if defined(__linux__)
  timex observation{}; // modes=0 is read-only.
  const int result = adjtimex(&observation);
  return result >= 0 && result != TIME_ERROR &&
         !(observation.status & (STA_UNSYNC | STA_CLOCKERR)) &&
         observation.maxerror >= 0 && observation.maxerror <= 500000;
#else
  return false;
#endif
}
WtpApplication::WtpApplication(WtpScheduleClock &clock, wtp::ByteStream &stream,
                               WtpSettings settings,
                               wtp::SessionOptions options,
                               std::function<bool()> reopen)
    : clock_(clock), stream_(stream), settings_(std::move(settings)),
      reopen_(std::move(reopen)), scheduler_(clock, std::move(options)),
      job_prefix_(wtp_random_identity().substr(0, 16)) {}
WtpApplication::~WtpApplication() {
  stop();
  scheduler_.disconnect();
}
void WtpApplication::join() {
  if (worker_.joinable())
    worker_.join();
}
StartupQuiesceResult WtpApplication::connect_idle() {
  if (scheduler_.phase() != WtpSchedulePhase::Idle)
    return {false, "Pico work requires explicit recovery"};
  const auto phase = scheduler_.status().session_phase;
  if (phase == wtp::SessionPhase::IdentityChanged ||
      phase == wtp::SessionPhase::Fault)
    return {false, "Pico identity or protocol fault remains latched"};
  if (phase != wtp::SessionPhase::Ready) {
    scheduler_.disconnect();
    if (!reopen_() || !scheduler_.connect(stream_)) {
      ready_ = false;
      return {false, "Pico endpoint could not negotiate; inspect endpoint "
                     "identity and connection"};
    }
  }
  auto result = scheduler_.inspect_idle();
  last_idle_poll_ms_ = clock_.now_ms();
  ready_ = result.ok;
  if (result.ok) idle_detached_ = false;
  return result;
}
StartupQuiesceResult WtpApplication::inspect() {
  std::lock_guard lock(control_);
  if (active_ || skip_pending_)
    return {false, "Pico work is active"};
  join();
  return connect_idle();
}
bool WtpApplication::poll_idle() {
  std::unique_lock lock(control_, std::try_to_lock);
  if (!lock || active_ || skip_pending_ ||
      scheduler_.phase() != WtpSchedulePhase::Idle)
    return false;
  join();
  return observe_idle_if_due();
}
bool WtpApplication::observe_idle_if_due() {
  // A healthy negotiated session may be observed, including a foreign owner.
  // Disconnection, identity/fault latches and unresolved work still need the
  // existing explicit inspection/recovery path; never reconnect or resubmit here.
  if (scheduler_.status().session_phase != wtp::SessionPhase::Ready)
    return false;
  const auto now = clock_.now_ms();
  if (last_idle_poll_ms_ && now >= *last_idle_poll_ms_ &&
      now - *last_idle_poll_ms_ < 1000)
    return false;
  last_idle_poll_ms_ = now;
  ready_ = scheduler_.inspect_idle().ok;
  return true;
}
std::uint64_t WtpApplication::preparation_lead_ns() const {
  const auto s = scheduler_.status();
  if (!s.capabilities ||
      s.capabilities->minimum_arm_lead_ns > 86400000000000ULL - 8000000000ULL)
    throw std::runtime_error(
        "Pico preparation lead is unavailable or exceeds one day");
  return std::max<std::uint64_t>(
      8000000000ULL, s.capabilities->minimum_arm_lead_ns + 7000000000ULL);
}
void WtpApplication::prepare(TransmissionRequest request) {
  std::lock_guard lock(control_);
  if (active_ || skip_pending_)
    throw std::runtime_error("Pico work is active");
  join();
  if (!ready_ || scheduler_.phase() != WtpSchedulePhase::Idle)
    throw std::runtime_error(
        "Pico requires explicit reconciliation before new work");
  if (++request_sequence_ > 4096)
    throw std::runtime_error(
        "Pico session job capacity exhausted; stop safely before restarting");
  request.id.value = request_sequence_;
  if (request.output.backend != BackendKind::WTP || request.output.gpio ||
      request.output.output != ClockSource::UNSPECIFIED ||
      request.calibration.ppm != 0)
    throw std::runtime_error(
        "Pico request contains incompatible host output controls");
  request.policy.allow_quantization = settings_.allow_frequency_adjustment;
  if (request.mode == TransmissionMode::WSPR) {
    auto &prepared = std::get<WsprPayload>(request.payload).prepared;
    if (prepared.frames.empty())
      throw std::runtime_error("Pico WSPR frame missing");
    const auto index = prepared.current_frame ? prepared.current_frame - 1 : 0;
    if (index >= prepared.frames.size())
      throw std::runtime_error("Pico WSPR frame index invalid");
    auto frame = prepared.frames[index];
    prepared.frames = {std::move(frame)};
    prepared.current_frame = 1;
  }
  if (request.slot.start_time.time_since_epoch().count() == 0) {
    const auto utc = clock_.utc_now_ns();
    const auto lead = preparation_lead_ns();
    if (!utc || *utc > INT64_MAX - lead - 120000000000ULL)
      throw std::runtime_error(
          "Host UTC is not synchronized for Pico scheduling");
    const auto earliest = *utc + lead;
    const auto start =
        request.mode == TransmissionMode::WSPR
            ? ((earliest - 1000000000ULL + 119999999999ULL) / 120000000000ULL) *
                      120000000000ULL +
                  1000000000ULL
            : earliest;
    request.slot.start_time = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(start)));
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string suffix(16, '0');
  auto number = request_sequence_;
  for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) {
    *it = hex[number & 15];
    number >>= 4;
  }
  mode_ = request.mode;
  if (!scheduler_.submit(std::move(request), job_prefix_ + suffix,
                         settings_.start_uncertainty_ns))
    throw std::runtime_error(scheduler_.diagnostic());
}
void WtpApplication::prepare_skip() {
  std::lock_guard lock(control_);
  if (active_ || skip_pending_ || !ready_ ||
      scheduler_.phase() != WtpSchedulePhase::Idle)
    throw std::runtime_error("Pico is not idle for a skipped window");
  join();
  const auto utc = clock_.utc_now_ns();
  if (!utc || *utc > INT64_MAX - 128000000000ULL)
    throw std::runtime_error(
        "Host UTC is not synchronized for a skipped window");
  skip_start_ns_ =
      ((*utc + 7000000000ULL + 119999999999ULL) / 120000000000ULL) *
          120000000000ULL +
      1000000000ULL;
  skip_stop_ = false;
  skip_pending_ = true;
  mode_ = TransmissionMode::WSPR;
}
WtpScheduleReport WtpApplication::run_skip() {
  WtpScheduleReport result;
  result.start_utc_ns = skip_start_ns_;
  const auto deadline = clock_.now_ms() + 130000;
  while (!skip_stop_) {
    (void)observe_idle_if_due(); // A skipped slot submits no job, but retains its idle session.
    const auto utc = clock_.utc_now_ns();
    if (!utc || clock_.now_ms() > deadline) {
      result.error = "Host UTC unavailable or changed during skipped window";
      break;
    }
    if (*utc >= skip_start_ns_) {
      result.outcome = WtpScheduleOutcome::Complete;
      result.skipped = true;
      break;
    }
    clock_.wait_ms(20);
  }
  if (skip_stop_)
    result.outcome = WtpScheduleOutcome::Cancelled;
  skip_pending_ = false;
  return result;
}
void WtpApplication::start() {
  std::lock_guard lock(control_);
  if (active_)
    throw std::runtime_error("Pico worker already active");
  join();
  if (scheduler_.phase() != WtpSchedulePhase::Waiting && !skip_pending_)
    throw std::runtime_error("No prepared Pico request");
  active_ = true;
  try {
    worker_ = std::thread([this] {
      auto result = skip_pending_ ? run_skip() : scheduler_.run();
      if (result.outcome == WtpScheduleOutcome::Blocked)
        ready_ = false;
      {
        std::lock_guard lock(completion_mutex_);
        completion_ = std::move(result);
      }
      active_ = false;
    });
  } catch (...) {
    active_ = false;
    scheduler_.request_stop();
    scheduler_.run();
    throw;
  }
}
CleanupResult WtpApplication::stop() {
  std::lock_guard lock(control_);
  skip_stop_ = true;
  scheduler_.request_stop();
  join();
  skip_pending_ = false;
  if (scheduler_.phase() == WtpSchedulePhase::Waiting ||
      scheduler_.phase() == WtpSchedulePhase::Invalidated)
    scheduler_.run();
  {
    std::lock_guard lock(completion_mutex_);
    completion_.reset();
  }
  const auto s = scheduler_.status();
  return {s.phase != WtpSchedulePhase::Blocked, s.diagnostic};
}
CleanupResult WtpApplication::recover() {
  std::lock_guard lock(control_);
  if (active_ || skip_pending_ ||
      (scheduler_.phase() != WtpSchedulePhase::Idle &&
       scheduler_.phase() != WtpSchedulePhase::Blocked))
    return {false, "Stop Pico work before reconciliation"};
  join();
  if (scheduler_.phase() == WtpSchedulePhase::Idle) {
    auto result = connect_idle();
    return {result.ok, result.error};
  }
  if (scheduler_.status().session_phase == wtp::SessionPhase::IdentityChanged ||
      scheduler_.status().session_phase == wtp::SessionPhase::Fault)
    return {false, "Pico identity or protocol fault remains latched"};
  scheduler_.disconnect();
  if (!reopen_() || !scheduler_.connect(stream_))
    return {false, "Pico reconnect failed; original job remains unresolved"};
  auto result = scheduler_.recover();
  ready_ = result.ok;
  return result;
}
bool WtpApplication::ready() const {
  return ready_ && scheduler_.status().phase != WtpSchedulePhase::Blocked;
}
bool WtpApplication::replaceable() const {
  const auto s = scheduler_.status();
  if (idle_detached_ && !active_ && !skip_pending_ &&
      s.phase == WtpSchedulePhase::Idle && !s.uncertain && !s.safety_fault)
    return true;
  // A path that never negotiated and never submitted work may be corrected.
  if (!active_ && !skip_pending_ && s.phase == WtpSchedulePhase::Idle &&
      !s.identity && s.job_id.empty() && !s.uncertain && !s.safety_fault &&
      s.session_phase == wtp::SessionPhase::Disconnected)
    return true;
  return !active_ && !skip_pending_ && s.phase == WtpSchedulePhase::Idle &&
         !s.uncertain && !s.safety_fault &&
         s.session_phase == wtp::SessionPhase::Ready && s.remote &&
         !s.remote->owner_id && !s.remote->output_active &&
         s.remote->state != wtp::State::Loaded &&
         s.remote->state != wtp::State::Armed &&
         s.remote->state != wtp::State::Running;
}
bool WtpApplication::idle_management(const std::function<void()> &request) {
  std::lock_guard lock(control_);
  if (active_ || skip_pending_ || scheduler_.phase() != WtpSchedulePhase::Idle)
    return false;
  join();
  if (!connect_idle().ok || !replaceable()) return false;
  scheduler_.disconnect();
  ready_ = false;
  idle_detached_ = true; // Fresh unowned/inactive evidence preceded deliberate close.
  try { request(); }
  catch (...) { (void)connect_idle(); throw; }
  (void)connect_idle(); // Read-only same-session negotiation; never recovery.
  return true;
}
std::optional<WtpScheduleReport> WtpApplication::take_completion() {
  if (active_)
    return {};
  std::lock_guard lock(completion_mutex_);
  auto result = std::move(completion_);
  completion_.reset();
  return result;
}
} // namespace wsprrypi
