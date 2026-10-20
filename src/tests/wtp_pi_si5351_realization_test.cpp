#include "wtp_endpoint/si5351_realization.hpp"

#include <cstdint>
#include <stdexcept>

int main() {
    constexpr std::uint64_t requested = 14'097'100'000'000'000ULL;
    const auto realized = wsprrypi::wtp_si5351_tone_frequency_nhz(
        requested, 27'000'000, 0.0, 0);
    if (!realized || *realized < requested - 1'000'000'000ULL ||
        *realized > requested + 1'000'000'000ULL)
        throw std::runtime_error("20 m tone realization is unavailable");
    if (wsprrypi::wtp_si5351_tone_frequency_nhz(requested, 0, 0.0, 0) ||
        wsprrypi::wtp_si5351_tone_frequency_nhz(requested, 27'000'000, 0.0, 3))
        throw std::runtime_error("Invalid Si5351 selection must be rejected");
}
