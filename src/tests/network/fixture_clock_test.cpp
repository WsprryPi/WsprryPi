// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "fixture_clock.hpp"
#include "standalone/dry_run_engine.hpp"
#include "time/mapping.hpp"
#include <iostream>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace {
std::uint64_t mono;
std::uint64_t now() { return mono; }
constexpr std::uint64_t epoch = 1700000000000000000ULL;
struct TornClock final : wsprrypico::wtp::Clock {
  wsprrypico::wtp::ClockSnapshot snapshot() const override {
    // Independently sampled wall time stepped between observations. The real
    // engine must still refuse the inconsistent mapping; do not relax it.
    return {wsprrypico::wtp::ClockState::Synchronized, epoch + mono + 2000000,
            mono, 1000000, 0, wsprrypico::wtp::LeapState::Normal, {}};
  }
};
}
int main() {
  using namespace wsprrypico;
  mono = 0;
  network_test::FixtureClock clock{epoch, 0, now};
  for (const auto elapsed : {0ULL, 1000ULL, 60000000000ULL}) {
    mono = elapsed;
    const auto sample = clock.snapshot();
    CHECK(sample.utc_now_ns == epoch + elapsed && sample.monotonic_now_ns == elapsed);
    CHECK(sample.uncertainty_ns == 1000000);
    std::uint64_t projected{};
    CHECK(time::project(sample, 60000000000ULL, projected) && projected == epoch + 60000000000ULL);
  }
  wtp::Job job{}; job.total_duration_ns = 1000000000;
  const auto exercise = [&](const wtp::Clock &source, std::uint64_t start, std::uint64_t late,
                            wtp::EngineState expected) {
    standalone::DryRunEngine engine;
    wtp::LocalStartConditions conditions{&source, epoch + start, 1000000};
    CHECK(engine.schedule(job, start, conditions));
    mono = start + late;
    CHECK(engine.poll(mono).state == expected);
  };
  exercise(clock, 60000000000ULL, 134800, wtp::EngineState::Running);
  exercise(clock, 60000000000ULL, 1000000000, wtp::EngineState::Missed);
  CHECK(wtp::start_window_ns(epoch + 59999999999ULL) == 1);
  exercise(clock, 59999999999ULL, 134800, wtp::EngineState::Missed);
  mono = 9;
  network_test::FixtureClock backwards{epoch, 10, now};
  bool refused = false;
  try { (void)backwards.snapshot(); } catch (const std::runtime_error &error) {
    refused = std::string(error.what()) == "Invalid fixture monotonic clock progression";
  }
  CHECK(refused);
  network_test::FixtureClock overflow{UINT64_MAX, 0, now};
  refused = false;
  try { (void)overflow.snapshot(); } catch (const std::runtime_error &error) {
    refused = std::string(error.what()) == "Invalid fixture monotonic clock progression";
  }
  CHECK(refused);
  TornClock torn;
  exercise(torn, 60000000000ULL, 134800, wtp::EngineState::Missed);
  std::cout << "Fixture clock projection, coherent full-second start, narrow-window, late-start and torn-clock refusals passed\n";
}
