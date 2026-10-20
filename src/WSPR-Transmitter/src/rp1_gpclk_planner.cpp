#include "rp1_gpclk_planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace wsprrypi
{
namespace
{
constexpr double kMaximumSourceRatePpm = 200.0;

Rp1GpclkPlanResult reject(const std::string& error)
{
    Rp1GpclkPlanResult result;
    result.error = error;
    return result;
}

bool finitePositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

long double wordFrequency(
    long double corrected_parent_hz,
    std::uint64_t word,
    std::uint64_t scale)
{
    return corrected_parent_hz * static_cast<long double>(scale) /
        static_cast<long double>(word);
}
}

Rp1GpclkPlanResult planRp1GpclkWspr(const Rp1GpclkPlannerInput& input)
{
    if (!finitePositive(input.center_frequency_hz) ||
        !finitePositive(input.tone_spacing_hz) ||
        !finitePositive(input.parent_frequency_hz) ||
        !finitePositive(input.maximum_output_hz) ||
        !finitePositive(input.maximum_average_error_hz))
    {
        return reject("RP1 GPCLK frequencies and tolerance must be finite and positive.");
    }
    if (!std::isfinite(input.source_rate_ppm) ||
        std::fabs(input.source_rate_ppm) > kMaximumSourceRatePpm)
    {
        return reject("RP1 GPCLK source-rate PPM must be finite and within +/-200.");
    }
    if (!std::isfinite(input.intrinsic_source_rate_ppm) ||
        std::fabs(input.intrinsic_source_rate_ppm) > kMaximumSourceRatePpm)
    {
        return reject("RP1 GPCLK intrinsic PPM must be finite and within +/-200.");
    }
    if (input.integer_bits == 0 || input.fractional_bits == 0 ||
        input.integer_bits + input.fractional_bits > 63)
    {
        return reject("RP1 GPCLK divider widths must form a non-empty field no wider than 63 bits.");
    }
    if (input.dither_sequence_length == 0)
    {
        return reject("RP1 GPCLK dither sequence length must be positive.");
    }

    const std::uint64_t scale = std::uint64_t{1} << input.fractional_bits;
    const std::uint64_t maximum_integer =
        (std::uint64_t{1} << input.integer_bits) - 1;
    const std::uint64_t maximum_word =
        (maximum_integer << input.fractional_bits) | (scale - 1);
    const long double corrected_parent =
        static_cast<long double>(input.parent_frequency_hz) *
        (1.0L + (static_cast<long double>(input.source_rate_ppm) +
                 static_cast<long double>(input.intrinsic_source_rate_ppm)) * 1.0e-6L);

    if (!std::isfinite(static_cast<double>(corrected_parent)) ||
        corrected_parent <= 0.0L)
    {
        return reject("RP1 GPCLK corrected parent frequency is invalid.");
    }

    Rp1GpclkPlanResult result;
    result.plan.nominal_parent_frequency_hz = input.parent_frequency_hz;
    result.plan.corrected_parent_frequency_hz =
        static_cast<double>(corrected_parent);
    result.plan.integer_bits = input.integer_bits;
    result.plan.fractional_bits = input.fractional_bits;
    result.plan.dither_sequence_length = input.dither_sequence_length;

    std::uint64_t shared_integer = 0;
    std::set<std::uint64_t> nearest_words;

    for (std::size_t tone_index = 0; tone_index < result.plan.tones.size(); ++tone_index)
    {
        auto& tone = result.plan.tones[tone_index];
        tone.requested_frequency_hz = input.center_frequency_hz +
            (static_cast<double>(tone_index) - 1.5) * input.tone_spacing_hz;
        if (!finitePositive(tone.requested_frequency_hz) ||
            tone.requested_frequency_hz > input.maximum_output_hz)
        {
            return reject("RP1 GPCLK WSPR tone exceeds the enforced output range.");
        }
        if (static_cast<long double>(tone.requested_frequency_hz) > corrected_parent)
        {
            return reject("RP1 GPCLK WSPR tone exceeds the corrected parent frequency.");
        }

        const long double ideal_word = corrected_parent /
            static_cast<long double>(tone.requested_frequency_hz) *
            static_cast<long double>(scale);
        if (!std::isfinite(static_cast<double>(ideal_word)) ||
            ideal_word < static_cast<long double>(scale) ||
            ideal_word > static_cast<long double>(maximum_word))
        {
            return reject("RP1 GPCLK ideal divider is outside the configured fixed-point field.");
        }

        const long double floor_word = std::floor(ideal_word);
        const bool exact = floor_word == ideal_word;
        tone.lower_divider_word = static_cast<std::uint64_t>(floor_word);
        tone.upper_divider_word = exact
            ? tone.lower_divider_word
            : tone.lower_divider_word + 1;
        if (tone.upper_divider_word > maximum_word)
        {
            return reject("RP1 GPCLK adjacent divider word overflows the configured field.");
        }

        const std::uint64_t lower_integer =
            tone.lower_divider_word >> input.fractional_bits;
        const std::uint64_t upper_integer =
            tone.upper_divider_word >> input.fractional_bits;
        if (lower_integer != upper_integer)
        {
            return reject("RP1 GPCLK plan would require a live integer-divider transition.");
        }
        if (shared_integer == 0)
            shared_integer = lower_integer;
        else if (shared_integer != lower_integer)
            return reject("RP1 GPCLK four-tone plan does not share one integer divider.");

        const long double lower_frequency = wordFrequency(
            corrected_parent, tone.lower_divider_word, scale);
        const long double upper_frequency = wordFrequency(
            corrected_parent, tone.upper_divider_word, scale);
        tone.ideal_divider = static_cast<double>(
            ideal_word / static_cast<long double>(scale));
        tone.lower_word_frequency_hz = static_cast<double>(lower_frequency);
        tone.upper_word_frequency_hz = static_cast<double>(upper_frequency);

        const long double requested = tone.requested_frequency_hz;
        const long double lower_error = lower_frequency - requested;
        const long double upper_error = upper_frequency - requested;
        if (std::fabs(lower_error) <= std::fabs(upper_error))
        {
            tone.nearest_divider_word = tone.lower_divider_word;
            tone.nearest_frequency_hz = tone.lower_word_frequency_hz;
            tone.nearest_error_hz = static_cast<double>(lower_error);
        }
        else
        {
            tone.nearest_divider_word = tone.upper_divider_word;
            tone.nearest_frequency_hz = tone.upper_word_frequency_hz;
            tone.nearest_error_hz = static_cast<double>(upper_error);
        }
        nearest_words.insert(tone.nearest_divider_word);

        long double lower_ratio = 1.0L;
        if (tone.lower_divider_word != tone.upper_divider_word)
        {
            lower_ratio = (requested - upper_frequency) /
                (lower_frequency - upper_frequency);
            lower_ratio = std::max(0.0L, std::min(1.0L, lower_ratio));
        }
        const auto lower_count = static_cast<std::uint64_t>(std::llround(
            lower_ratio * input.dither_sequence_length));
        tone.lower_word_count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            lower_count, input.dither_sequence_length));
        tone.upper_word_count =
            input.dither_sequence_length - tone.lower_word_count;
        tone.lower_word_ratio = static_cast<double>(lower_ratio);
        const long double average_frequency =
            (lower_frequency * tone.lower_word_count +
             upper_frequency * tone.upper_word_count) /
            input.dither_sequence_length;
        tone.average_frequency_hz = static_cast<double>(average_frequency);
        tone.average_error_hz = static_cast<double>(average_frequency - requested);
        if (std::fabs(tone.average_error_hz) > input.maximum_average_error_hz)
        {
            return reject("RP1 GPCLK finite dither plan exceeds the numerical average-error tolerance.");
        }
    }

    result.plan.shared_integer_divider = shared_integer;
    result.plan.nearest_words_are_distinct = nearest_words.size() == 4;
    result.plan.average_tones_are_ordered = true;
    for (std::size_t i = 1; i < result.plan.tones.size(); ++i)
    {
        result.plan.nearest_spacing_error_hz[i - 1] =
            result.plan.tones[i].nearest_frequency_hz -
            result.plan.tones[i - 1].nearest_frequency_hz -
            input.tone_spacing_hz;
        result.plan.average_spacing_error_hz[i - 1] =
            result.plan.tones[i].average_frequency_hz -
            result.plan.tones[i - 1].average_frequency_hz -
            input.tone_spacing_hz;
        if (result.plan.tones[i].average_frequency_hz <=
            result.plan.tones[i - 1].average_frequency_hz)
        {
            result.plan.average_tones_are_ordered = false;
        }
    }
    if (!result.plan.average_tones_are_ordered)
        return reject("RP1 GPCLK dither averages do not preserve four ordered WSPR tones.");

    result.ok = true;
    return result;
}

std::vector<std::uint64_t> buildRp1GpclkDitherSequence(
    const Rp1GpclkTonePlan& tone)
{
    const std::uint64_t length =
        static_cast<std::uint64_t>(tone.lower_word_count) +
        static_cast<std::uint64_t>(tone.upper_word_count);
    if (length == 0 || tone.lower_word_count > length)
        return {};

    std::vector<std::uint64_t> sequence;
    sequence.reserve(static_cast<std::size_t>(length));
    std::uint64_t accumulator = 0;
    for (std::uint64_t i = 0; i < length; ++i)
    {
        accumulator += tone.lower_word_count;
        if (accumulator >= length)
        {
            sequence.push_back(tone.lower_divider_word);
            accumulator -= length;
        }
        else
        {
            sequence.push_back(tone.upper_divider_word);
        }
    }
    return sequence;
}

} // namespace wsprrypi
