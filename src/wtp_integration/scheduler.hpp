// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "WSPR-Transmitter/src/transmission_controller.hpp"
#include "backend.hpp"
#include <functional>
#include <mutex>

namespace wsprrypi {
// Checked epoch arithmetic, including signed offsets. No floating-point time.
std::optional<std::uint64_t> wtp_slot_utc_ns(const ScheduledSlot &) noexcept;
class WtpScheduleClock : public WtpHostClock {
public:
  // nullopt means host UTC is unavailable/untrusted, independent of device UTC.
  virtual std::optional<std::uint64_t> utc_now_ns() const = 0;
};
class WtpSystemScheduleClock final : public WtpScheduleClock {
public:
  // Required provider attests current host UTC validity; no implicit trust.
  explicit WtpSystemScheduleClock(std::function<bool()> utc_valid);
  std::uint64_t now_ms() const override;
  void wait_ms(std::uint64_t) override;
  std::optional<std::uint64_t> utc_now_ns() const override;

private:
  std::function<bool()> utc_valid_;
  WtpSteadyClock steady_;
};
struct WtpSchedulePolicy {
  std::uint64_t preparation_ms{5000}, arm_submission_ms{1000};
  std::uint64_t clock_step_tolerance_ms{100}, maximum_wait_ms{86400000};
};
enum class WtpSchedulePhase {
  Idle,
  Waiting,
  Invalidated,
  Preparing,
  Executing,
  Blocked
};
enum class WtpScheduleOutcome {
  Complete,
  Cancelled,
  Invalidated,
  Failed,
  Blocked
};
struct WtpScheduleReport {
  WtpScheduleOutcome outcome{WtpScheduleOutcome::Failed};
  RequestId request_id{};
  std::string job_id, error;
  std::uint64_t start_utc_ns{}, dispatch_utc_ns{};
  bool arm_handed_off{}, reload_deferred{}, skipped{};
  ExecutionResult execution{};
  std::optional<wtp::HelloResponse> identity{};
  std::optional<wtp::JobEvidence> job{};
  std::vector<wtp::Adjustment> adjustments{};
};
// Published observations, not a live electrical measurement. All remote fields
// belong to observed_ms; status_observed_ms identifies the last accepted STATUS.
// last_report and last_recovery are historical and never change current output.
struct WtpRecoveryReport {
  std::uint64_t observed_ms{};
  CleanupResult cleanup;
  RequestId request_id{};
  std::string job_id;
  std::optional<wtp::HelloResponse> identity;
  std::optional<wtp::JobEvidence> job{};
};
struct WtpRuntimeStatus {
  std::uint64_t revision{}, observed_ms{};
  WtpSchedulePhase phase{WtpSchedulePhase::Idle};
  wtp::SessionPhase session_phase{wtp::SessionPhase::Disconnected};
  RequestId request_id{};
  std::string job_id, session_id, owner_id;
  std::uint64_t start_utc_ns{}, dispatch_utc_ns{};
  bool arm_handed_off{}, stop_requested{}, reload_requested{};
  bool uncertain{}, safety_fault{}, owns{}, lease_valid{}, recovery_required{};
  std::optional<wtp::HelloResponse> identity; // Last verified identity, even offline.
  std::optional<wtp::Capabilities> capabilities; // Current negotiation only.
  std::optional<wtp::Status> remote; // Absent on invalidation/disconnect.
  std::optional<std::uint64_t> status_observed_ms;
  std::optional<wtp::JobEvidence> job{}; // Only the selected request's job identity.
  std::vector<wtp::Adjustment> adjustments{};
  std::optional<WtpScheduleReport> last_report;
  std::optional<WtpRecoveryReport> last_recovery;
  std::string diagnostic, session_diagnostic;
};
// Data serialization only: no UI/websocket publication or production selection.
std::string wtp_runtime_status_json(const WtpRuntimeStatus &);
// Dedicated early scheduler for explicitly constructed WTP development runtime.
// One owner thread calls connect/submit/run/recover/disconnect and reads
// reports. Only request_stop(), invalidate_pending() and phase() may be
// concurrent with run(). status() may be read from any thread. Clock and stream
// outlive this object. No threads, devices or fallback are created.
class WtpScheduler {
public:
  WtpScheduler(WtpScheduleClock &, wtp::SessionOptions, std::function<bool()> dispatch_admission = {});
  bool connect(wtp::ByteStream &);
  bool disconnect(); // local closure only, while Idle/Blocked
  bool submit(TransmissionRequest, std::string job_id,
              std::uint64_t uncertainty_ns, WtpSchedulePolicy = {});
  WtpScheduleReport run(); // one frozen job, including bounded cleanup
  void request_stop() noexcept;
  // Reload cancels waiting work and defers after commitment.
  void invalidate_pending() noexcept;
  // Explicit reconciliation after same-session reconnect.
  CleanupResult recover();
  StartupQuiesceResult inspect_idle(); // Read-only, owner thread, Idle only.
  WtpRuntimeStatus status() const; // Coherent copy, no transport/clock access.
  WtpSchedulePhase phase() const noexcept { return phase_.load(); }
  const std::string &diagnostic() const noexcept { return error_; }
  const WtpTransmitBackend &backend() const noexcept { return backend_; }

private:
  struct Pending {
    TransmissionRequest request;
    WtpPlanOptions options;
    WtpSchedulePolicy policy;
    std::uint64_t dispatch{}, arm_notice{}, anchor_utc{}, anchor_mono{};
  };
  void publish(); // Sole owner only. Readers never touch backend/session.
  bool fail(std::string);
  std::optional<std::uint64_t> observe();
  bool admit_arm();
  WtpScheduleReport finish(WtpScheduleOutcome, std::string,
                           ExecutionResult = {});
  WtpScheduleClock &clock_;
  std::function<bool()> dispatch_admission_;
  WtpTransmitBackend backend_;
  ExecutionPlanCompiler compiler_;
  TransmissionController controller_;
  std::optional<Pending> pending_;
  std::set<std::string> used_jobs_;
  std::atomic<WtpSchedulePhase> phase_{WtpSchedulePhase::Idle};
  std::atomic_bool stopped_{false}, invalidated_{false};
  bool handed_off_{};
  std::string error_;
  std::optional<WtpScheduleReport> last_report_;
  std::optional<WtpRecoveryReport> last_recovery_;
  mutable std::mutex status_mutex_;
  WtpRuntimeStatus status_;
};
} // namespace wsprrypi
