// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_integration/application.hpp"
#include "wtp_integration/network_http.hpp"
#include "WTP-Client/include/wtp/frame_parser.hpp"
#include "json.hpp"
#include <deque>
#include <iostream>
#include <thread>
using namespace wsprrypi;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
namespace {
struct Resolver : TlsResolver {
  std::vector<std::string> addresses{"127.0.0.1"};
  std::string host;
  unsigned calls{};
  bool begin(const std::string &value, unsigned) override { host = value; ++calls; return true; }
  auto poll() -> std::optional<std::vector<std::string>> override { return addresses; }
  void cancel() noexcept override {}
};
struct Clock : WtpScheduleClock {
  std::uint64_t now_ms() const override { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  std::optional<std::uint64_t> utc_now_ns() const override { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
  void wait_ms(std::uint64_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};
struct Filter : wtp::ByteStream {
  TlsStream &tls;
  Clock &clock;
  wtp::FrameParser parser;
  std::deque<std::uint8_t> queued;
  std::string drop;
  bool dropped{};
  std::size_t accepted_bytes{};
  Filter(TlsStream &s, Clock &c) : tls(s), clock(c) {}
  wtp::IoResult write(std::span<const std::uint8_t> bytes) override {
    auto result = tls.write(bytes.first(std::min<std::size_t>(bytes.size(), 37)));
    if (result.state == wtp::IoState::Progress) accepted_bytes += result.count;
    return result;
  }
  wtp::IoResult read(std::span<std::uint8_t> bytes) override {
    if (queued.empty()) {
      std::array<std::uint8_t, 113> input{};
      const auto result = tls.read(input);
      if (result.state != wtp::IoState::Progress) return result;
      const auto parsed = parser.feed(std::span(input).first(result.count), clock.now_ms());
      CHECK(parsed.consumed == result.count);
      for (const auto &event : parsed.events) {
        CHECK(event.kind == wtp::FrameEventKind::Payload);
        const auto j = nlohmann::json::parse(event.payload);
        if (!drop.empty() && j.value("type", "") == "response" && j.value("op", "") == drop) {
          drop.clear(); dropped = true; close(); return {wtp::IoState::Failed};
        }
        const auto frame = wtp::encode_frame(event.payload);
        queued.insert(queued.end(), frame.begin(), frame.end());
      }
    }
    const auto count = std::min(bytes.size(), queued.size());
    for (std::size_t i = 0; i < count; ++i) { bytes[i] = queued.front(); queued.pop_front(); }
    return {count ? wtp::IoState::Progress : wtp::IoState::WouldBlock, count};
  }
  void close() noexcept override { tls.close(); parser = {}; queued.clear(); }
};
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  try {
    Clock clock;
    const std::string directory = argv[1];
    const std::string hostname = "wsprrypico-" + std::string(32, 'a') + ".local";
    TlsSelection selection{"WSPRRYpico-" + std::string(32, 'a') + ".LOCAL.", "", directory + "/client-ca.crt", directory + "/client.crt", directory + "/client.key", 18443};
    auto credentials = std::make_shared<TlsCredentials>(selection);
    auto resolver = std::make_unique<Resolver>(); auto *resolution = resolver.get();
    TlsStream tls([&] { return clock.now_ms(); }, TlsStream::Access::LoopbackTest, std::move(resolver));
    Filter stream(tls, clock);
    const auto open = [&] {
      stream.close();
      if (!tls.begin_open(selection, credentials)) return false;
      while (tls.opening()) { tls.poll_open(); clock.wait_ms(1); }
      return tls.ready();
    };
    // Actual TLS/server management using explicit injected loopback resolution.
    // This seam neither implements nor qualifies Linux NSS or mDNS.
    const auto http = [&](const std::string &resource, const std::string &method = "GET", const std::string &body = "", const std::string &revision = "") {
      return pico_http_request(tls, selection, credentials, [&] { return clock.now_ms(); }, resource, method, body, revision);
    };
    const auto restart = [](const char *which) {
      std::cout << "RESTART " << which << std::endl;
      std::string ready; std::getline(std::cin, ready); CHECK(ready == "READY");
    };
    auto config = http("config"); CHECK(config.status == 200 && !config.etag.empty());
    CHECK(resolution->host == hostname && tls.observation().authenticated_identity == hostname);
    selection.host = "127.0.0.1"; selection.expected_identity = hostname;
    CHECK(http("network").status == 200 && tls.observation().authenticated_identity == hostname);
    selection.expected_identity = "127.0.0.1";
    CHECK(http("network").status == 200 && tls.observation().authenticated_identity == "127.0.0.1");
    selection.expected_identity = "127.0.0.2";
    CHECK(http("network").status == 503 && tls.observation().authenticated_identity.empty());
    selection.expected_identity = "wrong.local";
    CHECK(http("network").status == 503 && tls.observation().authenticated_identity.empty());
    selection.host = hostname; selection.expected_identity.clear();
    if (std::getenv("WSPRRY_TEST_SECOND_LOOPBACK")) {
      restart("address2"); resolution->addresses = {"127.0.0.2"};
      CHECK(http("network").status == 200);
      CHECK(tls.observation().address == "127.0.0.2" && tls.observation().authenticated_identity == hostname);
      restart("address1"); resolution->addresses = {"127.0.0.1"};
      CHECK(http("network").status == 200 && tls.observation().address == "127.0.0.1");
      std::cout << "Actual changed IPv4 loopback address with unchanged DNS TLS identity passed\n";
    }
    config = http("config"); CHECK(config.status == 200);

    const std::string body = R"({"version":1,"enabled":false,"station":{"callsign":"AA0NT","locator":"EM18","power_dbm":20},"wifi":{"ssid":"test","password":"never-echo-this","ntp_ipv4":"192.0.2.1"},"schedules":[{"period_s":120,"phase_s":0}]})";
    CHECK(http("config", "PUT", body).status == 428);
    auto saved = http("config", "PUT", body, config.etag);
    CHECK(saved.status == 200 && saved.body.find("never-echo-this") == std::string::npos);
    CHECK(http("config", "PUT", body, config.etag).status == 412);
    CHECK(http("schedules").status == 200);
    CHECK(http("network").status == 200);
    WtpSettings settings;
    settings.device_id = std::string(32, 'a'); settings.start_uncertainty_ns = 1000000;
    {
    WtpApplication app(clock, stream, settings, {std::string(32, '1'), std::string(32, '2'), settings.device_id}, open);
    CHECK(app.inspect().ok);
    CHECK(app.status().identity->device_id == settings.device_id);
    const auto idle_session = app.status().session_id;
    const auto idle_started = clock.now_ms();
    unsigned idle_polls = 0;
    while (clock.now_ms() - idle_started < 8000) {
      if (app.poll_idle()) ++idle_polls;
      CHECK(app.ready() && app.status().session_id == idle_session);
      clock.wait_ms(25);
    }
    CHECK(idle_polls >= 7 && app.status().status_observed_ms &&
          clock.now_ms() - *app.status().status_observed_ms < 1500);
    std::cout << "Actual Pico TLS idle connection retained beyond five-second timeout by production STATUS polling\n";

    const auto request = [&] {
      TransmissionRequest r;
      r.output.backend = BackendKind::WTP; r.mode = TransmissionMode::TONE;
      r.payload = TonePayload{14097100, 200ms, {}};
      r.policy.allow_unqualified_frequency = true;
      return r;
    };
    const auto finish = [&](std::uint64_t budget_ms = 30000) {
      const auto deadline = clock.now_ms() + budget_ms;
      while (clock.now_ms() < deadline) { if (auto r = app.take_completion()) return *r; clock.wait_ms(2); }
      throw std::runtime_error("application completion deadline");
    };
    app.prepare(request()); app.start();
    auto completed = finish();
    if (completed.outcome != WtpScheduleOutcome::Complete) std::cerr << completed.error << '\n';
    CHECK(completed.outcome == WtpScheduleOutcome::Complete && completed.job && completed.job->completed());
    CHECK(app.replaceable());
    auto future = request();
    future.slot.start_time = std::chrono::system_clock::now() + 60s;
    const auto requested_start = *wtp_slot_utc_ns(future.slot);
    app.prepare(future); app.start();
    auto waited = finish(90000);
    CHECK(waited.outcome == WtpScheduleOutcome::Complete && waited.job &&
          waited.job->completed() && waited.execution.cleanup.ok);
    CHECK(waited.start_utc_ns == requested_start && waited.arm_handed_off);
    CHECK(app.replaceable());
    std::cout << "Actual TLS 60-second scheduled wait completed at the requested slot\n";
    unsigned management_calls = 0;
    CHECK(app.idle_management([&] { CHECK(http("network").status == 200); ++management_calls; }));
    CHECK(management_calls == 1 && app.ready());
    for (const std::string operation : {"LOAD", "ARM"}) {
      stream.drop = operation; stream.dropped = false;
      app.prepare(request()); app.start();
      CHECK(!app.idle_management([&] { ++management_calls; }));
      auto result = finish();
      CHECK(stream.dropped && result.outcome == WtpScheduleOutcome::Blocked);
      CHECK(!app.replaceable() && !app.ready());
      const auto before_failed_reconnect = stream.accepted_bytes;
      resolution->addresses.clear();
      CHECK(!app.recover().ok && !app.replaceable() && !app.ready());
      CHECK(stream.accepted_bytes == before_failed_reconnect && tls.observation().authenticated_identity.empty());
      resolution->addresses = {"127.0.0.1"}; selection.expected_identity = "wrong.local";
      CHECK(!app.recover().ok && !app.replaceable() && !app.ready());
      CHECK(stream.accepted_bytes == before_failed_reconnect && tls.observation().authenticated_identity.empty());
      selection.expected_identity.clear();
      const auto recovered = app.recover();
      if (!recovered.ok) std::cerr << operation << ": " << recovered.error << "\n" << wtp_runtime_status_json(app.status()) << "\n";
      CHECK(recovered.ok && app.ready());
      CHECK(app.status().last_report->outcome == WtpScheduleOutcome::Blocked);
    }
    stream.drop = "ABORT"; stream.dropped = false;
    app.prepare(request()); app.start();
    const auto deadline = clock.now_ms() + 15000;
    while (clock.now_ms() < deadline) {
      auto s = app.status(); if (s.remote && s.remote->state == wtp::State::Armed) break;
      clock.wait_ms(1);
    }
    CHECK(!app.stop().ok && stream.dropped && !app.replaceable());
    CHECK(app.recover().ok && app.replaceable());
    CHECK(management_calls == 1);
    }
    // Portable Session remains the authority across actual authenticated sockets.
    const auto sid = std::string(32, '3'), owner = std::string(32, '4');
    wtp::Session session({sid, owner, settings.device_id});
    const auto pump = [&] {
      const auto deadline = clock.now_ms() + 20000;
      while (session.busy() && clock.now_ms() < deadline) {
        session.poll(clock.now_ms()); clock.wait_ms(1);
      }
      CHECK(!session.busy());
    };
    const auto connect = [&] {
      CHECK(open()); CHECK(session.connect(stream, clock.now_ms())); pump();
    };
    const auto send = [&](wtp::Operation op, wtp::RequestBody body) {
      CHECK(session.request(op, std::move(body), clock.now_ms())); pump();
      const auto result = session.take_result(); CHECK(result); return result->kind;
    };
    connect(); CHECK(session.phase() == wtp::SessionPhase::Ready);
    CHECK(send(wtp::Operation::Claim, wtp::LeaseRequest{owner, 30000}) == wtp::ResultKind::Acknowledged);
    stream.drop = "LOAD";
    const auto job = std::string(32, '5');
    CHECK(send(wtp::Operation::Load, wtp::Job{job, wtp::Mode::Tone, 100000000,
        {{0, 100000000, true, 14097100000000000ULL}}, {}}) == wtp::ResultKind::Unknown);
    connect(); CHECK(session.uncertain());
    CHECK(session.retry_uncertain(clock.now_ms())); pump();
    CHECK(session.take_result()->kind == wtp::ResultKind::Acknowledged && !session.uncertain());
    session.disconnect();
    {
      WtpApplication foreign(clock, stream, settings,
          {std::string(32, '6'), std::string(32, '7'), settings.device_id}, open);
      CHECK(!foreign.inspect().ok && !foreign.ready());
      CHECK(!foreign.replaceable());
    }
    connect(); CHECK(session.owns());
    CHECK(send(wtp::Operation::Abort, wtp::AbortRequest{job}) == wtp::ResultKind::Acknowledged);
    CHECK(send(wtp::Operation::Release, wtp::Empty{}) == wtp::ResultKind::Acknowledged);
    session.disconnect();
    restart("boot"); connect();
    CHECK(session.phase() == wtp::SessionPhase::IdentityChanged && !session.owns());
    session.disconnect();
    restart("device");
    wtp::Session different({sid, owner, std::string(32, 'b')});
    CHECK(open()); CHECK(different.connect(stream, clock.now_ms()));
    const auto deadline2 = clock.now_ms() + 15000;
    while (different.busy() && clock.now_ms() < deadline2) { different.poll(clock.now_ms()); clock.wait_ms(1); }
    CHECK(different.phase() == wtp::SessionPhase::IdentityChanged && !different.owns());
    different.disconnect();
    std::cout << "Actual Pico TLS: named and explicit-IP identities, failed-resolution/handshake recovery, shared management, finite application job, partial I/O, lost LOAD/ARM/ABORT same-request replay, foreign ownership, boot change/device mismatch and same-session recovery passed\n";
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
