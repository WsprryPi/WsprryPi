// SPDX-License-Identifier: MIT
#include "json.hpp"
#include "wtp_backend_peer.hpp"
#include "wtp_integration/scheduler.hpp"
#include <limits>
#include <thread>
using namespace wsprrypi;
using namespace backend_test;
using namespace std::chrono_literals;
namespace {
struct Clock : WtpScheduleClock {
  Peer &peer;
  std::int64_t utc_shift{};
  bool valid{true}, stalled{};
  std::function<void()> tick, observe_tick;
  explicit Clock(Peer &p) : peer(p) {}
  std::uint64_t now_ms() const override { return peer.now; }
  std::optional<std::uint64_t> utc_now_ns() const override {
    if (observe_tick)
      observe_tick();
    return valid ? std::optional<std::uint64_t>(Peer::utc_base + peer.mono() +
                                                utc_shift)
                 : std::nullopt;
  }
  void wait_ms(std::uint64_t ms) override {
    if (!stalled)
      peer.advance(peer.now + ms);
    if (tick)
      tick();
  }
};
ScheduledSlot slot(std::uint64_t start, std::int64_t offset = 0) {
  return {std::chrono::system_clock::time_point(
              std::chrono::duration_cast<std::chrono::system_clock::duration>(
                  std::chrono::nanoseconds(start))),
          std::chrono::nanoseconds(offset)};
}
TransmissionRequest request(std::uint64_t start) {
  TransmissionRequest r;
  r.id.value = 7;
  r.slot = slot(start);
  r.output.backend = BackendKind::WTP;
  r.mode = TransmissionMode::TONE;
  r.payload = TonePayload{14097100, 100ms, {}};
  r.policy.allow_unqualified_frequency = true;
  return r;
}
struct Fixture {
  Peer peer;
  Clock clock{peer};
  WtpScheduler scheduler{clock, {sid, owner_id, device}};
  Fixture() { CHECK(scheduler.connect(peer)); }
  TransmissionRequest future(std::uint64_t ahead_ms = 8000) {
    return request(*clock.utc_now_ns() + ahead_ms * 1000000);
  }
  void submit(TransmissionRequest r, char id = 'a', WtpSchedulePolicy p = {}) {
    if (!scheduler.submit(r, std::string(32, id), 1000, p))
      throw std::runtime_error(scheduler.diagnostic());
  }
  unsigned count(Operation op) const {
    return std::count(peer.operations.begin(), peer.operations.end(), op);
  }
};
void long_wait_connection_budget() {
  Fixture f;
  const auto start = f.future(60000);
  f.submit(start);
  auto progress = f.peer.now;
  unsigned waiting_status = 0;
  f.peer.before_handle = [&](Operation op) {
    progress = f.peer.now;
    if (f.scheduler.phase() == WtpSchedulePhase::Waiting) {
      CHECK(op == Operation::Status);
      ++waiting_status;
      CHECK(!f.peer.owner && !f.peer.job);
    }
  };
  f.clock.tick = [&] {
    if (f.peer.now - progress >= 30000)
      f.peer.close();
  };
  auto result = f.scheduler.run();
  CHECK(result.outcome == WtpScheduleOutcome::Complete);
  CHECK(waiting_status >= 10);
  CHECK(result.start_utc_ns == *wtp_slot_utc_ns(start.slot));
  CHECK(f.count(Operation::Claim) == 1 && f.count(Operation::Load) == 1 &&
        f.count(Operation::Arm) == 1 && f.peer.executions == 1);
}
void waiting_status_failures() {
  for (int fault = 0; fault < 5; ++fault) {
    Fixture f;
    f.submit(f.future(60000));
    bool injected = false;
    f.peer.before_handle = [&](Operation op) {
      if (injected || f.scheduler.phase() != WtpSchedulePhase::Waiting ||
          op != Operation::Status)
        return;
      injected = true;
      if (fault == 0) f.peer.lose_reply = Operation::Status;
      if (fault == 1) f.peer.boot_id = std::string(32, '9');
      if (fault == 2) f.scheduler.request_stop();
      if (fault == 3) f.scheduler.invalidate_pending();
      if (fault == 4) f.peer.advance(f.peer.now + 60000);
    };
    const auto result = f.scheduler.run();
    CHECK(injected);
    CHECK(f.count(Operation::Claim) == 0 && f.count(Operation::Load) == 0 &&
          f.count(Operation::Arm) == 0 && !result.arm_handed_off);
    if (fault < 2) {
      CHECK(result.outcome == WtpScheduleOutcome::Blocked);
      CHECK(!f.scheduler.submit(f.future(), std::string(32, 'b'), 1000));
    } else if (fault == 2) {
      CHECK(result.outcome == WtpScheduleOutcome::Cancelled);
      f.submit(f.future(60000), 'b');
      CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Complete);
    } else if (fault == 3) {
      CHECK(result.outcome == WtpScheduleOutcome::Invalidated);
    } else {
      CHECK(result.outcome != WtpScheduleOutcome::Complete);
    }
  }
}
void arithmetic() {
  CHECK(wtp_slot_utc_ns(slot(1000000000, 1)) == 1000000001);
  CHECK(wtp_slot_utc_ns(slot(1000000000, -1)) == 999999999);
  CHECK(!wtp_slot_utc_ns(slot(0)));
  CHECK(!wtp_slot_utc_ns(slot(1, -1)));
  CHECK(!wtp_slot_utc_ns(slot(1, std::numeric_limits<std::int64_t>::min())));
  ScheduledSlot pre{std::chrono::system_clock::time_point(-1s), 2s};
  CHECK(!wtp_slot_utc_ns(pre));
  WtpSystemScheduleClock invalid([] { return false; });
  CHECK(!invalid.utc_now_ns());
  bool threw = false;
  try {
    WtpSystemScheduleClock missing({});
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}
void early_and_frozen() {
  Fixture f;
  auto r = f.future();
  r.slot.start_offset = 1234567ns;
  const auto expected = *wtp_slot_utc_ns(r.slot);
  f.submit(r);
  std::get<TonePayload>(r.payload).frequency_hz = 15000000;
  r.slot = slot(1);
  CHECK(!f.scheduler.submit(r, std::string(32, 'b'), 1000));
  std::uint64_t load_time{}, arm_time{};
  f.peer.before_handle = [&](Operation op) {
    if (op == Operation::Load)
      load_time = *f.clock.utc_now_ns();
    if (op == Operation::Arm)
      arm_time = *f.clock.utc_now_ns();
  };
  auto report = f.scheduler.run();
  CHECK(report.outcome == WtpScheduleOutcome::Complete &&
        report.execution.cleanup.ok && report.error.empty());
  CHECK(report.start_utc_ns == expected && report.request_id.value == 7 &&
        report.arm_handed_off);
  CHECK(load_time >= report.dispatch_utc_ns && load_time < expected &&
        arm_time < expected);
  CHECK(report.dispatch_utc_ns == expected - 6001000000ULL);
  CHECK(f.peer.executions == 1 && !f.peer.active && !f.peer.owner);
  CHECK(f.count(Operation::Load) == 1 && f.count(Operation::Arm) == 1);
  for (auto &payload : f.peer.payloads) {
    auto j = nlohmann::json::parse(payload);
    if (j["op"] == "ARM")
      CHECK(j["body"]["start_utc_ns"] == std::to_string(expected));
    if (j["op"] == "LOAD")
      CHECK(j["body"]["events"][0]["frequency_nhz"] == "14097100000000000");
  }
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Failed);
  CHECK(!f.scheduler.submit(f.future(), std::string(32, 'a'), 1000));
  f.submit(f.future(), 'b');
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Complete &&
        f.peer.executions == 2);
}
void wspr_slot_and_complete_plan() {
  Fixture f;
  auto r = f.future();
  r.mode = TransmissionMode::WSPR;
  const auto base =
      (*f.clock.utc_now_ns() / 120000000000ULL + 1) * 120000000000ULL;
  r.slot = slot(base, 1000000000);
  WsprPayload w;
  w.base_frequency_hz = 14097100;
  w.prepared.power_dbm = 20;
  w.prepared.frames.resize(1);
  for (std::size_t i = 0; i < 162; ++i)
    w.prepared.frames[0].symbols[i] = i % 4;
  r.payload = w;
  f.submit(r);
  auto report = f.scheduler.run();
  CHECK(report.outcome == WtpScheduleOutcome::Complete &&
        report.start_utc_ns == base + 1000000000);
  CHECK(f.count(Operation::Load) == 1 && f.count(Operation::Arm) == 1 &&
        f.count(Operation::Renew) >= 3);
  for (const auto &payload : f.peer.payloads) {
    auto j = nlohmann::json::parse(payload);
    if (j["op"] == "LOAD") {
      CHECK(j["body"]["events"].size() == 162);
      CHECK(j["body"]["total_duration_ns"] == "110591999892");
    }
  }
  auto paired = f.future();
  paired.mode = TransmissionMode::WSPR;
  w.prepared.frames.resize(2);
  paired.payload = w;
  CHECK(!f.scheduler.submit(paired, std::string(32, 'b'), 1000));
  CHECK(f.count(Operation::Load) == 1);
}
void reject() {
  for (int kind = 0; kind < 12; ++kind) {
    Fixture f;
    auto r = f.future();
    WtpSchedulePolicy p;
    switch (kind) {
    case 0:
      r.slot = slot(*f.clock.utc_now_ns() + 1000000000);
      break;
    case 1:
      f.clock.valid = false;
      break;
    case 2:
      p.preparation_ms = 10000;
      break;
    case 3:
      p.arm_submission_ms = 0;
      break;
    case 4:
      p.maximum_wait_ms = 10;
      break;
    case 5:
      p.preparation_ms = std::numeric_limits<std::uint64_t>::max();
      break;
    case 6:
      r.output.gpio = 4;
      break;
    case 7:
      r.output.output = ClockSource::GPIO_CLK;
      break;
    case 8:
      r.policy.hardware_profile = HardwareProfile::RP1_GPCLK;
      break;
    case 9:
      r.calibration.ppm = 1;
      break;
    case 10:
      std::get<TonePayload>(r.payload).duration.reset();
      break;
    case 11:
      r.id.value = 0;
      break;
    }
    CHECK(!f.scheduler.submit(r, std::string(32, 'a'), 1000, p));
    CHECK(f.count(Operation::Claim) == 0 && f.count(Operation::Load) == 0);
  }
}
void clock_attacks() {
  for (int kind = 0; kind < 9; ++kind) {
    Fixture f;
    f.submit(f.future(), 'a',
             kind == 7 ? WtpSchedulePolicy{3000, 1000, 100, 86400000}
                       : WtpSchedulePolicy{});
    bool injected = false;
    f.clock.tick = [&] {
      if (injected)
        return;
      if (kind == 0 || kind == 1 || kind == 2 || kind == 3) {
        injected = true;
        if (kind == 0)
          f.clock.utc_shift += 1000000000;
        if (kind == 1)
          f.clock.utc_shift -= 1000000000;
        if (kind == 2)
          f.clock.valid = false;
        if (kind == 3)
          f.peer.advance(f.peer.now + 9000);
      }
    };
    f.peer.before_handle = [&](Operation op) {
      if (op == Operation::Load && !injected) {
        injected = true;
        if (kind == 4)
          f.clock.utc_shift += 1000000000;
        if (kind == 5)
          f.clock.utc_shift -= 1000000000;
        if (kind == 6)
          f.clock.valid = false;
        if (kind == 7)
          f.peer.advance(f.peer.now + 3500);
        if (kind == 8)
          f.peer.advance(f.peer.now + 5500);
      }
    };
    auto report = f.scheduler.run();
    CHECK(report.outcome == (kind == 8 ? WtpScheduleOutcome::Blocked
                                       : WtpScheduleOutcome::Failed));
    CHECK(f.count(Operation::Arm) == 0 && f.peer.executions == 0);
    if (kind < 4)
      CHECK(f.count(Operation::Load) == 0);
    else if (kind < 8)
      CHECK(report.execution.cleanup.ok && !f.peer.owner);
    else
      CHECK(!report.execution.cleanup.ok);
  }
  Fixture f;
  f.submit(f.future());
  f.peer.before_handle = [&](Operation op) {
    if (op == Operation::Arm)
      f.clock.utc_shift += 1000000000000ULL;
  };
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Complete);
  Fixture g;
  g.submit(g.future());
  g.clock.stalled = true;
  CHECK(g.scheduler.run().outcome == WtpScheduleOutcome::Failed &&
        g.count(Operation::Load) == 0);
  Fixture h;
  h.submit(h.future());
  h.peer.filter = [](Operation op, std::string &reply) {
    if (op != Operation::GetClock)
      return;
    auto j = nlohmann::json::parse(reply);
    j["body"]["utc_now_ns"] = "1";
    reply = j.dump();
  };
  CHECK(h.scheduler.run().outcome == WtpScheduleOutcome::Failed &&
        h.count(Operation::Arm) == 0);
}
void stop_and_reload() {
  for (int stage = 0; stage < 4; ++stage) {
    for (bool reload : {false, true}) {
      Fixture f;
      f.submit(f.future());
      bool signalled = false;
      auto signal = [&] {
        signalled = true;
        if (reload)
          f.scheduler.invalidate_pending();
        else
          f.scheduler.request_stop();
      };
      if (stage == 0)
        signal();
      if (stage == 1)
        f.peer.before_handle = [&](Operation op) {
          if (op == Operation::Claim && !signalled)
            signal();
        };
      if (stage == 2)
        f.peer.before_handle = [&](Operation op) {
          if (op == Operation::Load && !signalled)
            signal();
        };
      if (stage == 3)
        f.clock.tick = [&] {
          if (f.peer.state == State::Running && !signalled)
            signal();
        };
      auto report = f.scheduler.run();
      CHECK(signalled);
      if (reload && stage == 0)
        CHECK(report.outcome == WtpScheduleOutcome::Invalidated &&
              !report.reload_deferred);
      else if (reload)
        CHECK(report.outcome == WtpScheduleOutcome::Complete &&
              report.reload_deferred);
      else
        CHECK(report.outcome == WtpScheduleOutcome::Cancelled);
      CHECK(f.scheduler.phase() == WtpSchedulePhase::Idle && !f.peer.active &&
            !f.peer.owner);
      if (stage == 0)
        CHECK(f.count(Operation::Claim) == 0);
      if (!reload && stage < 3)
        CHECK(f.count(Operation::Arm) == 0);
    }
  }
}
void concurrent_commit_boundary() {
  for (bool reload : {false, true}) {
    Fixture f;
    auto request = f.future();
    const auto dispatch = *wtp_slot_utc_ns(request.slot) - 6001000000ULL;
    f.submit(request);
    bool signalled = false;
    // Signal after run() checked its flags but before its preparation CAS.
    f.clock.observe_tick = [&] {
      if (signalled || Peer::utc_base + f.peer.mono() < dispatch)
        return;
      signalled = true;
      std::thread signal([&] {
        if (reload)
          f.scheduler.invalidate_pending();
        else
          f.scheduler.request_stop();
      });
      signal.join();
    };
    auto report = f.scheduler.run();
    CHECK(signalled && f.count(Operation::Claim) == 0 &&
          f.count(Operation::Load) == 0);
    CHECK(report.outcome == (reload ? WtpScheduleOutcome::Invalidated
                                    : WtpScheduleOutcome::Cancelled));
    CHECK(!report.reload_deferred);
  }
  Fixture f;
  f.submit(f.future());
  f.clock.tick = [&] { f.peer.advance(f.peer.now - 20); };
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Failed &&
        f.count(Operation::Claim) == 0);
}
void blocked_recovery() {
  Fixture f;
  f.submit(f.future());
  f.peer.lose_reply = Operation::Arm;
  auto report = f.scheduler.run();
  CHECK(report.outcome == WtpScheduleOutcome::Blocked &&
        f.scheduler.phase() == WtpSchedulePhase::Blocked);
  CHECK(!f.scheduler.submit(f.future(), std::string(32, 'b'), 1000));
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
  f.scheduler.invalidate_pending();
  f.scheduler.request_stop();
  CHECK(!f.scheduler.recover().ok);
  f.peer.open();
  CHECK(f.scheduler.connect(f.peer));
  CHECK(f.scheduler.phase() == WtpSchedulePhase::Blocked);
  CHECK(f.count(Operation::Arm) == 1 && f.count(Operation::Load) == 1);
  CHECK(f.scheduler.recover().ok && !f.peer.active && !f.peer.owner);
  f.submit(f.future(), 'b');
  CHECK(f.scheduler.run().outcome == WtpScheduleOutcome::Complete);
  CHECK(f.count(Operation::Load) == 2 && f.count(Operation::Arm) == 2);

  Fixture g;
  g.submit(g.future());
  g.peer.lose_reply = Operation::Arm;
  CHECK(g.scheduler.run().outcome == WtpScheduleOutcome::Blocked);
  g.peer.boot_id = std::string(32, 'b');
  g.peer.open();
  CHECK(!g.scheduler.connect(g.peer) && !g.scheduler.recover().ok);
  CHECK(g.count(Operation::Arm) == 1 && g.count(Operation::Load) == 1);
}
} // namespace
int main() {
  try {
    long_wait_connection_budget();
    waiting_status_failures();
    arithmetic();
    early_and_frozen();
    wspr_slot_and_complete_plan();
    reject();
    clock_attacks();
    stop_and_reload();
    concurrent_commit_boundary();
    blocked_recovery();
    std::cout << checks << " WTP scheduler checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
