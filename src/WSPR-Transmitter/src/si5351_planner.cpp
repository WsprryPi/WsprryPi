#include "si5351_planner.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    static constexpr std::uint8_t kOutputEnableRegister = 3;
    static constexpr std::uint8_t kClkControlBaseRegister = 16;
    static constexpr std::uint8_t kPllAParameterBaseRegister = 26;
    static constexpr std::uint8_t kMs0ParameterBaseRegister = 42;
    static constexpr std::uint8_t kPllResetRegister = 177;
    static constexpr std::uint8_t kOutputDisableAll = 0xff;
    static constexpr std::uint8_t kClkPowerDown = 0x80;
    static constexpr std::uint8_t kClkMultisynthIntegerMode = 0x40;
    static constexpr std::uint8_t kClkInputMultisynth = 0x0c;
    static constexpr std::uint8_t kMultisynthDivideBy4 = 0x0c;
    static constexpr std::uint8_t kResetPllA = 0x20;
    static constexpr std::uint32_t kMaxFractionDenominator = 1048575;
    static constexpr std::uint32_t kMinPllMultiplier = 15;
    static constexpr std::uint32_t kMaxPllMultiplier = 90;
    static constexpr std::uint64_t kMinPllFrequencyHz = 600000000;
    static constexpr std::uint64_t kMaxPllFrequencyHz = 900000000;
    static constexpr std::uint32_t kMaxMultisynthDivider = 2048;
    // QRP Labs' documented low-frequency practice keeps the MultiSynth at or
    // above 1 MHz and uses the final R-divider below that point.  This leaves
    // margin above the Si5351's absolute 500 kHz MultiSynth floor.
    static constexpr double kMinMultisynthOutputHz = Si5351Planner::minimum_output_hz * 128.0;
    static constexpr double kMaxMultisynthOutputHz = Si5351Planner::maximum_output_hz;
    static constexpr double kMaxCalibrationPpm = 200.0;

    struct DividerParameters
    {
        bool valid = false;
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::uint32_t c = 1;
        std::uint32_t p1 = 0;
        std::uint32_t p2 = 0;
        std::uint32_t p3 = 1;
        double actual_ratio = 0.0;
        bool integer_mode = false;
        bool divide_by_4 = false;
        std::uint8_t r_divider_code = 0;
    };

    struct RationalApproximation
    {
        bool valid = false;
        std::uint64_t numerator = 0;
        std::uint32_t denominator = 1;
    };

    static bool output_index(
        Si5351Device::Output output,
        std::uint8_t& index)
    {
        switch (output)
        {
            case Si5351Device::Output::CLK0:
                index = 0;
                return true;
            case Si5351Device::Output::CLK1:
                index = 1;
                return true;
            case Si5351Device::Output::CLK2:
                index = 2;
                return true;
        }

        return false;
    }

    static RationalApproximation approximate_ratio(double ratio)
    {
        RationalApproximation approximation;
        if (!std::isfinite(ratio) || ratio <= 0.0)
            return approximation;

        // Walk continued-fraction convergents, then compare the final legal
        // semiconvergent with the last full convergent.  This finds the
        // closest fraction whose denominator fits the Si5351 register field.
        const long double target = static_cast<long double>(ratio);
        long double remainder = target;
        std::uint64_t p0 = 0;
        std::uint64_t q0 = 1;
        std::uint64_t p1 = 1;
        std::uint64_t q1 = 0;

        while (true)
        {
            const std::uint64_t coefficient =
                static_cast<std::uint64_t>(std::floor(remainder));
            if (q1 != 0 && coefficient >
                (kMaxFractionDenominator - q0) / q1)
            {
                break;
            }

            const std::uint64_t q2 = q0 + coefficient * q1;
            if (q2 > kMaxFractionDenominator)
                break;

            const std::uint64_t p2 = p0 + coefficient * p1;
            p0 = p1;
            q0 = q1;
            p1 = p2;
            q1 = q2;

            const long double fractional = remainder -
                static_cast<long double>(coefficient);
            if (fractional == 0.0L)
                break;

            remainder = 1.0L / fractional;
        }

        if (q1 == 0)
            return approximation;

        std::uint64_t best_numerator = p1;
        std::uint64_t best_denominator = q1;
        if (q0 != 0 && q1 <= kMaxFractionDenominator)
        {
            const std::uint64_t scale =
                (kMaxFractionDenominator - q0) / q1;
            const std::uint64_t bound_numerator = p0 + scale * p1;
            const std::uint64_t bound_denominator = q0 + scale * q1;
            const long double convergent_error = std::fabs(
                target - static_cast<long double>(p1) /
                    static_cast<long double>(q1));
            const long double bound_error = std::fabs(
                target - static_cast<long double>(bound_numerator) /
                    static_cast<long double>(bound_denominator));
            if (bound_error < convergent_error)
            {
                best_numerator = bound_numerator;
                best_denominator = bound_denominator;
            }
        }

        approximation.valid = true;
        approximation.numerator = best_numerator;
        approximation.denominator =
            static_cast<std::uint32_t>(best_denominator);
        return approximation;
    }

    static DividerParameters build_divider_parameters(
        double ratio,
        std::uint32_t minimum_ratio,
        std::uint32_t maximum_ratio)
    {
        DividerParameters params;
        if (!std::isfinite(ratio) ||
            ratio < static_cast<double>(minimum_ratio) ||
            ratio > static_cast<double>(maximum_ratio))
        {
            return params;
        }

        const RationalApproximation approximation =
            approximate_ratio(ratio);
        if (!approximation.valid || approximation.denominator == 0)
            return params;

        params.a = static_cast<std::uint32_t>(
            approximation.numerator / approximation.denominator);
        params.b = static_cast<std::uint32_t>(
            approximation.numerator % approximation.denominator);
        params.c = approximation.denominator;
        if (params.a < minimum_ratio || params.a > maximum_ratio)
            return params;

        const std::uint32_t intermediate =
            static_cast<std::uint32_t>(
                (128ULL * params.b) / params.c);

        params.p1 = 128U * params.a + intermediate - 512U;
        params.p2 = 128U * params.b - params.c * intermediate;
        params.p3 = params.c;
        params.actual_ratio = static_cast<double>(params.a) +
            static_cast<double>(params.b) / static_cast<double>(params.c);
        params.valid = true;
        return params;
    }

    static bool is_exact_ratio(double ratio, std::uint32_t value)
    {
        return ratio == static_cast<double>(value);
    }

    static DividerParameters build_multisynth_parameters(double ratio)
    {
        DividerParameters params;
        if (!std::isfinite(ratio) || ratio <= 0.0)
            return params;

        if (is_exact_ratio(ratio, 4))
        {
            params.valid = true;
            params.a = 4;
            params.b = 0;
            params.c = 1;
            params.p1 = 0;
            params.p2 = 0;
            params.p3 = 1;
            params.actual_ratio = 4.0;
            params.integer_mode = true;
            params.divide_by_4 = true;
            return params;
        }

        const bool exact_six = is_exact_ratio(ratio, 6);
        const bool exact_eight = is_exact_ratio(ratio, 8);
        const double minimum_fractional_ratio = 8.0 +
            1.0 / static_cast<double>(kMaxFractionDenominator);
        if (!exact_six && !exact_eight &&
            (ratio < minimum_fractional_ratio ||
             ratio > static_cast<double>(kMaxMultisynthDivider)))
        {
            return params;
        }

        params = build_divider_parameters(
            ratio,
            exact_six ? 6U : 8U,
            kMaxMultisynthDivider);
        if (!params.valid)
            return params;

        params.integer_mode = params.b == 0 && (params.a % 2U) == 0;
        return params;
    }

    static bool valid_parked_pll_frequency(std::uint64_t frequency_hz)
    {
        return frequency_hz >= kMinPllFrequencyHz &&
            frequency_hz <= kMaxPllFrequencyHz;
    }

    static std::uint32_t select_r_divider(
        const std::vector<Si5351Planner::ToneEntry>& tones,
        std::uint64_t parked_pll_hz)
    {
        static constexpr std::uint32_t dividers[] = {
            1, 2, 4, 8, 16, 32, 64, 128};
        std::uint32_t output_domain_fallback = 0;

        for (std::uint8_t code = 0; code < 8; ++code)
        {
            const std::uint32_t divider = dividers[code];
            bool valid = !tones.empty();
            for (const Si5351Planner::ToneEntry& tone : tones)
            {
                const double internal_hz =
                    tone.frequency_hz * static_cast<double>(divider);
                if (!std::isfinite(internal_hz) ||
                    internal_hz < kMinMultisynthOutputHz ||
                    internal_hz > kMaxMultisynthOutputHz)
                {
                    valid = false;
                    break;
                }

                const double multisynth_ratio =
                    static_cast<double>(parked_pll_hz) / internal_hz;
                if (!build_multisynth_parameters(multisynth_ratio).valid)
                    valid = false;
            }
            if (valid)
                return divider;

            bool in_output_domain = !tones.empty();
            for (const Si5351Planner::ToneEntry& tone : tones)
            {
                const double internal_hz =
                    tone.frequency_hz * static_cast<double>(divider);
                if (!std::isfinite(internal_hz) ||
                    internal_hz < kMinMultisynthOutputHz ||
                    internal_hz > kMaxMultisynthOutputHz)
                {
                    in_output_domain = false;
                    break;
                }
            }
            if (output_domain_fallback == 0 && in_output_domain)
                output_domain_fallback = divider;
        }

        // Preserve the existing guarded PLL-retune path for frequencies such
        // as 2 m whose output stage is legal but whose parked-PLL ratio is not.
        return output_domain_fallback;
    }

    static bool set_r_divider_code(
        DividerParameters& params,
        std::uint32_t r_divider)
    {
        std::uint32_t value = r_divider;
        std::uint8_t code = 0;
        while (value > 1 && code < 7)
        {
            if ((value % 2U) != 0)
                return false;
            value /= 2U;
            ++code;
        }
        if (value != 1)
            return false;

        params.r_divider_code = code;
        return true;
    }

    static void append_parameter_writes(
        std::vector<Si5351Device::RegisterWrite>& writes,
        std::uint8_t base_register,
        const DividerParameters& params)
    {
        writes.push_back(Si5351Device::RegisterWrite{
            base_register,
            static_cast<std::uint8_t>((params.p3 >> 8) & 0xff)});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 1),
            static_cast<std::uint8_t>(params.p3 & 0xff)});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 2),
            static_cast<std::uint8_t>(
                (params.divide_by_4 ? kMultisynthDivideBy4 : 0U) |
                ((params.r_divider_code & 0x07U) << 4) |
                ((params.p1 >> 16) & 0x03))});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 3),
            static_cast<std::uint8_t>((params.p1 >> 8) & 0xff)});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 4),
            static_cast<std::uint8_t>(params.p1 & 0xff)});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 5),
            static_cast<std::uint8_t>(
                (((params.p3 >> 16) & 0x0f) << 4) |
                ((params.p2 >> 16) & 0x0f))});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 6),
            static_cast<std::uint8_t>((params.p2 >> 8) & 0xff)});
        writes.push_back(Si5351Device::RegisterWrite{
            static_cast<std::uint8_t>(base_register + 7),
            static_cast<std::uint8_t>(params.p2 & 0xff)});
    }

    static Si5351Planner::DividerPlan divider_plan(
        const DividerParameters& params)
    {
        Si5351Planner::DividerPlan plan;
        plan.valid = params.valid;
        plan.integer = params.a;
        plan.numerator = params.b;
        plan.denominator = params.c;
        plan.p1 = params.p1;
        plan.p2 = params.p2;
        plan.p3 = params.p3;
        plan.actual_ratio = params.actual_ratio;
        return plan;
    }

    static std::uint8_t multisynth_base_register(std::uint8_t index)
    {
        return static_cast<std::uint8_t>(
            kMs0ParameterBaseRegister + index * 8);
    }

    static std::uint8_t clock_control_register(std::uint8_t index)
    {
        return static_cast<std::uint8_t>(
            kClkControlBaseRegister + index);
    }
}

/**
 * @brief Construct a planner
 *
 * @param config Planner configuration
 */
Si5351Planner::Si5351Planner(const Config& config)
    : config_(config)
{
}

Si5351Planner::Plan Si5351Planner::buildPlan(
    Mode mode,
    const std::vector<ToneEntry>& tones) const
{
    Plan plan;
    plan.mode = mode;
    plan.calibration_ppm = config_.calibration_ppm;
    plan.effective_reference_hz = effectiveReferenceHz();
    plan.startup_writes = buildStartupWrites();
    plan.idle_writes = buildIdleWrites();
    plan.tone_sets.reserve(tones.size());

    std::uint32_t r_divider = select_r_divider(tones, config_.parked_pll_hz);
    std::uint32_t integer_multisynth = 0;
    if (config_.prefer_integer_multisynth)
    {
        // Choose one even integer divider and R-divider for the entire set.
        // Prefer the smallest R, then the VCO nearest the parked frequency.
        r_divider = 0;
        if ((mode == Mode::WSPR || mode == Mode::TONE) &&
            !tones.empty() && effectiveReferenceHz() > 0.0)
        {
            for (std::uint32_t r = 1; r <= 128 && r_divider == 0; r *= 2)
            {
                double best_distance = std::numeric_limits<double>::infinity();
                for (std::uint32_t divider = 6; divider <= 900; divider += 2)
                {
                    bool valid = true;
                    double distance = 0.0;
                    for (const auto& tone : tones)
                    {
                        const double pll = tone.frequency_hz * r * divider;
                        const double ratio = pll / effectiveReferenceHz();
                        if (!std::isfinite(pll) || pll < kMinPllFrequencyHz ||
                            pll > kMaxPllFrequencyHz || ratio < kMinPllMultiplier ||
                            ratio > kMaxPllMultiplier)
                        { valid = false; break; }
                        distance = std::max(distance,
                            std::fabs(pll - config_.parked_pll_hz));
                    }
                    if (valid && distance < best_distance)
                    { best_distance = distance; integer_multisynth = divider; r_divider = r; }
                }
            }
        }
    }

    for (const ToneEntry& tone : tones)
    {
        plan.tone_sets.push_back(buildToneRegisterSet(
            tone.frequency_hz,
            r_divider,
            config_.allow_guarded_pll_retune &&
                (!config_.prefer_integer_multisynth || mode == Mode::WSPR || mode == Mode::TONE),
            integer_multisynth));
    }

    return plan;
}

const Si5351Planner::Config& Si5351Planner::getConfig() const noexcept
{
    return config_;
}

std::vector<Si5351Device::RegisterWrite>
Si5351Planner::buildStartupWrites() const
{
    std::vector<Si5351Device::RegisterWrite> writes;

    writes.push_back(Si5351Device::RegisterWrite{
        kOutputEnableRegister,
        kOutputDisableAll});

    std::uint8_t tx_index = 0;
    if (!output_index(config_.tx_output, tx_index))
        return writes;

    const std::uint8_t powered_down_control =
        static_cast<std::uint8_t>(kClkPowerDown | kClkInputMultisynth);

    writes.push_back(Si5351Device::RegisterWrite{
        clock_control_register(tx_index),
        powered_down_control});

    if (config_.park_unused_outputs)
    {
        for (std::uint8_t index = 0; index < 3; ++index)
        {
            if (index == tx_index)
                continue;

            writes.push_back(Si5351Device::RegisterWrite{
                clock_control_register(index),
                powered_down_control});
        }
    }

    const double effective_reference_hz = effectiveReferenceHz();
    if (effective_reference_hz <= 0.0 ||
        !valid_parked_pll_frequency(config_.parked_pll_hz))
        return writes;

    const double pll_ratio =
        static_cast<double>(config_.parked_pll_hz) /
        effective_reference_hz;
    const DividerParameters pll_params = build_divider_parameters(
        pll_ratio,
        kMinPllMultiplier,
        kMaxPllMultiplier);

    if (!pll_params.valid)
        return writes;

    append_parameter_writes(
        writes,
        kPllAParameterBaseRegister,
        pll_params);
    writes.push_back(Si5351Device::RegisterWrite{
        kPllResetRegister,
        kResetPllA});

    return writes;
}

std::vector<Si5351Device::RegisterWrite>
Si5351Planner::buildIdleWrites() const
{
    std::vector<Si5351Device::RegisterWrite> writes;
    if (!config_.disable_tx_output_when_idle)
        return writes;

    std::uint8_t tx_index = 0;
    if (!output_index(config_.tx_output, tx_index))
        return writes;

    const std::uint8_t powered_down_control =
        static_cast<std::uint8_t>(kClkPowerDown | kClkInputMultisynth);

    writes.push_back(Si5351Device::RegisterWrite{
        kOutputEnableRegister,
        kOutputDisableAll});
    writes.push_back(Si5351Device::RegisterWrite{
        clock_control_register(tx_index),
        powered_down_control});

    if (config_.park_unused_outputs)
    {
        for (std::uint8_t index = 0; index < 3; ++index)
        {
            if (index == tx_index)
                continue;

            writes.push_back(Si5351Device::RegisterWrite{
                clock_control_register(index),
                powered_down_control});
        }
    }

    return writes;
}

Si5351Planner::ToneRegisterSet
Si5351Planner::buildToneRegisterSet(
    double frequency_hz,
    std::uint32_t r_divider,
    bool allow_pll_retune_candidate,
    std::uint32_t integer_multisynth) const
{
    ToneRegisterSet tone;
    tone.requested_hz = frequency_hz;
    tone.r_divider = r_divider == 0 ? 1 : r_divider;
    tone.actual_hz = quantizeFrequency(frequency_hz, r_divider);

    std::uint8_t tx_index = 0;
    if (!output_index(config_.tx_output, tx_index))
        return tone;

    if (frequency_hz <= 0.0 || r_divider == 0 ||
        !valid_parked_pll_frequency(config_.parked_pll_hz))
        return tone;

    const double multisynth_ratio =
        static_cast<double>(config_.parked_pll_hz) /
        (frequency_hz * static_cast<double>(r_divider));
    DividerParameters ms_params =
        build_multisynth_parameters(multisynth_ratio);
    if (ms_params.valid && !set_r_divider_code(ms_params, r_divider))
        ms_params.valid = false;

    if (!ms_params.valid || integer_multisynth != 0)
    {
        tone.actual_hz = 0.0;

        // Tune through the PLL with a common integer MultiSynth. The default
        // fallback is divide-by-6 for 2 m. Full initial programming requires
        // inhibition; compatible PLL-only transitions are a backend option.
        const double effective_reference_hz = effectiveReferenceHz();
        if (!allow_pll_retune_candidate || effective_reference_hz <= 0.0)
            return tone;

        const std::uint32_t divider = integer_multisynth != 0 ? integer_multisynth : 6;
        const std::uint32_t candidate_r = integer_multisynth != 0 ? r_divider : 1;
        const double target_pll_hz = frequency_hz * divider * candidate_r;
        if (target_pll_hz < static_cast<double>(kMinPllFrequencyHz) ||
            target_pll_hz > static_cast<double>(kMaxPllFrequencyHz))
        {
            return tone;
        }

        const DividerParameters pll_params = build_divider_parameters(
            target_pll_hz / effective_reference_hz,
            kMinPllMultiplier,
            kMaxPllMultiplier);
        DividerParameters candidate_ms_params =
            build_multisynth_parameters(divider);
        if (!set_r_divider_code(candidate_ms_params, candidate_r))
            candidate_ms_params.valid = false;
        if (!pll_params.valid || !candidate_ms_params.valid)
            return tone;

        PllRetuneCandidate& candidate = tone.pll_retune_candidate;
        candidate.valid = true;
        candidate.target_pll_hz = target_pll_hz;
        candidate.actual_pll_hz =
            effective_reference_hz *
            pll_params.actual_ratio;
        candidate.r_divider = candidate_r;
        candidate.pll = divider_plan(pll_params);
        candidate.multisynth = divider_plan(candidate_ms_params);
        append_parameter_writes(
            candidate.pll_writes,
            kPllAParameterBaseRegister,
            pll_params);
        append_parameter_writes(
            candidate.multisynth_writes,
            multisynth_base_register(tx_index),
            candidate_ms_params);
        tone.writes = candidate.pll_writes;
        tone.writes.insert(
            tone.writes.end(),
            candidate.multisynth_writes.begin(),
            candidate.multisynth_writes.end());
        tone.writes.push_back(Si5351Device::RegisterWrite{
            clock_control_register(tx_index),
            static_cast<std::uint8_t>(
                kClkInputMultisynth | kClkMultisynthIntegerMode)});
        tone.writes.push_back(Si5351Device::RegisterWrite{
            kPllResetRegister,
            kResetPllA});
        tone.requires_output_inhibit = true;
        tone.r_divider = candidate_r;
        tone.actual_hz = candidate.actual_pll_hz /
            (candidate_ms_params.actual_ratio * candidate_r);
        return tone;
    }

    append_parameter_writes(
        tone.writes,
        multisynth_base_register(tx_index),
        ms_params);
    tone.writes.push_back(Si5351Device::RegisterWrite{
        clock_control_register(tx_index),
        static_cast<std::uint8_t>(
            kClkInputMultisynth |
            (ms_params.integer_mode ? kClkMultisynthIntegerMode : 0U))});

    return tone;
}

double Si5351Planner::quantizeFrequency(
    double requested_hz,
    std::uint32_t r_divider) const
{
    if (requested_hz <= 0.0 || r_divider == 0 ||
        !valid_parked_pll_frequency(config_.parked_pll_hz))
        return 0.0;

    const double multisynth_ratio =
        static_cast<double>(config_.parked_pll_hz) /
        (requested_hz * static_cast<double>(r_divider));
    const DividerParameters ms_params =
        build_multisynth_parameters(multisynth_ratio);

    if (!ms_params.valid || ms_params.actual_ratio <= 0.0)
        return 0.0;

    const double reference_hz = effectiveReferenceHz();
    if (reference_hz <= 0.0) return 0.0;
    const DividerParameters pll_params = build_divider_parameters(
        static_cast<double>(config_.parked_pll_hz) / reference_hz,
        kMinPllMultiplier, kMaxPllMultiplier);
    if (!pll_params.valid) return 0.0;

    return reference_hz * pll_params.actual_ratio /
        (ms_params.actual_ratio * static_cast<double>(r_divider));
}

double Si5351Planner::effectiveReferenceHz() const noexcept
{
    if (config_.reference_hz == 0 ||
        !std::isfinite(config_.calibration_ppm) ||
        std::fabs(config_.calibration_ppm) > kMaxCalibrationPpm)
    {
        return 0.0;
    }

    const double correction_scale =
        1.0 - config_.calibration_ppm * 1.0e-6;
    if (!std::isfinite(correction_scale) || correction_scale <= 0.0)
        return 0.0;

    const double effective_reference_hz =
        static_cast<double>(config_.reference_hz) * correction_scale;
    if (!std::isfinite(effective_reference_hz) ||
        effective_reference_hz <= 0.0)
    {
        return 0.0;
    }

    return effective_reference_hz;
}
