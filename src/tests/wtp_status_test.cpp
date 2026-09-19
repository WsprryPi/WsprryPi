// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "json.hpp"
#include "wtp_backend_peer.hpp"
#include "wtp_integration/scheduler.hpp"
#include <thread>
using namespace wsprrypi;
using namespace backend_test;
using namespace std::chrono_literals;
namespace {
struct Clock : WtpScheduleClock {
  Peer peer;
  std::function<void()> tick;
  std::uint64_t now_ms() const override { return peer.now; }
  std::optional<std::uint64_t> utc_now_ns() const override {
    return Peer::utc_base + peer.mono();
  }
  void wait_ms(std::uint64_t n) override {
    peer.advance(peer.now + n);
    if (tick)
      tick();
  }
};
struct Fixture {
  Clock clock;
  Peer &peer{clock.peer};
  WtpScheduler scheduler{clock, {sid, owner_id, device}};
  Fixture() { CHECK(scheduler.connect(peer)); }
  void submit(char id = 'a', bool adjustment = false) {
    TransmissionRequest r;
    r.id.value = static_cast<unsigned>(id);
    r.output.backend = BackendKind::WTP;
    r.mode = TransmissionMode::TONE;
    r.payload = TonePayload{14097100, 200ms, {}};
    r.policy.allow_unqualified_frequency = true;
    r.policy.allow_quantization = adjustment;
    r.slot.start_time = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(*clock.utc_now_ns() + 8000000000ULL)));
    CHECK(scheduler.submit(r, std::string(32, id), 1000));
  }
  unsigned count(Operation op) const {
    return std::count(peer.operations.begin(), peer.operations.end(), op);
  }
};
void lifecycle() {
  Fixture f;
  auto idle = f.scheduler.status();
  CHECK(idle.remote && idle.remote->state == State::Empty &&
        !idle.remote->output_active && idle.status_observed_ms &&
        idle.identity->device_id == device && idle.capabilities);
  auto n = f.peer.operations.size();
  f.scheduler.status();
  CHECK(f.peer.operations.size() == n);
  f.submit();
  CHECK(f.scheduler.status().phase == WtpSchedulePhase::Waiting &&
        !f.scheduler.status().job && !f.scheduler.status().last_report);
  bool loaded = false, armed = false, running = false, unknown = false;
  auto last_peer_state = f.peer.state;
  std::uint64_t event_id = 0;
  f.clock.tick = [&] {
    if (f.peer.state != last_peer_state) {
      last_peer_state = f.peer.state;
      if (last_peer_state == State::Running ||
          last_peer_state == State::Complete)
        f.peer.advisory(++event_id);
    }
    auto s = f.scheduler.status();
    CHECK(s.observed_ms <= f.clock.now_ms());
    if (s.status_observed_ms)
      CHECK(*s.status_observed_ms <= s.observed_ms);
    if (!s.remote)
      unknown = true;
    if (!s.job)
      return;
    CHECK(s.job->job_id == s.job_id && s.job->device_id == device &&
          s.job->boot_id == boot && s.job->authoritative);
    if (s.job->state == State::Loaded)
      loaded = true;
    if (s.job->state == State::Armed) {
      if (*f.clock.utc_now_ns() < s.start_utc_ns)
        armed = true;
      CHECK(s.arm_handed_off && s.job->output_active == false);
    }
    if (s.job->state == State::Running) {
      running = true;
      CHECK(s.arm_handed_off && s.job->output_active == true);
    }
  };
  f.peer.before_handle = [&](Operation) { f.clock.tick(); };
  auto result = f.scheduler.run();
  if (result.outcome != WtpScheduleOutcome::Complete)
    std::cerr << result.error << " / " << result.execution.cleanup.error
              << "\n";
  CHECK(result.outcome == WtpScheduleOutcome::Complete && result.job &&
        result.job->completed());
  CHECK(loaded && armed && running && unknown);
  auto done = f.scheduler.status();
  CHECK(done.phase == WtpSchedulePhase::Idle && done.job->completed() &&
        done.last_report->job->completed() && !done.owns && !done.lease_valid);
  f.peer.before_handle = {};
  f.clock.tick = {};
  CHECK(f.scheduler.disconnect());
  auto offline = f.scheduler.status();
  CHECK(!offline.remote && !offline.job && !offline.capabilities &&
        !offline.status_observed_ms && offline.last_report->job->completed());
  auto j = nlohmann::json::parse(wtp_runtime_status_json(offline));
  CHECK(j["remote"].is_null() && j["job"].is_null() &&
        j["last_report"]["job"]["output_active"] == false);
  f.peer.open();
  CHECK(f.scheduler.connect(f.peer));
  f.submit('b');
  auto next = f.scheduler.status();
  CHECK(!next.job && next.adjustments.empty() &&
        next.job_id == std::string(32, 'b') &&
        next.last_report->job_id == std::string(32, 'a'));
  f.scheduler.request_stop();
  auto cancelled = f.scheduler.run();
  CHECK(cancelled.outcome == WtpScheduleOutcome::Cancelled && !cancelled.job &&
        !cancelled.execution.cleanup_attempted && f.count(Operation::Arm) == 1);
}
void recovery_matrix() {
  for (auto op : {Operation::Claim, Operation::Load, Operation::Arm,
                  Operation::Abort, Operation::Release}) {
    Fixture f;
    f.submit();
    f.peer.lose_reply = op;
    if (op == Operation::Abort)
      f.clock.tick = [&] {
        if (f.peer.state == State::Running)
          f.scheduler.request_stop();
      };
    CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
    auto blocked = f.scheduler.status();
    CHECK(blocked.recovery_required && !blocked.remote && !blocked.job &&
          blocked.uncertain);
    CHECK(!f.scheduler.recover().ok);
    CHECK(!f.scheduler.status().last_recovery->cleanup.ok);
    f.clock.tick = {};
    f.peer.open();
    CHECK(f.scheduler.connect(f.peer));
    CHECK(f.scheduler.status().recovery_required);
    const auto loads = f.count(Operation::Load), arms = f.count(Operation::Arm);
    CHECK(f.scheduler.recover().ok);
    auto recovered = f.scheduler.status();
    CHECK(!recovered.recovery_required && recovered.last_recovery->cleanup.ok &&
          recovered.last_report->outcome == WtpScheduleOutcome::Blocked &&
          recovered.remote && !recovered.remote->output_active &&
          !recovered.owns && recovered.diagnostic.empty());
    CHECK(f.count(Operation::Load) == loads && f.count(Operation::Arm) == arms);
  }
}
void unsafe_recovery() {
  for (int attack = 0; attack < 5; ++attack) {
    Fixture f;
    f.submit();
    f.peer.lose_reply = Operation::Arm;
    CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
    if (attack == 0)
      f.peer.boot_id = std::string(32, 'b');
    if (attack == 1)
      f.peer.device_id = std::string(32, 'b');
    if (attack == 2) { // Missing record is not safe retransmission evidence.
      f.peer.owner.reset();
      f.peer.job.reset();
      f.peer.records.clear();
      f.peer.state = State::Empty;
      f.peer.active = false;
    }
    if (attack == 3) {
      f.peer.owner = std::string(32, 'f');
      f.peer.job->job_id = std::string(32, 'f');
    }
    if (attack == 4) {
      f.peer.state = State::Failed;
      f.peer.active = true;
    }
    f.peer.open();
    CHECK(f.scheduler.connect(f.peer) == (attack >= 2));
    const auto aborts = f.count(Operation::Abort);
    CHECK(!f.scheduler.recover().ok);
    auto s = f.scheduler.status();
    CHECK(s.recovery_required && !s.last_recovery->cleanup.ok &&
          s.last_report->outcome == WtpScheduleOutcome::Blocked);
    if (attack < 2)
      CHECK(s.session_phase == SessionPhase::IdentityChanged && !s.remote);
    if (attack == 2)
      CHECK(s.remote && !s.remote->output_active && !s.job && s.uncertain);
    if (attack == 3)
      CHECK(f.count(Operation::Abort) == aborts && !s.owns && !s.job);
    if (attack == 4) {
      CHECK(s.safety_fault);
      f.peer.record(
          State::Aborted); // A later idle label cannot clear the fault.
      CHECK(!f.scheduler.recover().ok && f.scheduler.status().safety_fault);
    }
    CHECK(f.count(Operation::Arm) == 1 && f.count(Operation::Load) == 1);
  }
  Fixture foreign;
  foreign.peer.owner = std::string(32, 'f');
  foreign.peer.expiry = std::numeric_limits<std::uint64_t>::max();
  foreign.submit();
  CHECK(foreign.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
  CHECK(foreign.count(Operation::Claim) == 0 &&
        foreign.count(Operation::Abort) == 0 &&
        foreign.count(Operation::Release) == 0);
}
void missed_and_cancelled() {
  for (bool missed : {false, true}) {
    Fixture f;
    f.submit();
    f.clock.tick = [&] {
      if (f.peer.state == State::Running) {
        if (missed)
          f.peer.record(State::Missed);
        else
          f.scheduler.request_stop();
      }
    };
    auto r = f.scheduler.run();
    CHECK(r.outcome == (missed ? WtpScheduleOutcome::Failed
                               : WtpScheduleOutcome::Cancelled));
    CHECK(r.job && r.job->state == (missed ? State::Missed : State::Aborted) &&
          r.job->output_active == false && r.execution.cleanup.ok);
  }
}
void adjustment_and_history_binding() {
  Fixture f;
  f.submit('a', true);
  f.peer.filter = [&](Operation op, std::string &reply) {
    if (op != Operation::Load)
      return;
    auto j = nlohmann::json::parse(reply);
    j["body"]["adjustments"] = nlohmann::json::array(
        {{{"event_index", 0},
          {"requested_frequency_nhz", "14097100000000000"},
          {"realized_frequency_nhz", "14097100000000001"}}});
    reply = j.dump();
  };
  f.peer.lose_reply = Operation::Release;
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
  auto s = f.scheduler.status();
  CHECK(!s.job && s.adjustments.size() == 1 &&
        s.last_report->adjustments.size() == 1 &&
        s.last_report->identity->boot_id == boot);
  CHECK(!f.scheduler.recover().ok);
  CHECK(f.scheduler.status().last_recovery->job_id == std::string(32, 'a') &&
        f.scheduler.status().last_recovery->identity->device_id == device);
  f.peer.open();
  CHECK(f.scheduler.connect(f.peer));
  CHECK(f.scheduler.recover().ok);
  f.peer.filter = {};
  f.submit('b');
  CHECK(f.scheduler.status().adjustments.empty());
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Complete);
  s = f.scheduler.status();
  CHECK(s.last_report->job_id == std::string(32, 'b') &&
        s.last_recovery->job_id == std::string(32, 'a') &&
        s.last_report->adjustments.empty());
  // A historical completed record cannot make current foreign output inactive.
  f.submit('c');
  f.peer.lose_reply = Operation::Arm;
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
  f.peer.record(State::Complete);
  f.peer.job->job_id = std::string(32, 'f');
  f.peer.owner = std::string(32, 'f');
  f.peer.state = State::Running;
  f.peer.active = true;
  f.peer.start = f.peer.mono() + 100000000000ULL;
  f.peer.open();
  CHECK(f.scheduler.connect(f.peer));
  s = f.scheduler.status();
  CHECK(s.job && s.job->state == State::Complete &&
        s.job->output_active == false && s.job->device_output_active == true &&
        !s.job->completed());
  CHECK(!f.scheduler.recover().ok && f.count(Operation::Abort) == 0);
}
void exact_json() {
  WtpRuntimeStatus s;
  s.request_id.value = s.start_utc_ns = s.observed_ms = UINT64_MAX;
  s.adjustments.push_back({7, UINT64_MAX, UINT64_MAX - 1});
  auto j = nlohmann::json::parse(wtp_runtime_status_json(s));
  CHECK(j["request_id"] == "18446744073709551615" &&
        j["start_utc_ns"] == "18446744073709551615" &&
        j["adjustments"][0]["realized_frequency_nhz"] ==
            "18446744073709551614");
  CHECK(j["job"].is_null() && j["remote"].is_null() &&
        j["last_report"].is_null());
}
void concurrent_readers() {
  Fixture f;
  f.submit();
  std::atomic_bool done{false}, started{false}, valid{true};
  std::atomic<unsigned> reads{};
  std::thread reader([&] {
    std::uint64_t revision = 0;
    do {
      auto s = f.scheduler.status();
      if (s.revision < revision ||
          (s.job && (!s.remote || s.job->job_id != s.job_id || !s.identity ||
                     s.job->boot_id != s.identity->boot_id)))
        valid = false;
      revision = s.revision;
      auto j = nlohmann::json::parse(wtp_runtime_status_json(s));
      if (j["job_id"] != s.job_id)
        valid = false;
      ++reads;
      started = true;
    } while (!done);
  });
  while (!started)
    std::this_thread::yield();
  auto result = f.scheduler.run();
  done = true;
  reader.join();
  CHECK(result.outcome == WtpScheduleOutcome::Complete && valid && reads > 0);
}
} // namespace
int main() {
  try {
    lifecycle();
    recovery_matrix();
    unsafe_recovery();
    missed_and_cancelled();
    adjustment_and_history_binding();
    exact_json();
    concurrent_readers();
    std::cout << checks << " WTP status/recovery checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
