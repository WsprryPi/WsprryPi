// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "scheduler.hpp"
#include "wtp_settings.hpp"
#include <mutex>
#include <thread>

namespace wsprrypi {
// Application owner: operations serialize; only stop/status may overlap run.
// Completion is pulled by the application loop, never invoked on the worker.
class WtpApplication {
public:
  WtpApplication(WtpScheduleClock &, wtp::ByteStream &, WtpSettings,
                 wtp::SessionOptions, std::function<bool()> reopen);
  ~WtpApplication();
  StartupQuiesceResult inspect();
  bool poll_idle(); // Bounded read-only STATUS, at most once per second while idle.
  void prepare(TransmissionRequest);
  void prepare_skip();
  std::uint64_t preparation_lead_ns() const;
  bool skipping() const noexcept { return skip_pending_.load(); }
  void start();
  CleanupResult stop();
  CleanupResult recover();
  bool idle_management(const std::function<void()> &);
  bool replaceable() const;
  bool ready() const;
  bool active() const noexcept { return active_.load(); }
  WtpRuntimeStatus status() const { return scheduler_.status(); }
  std::optional<WtpScheduleReport> take_completion();
  TransmissionMode mode() const noexcept { return mode_.load(); }
  WtpSchedulePhase phase() const noexcept { return scheduler_.phase(); }
  void invalidate() noexcept { scheduler_.invalidate_pending(); }

private:
  StartupQuiesceResult connect_idle();
  bool observe_idle_if_due(); // Called only by the serialized transport owner.
  void join();
  WtpScheduleReport run_skip();
  WtpScheduleClock &clock_;
  wtp::ByteStream &stream_;
  WtpSettings settings_;
  std::function<bool()> reopen_;
  WtpScheduler scheduler_;
  mutable std::mutex control_;
  std::thread worker_;
  std::atomic_bool active_{false}, ready_{false}, skip_pending_{false},
      skip_stop_{false};
  bool idle_detached_{};
  std::optional<std::uint64_t> last_idle_poll_ms_;
  std::uint64_t skip_start_ns_{};
  std::atomic<TransmissionMode> mode_{TransmissionMode::WSPR};
  std::mutex completion_mutex_;
  std::optional<WtpScheduleReport> completion_;
  std::uint64_t request_sequence_{};
  std::string job_prefix_;
};
std::string wtp_random_identity();
bool wtp_host_utc_valid() noexcept;
} // namespace wsprrypi
