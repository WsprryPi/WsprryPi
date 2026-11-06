// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "wtp/job_service.hpp"
#include <limits>
#include <stdexcept>
namespace network_test {
// A logical UTC epoch advances with one monotonic clock. This software fixture
// does not measure host UTC accuracy or qualify physical transmitter timing.
class FixtureClock final : public wsprrypico::wtp::Clock {
  std::uint64_t utc_, mono_;
  std::uint64_t (*read_)();
  mutable wsprrypico::wtp::ClockSnapshot last_{};
public:
  FixtureClock(std::uint64_t utc, std::uint64_t mono, std::uint64_t (*read)())
      : utc_(utc), mono_(mono), read_(read) {}
  wsprrypico::wtp::ClockSnapshot snapshot() const override {
    const auto now = read_();
    if (now < mono_ || now - mono_ > std::numeric_limits<std::uint64_t>::max() - utc_)
      throw std::runtime_error("Invalid fixture monotonic clock progression");
    last_ = {wsprrypico::wtp::ClockState::Synchronized, utc_ + (now - mono_), now,
             1000000, 0, wsprrypico::wtp::LeapState::Normal, {}};
    return last_;
  }
  const wsprrypico::wtp::ClockSnapshot &last_snapshot() const { return last_; }
};
}
