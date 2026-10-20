// SPDX-License-Identifier: MIT
#include "WSPR-Transmitter/src/transmission_controller.hpp"
#include "WTP-Client/tests/reference_bridge.hpp"
#include "wtp_integration/scheduler.hpp"
#include <iostream>
#include <stdexcept>
using namespace wsprrypi;
using namespace std::chrono_literals;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(std::string(#x) + " line " +                    \
                               std::to_string(__LINE__));                      \
  } while (false)
namespace {
struct Stream : wtp::ByteStream {
  std::unique_ptr<ReferenceEndpoint> peer = reference_endpoint();
  void close() noexcept override { peer->disconnect(); }
  wtp::IoResult read(std::span<std::uint8_t> bytes) override {
    if (peer->closed())
      return {wtp::IoState::Closed};
    auto n = peer->read(bytes);
    return {n ? wtp::IoState::Progress : wtp::IoState::WouldBlock, n};
  }
  wtp::IoResult write(std::span<const std::uint8_t> bytes) override {
    if (peer->closed())
      return {wtp::IoState::Closed};
    auto n = peer->receive(bytes);
    return {n ? wtp::IoState::Progress : wtp::IoState::WouldBlock, n};
  }
};
struct Clock : WtpScheduleClock {
  Stream &stream;
  std::uint64_t now{};
  explicit Clock(Stream &s) : stream(s) {}
  std::uint64_t now_ms() const override { return now; }
  std::optional<std::uint64_t> utc_now_ns() const override {
    return 1000000000000ULL + now * 1000000;
  }
  void wait_ms(std::uint64_t ms) override {
    // Advance device timing independently at every millisecond, even though
    // host status I/O proceeds only every ten milliseconds.
    for (std::uint64_t i = 0; i < ms; ++i)
      stream.peer->advance(++now);
  }
};
} // namespace
int main() {
  try {
    Stream stream;
    Clock clock(stream);
    WtpScheduler scheduler(clock, {std::string(32, '1'), std::string(32, '2'),
                                   std::string(32, '4')});
    stream.peer->connect();
    CHECK(scheduler.connect(stream));
    TransmissionRequest request;
    request.id.value = 1;
    request.output.backend = BackendKind::WTP;
    request.mode = TransmissionMode::TONE;
    request.payload = TonePayload{14097100, 100ms, {}};
    request.policy.allow_unqualified_frequency = true;
    for (char id : {'a', 'b', 'c'}) {
      const auto start = *clock.utc_now_ns() + 20000000000ULL;
      request.slot = {
          std::chrono::system_clock::time_point(
              std::chrono::duration_cast<std::chrono::system_clock::duration>(
                  std::chrono::nanoseconds(start))),
          {}};
      CHECK(scheduler.submit(request, std::string(32, id), 1000,
                             {8000, 1000, 100, 86400000}));
      if (id == 'b')
        scheduler.invalidate_pending();
      auto r = scheduler.run();
      if (r.outcome == WtpScheduleOutcome::Blocked ||
          r.outcome == WtpScheduleOutcome::Failed)
        throw std::runtime_error(r.error +
                                 " cleanup: " + r.execution.cleanup.error);
      CHECK(id == 'b' ? r.outcome == WtpScheduleOutcome::Invalidated
                      : r.outcome == WtpScheduleOutcome::Complete);
      CHECK(r.start_utc_ns == start && !stream.peer->output_active());
      auto status = scheduler.status();
      CHECK(status.last_report && status.last_report->outcome == r.outcome);
      CHECK(status.identity && status.identity->device_id == std::string(32, '4'));
      if (id != 'b') {
        CHECK(status.remote && !status.remote->output_active && !status.owns);
        CHECK(status.job && status.job->completed() && r.job && r.job->completed());
      } else {
        CHECK(!status.job && !r.job && !r.execution.cleanup_attempted);
      }
    }
    CHECK(stream.peer->executions() == 2);
    std::cout << "Pinned Pico scheduler interoperability passed: two early "
                 "scheduled executions, one pending invalidation\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
