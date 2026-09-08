// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
// WsprryPi-owned harness: actual unmodified Pico TLS server and common service.
#include "network/pico/server.hpp"
#include "network_support.hpp"
#include "pico/time.h"
#include <chrono>
#include <csignal>
#include <thread>
namespace {
struct Clock : wsprrypico::wtp::Clock {
  wsprrypico::wtp::ClockSnapshot snapshot() const override {
    using namespace std::chrono;
    return {wsprrypico::wtp::ClockState::Synchronized,
      static_cast<std::uint64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()),
      time_us_64() * 1000, 1000000, 0, wsprrypico::wtp::LeapState::Normal, {}};
  }
};
}
int main(int argc, char **argv) {
  std::signal(SIGPIPE, SIG_IGN);
  network_test::Flash flash;
  wsprrypico::standalone::Store store{flash};
  if (!store.load()) return 1;
  Clock clock;
  network_test::Identity identities;
  if (argc > 1) identities.boot = static_cast<unsigned>(std::stoul(argv[1]));
  wsprrypico::standalone::DryRunEngine engine;
  wsprrypico::wtp::JobService service{clock, engine, identities};
  wsprrypico::standalone::Scheduler scheduler{store, service};
  network_test::Network network;
  const std::string device(32, argc > 2 ? argv[2][0] : 'a');
  wsprrypico::network::BrowserApi api{service, store, scheduler, network, device, "phase11-host-test"};
  // Match the pinned Pico standalone image's active-job connection policy.
  api.set_active_job_connections(true);
  wsprrypico::network::PicoServer server{service, api, device, "phase11-host-test"};
  if (!server.start()) { std::cerr << "TLS server initialization failed\n"; return 1; }
  std::cout << "READY " << server.port() << std::endl;
  while (true) {
    mock_tcp_poll();
    const auto *test_address = std::getenv("WSPRRY_TEST_LISTEN_ADDRESS");
    server.poll(network.enabled, std::string(test_address ? test_address : "127.0.0.1") + ":" + std::to_string(server.port()));
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}
