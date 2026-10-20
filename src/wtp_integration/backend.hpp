// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "WSPR-Transmitter/src/transmission_backend.hpp"
#include "WTP-Client/include/wtp/session.hpp"
#include "execution_plan.hpp"
#include <atomic>
#include <functional>
#include <set>

namespace wsprrypi {
// One monotonic domain, including across reconnect. wait_ms must return after
// advancing time; production waits are bounded to 10 ms by the backend.
class WtpHostClock {
public:
  virtual ~WtpHostClock() = default;
  virtual std::uint64_t now_ms() const = 0;
  virtual void wait_ms(std::uint64_t duration) = 0;
};
class WtpSteadyClock final : public WtpHostClock {
public:
  std::uint64_t now_ms() const override;
  void wait_ms(std::uint64_t duration) override;
};

// Explicit developer construction, not a production factory registration.
// Clock and connected stream outlive this object. Only stop() is thread safe.
class WtpTransmitBackend final : public ITransmissionBackend {
public:
  // Optional owner-thread gate runs after reconciliation, immediately before
  // requesting ARM. It may veto; it cannot replace device clock admission.
  WtpTransmitBackend(WtpHostClock &, wtp::SessionOptions,
                     std::function<bool()> arm_admission = {});
  ~WtpTransmitBackend() override;
  bool connect(wtp::ByteStream &); // explicit same-session reconciliation only
  void disconnect();               // does NOT establish inactive output
  bool schedule(WtpPlanOptions);   // only after successful cleanup
  BackendInfo info() const override;
  BackendCapabilities capabilities() const override;
  BackendCompileResult configure(const ExecutionPlan &,
                                 const BackendExecutionInputs &) override;
  ExecutionResult execute(const ExecutionPlan &) override;
  StartupQuiesceResult quiesceForStartup() override;
  void stop() noexcept override;
  CleanupResult cleanup() noexcept override;
  const wtp::Session &session() const noexcept { return session_; }
  const std::vector<wtp::Adjustment> &adjustments() const noexcept {
    return adjustments_;
  }
  const std::string &diagnostic() const noexcept { return error_; }

private:
  friend class WtpScheduler;
  // Owner-thread publication hook; no caller callback or transport activity.
  std::function<void()> observation_;
  std::optional<std::uint64_t> status_observed_ms_;
  void observed();
  bool settle(std::uint64_t deadline, bool cancellable);
  bool pause(std::uint64_t deadline, bool cancellable);
  bool transact(wtp::Operation, wtp::RequestBody, std::uint64_t deadline,
                bool cancellable);
  bool fresh(std::uint64_t deadline, bool cancellable);
  bool renew(std::uint64_t deadline);
  bool clock_admits(const wtp::Clock &, std::uint64_t observed_since);
  bool terminal_safe() const;
  bool fail(std::string);
  std::uint64_t deadline() const;
  CleanupResult clean();
  ExecutionResult cancel_execution();
  WtpHostClock &clock_;
  const wtp::SessionOptions options_;
  wtp::Session session_;
  std::function<bool()> arm_admission_;
  std::atomic_bool stopped_{false};
  std::optional<WtpPlanOptions> schedule_;
  std::optional<WtpPreparedPlan> prepared_;
  std::optional<wtp::TransactionResult> result_;
  std::vector<wtp::Adjustment> adjustments_;
  std::string adjustments_job_id_;
  std::set<std::string> used_jobs_;
  std::string error_;
  bool loaded_{}, configured_{}, executed_{};
};
} // namespace wsprrypi
