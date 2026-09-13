// SPDX-License-Identifier: MIT
#include "WSPR-Transmitter/src/gpio_band_policy.hpp"
#include "WSPR-Transmitter/src/transmission_controller.hpp"
#include "json.hpp"
#include "wtp_backend_peer.hpp"
#include "wtp_integration/backend.hpp"
using namespace wsprrypi;
using namespace backend_test;
using namespace std::chrono_literals;
using Json = nlohmann::json;
namespace {
struct Clock : WtpHostClock {
  Peer &peer;
  std::function<void()> tick;
  bool stall{}, regress{};
  explicit Clock(Peer &p) : peer(p) {}
  std::uint64_t now_ms() const override { return peer.now; }
  void wait_ms(std::uint64_t ms) override {
    if (regress)
      peer.now = peer.now ? peer.now - 1 : 0;
    else if (!stall)
      peer.advance(peer.now + ms);
    if (tick)
      tick();
  }
};
TransmissionRequest request() {
  TransmissionRequest r;
  r.id.value = 7;
  r.output.backend = BackendKind::WTP;
  r.mode = TransmissionMode::TONE;
  r.payload = TonePayload{14097100, 100ms, {}};
  r.policy.allow_unqualified_frequency = true;
  return r;
}
struct Fixture {
  Peer peer;
  Clock clock{peer};
  WtpTransmitBackend backend{clock, {sid, owner_id, device}};
  ExecutionPlanCompiler compiler;
  TransmissionController controller{compiler, backend};
  explicit Fixture(bool extended = false) {
    if (extended) peer.filter = [](Operation op, std::string& reply) {
      if (op != Operation::Caps) return;
      auto j = Json::parse(reply);
      j["body"]["max_events"] = 512;
      j["body"]["max_job_duration_ns"] = "3600000000000";
      reply = j.dump();
    };
    CHECK(backend.capabilities().supported_modes == 0);
    CHECK(backend.connect(peer));
  }
  void schedule(char id = 'a', std::uint64_t ahead_ms = 500) {
    CHECK(backend.schedule({std::string(32, id),
                            Peer::utc_base + peer.mono() + ahead_ms * 1000000,
                            1000}));
  }
  void prepare() {
    schedule();
    auto result = controller.prepare(request());
    if (!result.ok)
      throw std::runtime_error(result.error);
    CHECK(peer.prepares == 1 && peer.executions == 0 &&
          peer.state == State::Loaded);
  }
  unsigned count(Operation op) {
    return std::count(peer.operations.begin(), peer.operations.end(), op);
  }
};
void complete_and_repeat() {
  Fixture f;
  CHECK(f.backend.info().kind == BackendKind::WTP);
  CHECK(f.backend.capabilities().output_class ==
        BackendOutputClass::EXTERNAL_CLOCK_RF);
  CHECK(supports_mode(f.backend.capabilities(), TransmissionMode::TONE));
  CHECK(!supports_mode(f.backend.capabilities(), TransmissionMode::CW));
  CHECK(!evaluate_frequency_policy(BackendKind::WTP, TransmissionMode::TONE,
                                   14097100)
             .allowed);
  CHECK(f.backend.quiesceForStartup().ok);
  f.prepare();
  const auto saved = *f.controller.prepared_plan();
  auto result = f.controller.execute_prepared();
  CHECK(result.ok && !result.stopped && !result.faulted && result.cleanup.ok);
  CHECK(f.peer.executions == 1 && !f.peer.active && !f.peer.owner);
  CHECK(f.count(Operation::Load) == 1 && f.count(Operation::Arm) == 1 &&
        f.count(Operation::Abort) == 0);
  CHECK(!f.backend.execute(saved).ok);
  CHECK(
      !f.backend.schedule({std::string(32, 'a'),
                           Peer::utc_base + f.peer.mono() + 1000000000, 1000}));
  f.schedule('b');
  CHECK(f.controller.prepare(request()).ok);
  CHECK(f.controller.execute_prepared().ok);
  CHECK(f.peer.executions == 2 && f.peer.prepares == 2 &&
        f.count(Operation::Arm) == 2);
}
void hour_job_completion_and_abort() {
  for (unsigned scenario : {0U, 1U, 2U}) {
    Fixture f(true);
    f.schedule();
    auto input = request();
    std::get<TonePayload>(input.payload).duration = 3600s - 1ns;
    CHECK(f.controller.prepare(input).ok);
    CHECK(f.peer.job->total_duration_ns == 3'600'000'000'000ULL);
    f.clock.tick = [&] {
      if ((scenario == 1 && f.peer.state == State::Armed) ||
          (scenario == 2 && f.peer.state == State::Running &&
           f.peer.mono() >= f.peer.start + 120'000'000'000ULL))
        f.backend.stop();
    };
    const auto result = f.controller.execute_prepared();
    CHECK(result.cleanup.ok && !result.faulted && !f.peer.active && !f.peer.owner);
    CHECK(f.count(Operation::Load) == 1 && f.count(Operation::Arm) == 1);
    if (scenario == 0) {
      CHECK(result.ok && !result.stopped);
      CHECK(!f.peer.records.empty() && f.peer.records.front().state == State::Complete);
      CHECK(f.peer.records.front().ended >= f.peer.start + 3'600'000'000'000ULL);
      CHECK(f.count(Operation::Renew) > 100);
      CHECK(f.count(Operation::Abort) == 0);
    } else {
      CHECK(!result.ok && result.stopped && f.count(Operation::Abort) == 1);
      if (scenario == 2) {
        CHECK(f.peer.mono() >= f.peer.start + 120'000'000'000ULL);
        CHECK(f.count(Operation::Renew) > 2);
      }
    }
  }
}
void rejection_before_mutation() {
  for (int kind = 0; kind < 12; ++kind) {
    Fixture f;
    f.schedule();
    auto p = f.compiler.compile(request());
    p.id.value = 1;
    BackendExecutionInputs inputs;
    switch (kind) {
    case 0:
      p.backend = BackendKind::SIMULATED;
      break;
    case 1:
      inputs.power_level = 1;
      break;
    case 2:
      inputs.tx_gpio = 2;
      break;
    case 3:
      inputs.configured_tx_gpio = 4;
      break;
    case 4:
      inputs.rp1_development.enabled = true;
      break;
    case 5:
      p.calibration.ppm = 1;
      break;
    case 6:
      p.events.front().envelope.fade_shape = FadeShape::LINEAR;
      break;
    case 7:
      p.duration_was_explicit = false;
      break;
    case 8:
      p.mode = TransmissionMode::CW;
      break;
    case 9:
      p.policy.allow_unqualified_frequency = false;
      break;
    case 10:
      p.events.front().frequency_hz = 40000000;
      break;
    case 11:
      p.events.front().duration = 0ns;
      break;
    }
    CHECK(!f.backend.configure(p, inputs).ok);
    CHECK(f.count(Operation::Claim) == 0 && f.count(Operation::Load) == 0 &&
          f.count(Operation::Arm) == 0);
  }
  Fixture f;
  f.prepare();
  auto p = *f.controller.prepared_plan();
  p.events.front().frequency_hz += 1;
  CHECK(!f.backend.execute(p).ok && f.count(Operation::Arm) == 0);
  CHECK(f.backend.cleanup().ok);
}
void foreign_and_fault() {
  for (bool active : {false, true}) {
    Fixture f;
    f.peer.owner = std::string(32, 'f');
    f.peer.expiry = 1000000000000ULL;
    if (active) {
      f.peer.job = Job{jid,
                       Mode::Tone,
                       1000000000,
                       {{0, 1000000000, true, 14097100000000000}},
                       {}};
      f.peer.state = State::Running;
      f.peer.active = true;
      f.peer.start = f.peer.mono();
    }
    CHECK(!f.backend.quiesceForStartup().ok);
    f.schedule();
    CHECK(!f.controller.prepare(request()).ok);
    CHECK(!f.backend.cleanup().ok);
    CHECK(f.count(Operation::Claim) == 0 && f.count(Operation::Abort) == 0 &&
          f.count(Operation::Release) == 0);
  }
  Fixture f;
  f.prepare();
  // Use a matching status with an impossible active terminal state.
  f.peer.filter = [&](Operation op, std::string &reply) {
    if (op == Operation::Arm)
      f.peer.record(State::Complete, true);
    if (op == Operation::Status && f.peer.state == State::Complete) {
      auto j = Json::parse(reply);
      j["body"]["output_active"] = true;
      reply = j.dump();
    }
  };
  auto r = f.controller.execute_prepared();
  CHECK(!r.ok && r.faulted && !r.cleanup.ok &&
        f.backend.session().safety_fault());
  CHECK(!f.backend.schedule({std::string(32, 'b'), 1, 1}));
}
void clocks() {
  for (int kind = 0; kind < 8; ++kind) {
    Fixture f;
    f.prepare();
    f.peer.filter = [&, kind](Operation op, std::string &reply) {
      if (op != Operation::GetClock)
        return;
      auto j = Json::parse(reply);
      auto &c = j["body"];
      switch (kind) {
      case 0:
        c["state"] = "unsynchronized";
        break;
      case 1:
        c["uncertainty_ns"] = "1001";
        break;
      case 2:
        c["state"] = "holdover";
        c["sync_age_ns"] = "1000000001";
        break;
      case 3:
        c["leap"] = "unknown";
        break;
      case 4:
        c["utc_now_ns"] =
            std::to_string(Peer::utc_base + f.peer.mono() + 500000000);
        break;
      case 5:
        c["utc_now_ns"] = "1";
        break;
      case 6:
        c["leap"] = "insert_pending";
        c["leap_transition_utc_ns"] =
            std::to_string(Peer::utc_base + f.peer.mono() + 500000000);
        break;
      case 7:
        f.peer.advance(f.peer.now + 600);
        break; // Old observation cannot admit a late ARM.
      }
      reply = j.dump();
    };
    auto r = f.controller.execute_prepared();
    if (r.ok || f.count(Operation::Arm) != 0 || !r.cleanup.ok)
      throw std::runtime_error("clock case " + std::to_string(kind) + ": " +
                               r.error + " cleanup: " + r.cleanup.error);
    CHECK(!r.ok && f.count(Operation::Arm) == 0 && r.cleanup.ok);
  }
}
void cancellation() {
  for (int timing = 0; timing < 4; ++timing) {
    Fixture f;
    f.prepare();
    if (timing == 0)
      f.backend.stop();
    else
      f.clock.tick = [&] {
        if ((timing == 1 && f.peer.state == State::Armed) ||
            (timing == 2 && f.peer.state == State::Running) ||
            (timing == 3 && f.peer.state == State::Complete))
          f.backend.stop();
      };
    auto r = f.controller.execute_prepared();
    CHECK(r.cleanup.ok && !r.faulted && !f.peer.active && !f.peer.owner);
    CHECK(timing == 3 ? r.ok && !r.stopped : r.stopped && !r.ok);
    CHECK(f.count(Operation::Arm) == (timing == 0 ? 0U : 1U));
  }
  Fixture f;
  f.prepare();
  f.peer.fail_abort = true;
  f.backend.stop();
  auto r = f.controller.execute_prepared();
  CHECK(r.faulted && !r.stopped && !r.cleanup.ok);
}
void unknown_and_reconnect() {
  for (auto op : {Operation::Claim, Operation::Load, Operation::Arm,
                  Operation::Abort, Operation::Release}) {
    Fixture f;
    if (op == Operation::Claim || op == Operation::Load) {
      f.schedule();
      f.peer.lose_reply = op;
      CHECK(!f.controller.prepare(request()).ok);
    } else {
      f.prepare();
      f.peer.lose_reply = op;
      if (op == Operation::Abort)
        f.backend.stop();
      CHECK(!f.controller.execute_prepared().cleanup.ok);
    }
    CHECK(f.backend.session().uncertain());
    auto loads = f.count(Operation::Load), arms = f.count(Operation::Arm);
    f.peer.open();
    CHECK(f.backend.connect(f.peer));
    CHECK(f.count(Operation::Load) == loads && f.count(Operation::Arm) == arms);
    auto c = f.backend.cleanup();
    if (!c.ok)
      throw std::runtime_error(c.error);
    CHECK(!f.peer.active && !f.peer.owner);
    CHECK(f.count(Operation::Load) == loads && f.count(Operation::Arm) == arms);
  }
  for (int kind = 0; kind < 3; ++kind) {
    Fixture f;
    f.prepare();
    f.peer.lose_reply = Operation::Arm;
    CHECK(!f.backend.execute(*f.controller.prepared_plan()).ok);
    if (kind == 0)
      f.peer.boot_id = std::string(32, 'b');
    if (kind == 1)
      f.peer.device_id = std::string(32, 'c');
    if (kind == 2) {
      f.peer.job.reset();
      f.peer.state = State::Empty;
      f.peer.records.clear();
    }
    f.peer.open();
    if (kind < 2)
      CHECK(!f.backend.connect(f.peer));
    else
      CHECK(f.backend.connect(f.peer));
    CHECK(!f.backend.cleanup().ok);
    CHECK(f.count(Operation::Arm) == 1 && f.count(Operation::Load) == 1);
  }
}
void adjustments_and_renewal() {
  for (bool invalid : {false, true}) {
    Fixture f;
    f.schedule();
    auto r = request();
    r.policy.allow_quantization = true;
    f.peer.filter = [&](Operation op, std::string &reply) {
      if (op != Operation::Load)
        return;
      auto j = Json::parse(reply);
      j["body"]["adjustments"] = {
          {{"event_index", 0},
           {"requested_frequency_nhz", "14097100000000000"},
           {"realized_frequency_nhz",
            invalid ? "31000000000000000" : "14097100000000001"}}};
      reply = j.dump();
    };
    auto p = f.compiler.compile(r);
    p.id.value = 1;
    auto c = invalid ? f.backend.configure(p, {}) : f.controller.prepare(r);
    CHECK(c.ok != invalid);
    CHECK(f.backend.adjustments().size() == 1);
    if (!invalid) {
      CHECK(c.adjustments.empty() &&
            f.controller.prepared_plan()->events.front().frequency_hz ==
                14097100);
      CHECK(f.controller.execute_prepared().ok);
    } else {
      CHECK(!f.backend.execute(p).ok && f.count(Operation::Arm) == 0);
      CHECK(f.backend.cleanup().ok);
    }
  }
  Fixture f;
  // The peer grants a shorter negotiated lease; renewal uses the actual grant.
  f.peer.filter = [&](Operation op, std::string &reply) {
    if (op == Operation::Claim || op == Operation::Renew) {
      auto j = Json::parse(reply);
      j["body"]["granted_lease_ms"] = 5000;
      f.peer.expiry = f.peer.mono() + 5000000000ULL;
      j["body"]["expires_monotonic_ns"] = std::to_string(f.peer.expiry);
      reply = j.dump();
    }
  };
  f.schedule();
  auto r = request();
  std::get<TonePayload>(r.payload).duration = 6s;
  CHECK(f.controller.prepare(r).ok && f.controller.execute_prepared().ok);
  CHECK(f.count(Operation::Renew) >= 2 && f.peer.executions == 1);
}
void cleanup_races_and_faults() {
  Fixture f;
  f.prepare();
  f.clock.tick = [&] {
    if (f.peer.state == State::Running)
      f.backend.stop();
  };
  f.peer.before_handle = [&](Operation op) {
    if (op == Operation::Abort)
      f.peer.advance(f.peer.now + 200);
  };
  auto r = f.controller.execute_prepared();
  CHECK(r.ok && !r.stopped && r.cleanup.ok && f.count(Operation::Abort) == 1);
  CHECK(!f.backend.execute(f.compiler.compile(request())).ok);

  Fixture g;
  g.prepare();
  g.peer.filter = [&](Operation op, std::string &) {
    if (op == Operation::Release)
      g.peer.advisory(
          0, "DEVICE_FAULT",
          R"({"state":"failed","output_active":false,"error":{"code":"DEVICE_FAULT","message":"fault during release","retryable":false}})");
  };
  auto fault = g.controller.execute_prepared();
  CHECK(!fault.ok && fault.faulted && !fault.cleanup.ok &&
        g.backend.session().safety_fault());

  Fixture h;
  h.prepare();
  h.peer.before_handle = [&](Operation op) {
    if (op == Operation::Arm)
      h.peer.advance(h.peer.now + 1000);
  };
  auto late = h.controller.execute_prepared();
  CHECK(!late.ok && late.cleanup.ok && h.peer.executions == 0 &&
        h.count(Operation::Arm) == 1);
  CHECK(!h.backend.execute(h.compiler.compile(request())).ok &&
        h.count(Operation::Arm) == 1);
}
void missed_is_not_cancelled() {
  Fixture f;
  f.prepare();
  f.peer.before_handle = [&](Operation op) {
    if (op == Operation::Abort)
      f.peer.record(State::Missed);
  };
  f.backend.stop();
  auto r = f.controller.execute_prepared();
  CHECK(!r.ok && !r.stopped && r.faulted && r.cleanup.ok);
  CHECK(f.count(Operation::Arm) == 0);
}
void nonuniform_adjustments() {
  Fixture f;
  f.schedule();
  struct Fixed : IExecutionPlanCompiler {
    ExecutionPlan p;
    ExecutionPlan compile(const TransmissionRequest &) const override {
      return p;
    }
  } compiler;
  compiler.p = f.compiler.compile(request());
  compiler.p.policy.allow_quantization = true;
  compiler.p.events.front().duration = 50ms;
  auto second = compiler.p.events.front();
  second.offset_from_start = 50ms;
  second.frequency_hz = 14097101;
  compiler.p.events.insert(compiler.p.events.begin() + 1, second);
  compiler.p.summary.event_count = compiler.p.events.size();
  compiler.p.summary.max_frequency_hz = 14097101;
  TransmissionController controller(compiler, f.backend);
  f.peer.filter = [](Operation op, std::string &reply) {
    if (op != Operation::Load)
      return;
    auto j = Json::parse(reply);
    j["body"]["adjustments"] = {
        {{"event_index", 0},
         {"requested_frequency_nhz", "14097100000000000"},
         {"realized_frequency_nhz", "14097100000000001"}},
        {{"event_index", 1},
         {"requested_frequency_nhz", "14097101000000000"},
         {"realized_frequency_nhz", "14097101000000003"}}};
    reply = j.dump();
  };
  auto c = controller.prepare(request());
  CHECK(c.ok && c.adjustments.empty() && f.backend.adjustments().size() == 2);
  CHECK(controller.prepared_plan()->events.at(0).frequency_hz == 14097100);
  CHECK(controller.prepared_plan()->events.at(1).frequency_hz == 14097101);
  CHECK(controller.execute_prepared().ok);
}
void bounded_waits() {
  for (int kind = 0; kind < 3; ++kind) {
    Fixture f;
    f.prepare();
    auto before = f.peer.now;
    if (kind == 0)
      f.peer.stall_read = true;
    if (kind == 1)
      f.clock.stall = true;
    if (kind == 2)
      f.clock.regress = true;
    auto r = f.controller.execute_prepared();
    CHECK(!r.ok && r.faulted && !r.cleanup.ok);
    CHECK(f.peer.now <= before + 10000 && f.count(Operation::Arm) <= 1);
  }
}
} // namespace
int main() {
  try {
    complete_and_repeat();
    hour_job_completion_and_abort();
    rejection_before_mutation();
    foreign_and_fault();
    clocks();
    cancellation();
    unknown_and_reconnect();
    adjustments_and_renewal();
    cleanup_races_and_faults();
    missed_is_not_cancelled();
    nonuniform_adjustments();
    bounded_waits();
    std::cout << checks << " WTP backend checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
