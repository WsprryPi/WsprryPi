#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wsprrypi
{
inline constexpr double kRp1GpclkNominalParentFrequencyHz = 200000000.0;

// Direct GPCLK output only. Frequencies above this bound require a separate
// harmonic-aware RF plan; increasing the parent rate does not remove it.
inline constexpr double kRp1GpclkMaximumDirectOutputHz = 100000000.0;

struct Rp1GpclkPlannerInput
{
    double center_frequency_hz{0.0};
    double tone_spacing_hz{0.0};
    // Explicit for numerical experiments. Physical RP1 event compilation uses
    // kRp1GpclkNominalParentFrequencyHz; the DMA pacing rate is not this parent.
    double parent_frequency_hz{0.0};
    double source_rate_ppm{0.0};
    double maximum_output_hz{kRp1GpclkMaximumDirectOutputHz};
    unsigned integer_bits{16};
    unsigned fractional_bits{16};
    std::uint32_t dither_sequence_length{65536};
    double maximum_average_error_hz{0.01};
    // Separate from caller correction so its existing +/-200 ppm range remains
    // valid. Physical adapters supply the shared RP1 baseline exactly once;
    // standalone numerical experiments retain an explicit zero default.
    double intrinsic_source_rate_ppm{0.0};
};

struct Rp1GpclkTonePlan
{
    double requested_frequency_hz{0.0};
    double ideal_divider{0.0};
    std::uint64_t lower_divider_word{0};
    std::uint64_t upper_divider_word{0};
    double lower_word_frequency_hz{0.0};
    double upper_word_frequency_hz{0.0};
    std::uint64_t nearest_divider_word{0};
    double nearest_frequency_hz{0.0};
    double nearest_error_hz{0.0};
    std::uint32_t lower_word_count{0};
    std::uint32_t upper_word_count{0};
    double lower_word_ratio{0.0};
    double average_frequency_hz{0.0};
    double average_error_hz{0.0};
};

struct Rp1GpclkPlan
{
    double nominal_parent_frequency_hz{0.0};
    double corrected_parent_frequency_hz{0.0};
    unsigned integer_bits{0};
    unsigned fractional_bits{0};
    std::uint32_t dither_sequence_length{0};
    std::uint64_t shared_integer_divider{0};
    std::array<Rp1GpclkTonePlan, 4> tones{};
    std::array<double, 3> nearest_spacing_error_hz{};
    std::array<double, 3> average_spacing_error_hz{};
    bool nearest_words_are_distinct{false};
    bool average_tones_are_ordered{false};
};

struct Rp1GpclkPlanResult
{
    bool ok{false};
    Rp1GpclkPlan plan{};
    std::string error;
};

/**
 * Build a hardware-independent RP1 GPCLK numerical plan.
 *
 * A positive source-rate PPM value means the physical parent runs fast.  The
 * returned divider words use an integer.fractional fixed-point encoding and
 * are not applied to hardware by this API.
 */
Rp1GpclkPlanResult planRp1GpclkWspr(const Rp1GpclkPlannerInput& input);

/**
 * Materialize the deterministic balanced two-word sequence for one tone.
 */
std::vector<std::uint64_t> buildRp1GpclkDitherSequence(
    const Rp1GpclkTonePlan& tone);

} // namespace wsprrypi
