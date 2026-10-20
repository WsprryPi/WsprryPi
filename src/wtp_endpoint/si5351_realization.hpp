#pragma once

#include <cstdint>
#include <optional>

namespace wsprrypi {

// Uses the same finite TONE planner settings as the Si5351 backend. This is
// planning only: no I2C access or claim that the reference oscillator is exact.
std::optional<std::uint64_t> wtp_si5351_tone_frequency_nhz(
    std::uint64_t requested_nhz, std::uint32_t reference_hz,
    double calibration_ppm, int tx_output);

} // namespace wsprrypi
