// SPDX-License-Identifier: MIT
#pragma once
#include "solar_policy.hpp"
#include <mutex>
namespace wsprrypi {
// All inputs are target-local observations. No clock-setting operation exists.
struct SolarClockSample {
    std::uint64_t utc_ns{},monotonic_ns{},elapsed_ns{};
    std::optional<std::uint64_t> maximum_error_ns;
    bool synchronized=false,alarm=false,provider_available=false;
};
class SolarClockTracker {
public:
    SolarClockObservation observe(const SolarClockSample &);
private:
    std::optional<SolarClockSample> previous_,healthy_;
    SolarClockState state_=SolarClockState::Unavailable;
    std::uint64_t generation_=1;
};
SolarClockSample read_solar_clock_sample() noexcept;
// Ordinary health polling is cached to <=1 Hz. Commitment deliberately obtains
// a fresh sample and invalidates preparation on clock-health/generation changes.
class SolarClockProvider {
public:
    SolarClockObservation observe(bool commitment=false);
private:
    std::mutex mutex_;
    SolarClockTracker tracker_;
    std::optional<SolarClockSample> last_;
    SolarClockObservation observation_;
};
}
