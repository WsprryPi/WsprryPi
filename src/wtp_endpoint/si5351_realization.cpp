#include "wtp_endpoint/si5351_realization.hpp"

#include "WSPR-Transmitter/src/si5351_planner.hpp"

#include <cmath>
#include <limits>
#include <vector>

namespace wsprrypi {
std::optional<std::uint64_t> wtp_si5351_tone_frequency_nhz(
    std::uint64_t requested_nhz, std::uint32_t reference_hz,
    double calibration_ppm, int tx_output) {
    if (reference_hz == 0 || tx_output < 0 || tx_output > 2 ||
        !std::isfinite(calibration_ppm) || requested_nhz == 0)
        return std::nullopt;
    const double requested_hz = static_cast<double>(
        static_cast<long double>(requested_nhz) / 1'000'000'000.0L);
    if (!std::isfinite(requested_hz) || requested_hz <= 0.0)
        return std::nullopt;
    Si5351Planner::Config config;
    config.reference_hz = reference_hz;
    config.calibration_ppm = calibration_ppm;
    config.tx_output = static_cast<Si5351Device::Output>(tx_output);
    const auto plan = Si5351Planner(config).buildPlan(
        Si5351Planner::Mode::TONE, std::vector<Si5351Planner::ToneEntry>{{requested_hz}});
    if (plan.tone_sets.size() != 1) return std::nullopt;
    const double realized_hz = plan.tone_sets[0].actual_hz;
    const long double realized_nhz =
        static_cast<long double>(realized_hz) * 1'000'000'000.0L;
    if (!std::isfinite(realized_hz) || realized_hz <= 0.0 ||
        realized_nhz > static_cast<long double>(std::numeric_limits<long long>::max()) ||
        realized_nhz < 1.0L)
        return std::nullopt;
    return static_cast<std::uint64_t>(std::llround(realized_nhz));
}
} // namespace wsprrypi
