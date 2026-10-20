// SPDX-License-Identifier: MIT
#include "WSPR-Transmitter/src/transmission_controller.hpp"
#include "WTP-Client/tests/reference_bridge.hpp"
#include "wtp_integration/backend.hpp"
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
struct Clock : WtpHostClock {
  Stream &stream;
  std::uint64_t now{};
  explicit Clock(Stream &s) : stream(s) {}
  std::uint64_t now_ms() const override { return now; }
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
    WtpTransmitBackend backend(
        clock,
        {std::string(32, '1'), std::string(32, '2'), std::string(32, '4')});
    ExecutionPlanCompiler compiler;
    TransmissionController controller(compiler, backend);
    stream.peer->connect();
    CHECK(backend.connect(stream));
    CHECK(controller.quiesceForStartup().ok);
    TransmissionRequest request;
    request.id.value = 1;
    request.output.backend = BackendKind::WTP;
    request.mode = TransmissionMode::TONE;
    request.payload = TonePayload{14097100, 100ms, {}};
    request.policy.allow_unqualified_frequency = true;
    for (char id : {'a', 'b'}) {
      CHECK(backend.schedule(
          {std::string(32, id),
           1000000000000ULL + clock.now * 1000000 + 8000000000ULL, 1000}));
      auto c = controller.prepare(request);
      if (!c.ok)
        throw std::runtime_error(std::string("prepare ") + id + " @" +
                                 std::to_string(clock.now) + ": " + c.error);
      if (id == 'b')
        backend.stop();
      auto r = controller.execute_prepared();
      if (r.faulted)
        throw std::runtime_error(std::string("execute ") + id + " @" +
                                 std::to_string(clock.now) + ": " + r.error);
      CHECK(id == 'a' ? r.ok && !r.stopped : !r.ok && r.stopped);
      CHECK(r.cleanup.ok && !stream.peer->output_active());
    }
    CHECK(stream.peer->executions() == 1);
    std::cout
        << "Pinned Pico backend/controller interoperability passed: "
           "completion, cancellation, repeated cleanup; one local execution\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
