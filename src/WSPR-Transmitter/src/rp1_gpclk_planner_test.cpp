#include "rp1_gpclk_planner.hpp"
#include "chipset_offsets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

wsprrypi::Rp1GpclkPlannerInput inputFor(
    double center_hz,
    double parent_hz = 200000000.0,
    std::uint32_t sequence_length = 65536,
    double tolerance_hz = 0.01)
{
    return {
        center_hz,
        1.46484375,
        parent_hz,
        0.0,
        100000000.0,
        16,
        16,
        sequence_length,
        tolerance_hz};
}

void test_all_bands_and_parents()
{
    struct Band
    {
        const char* name;
        double center_hz;
    };
    constexpr std::array<Band, 15> bands{{
        {"2200m", 137500.0}, {"630m", 475700.0},
        {"160m", 1838100.0}, {"80m", 3570100.0},
        {"60m", 5288700.0}, {"40m", 7040100.0},
        {"30m", 10140200.0},
        {"20m", 14097100.0}, {"17m", 18106100.0},
        {"15m", 21096100.0}, {"12m", 24926100.0},
        {"10m", 28126100.0}, {"6m", 50294500.0},
        {"4m", 70092500.0}, {"2m", 144490500.0}}};

    for (const auto& band : bands)
    {
        const auto pll = wsprrypi::planRp1GpclkWspr(inputFor(band.center_hz));
        if (std::string(band.name) == "2m")
        {
            expect(!pll.ok && pll.error.find("output range") != std::string::npos,
                   "2 m must fail under the enforced 100 MHz GPCLK ceiling");
        }
        else
        {
            expect(pll.ok, std::string(band.name) + " must have a finite 200 MHz-parent dither plan");
            if (pll.ok)
            {
                expect(pll.plan.average_tones_are_ordered,
                       std::string(band.name) + " dither averages must retain tone order");
                for (const auto& tone : pll.plan.tones)
                    expect(std::fabs(tone.average_error_hz) <= 0.01,
                           std::string(band.name) + " average error must meet tolerance");
            }
        }

        // Alternate parents are numerical models, not current provider support.
        const auto xosc = wsprrypi::planRp1GpclkWspr(
            inputFor(band.center_hz, 50000000.0));
        const bool above_xosc =
            band.center_hz + 1.5 * 1.46484375 > 50000000.0;
        if (above_xosc)
            expect(!xosc.ok, std::string(band.name) + " must reject tones above the 50 MHz parent");
        else
            expect(xosc.ok, std::string(band.name) + " must plan in the offline 50 MHz model");
    }
}

void test_single_word_evidence()
{
    const auto lf = wsprrypi::planRp1GpclkWspr(inputFor(137500.0));
    const auto mf = wsprrypi::planRp1GpclkWspr(inputFor(475700.0));
    const auto hf = wsprrypi::planRp1GpclkWspr(inputFor(14097100.0));
    expect(lf.ok && lf.plan.nearest_words_are_distinct,
           "2200 m nearest words should remain numerically distinct");
    expect(mf.ok && mf.plan.nearest_words_are_distinct,
           "630 m nearest words should remain numerically distinct");
    expect(hf.ok && !hf.plan.nearest_words_are_distinct,
           "20 m must expose collapsed nearest-divider tones");
    expect(hf.ok && hf.plan.average_tones_are_ordered,
           "20 m dither averages must restore distinct ordered tones");
}

void test_intrinsic_offset_composition()
{
    expect(wsprrypi::chipsetIntrinsicOffsetPpm(wsprrypi::ClockChipset::Rp1) == -46.245,
           "RP1 intrinsic baseline must retain its approved value and sign");
    for (double ppm : {-200.0, 0.0, 46.245, 200.0})
    {
        auto input = inputFor(14097100.0);
        input.source_rate_ppm = ppm;
        input.intrinsic_source_rate_ppm =
            wsprrypi::chipsetIntrinsicOffsetPpm(wsprrypi::ClockChipset::Rp1);
        const auto result = wsprrypi::planRp1GpclkWspr(input);
        expect(result.ok, "intrinsic offset must preserve the full caller PPM range");
        if (!result.ok) continue;
        expect(result.plan.nominal_parent_frequency_hz == 200000000.0,
               "intrinsic correction must not change nominal parent identity");
        const double expected = 200000000.0 * (1.0 + (ppm - 46.245) * 1e-6);
        expect(std::fabs(result.plan.corrected_parent_frequency_hz - expected) < 1e-6,
               "intrinsic and caller PPM must add once, not multiply or change sign");
    }
}

void test_calibration_direction_and_determinism()
{
    auto zero_input = inputFor(14097100.0);
    auto positive_input = zero_input;
    positive_input.source_rate_ppm = 100.0;
    auto negative_input = zero_input;
    negative_input.source_rate_ppm = -100.0;

    const auto zero = wsprrypi::planRp1GpclkWspr(zero_input);
    const auto positive = wsprrypi::planRp1GpclkWspr(positive_input);
    const auto negative = wsprrypi::planRp1GpclkWspr(negative_input);
    expect(zero.ok && positive.ok && negative.ok,
           "zero and signed calibration plans must be valid");
    if (zero.ok && positive.ok && negative.ok)
    {
        expect(positive.plan.corrected_parent_frequency_hz >
                   zero.plan.corrected_parent_frequency_hz &&
               negative.plan.corrected_parent_frequency_hz <
                   zero.plan.corrected_parent_frequency_hz,
               "positive PPM must model a faster RP1 source");
        expect(positive.plan.tones[0].lower_divider_word >
                   negative.plan.tones[0].lower_divider_word,
               "a faster modeled source must require a larger divider");
        expect(positive.plan.average_tones_are_ordered &&
                   negative.plan.average_tones_are_ordered,
               "signed calibration must preserve tone ordering");

        const auto first = wsprrypi::buildRp1GpclkDitherSequence(
            positive.plan.tones[0]);
        const auto second = wsprrypi::buildRp1GpclkDitherSequence(
            positive.plan.tones[0]);
        expect(first == second, "dither sequence generation must be deterministic");
        expect(std::count(first.begin(), first.end(),
                          positive.plan.tones[0].lower_divider_word) ==
                   positive.plan.tones[0].lower_word_count,
               "balanced sequence must contain the planned lower-word count");
    }
}

void test_sequence_lengths_and_tolerance()
{
    for (const std::uint32_t length : {64U, 257U, 4096U, 65536U})
    {
        const auto plan = wsprrypi::planRp1GpclkWspr(
            inputFor(475700.0, 200000000.0, length, 0.1));
        expect(plan.ok, "representative 630 m finite block must meet a 0.1 Hz arithmetic tolerance");
        if (plan.ok)
        {
            const auto sequence = wsprrypi::buildRp1GpclkDitherSequence(plan.plan.tones[2]);
            expect(sequence.size() == length,
                   "materialized sequence length must match the requested finite block");
        }
    }

    const auto too_short = wsprrypi::planRp1GpclkWspr(
        inputFor(14097100.0, 200000000.0, 1, 0.01));
    expect(!too_short.ok && too_short.error.find("average-error") != std::string::npos,
           "an insufficient finite block must fail the arithmetic tolerance");
}

void test_current_parent_dither_fixture()
{
    const auto rp1_dma_tick = wsprrypi::planRp1GpclkWspr(inputFor(
        14097100.0, wsprrypi::kRp1GpclkNominalParentFrequencyHz, 66792, 0.01));
    expect(rp1_dma_tick.ok,
           "the current 200 MHz-parent dither block must meet the 20 m tolerance");
    if (rp1_dma_tick.ok)
    {
        // Independent exact-rational fixture: nominal parent, no PPM correction.
        constexpr std::array<std::uint64_t, 4> expected_lower_words{
            929780, 929779, 929779, 929779};
        constexpr std::array<std::uint32_t, 4> expected_lower_counts{
            64873, 4534, 10987, 17440};
        expect(rp1_dma_tick.plan.nominal_parent_frequency_hz == 200000000.0 &&
               rp1_dma_tick.plan.corrected_parent_frequency_hz == 200000000.0,
               "the current fixture must use the uncorrected 200 MHz parent");
        for (std::size_t i = 0; i < rp1_dma_tick.plan.tones.size(); ++i)
        {
            const auto& tone = rp1_dma_tick.plan.tones[i];
            expect(tone.lower_divider_word == expected_lower_words[i] &&
                   tone.upper_divider_word == expected_lower_words[i] + 1,
                   "current-parent Q16 words must match the independent fixture");
            expect(tone.lower_word_count == expected_lower_counts[i] &&
                   tone.upper_word_count == 66792 - expected_lower_counts[i],
                   "current-parent dither counts must match the independent fixture");
            const long double mean = 200000000.0L * 65536.0L *
                (static_cast<long double>(tone.lower_word_count) / tone.lower_divider_word +
                 static_cast<long double>(tone.upper_word_count) / tone.upper_divider_word) /
                66792.0L;
            expect(std::fabs(mean - tone.requested_frequency_hz) < 0.0005L,
                   "current-parent words must synthesize the requested numerical mean");
        }
    }
}

void test_integer_boundary_and_invalid_inputs()
{
    const auto boundary = wsprrypi::planRp1GpclkWspr(inputFor(40000000.0));
    expect(!boundary.ok && boundary.error.find("integer") != std::string::npos,
           "a four-tone plan spanning an integer-divider boundary must fail closed");

    std::vector<wsprrypi::Rp1GpclkPlannerInput> invalid;
    auto base = inputFor(137500.0);
    for (double value : {0.0, -1.0,
                         std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()})
    {
        auto candidate = base;
        candidate.center_frequency_hz = value;
        invalid.push_back(candidate);
    }
    for (double ppm : {200.000001, -200.000001,
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
    {
        auto candidate = base;
        candidate.source_rate_ppm = ppm;
        invalid.push_back(candidate);
        candidate = base;
        candidate.intrinsic_source_rate_ppm = ppm;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.dither_sequence_length = 0;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.integer_bits = 0;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.fractional_bits = 0;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.integer_bits = 40;
        candidate.fractional_bits = 24;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.parent_frequency_hz = -1.0;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.maximum_output_hz = 0.0;
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.maximum_average_error_hz =
            std::numeric_limits<double>::quiet_NaN();
        invalid.push_back(candidate);
    }
    {
        auto candidate = base;
        candidate.center_frequency_hz = 1.0;
        invalid.push_back(candidate);
    }
    for (const auto& candidate : invalid)
        expect(!wsprrypi::planRp1GpclkWspr(candidate).ok,
               "invalid RP1 planner input must fail closed");

    const auto parent_limit = wsprrypi::planRp1GpclkWspr(
        inputFor(50294500.0, 50000000.0));
    expect(!parent_limit.ok && parent_limit.error.find("parent") != std::string::npos,
           "tones above the selected parent must fail closed");
}
}

int main()
{
    expect(wsprrypi::chipsetIntrinsicOffsetPpm(wsprrypi::ClockChipset::Rp1) == -46.245,
        "RP1 GPCLK must retain the shared intrinsic baseline");

    test_all_bands_and_parents();
    test_single_word_evidence();
    test_intrinsic_offset_composition();
    test_calibration_direction_and_determinism();
    test_sequence_lengths_and_tolerance();
    test_current_parent_dither_fixture();
    test_integer_boundary_and_invalid_inputs();

    if (failures != 0)
    {
        std::cerr << failures << " RP1 GPCLK planner test(s) failed\n";
        return 1;
    }
    std::cout << "RP1 GPCLK planner tests passed\n";
    return 0;
}
