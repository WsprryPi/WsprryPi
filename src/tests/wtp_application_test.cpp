// SPDX-License-Identifier: MIT
#include "json.hpp"
#include "wtp_backend_peer.hpp"
#include "wtp_integration/application.hpp"
#include "wtp_settings_json.hpp"
#include <thread>
using namespace wsprrypi;
using namespace backend_test;
using namespace std::chrono_literals;
namespace {
struct Clock : WtpScheduleClock {
  Peer peer;
  std::uint64_t now_ms() const override { return peer.now; }
  std::optional<std::uint64_t> utc_now_ns() const override {
    return Peer::utc_base + peer.mono();
  }
  void wait_ms(std::uint64_t n) override { peer.advance(peer.now + n); }
};
WtpSettings settings() {
  return {"/dev/ttyACM1", "serial", device, 0xcafe, 0x4012, 1000};
}
struct Fixture {
  Clock clock;
  WtpApplication app{
      clock, clock.peer, settings(), {sid, owner_id, device}, [&] {
        clock.peer.open();
        return true;
      }};
  Fixture() { CHECK(app.inspect().ok); }
  TransmissionRequest tone() {
    TransmissionRequest request;
    request.output.backend = BackendKind::WTP;
    request.mode = TransmissionMode::TONE;
    request.payload = TonePayload{14097100, 200ms, {}};
    request.policy.allow_unqualified_frequency = true;
    return request;
  }
  WtpScheduleReport finish() {
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto result = app.take_completion())
        return *result;
      std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("worker deadline expired");
  }
};
void config() {
  auto s = settings();
  CHECK(parse_wtp_settings(wtp_settings_json(s), true) == s);
  CHECK(parse_wtp_settings(nlohmann::json::object(), false) == WtpSettings{});
  for (const auto &key : {"Endpoint", "USB Serial", "Device ID",
                          "USB Vendor ID", "USB Product ID"}) {
    auto j = wtp_settings_json(s);
    j.erase(key);
    bool rejected = false;
    try {
      parse_wtp_settings(j, true);
    } catch (const std::exception &) {
      rejected = true;
    }
    CHECK(rejected);
  }
  for (auto value : {nlohmann::json(-1), nlohmann::json(65536),
                     nlohmann::json(1.5), nlohmann::json(true)}) {
    auto j = wtp_settings_json(s);
    j["USB Vendor ID"] = value;
    bool rejected = false;
    try {
      parse_wtp_settings(j, true);
    } catch (const std::exception &) {
      rejected = true;
    }
    CHECK(rejected);
  }
}
void idle_observation() {
  Fixture f;
  auto count = [&] { return std::count(f.clock.peer.operations.begin(),
      f.clock.peer.operations.end(), Operation::Status); };
  const auto initial = count();
  CHECK(!f.app.poll_idle());
  for (int second = 0; second < 8; ++second) {
    f.clock.peer.advance(f.clock.peer.now + 1000);
    CHECK(f.app.poll_idle());
    CHECK(!f.app.poll_idle());
    CHECK(f.app.ready() && f.app.status().session_id == sid);
  }
  CHECK(count() == initial + 8);
  CHECK(f.clock.peer.executions == 0 && f.clock.peer.prepares == 0);
  // Read-only observation of a foreign owner must neither claim nor clean it.
  f.clock.peer.owner = std::string(32, '8');
  f.clock.peer.expiry = f.clock.peer.mono() + 60000000000ULL;
  f.clock.peer.advance(f.clock.peer.now + 1000);
  CHECK(f.app.poll_idle() && !f.app.ready());
  CHECK(f.clock.peer.owner == std::string(32, '8'));
  CHECK(std::count(f.clock.peer.operations.begin(), f.clock.peer.operations.end(), Operation::Abort) == 0);
  CHECK(std::count(f.clock.peer.operations.begin(), f.clock.peer.operations.end(), Operation::Release) == 0);
  f.clock.peer.owner.reset();
  f.clock.peer.advance(f.clock.peer.now + 1000);
  CHECK(f.app.poll_idle() && f.app.ready());
  f.app.prepare(f.tone());
  const auto prepared_count = count();
  f.clock.peer.advance(f.clock.peer.now + 1000);
  CHECK(!f.app.poll_idle() && count() == prepared_count);
  CHECK(f.app.stop().ok);
  // A broken established transport cannot trigger automatic reconnect/replay.
  f.clock.peer.close();
  f.clock.peer.advance(f.clock.peer.now + 1000);
  CHECK(f.app.poll_idle() && !f.app.ready());
  const auto disconnected_count = f.clock.peer.operations.size();
  f.clock.peer.advance(f.clock.peer.now + 2000);
  CHECK(!f.app.poll_idle());
  CHECK(f.clock.peer.operations.size() == disconnected_count);
}
void jobs() {
  Fixture f;
  CHECK(f.app.ready() && f.app.replaceable());
  auto before = f.clock.peer.operations.size();
  f.app.prepare_skip();
  CHECK(!f.app.replaceable());
  f.app.start();
  CHECK(f.finish().skipped);
  CHECK(std::all_of(f.clock.peer.operations.begin() + before, f.clock.peer.operations.end(),
      [](Operation op) { return op == Operation::Status; }));
  CHECK(f.clock.peer.operations.size() > before && f.app.ready());
  f.app.prepare_skip();
  CHECK(f.app.stop().ok && !f.app.skipping());
  f.app.prepare(f.tone());
  CHECK(!f.app.replaceable());
  f.app.start();
  auto r = f.finish();
  CHECK(r.outcome == WtpScheduleOutcome::Complete && r.job->completed());
  CHECK(f.app.replaceable() && f.clock.peer.executions == 1);
  f.app.prepare(f.tone());
  f.app.stop();
  CHECK(!f.app.take_completion() && f.clock.peer.executions == 1);
  auto wspr = f.tone();
  wspr.mode = TransmissionMode::WSPR;
  WsprPayload payload;
  payload.base_frequency_hz = 14097100;
  payload.prepared.frames.resize(2);
  payload.prepared.frames[1].symbols.fill(2);
  payload.prepared.current_frame = 2;
  wspr.payload = payload;
  f.app.prepare(wspr);
  f.app.start();
  r = f.finish();
  CHECK(r.outcome == WtpScheduleOutcome::Complete);
  CHECK((r.start_utc_ns - 1000000000) % 120000000000ULL == 0);
  nlohmann::json last_load;
  for (const auto &text : f.clock.peer.payloads) {
    auto j = nlohmann::json::parse(text);
    if (j["op"] == "LOAD")
      last_load = j["body"];
  }
  CHECK(last_load["mode"] == "wspr" && last_load["events"].size() == 162);
  CHECK(last_load["events"][0]["frequency_nhz"] != "14097100000000000");
}
void recovery() {
  Fixture f;
  f.clock.peer.lose_reply = Operation::Arm;
  f.app.prepare(f.tone());
  f.app.start();
  CHECK(f.finish().outcome == WtpScheduleOutcome::Blocked);
  CHECK(!f.app.ready() && !f.app.replaceable() && !f.app.stop().ok);
  bool rejected = false;
  try {
    f.app.prepare(f.tone());
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected && f.app.recover().ok && f.app.ready());
  f.app.prepare(f.tone());
  f.app.start();
  CHECK(f.finish().outcome == WtpScheduleOutcome::Complete);
  CHECK(std::count(f.clock.peer.operations.begin(),
                   f.clock.peer.operations.end(), Operation::Arm) == 2);
}
} // namespace
int main() {
  try {
    config();
    idle_observation();
    jobs();
    recovery();
    std::cout << checks << " WTP application checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
