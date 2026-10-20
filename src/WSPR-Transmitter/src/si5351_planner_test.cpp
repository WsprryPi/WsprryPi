#include "si5351_planner.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint8_t kOutputEnableRegister = 3;
    constexpr std::uint8_t kClkControlBaseRegister = 16;
    constexpr std::uint8_t kPllResetRegister = 177;
    constexpr std::uint8_t kPllAParameterBaseRegister = 26;
    constexpr std::uint8_t kMs0ParameterBaseRegister = 42;
    constexpr std::uint8_t kIntegerMode = 0x40;
    constexpr std::uint8_t kMultisynthSource = 0x0c;
    constexpr std::uint8_t kDivideBy4 = 0x0c;
    constexpr std::uint8_t kRDividerMask = 0x70;
    constexpr std::uint32_t kMaxDenominator = 1048575;

    int failures = 0;

    struct DecodedDivider
    {
        bool valid = false;
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::uint32_t c = 1;
    };

    void expect(bool condition, const std::string& message)
    {
        if (condition)
            return;

        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }

    bool register_value(
        const std::vector<Si5351Device::RegisterWrite>& writes,
        std::uint8_t address,
        std::uint8_t& value)
    {
        for (const Si5351Device::RegisterWrite& write : writes)
        {
            if (write.address != address)
                continue;

            value = write.value;
            return true;
        }

        return false;
    }

    bool same_register_writes(
        const std::vector<Si5351Device::RegisterWrite>& lhs,
        const std::vector<Si5351Device::RegisterWrite>& rhs)
    {
        if (lhs.size() != rhs.size())
            return false;

        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i].address != rhs[i].address ||
                lhs[i].value != rhs[i].value)
            {
                return false;
            }
        }

        return true;
    }

    DecodedDivider decode_divider(
        const std::vector<Si5351Device::RegisterWrite>& writes,
        std::uint8_t base)
    {
        std::uint8_t values[8] = {};
        for (std::uint8_t offset = 0; offset < 8; ++offset)
        {
            if (!register_value(
                    writes,
                    static_cast<std::uint8_t>(base + offset),
                    values[offset]))
            {
                return DecodedDivider{};
            }
        }

        const std::uint32_t p3 =
            (static_cast<std::uint32_t>((values[5] >> 4) & 0x0f) << 16) |
            (static_cast<std::uint32_t>(values[0]) << 8) |
            values[1];
        const std::uint32_t p1 =
            (static_cast<std::uint32_t>(values[2] & 0x03) << 16) |
            (static_cast<std::uint32_t>(values[3]) << 8) |
            values[4];
        const std::uint32_t p2 =
            (static_cast<std::uint32_t>(values[5] & 0x0f) << 16) |
            (static_cast<std::uint32_t>(values[6]) << 8) |
            values[7];
        if (p3 == 0)
            return DecodedDivider{};

        DecodedDivider divider;
        divider.a = (p1 + 512U) / 128U;
        const std::uint32_t intermediate =
            p1 + 512U - 128U * divider.a;
        const std::uint64_t fractional_numerator =
            static_cast<std::uint64_t>(p2) +
            static_cast<std::uint64_t>(p3) * intermediate;
        if ((fractional_numerator % 128U) != 0)
            return DecodedDivider{};

        divider.b = static_cast<std::uint32_t>(
            fractional_numerator / 128U);
        divider.c = p3;
        divider.valid = divider.b < divider.c;
        return divider;
    }

    Si5351Planner::Plan build_plan(
        std::uint64_t parked_pll_hz,
        double multisynth_ratio,
        Si5351Device::Output output = Si5351Device::Output::CLK0)
    {
        Si5351Planner::Config config;
        config.reference_hz = 27000000;
        config.parked_pll_hz = parked_pll_hz;
        config.allow_guarded_pll_retune = false;
        config.tx_output = output;

        const double requested_hz =
            static_cast<double>(parked_pll_hz) / multisynth_ratio;
        return Si5351Planner(config).buildPlan(
            Si5351Planner::Mode::QRSS,
            {Si5351Planner::ToneEntry{requested_hz}});
    }

    bool valid_tone(const Si5351Planner::Plan& plan)
    {
        return plan.tone_sets.size() == 1 &&
            plan.tone_sets.front().actual_hz > 0.0 &&
            !plan.tone_sets.front().writes.empty();
    }

    void expect_r_divider(
        const Si5351Planner::ToneRegisterSet& tone,
        std::uint32_t divider,
        std::uint8_t code,
        const std::string& label)
    {
        expect(tone.r_divider == divider,
            label + " should expose the selected R divider");
        std::uint8_t parameter_byte = 0;
        expect(register_value(
                tone.writes,
                static_cast<std::uint8_t>(kMs0ParameterBaseRegister + 2),
                parameter_byte),
            label + " should program the CLK0 MultiSynth parameter byte");
        expect((parameter_byte & kRDividerMask) ==
                static_cast<std::uint8_t>(code << 4),
            label + " should encode the selected R divider");
    }

    void expect_valid_ratio(double ratio, const std::string& label)
    {
        const Si5351Planner::Plan plan = build_plan(600000000, ratio);
        expect(valid_tone(plan), label + " should be accepted");
    }

    void expect_invalid_ratio(double ratio, const std::string& label)
    {
        const Si5351Planner::Plan plan = build_plan(600000000, ratio);
        expect(!valid_tone(plan), label + " should be rejected");
        if (plan.tone_sets.size() == 1)
        {
            expect(
                plan.tone_sets.front().actual_hz == 0.0,
                label + " should report zero achievable frequency");
            expect(
                plan.tone_sets.front().writes.empty(),
                label + " should produce no tone writes");
        }
    }

    void test_documented_ratio_domain()
    {
        expect_valid_ratio(4.0, "exact divide-by-4");
        expect_valid_ratio(6.0, "exact divide-by-6");
        expect_valid_ratio(8.0, "exact divide-by-8");

        expect_invalid_ratio(3.999, "ratio below divide-by-4");
        expect_invalid_ratio(4.001, "fractional ratio above 4");
        expect_invalid_ratio(5.999, "fractional ratio below 6");
        expect_invalid_ratio(6.1, "fractional ratio above 6");
        expect_invalid_ratio(7.9, "fractional ratio below 8");

        const double minimum_fractional = 8.0 +
            1.0 / static_cast<double>(kMaxDenominator);
        expect_invalid_ratio(
            8.0 + 0.5 / static_cast<double>(kMaxDenominator),
            "fractional ratio below documented minimum");
        expect_valid_ratio(
            minimum_fractional,
            "documented minimum fractional ratio");
        expect_valid_ratio(9.5, "representative fractional ratio");
        expect_valid_ratio(2048.0, "maximum ratio");
        const Si5351Planner::Plan r_divided =
            build_plan(600000000, 2048.001);
        expect(valid_tone(r_divided),
            "output beyond the direct ratio limit should use an R divider");
        if (valid_tone(r_divided))
        {
            expect_r_divider(
                r_divided.tone_sets.front(),
                4,
                2,
                "output beyond the direct ratio limit");
        }
    }

    void test_special_integer_encoding()
    {
        const Si5351Device::Output outputs[] = {
            Si5351Device::Output::CLK0,
            Si5351Device::Output::CLK1,
            Si5351Device::Output::CLK2};

        for (std::uint8_t index = 0; index < 3; ++index)
        {
            const Si5351Planner::Plan plan =
                build_plan(600000000, 4.0, outputs[index]);
            expect(valid_tone(plan), "divide-by-4 should work on CLK0-2");
            if (!valid_tone(plan))
                continue;

            const auto& writes = plan.tone_sets.front().writes;
            const std::uint8_t base = static_cast<std::uint8_t>(
                kMs0ParameterBaseRegister + index * 8);
            std::uint8_t value = 0;
            expect(
                register_value(writes, base, value) && value == 0,
                "divide-by-4 P3 high byte should be zero");
            expect(
                register_value(writes, base + 1, value) && value == 1,
                "divide-by-4 P3 low byte should be one");
            expect(
                register_value(writes, base + 2, value) &&
                    (value & kDivideBy4) == kDivideBy4 &&
                    (value & 0x03) == 0,
                "divide-by-4 control bits and P1 high bits should be exact");
            expect(
                register_value(writes, base + 3, value) && value == 0,
                "divide-by-4 P1 middle byte should be zero");
            expect(
                register_value(writes, base + 4, value) && value == 0,
                "divide-by-4 P1 low byte should be zero");
            expect(
                register_value(writes, base + 5, value) && value == 0,
                "divide-by-4 P2/P3 high byte should be zero");
            expect(
                register_value(writes, base + 6, value) && value == 0,
                "divide-by-4 P2 middle byte should be zero");
            expect(
                register_value(writes, base + 7, value) && value == 0,
                "divide-by-4 P2 low byte should be zero");
            expect(
                register_value(
                    writes,
                    static_cast<std::uint8_t>(
                        kClkControlBaseRegister + index),
                    value) &&
                    value == (kIntegerMode | kMultisynthSource),
                "divide-by-4 should select integer MultiSynth mode");
        }

        for (const double ratio : {6.0, 8.0, 10.0})
        {
            const Si5351Planner::Plan plan = build_plan(600000000, ratio);
            std::uint8_t value = 0;
            expect(
                valid_tone(plan) &&
                    register_value(
                        plan.tone_sets.front().writes,
                        kClkControlBaseRegister,
                        value) &&
                    value == (kIntegerMode | kMultisynthSource),
                "even integer divider should select integer mode");
        }

        const Si5351Planner::Plan odd_integer = build_plan(600000000, 9.0);
        std::uint8_t odd_control = 0;
        expect(
            valid_tone(odd_integer) &&
                register_value(
                    odd_integer.tone_sets.front().writes,
                    kClkControlBaseRegister,
                    odd_control) &&
                odd_control == kMultisynthSource,
            "odd integer divider should remain in fractional mode");
    }

    void test_pll_frequency_domain()
    {
        for (const std::uint64_t pll_hz : {600000000ULL, 900000000ULL})
        {
            const Si5351Planner::Plan plan = build_plan(pll_hz, 9.0);
            std::uint8_t reset = 0;
            expect(valid_tone(plan), "PLL boundary should allow a tone");
            expect(
                register_value(plan.startup_writes, kPllResetRegister, reset),
                "PLL boundary should produce startup PLL programming");
        }

        for (const std::uint64_t pll_hz : {599999999ULL, 900000001ULL})
        {
            const Si5351Planner::Plan plan = build_plan(pll_hz, 9.0);
            std::uint8_t reset = 0;
            expect(!valid_tone(plan), "out-of-range PLL should reject tone");
            expect(
                !register_value(
                    plan.startup_writes,
                    kPllResetRegister,
                    reset),
                "out-of-range PLL should not be programmed or reset");
            std::uint8_t disabled = 0;
            expect(
                register_value(
                    plan.startup_writes,
                    kOutputEnableRegister,
                    disabled) &&
                    disabled == 0xff,
                "out-of-range PLL plan should begin with all outputs disabled");
        }
    }

    void test_bounded_rational_approximation()
    {
        const double exact_ratio = 32.0 +
            31029.0 / 284671.0;
        const Si5351Planner::Plan output_plan =
            build_plan(600000000, exact_ratio);
        expect(valid_tone(output_plan),
            "bounded-rational output plan should be valid");
        if (valid_tone(output_plan))
        {
            const DecodedDivider divider = decode_divider(
                output_plan.tone_sets.front().writes,
                kMs0ParameterBaseRegister);
            expect(divider.valid,
                "bounded-rational output parameters should decode");
            expect(
                divider.a == 32 &&
                    divider.b == 31029 &&
                    divider.c == 284671,
                "bounded-rational output should recover the exact "
                "in-range fraction");

            const double bounded_ratio = static_cast<double>(divider.a) +
                static_cast<double>(divider.b) /
                    static_cast<double>(divider.c);
            const std::uint32_t fixed_numerator =
                static_cast<std::uint32_t>(std::llround(
                    (exact_ratio - std::floor(exact_ratio)) *
                    kMaxDenominator));
            const double fixed_ratio = std::floor(exact_ratio) +
                static_cast<double>(fixed_numerator) /
                    static_cast<double>(kMaxDenominator);
            expect(
                std::fabs(bounded_ratio - exact_ratio) <
                    std::fabs(fixed_ratio - exact_ratio),
                "bounded rational should improve on the forced maximum "
                "denominator");
        }

        const Si5351Planner::Plan pll_plan = build_plan(850000000, 10.0);
        const DecodedDivider pll_divider = decode_divider(
            pll_plan.startup_writes,
            kPllAParameterBaseRegister);
        expect(pll_divider.valid,
            "bounded-rational PLL parameters should decode");
        expect(
            pll_divider.a == 31 &&
                pll_divider.b == 13 &&
                pll_divider.c == 27,
            "850 MHz from 27 MHz should use the exact reduced 31 + 13/27 "
            "feedback ratio");

        for (const double ratio : {6.0, 8.0, 10.0})
        {
            const Si5351Planner::Plan integer_plan =
                build_plan(600000000, ratio);
            const DecodedDivider integer_divider = decode_divider(
                integer_plan.tone_sets.front().writes,
                kMs0ParameterBaseRegister);
            expect(
                integer_divider.valid &&
                    integer_divider.b == 0 &&
                    integer_divider.c == 1,
                "integer ratios should use denominator one");
        }
    }

    void test_rejection_remains_output_disabled()
    {
        const Si5351Planner::Plan plan = build_plan(600000000, 6.1);
        expect(!valid_tone(plan), "invalid divider should reject the tone");

        std::uint8_t output_enable = 0;
        expect(
            register_value(
                plan.startup_writes,
                kOutputEnableRegister,
                output_enable) &&
                output_enable == 0xff,
            "rejected plan should keep every output disabled");

        for (std::uint8_t index = 0; index < 3; ++index)
        {
            std::uint8_t control = 0;
            expect(
                register_value(
                    plan.startup_writes,
                    static_cast<std::uint8_t>(
                        kClkControlBaseRegister + index),
                    control) &&
                    (control & 0x80) != 0,
                "rejected plan should keep each output driver powered down");
        }
    }

    void test_representative_existing_frequencies()
    {
        constexpr double frequencies_hz[] = {
            474200.0,
            1836600.0,
            3568600.0,
            7038600.0,
            10138700.0,
            14095600.0,
            18104600.0,
            21094600.0,
            24924600.0,
            28124600.0,
            50293000.0};

        Si5351Planner::Config config;
        for (const double frequency_hz : frequencies_hz)
        {
            const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
                Si5351Planner::Mode::TONE,
                {Si5351Planner::ToneEntry{frequency_hz}});
            expect(
                valid_tone(plan),
                "representative existing frequency " +
                    std::to_string(frequency_hz) +
                    " Hz should remain plannable");
            if (!valid_tone(plan))
                continue;

            const double error_hz = std::fabs(
                plan.tone_sets.front().actual_hz - frequency_hz);
            expect(
                error_hz < 5.0,
                "representative existing frequency " +
                    std::to_string(frequency_hz) +
                    " Hz error should remain below the existing "
                    "fixed-denominator resolution bound");
        }
    }

    void test_low_frequency_r_divider_plans()
    {
        struct Case
        {
            const char* label;
            double base_hz;
            std::uint32_t r_divider;
            std::uint8_t r_code;
        };
        constexpr Case cases[] = {
            {"160 m", 1838100.0, 1, 0},
            {"630 m", 475700.0, 4, 2},
            {"2200 m", 137500.0, 8, 3}};
        constexpr double spacing_hz = 1.46484375;

        for (const Case& test : cases)
        {
            Si5351Planner::Config config;
            std::vector<Si5351Planner::ToneEntry> tones;
            for (std::size_t i = 0; i < 4; ++i)
            {
                tones.push_back(Si5351Planner::ToneEntry{
                    test.base_hz + spacing_hz * static_cast<double>(i)});
            }

            const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
                Si5351Planner::Mode::WSPR,
                tones);
            expect(plan.tone_sets.size() == 4,
                std::string(test.label) + " should produce four tone sets");
            if (plan.tone_sets.size() != 4)
                continue;

            for (std::size_t i = 0; i < 4; ++i)
            {
                const Si5351Planner::ToneRegisterSet& tone =
                    plan.tone_sets[i];
                expect(tone.actual_hz > 0.0 && !tone.writes.empty(),
                    std::string(test.label) + " tone should be plannable");
                expect(!tone.requires_output_inhibit &&
                        !tone.pll_retune_candidate.valid,
                    std::string(test.label) +
                        " should retain the fixed parked PLL");
                expect_r_divider(
                    tone,
                    test.r_divider,
                    test.r_code,
                    std::string(test.label));
                expect(std::fabs(tone.actual_hz - tones[i].frequency_hz) <
                        0.000001,
                    std::string(test.label) +
                        " frequency error should remain below one microhertz");
                if (i != 0)
                {
                    expect(std::fabs(
                            (tone.actual_hz - plan.tone_sets[i - 1].actual_hz) -
                                spacing_hz) < 0.000002,
                        std::string(test.label) +
                            " adjacent tone spacing should remain WSPR-correct");
                }
            }
        }

        Si5351Planner::Config config;
        const Si5351Planner::Plan below_limit =
            Si5351Planner(config).buildPlan(
                Si5351Planner::Mode::TONE,
                {Si5351Planner::ToneEntry{7812.49}});
        expect(!valid_tone(below_limit),
            "frequency below the R-divider synthesis floor should fail closed");
    }

    void test_direct_four_tone_2m_candidate_plan()
    {
        constexpr double tones_hz[] = {
            144490497.802734375,
            144490499.267578125,
            144490500.732421875,
            144490502.197265625};
        constexpr std::uint32_t pll_numerators[] = {
            31029,
            113179,
            109829,
            50674};
        constexpr std::uint32_t pll_denominators[] = {
            284671,
            1038341,
            1007604,
            464897};

        Si5351Planner::Config config;
        std::vector<Si5351Planner::ToneEntry> tones;
        for (const double frequency_hz : tones_hz)
            tones.push_back(Si5351Planner::ToneEntry{frequency_hz});

        const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
            Si5351Planner::Mode::WSPR,
            tones);
        expect(plan.tone_sets.size() == 4,
            "2 m WSPR should produce four candidate tone plans");
        if (plan.tone_sets.size() != 4)
            return;

        for (std::size_t i = 0; i < plan.tone_sets.size(); ++i)
        {
            const Si5351Planner::ToneRegisterSet& tone = plan.tone_sets[i];
            const Si5351Planner::PllRetuneCandidate& candidate =
                tone.pll_retune_candidate;
            expect(candidate.valid,
                "2 m tone should have a PLL-retune candidate");
            expect(tone.requires_output_inhibit,
                "2 m tone must require output-inhibited transitions");
            expect(tone.writes.size() == 18,
                "2 m tone should contain PLL, MultiSynth, control, and "
                "PLL-reset writes");
            expect(candidate.r_divider == 1,
                "2 m candidate should use R divider one");
            expect(candidate.multisynth.valid &&
                    candidate.multisynth.integer == 6 &&
                    candidate.multisynth.numerator == 0 &&
                    candidate.multisynth.denominator == 1,
                "2 m candidate should use exact MultiSynth divide-by-6");
            expect(candidate.pll.valid &&
                    candidate.pll.integer == 32 &&
                    candidate.pll.numerator == pll_numerators[i] &&
                    candidate.pll.denominator == pll_denominators[i] &&
                    candidate.pll.denominator <= kMaxDenominator,
                "2 m candidate PLL ratio should match the reviewed bounded "
                "rational and fit the register fields");
            expect(candidate.pll_writes.size() == 8,
                "2 m candidate should contain eight PLL parameter writes");
            expect(candidate.multisynth_writes.size() == 8,
                "2 m candidate should contain eight MultiSynth writes");
            expect(tone.writes.front().address == kPllAParameterBaseRegister,
                "2 m transition should program PLL parameters first");
            expect(tone.writes[8].address == kMs0ParameterBaseRegister,
                "2 m transition should program MultiSynth parameters second");
            expect(tone.writes[16].address == kClkControlBaseRegister &&
                    tone.writes[16].value ==
                        (kIntegerMode | kMultisynthSource),
                "2 m transition should select integer MultiSynth mode");
            expect(tone.writes[17].address == kPllResetRegister &&
                    tone.writes[17].value == 0x20,
                "2 m transition should reset PLLA last");

            const DecodedDivider pll = decode_divider(
                candidate.pll_writes,
                kPllAParameterBaseRegister);
            const DecodedDivider multisynth = decode_divider(
                candidate.multisynth_writes,
                kMs0ParameterBaseRegister);
            expect(pll.valid &&
                    pll.a == candidate.pll.integer &&
                    pll.b == candidate.pll.numerator &&
                    pll.c == candidate.pll.denominator,
                "2 m candidate PLL writes should decode to its metadata");
            expect(multisynth.valid && multisynth.a == 6 &&
                    multisynth.b == 0 && multisynth.c == 1,
                "2 m candidate MultiSynth writes should decode to six");
            expect(std::fabs(tone.actual_hz - tones_hz[i]) < 0.00001,
                "2 m candidate error should remain below 10 microhertz");
        }

        expect(std::fabs(
                (plan.tone_sets[1].actual_hz -
                    plan.tone_sets[0].actual_hz) - 1.46484375) < 0.00002,
            "first 2 m candidate tone spacing should remain WSPR-correct");
        expect(std::fabs(
                (plan.tone_sets[2].actual_hz -
                    plan.tone_sets[1].actual_hz) - 1.46484375) < 0.00002,
            "second 2 m candidate tone spacing should remain WSPR-correct");
        expect(std::fabs(
                (plan.tone_sets[3].actual_hz -
                    plan.tone_sets[2].actual_hz) - 1.46484375) < 0.00002,
            "third 2 m candidate tone spacing should remain WSPR-correct");

        const Si5351Planner::Plan tone_mode = Si5351Planner(config).buildPlan(
            Si5351Planner::Mode::TONE,
            {Si5351Planner::ToneEntry{tones_hz[0]}});
        expect(tone_mode.tone_sets.front().pll_retune_candidate.valid &&
                tone_mode.tone_sets.front().requires_output_inhibit,
            "single-tone 2 m mode should receive the guarded PLL-retune "
            "candidate");

        for (const Si5351Planner::Mode mode : {
                 Si5351Planner::Mode::QRSS,
                 Si5351Planner::Mode::FSKCW,
                 Si5351Planner::Mode::DFCW})
        {
            const Si5351Planner::Plan cw_plan =
                Si5351Planner(config).buildPlan(
                    mode,
                    {Si5351Planner::ToneEntry{tones_hz[0]}});
            expect(cw_plan.tone_sets.front().pll_retune_candidate.valid &&
                       cw_plan.tone_sets.front().requires_output_inhibit,
                "qualified CW modes receive the guarded PLL-retune path");
        }
    }

    void test_calibrated_single_tone_planning()
    {
        constexpr double requested_hz = 144490497.802734375;
        constexpr double corrections[] = {0.0, 2.409358, -2.409358};

        Si5351Planner::Config parked_config;
        const Si5351Planner::Plan parked_plan =
            Si5351Planner(parked_config).buildPlan(
                Si5351Planner::Mode::TONE,
                {Si5351Planner::ToneEntry{14097100.0}});
        expect(valid_tone(parked_plan) &&
                !parked_plan.tone_sets.front().pll_retune_candidate.valid &&
                !parked_plan.tone_sets.front().requires_output_inhibit,
            "ordinary lower-frequency tones should retain the parked-PLL "
            "plan without guarded retuning");

        std::vector<Si5351Device::RegisterWrite> zero_writes;
        for (const double ppm : corrections)
        {
            Si5351Planner::Config config;
            config.calibration_ppm = ppm;
            const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
                Si5351Planner::Mode::TONE,
                {Si5351Planner::ToneEntry{requested_hz}});
            expect(plan.tone_sets.size() == 1 &&
                    plan.tone_sets.front().pll_retune_candidate.valid &&
                    plan.tone_sets.front().requires_output_inhibit,
                "zero, positive, and negative calibrated 2 m tones should "
                "produce one guarded PLL-retune plan");
            expect(plan.tone_sets.front().requested_hz == requested_hz,
                "single-tone calibration must preserve requested RF");
            expect(std::fabs(
                    plan.tone_sets.front().actual_hz - requested_hz) <
                    0.000025,
                "single-tone calculated output should remain within 25 "
                "microhertz");

            if (ppm == 0.0)
                zero_writes = plan.tone_sets.front().writes;
            else
                expect(!same_register_writes(
                        zero_writes,
                        plan.tone_sets.front().writes),
                    "nonzero single-tone calibration should change the "
                    "register plan");
        }

        constexpr double invalid_ppm[] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            201.0};
        for (const double ppm : invalid_ppm)
        {
            Si5351Planner::Config config;
            config.calibration_ppm = ppm;
            const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
                Si5351Planner::Mode::TONE,
                {Si5351Planner::ToneEntry{requested_hz}});
            expect(plan.effective_reference_hz == 0.0 &&
                    plan.tone_sets.size() == 1 &&
                    plan.tone_sets.front().actual_hz == 0.0 &&
                    plan.tone_sets.front().writes.empty(),
                "invalid single-tone calibration should fail closed");
        }
    }

    void test_calibrated_reference_planning()
    {
        constexpr double requested_hz = 144490497.802734375;

        Si5351Planner::Config zero_config;
        const Si5351Planner::Plan zero_plan =
            Si5351Planner(zero_config).buildPlan(
                Si5351Planner::Mode::WSPR,
                {Si5351Planner::ToneEntry{requested_hz}});
        expect(zero_plan.calibration_ppm == 0.0,
            "zero correction should remain visible in planner metadata");
        expect(zero_plan.effective_reference_hz == 27000000.0,
            "zero correction should preserve the nominal reference");

        Si5351Planner::Config positive_config = zero_config;
        positive_config.calibration_ppm = 2.409358;
        const Si5351Planner::Plan positive_plan =
            Si5351Planner(positive_config).buildPlan(
                Si5351Planner::Mode::WSPR,
                {Si5351Planner::ToneEntry{requested_hz}});
        expect(std::fabs(
                positive_plan.effective_reference_hz -
                    27000000.0 * (1.0 - 2.409358e-6)) < 1.0e-9,
            "positive correction should lower the effective reference using "
            "the GPIO-compatible sign convention");
        expect(positive_plan.tone_sets.front().requested_hz == requested_hz,
            "positive correction must preserve requested RF metadata");
        expect(positive_plan.tone_sets.front().pll_retune_candidate.valid,
            "positive correction should produce a usable 2 m plan");
        expect(!same_register_writes(
                positive_plan.tone_sets.front().writes,
                zero_plan.tone_sets.front().writes),
            "positive correction should change the Si5351 register plan");

        Si5351Planner::Config negative_config = zero_config;
        negative_config.calibration_ppm = -2.409358;
        const Si5351Planner::Plan negative_plan =
            Si5351Planner(negative_config).buildPlan(
                Si5351Planner::Mode::WSPR,
                {Si5351Planner::ToneEntry{requested_hz}});
        expect(std::fabs(
                negative_plan.effective_reference_hz -
                    27000000.0 * (1.0 + 2.409358e-6)) < 1.0e-9,
            "negative correction should raise the effective reference using "
            "the GPIO-compatible sign convention");
        expect(negative_plan.tone_sets.front().requested_hz == requested_hz,
            "negative correction must preserve requested RF metadata");
        expect(negative_plan.tone_sets.front().pll_retune_candidate.valid,
            "negative correction should produce a usable 2 m plan");
        expect(!same_register_writes(
                negative_plan.tone_sets.front().writes,
                zero_plan.tone_sets.front().writes),
            "negative correction should change the Si5351 register plan");
    }

    void test_invalid_calibration_fails_closed()
    {
        constexpr double requested_hz = 144490497.802734375;
        constexpr double invalid_ppm[] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            1000000.0};

        for (const double ppm : invalid_ppm)
        {
            Si5351Planner::Config config;
            config.calibration_ppm = ppm;
            const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
                Si5351Planner::Mode::WSPR,
                {Si5351Planner::ToneEntry{requested_hz}});
            expect(plan.effective_reference_hz == 0.0,
                "invalid correction should produce no effective reference");
            expect(!plan.startup_writes.empty() &&
                    plan.startup_writes.front().address ==
                        kOutputEnableRegister &&
                    plan.startup_writes.front().value == 0xff,
                "invalid correction should retain fail-closed startup");
            expect(plan.tone_sets.size() == 1 &&
                    plan.tone_sets.front().actual_hz == 0.0 &&
                    plan.tone_sets.front().writes.empty(),
                "invalid correction should produce no usable RF plan");
        }
    }

    void test_calibrated_four_tone_spacing_and_span()
    {
        constexpr double tones_hz[] = {
            144490497.802734375,
            144490499.267578125,
            144490500.732421875,
            144490502.197265625};
        constexpr double spacing_hz = 1.46484375;

        Si5351Planner::Config config;
        config.calibration_ppm = 2.409358;
        std::vector<Si5351Planner::ToneEntry> tones;
        for (const double frequency_hz : tones_hz)
            tones.push_back(Si5351Planner::ToneEntry{frequency_hz});

        const Si5351Planner::Plan plan = Si5351Planner(config).buildPlan(
            Si5351Planner::Mode::WSPR,
            tones);
        expect(plan.tone_sets.size() == 4,
            "calibrated 2 m WSPR should retain all four tones");
        if (plan.tone_sets.size() != 4)
            return;

        for (std::size_t i = 0; i < 4; ++i)
        {
            expect(plan.tone_sets[i].requested_hz == tones_hz[i],
                "calibration should preserve each requested RF tone");
            expect(std::fabs(plan.tone_sets[i].actual_hz - tones_hz[i]) <
                    0.000025,
                "calibrated 2 m tone error should remain below 25 microhertz");
            if (i != 0)
            {
                expect(std::fabs(
                        (plan.tone_sets[i].actual_hz -
                            plan.tone_sets[i - 1].actual_hz) - spacing_hz) <
                        0.00003,
                    "calibrated adjacent 2 m spacing should remain WSPR-correct");
            }
        }
        expect(std::fabs(
                (plan.tone_sets.back().actual_hz -
                    plan.tone_sets.front().actual_hz) -
                    3.0 * spacing_hz) < 0.00004,
            "calibrated four-tone span should remain WSPR-correct");
    }
}


void test_integer_multisynth_comparison()
{
    const Si5351Planner::Config legacy{27000000, 0.0, 850000000,
        Si5351Device::Output::CLK1, false, false};
    expect(!legacy.park_unused_outputs && !legacy.disable_tx_output_when_idle &&
        !legacy.prefer_integer_multisynth, "legacy aggregate config keeps its meanings");
    Si5351Planner::Config config;
    config.prefer_integer_multisynth = true;
    config.calibration_ppm = 3.470680;
    for (double base : {137500.0, 7040100.0, 144490500.0})
    {
        const auto plan = Si5351Planner(config).buildPlan(Si5351Planner::Mode::WSPR,
            {{base}, {base + 1.46484375}, {base + 2.9296875}, {base + 4.39453125}});
        for (const auto& tone : plan.tone_sets)
        {
            const auto& c = tone.pll_retune_candidate;
            expect(c.valid && tone.requires_output_inhibit, "integer candidate needs guarded first application");
            expect(c.multisynth.numerator == 0 && c.multisynth.integer % 2 == 0,
                "candidate MultiSynth must be even integer");
            expect(c.actual_pll_hz >= 600000000 && c.actual_pll_hz <= 900000000,
                "candidate VCO in documented range");
            expect(same_register_writes(c.multisynth_writes,
                plan.tone_sets.front().pll_retune_candidate.multisynth_writes),
                "complete tone set must share packed MultiSynth including R");
            expect(std::fabs(tone.actual_hz - tone.requested_hz) < .0001,
                "integer output planning retains frequency precision");
        }
    }
    for (const auto& tones : std::vector<std::vector<Si5351Planner::ToneEntry>>{
            {{7040100}, {144490500}}, {{0}}, {{-1}},
            {{std::numeric_limits<double>::quiet_NaN()}}, {{1e300}}})
    {
        const auto plan = Si5351Planner(config).buildPlan(Si5351Planner::Mode::TONE, tones);
        for (const auto& tone : plan.tone_sets)
            expect(tone.writes.empty() && tone.actual_hz == 0,
                "no common divider or invalid input must fail closed");
    }
    const auto unsupported = Si5351Planner(config).buildPlan(Si5351Planner::Mode::QRSS, {{7040100}});
    expect(unsupported.tone_sets[0].writes.empty(), "experiment does not broaden unreviewed modes");
}

void test_reported_frequency_from_packed_chain()
{
    bool exercised_pll_residual = false;
    for (double ppm : {-200.0, -17.1234567, 0.0, 3.470680, 199.9999})
    for (double base : {137500.0, 7040100.0, 14097100.0, 28000000.0, 100000000.0, 144490500.0})
    {
        Si5351Planner::Config cfg; cfg.calibration_ppm = ppm;
        const auto plan = Si5351Planner(cfg).buildPlan(Si5351Planner::Mode::WSPR,
            {{base}, {base+1.46484375}, {base+2.9296875}, {base+4.39453125}});
        double previous = 0;
        for (const auto& tone : plan.tone_sets)
        {
            const auto pll = decode_divider(tone.pll_retune_candidate.valid
                ? tone.pll_retune_candidate.pll_writes : plan.startup_writes, 26);
            const auto ms = decode_divider(tone.writes, 42);
            std::uint8_t r_byte = 0; register_value(tone.writes,44,r_byte);
            const unsigned r = 1U << ((r_byte >> 4) & 7);
            expect(pll.valid && ms.valid, "packed chain decodes");
            const double pll_ratio = pll.a + static_cast<double>(pll.b)/pll.c;
            const double ms_ratio = ms.a + static_cast<double>(ms.b)/ms.c;
            const double decoded_hz = cfg.reference_hz * (1 - ppm*1e-6) * pll_ratio / ms_ratio / r;
            expect(std::fabs(tone.actual_hz-decoded_hz) < std::max(1e-8,base*2e-15),
                "reported frequency includes the complete programmed chain");
            if (!tone.pll_retune_candidate.valid &&
                std::fabs(decoded_hz-static_cast<double>(cfg.parked_pll_hz)/ms_ratio/r) > 1e-7)
                exercised_pll_residual = true;
            if (previous != 0 && (base == 7040100.0 || base == 144490500.0))
                expect(std::fabs(tone.actual_hz-previous-1.46484375) < .0001,
                "complete-chain report retains WSPR spacing");
            previous = tone.actual_hz;
        }
    }
    expect(exercised_pll_residual, "fixture must expose the omitted PLL residual");
}

int main()
{
    test_reported_frequency_from_packed_chain();
    test_integer_multisynth_comparison();
    test_documented_ratio_domain();
    test_special_integer_encoding();
    test_pll_frequency_domain();
    test_bounded_rational_approximation();
    test_rejection_remains_output_disabled();
    test_representative_existing_frequencies();
    test_low_frequency_r_divider_plans();
    test_direct_four_tone_2m_candidate_plan();
    test_calibrated_single_tone_planning();
    test_calibrated_reference_planning();
    test_invalid_calibration_fails_closed();
    test_calibrated_four_tone_spacing_and_span();

    if (failures != 0)
    {
        std::cerr << failures << " Si5351 planner test(s) failed.\n";
        return 1;
    }

    std::cout << "Si5351 planner tests passed.\n";
    return 0;
}
