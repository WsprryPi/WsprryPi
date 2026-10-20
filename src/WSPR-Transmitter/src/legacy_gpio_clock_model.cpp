#include "legacy_gpio_clock_model.hpp"
#include "chipset_offsets.hpp"

#include <cmath>
#include <stdexcept>

namespace wsprrypi
{
    namespace
    {
        constexpr double kBcm2835PllDHz = 500000000.0;
        constexpr double kLaterLegacyPllDHz = 500000000.0;
        constexpr double kBcm2711PllDHz = 750000000.0;
        constexpr double kBcm2711OscillatorHz = 54000000.0;

        constexpr double kIssue429SdrInterceptHz = 0.4484;
        constexpr double kIssue429SdrProportionalPpm = 1.01012;
        constexpr double kGpclkDividerScale = 4096.0;
        constexpr double kGpclkDividerMask = 16777215.0;
        constexpr double kGpclkMash3MinimumDivisor = 5.0;

        void requireFinite(double value, const char *name)
        {
            if (!std::isfinite(value))
                throw std::invalid_argument(name);
        }

        void requirePositiveFrequency(double value, const char *name)
        {
            if (!std::isfinite(value) || value <= 0.0)
                throw std::invalid_argument(name);
        }
    }

    LegacyGpioClockModel legacyGpioClockModel(
        LegacyGpioProcessorProfile processor,
        LegacyGpioClockParent parent)
    {
        switch (processor)
        {
        case LegacyGpioProcessorProfile::Bcm2835:
            if (parent == LegacyGpioClockParent::PllD)
            {
                return {
                    processor,
                    parent,
                    kBcm2835PllDHz,
                    chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2835),
                    LegacyGpioIntrinsicEvidence::HistoricalAuthoritative};
            }
            break;
        case LegacyGpioProcessorProfile::Bcm2836Bcm2837:
            if (parent == LegacyGpioClockParent::PllD)
            {
                return {
                    processor,
                    parent,
                    kLaterLegacyPllDHz,
                    chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2836Bcm2837),
                    LegacyGpioIntrinsicEvidence::DiscoveryBaseline};
            }
            break;
        case LegacyGpioProcessorProfile::Bcm2711:
            if (parent == LegacyGpioClockParent::PllD)
            {
                return {
                    processor,
                    parent,
                    kBcm2711PllDHz,
                    chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2711),
                    LegacyGpioIntrinsicEvidence::DiscoveryBaseline};
            }
            if (parent == LegacyGpioClockParent::Oscillator)
            {
                return {
                    processor,
                    parent,
                    kBcm2711OscillatorHz,
                    chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2711),
                    LegacyGpioIntrinsicEvidence::DiscoveryBaseline};
            }
            break;
        }

        throw std::invalid_argument(
            "Unsupported legacy GPIO processor/parent clock model.");
    }

    std::optional<LegacyGpioProcessorProfile>
    legacyGpioProcessorProfileFromRevision(std::uint32_t revision) noexcept
    {
        if (revision == 0U)
            return std::nullopt;
        if ((revision & 0x800000U) == 0U)
            return LegacyGpioProcessorProfile::Bcm2835;

        switch ((revision >> 12U) & 0xFU)
        {
        case 0U: return LegacyGpioProcessorProfile::Bcm2835;
        case 1U:
        case 2U: return LegacyGpioProcessorProfile::Bcm2836Bcm2837;
        case 3U: return LegacyGpioProcessorProfile::Bcm2711;
        default: return std::nullopt;
        }
    }

    LegacyGpioCorrectionComposition composeLegacyGpioCorrection(
        double intrinsic_ppm,
        double additional_ppm) noexcept
    {
        LegacyGpioCorrectionComposition result;
        result.intrinsic_ppm = intrinsic_ppm;
        result.additional_ppm = additional_ppm;
        if (!std::isfinite(intrinsic_ppm) ||
            std::fabs(intrinsic_ppm) > kMaximumLegacyGpioCorrectionPpm ||
            !std::isfinite(additional_ppm) ||
            std::fabs(additional_ppm) > kMaximumLegacyGpioCorrectionPpm)
        {
            return result;
        }
        result.effective_ppm = intrinsic_ppm + additional_ppm;
        // Each input retains its independent +/-200 limit. The finite sum
        // is bounded by +/-400, preserving the full caller range with RP1.
        result.valid = std::isfinite(result.effective_ppm) &&
            std::fabs(result.effective_ppm) <=
                2.0 * kMaximumLegacyGpioCorrectionPpm;
        return result;
    }

    bool legacyGpioClockCanRepresent(
        double source_hz,
        double minimum_tone_hz,
        double maximum_tone_hz) noexcept
    {
        if (!std::isfinite(source_hz) || source_hz <= 0.0 ||
            !std::isfinite(minimum_tone_hz) || minimum_tone_hz <= 0.0 ||
            !std::isfinite(maximum_tone_hz) ||
            maximum_tone_hz < minimum_tone_hz)
        {
            return false;
        }
        for (const double tone_hz : {minimum_tone_hz, maximum_tone_hz})
        {
            const double scaled = source_hz / tone_hz * kGpclkDividerScale;
            const double lower = std::floor(scaled);
            const double upper = lower + 1.0;
            if (!std::isfinite(scaled) ||
                lower < kGpclkMash3MinimumDivisor * kGpclkDividerScale ||
                upper > kGpclkDividerMask)
            {
                return false;
            }
        }
        return true;
    }

    LegacyGpioClockSelection selectLegacyGpioClock(
        LegacyGpioProcessorProfile processor,
        double minimum_tone_hz,
        double maximum_tone_hz,
        double effective_ppm)
    {
        const auto try_parent = [&](LegacyGpioClockParent parent)
            -> std::optional<LegacyGpioClockSelection>
        {
            const auto model = legacyGpioClockModel(processor, parent);
            const double additional_ppm =
                effective_ppm -
                model.intrinsic_system_to_rf_difference_ppm;
            const auto correction = composeLegacyGpioCorrection(
                model.intrinsic_system_to_rf_difference_ppm,
                additional_ppm);
            if (!correction.valid)
                throw std::invalid_argument(
                    "Legacy GPIO correction composition is invalid.");
            const double corrected_rate_hz =
                model.nominal_rate_hz *
                (1.0 + correction.effective_ppm * 1.0e-6);
            if (legacyGpioClockCanRepresent(
                    corrected_rate_hz,
                    minimum_tone_hz,
                    maximum_tone_hz))
            {
                return LegacyGpioClockSelection{
                    model,
                    correction,
                    corrected_rate_hz};
            }
            return std::nullopt;
        };

        if (const auto plld = try_parent(LegacyGpioClockParent::PllD))
            return *plld;
        if (processor == LegacyGpioProcessorProfile::Bcm2711)
        {
            if (const auto oscillator =
                    try_parent(LegacyGpioClockParent::Oscillator))
                return *oscillator;
        }
        throw std::out_of_range(
            "Legacy GPIO frequency cannot be represented by an available parent.");
    }

    LegacyGpioClockSelection selectLegacyGpioClockForAdditionalCorrection(
        LegacyGpioProcessorProfile processor,
        double minimum_tone_hz,
        double maximum_tone_hz,
        double additional_ppm)
    {
        const auto try_parent = [&](LegacyGpioClockParent parent)
            -> std::optional<LegacyGpioClockSelection>
        {
            const auto model = legacyGpioClockModel(processor, parent);
            const auto correction = composeLegacyGpioCorrection(
                model.intrinsic_system_to_rf_difference_ppm,
                additional_ppm);
            if (!correction.valid)
                throw std::invalid_argument(
                    "Legacy GPIO correction composition is invalid.");
            const double corrected_rate_hz =
                model.nominal_rate_hz *
                (1.0 + correction.effective_ppm * 1.0e-6);
            if (legacyGpioClockCanRepresent(
                    corrected_rate_hz,
                    minimum_tone_hz,
                    maximum_tone_hz))
            {
                return LegacyGpioClockSelection{
                    model,
                    correction,
                    corrected_rate_hz};
            }
            return std::nullopt;
        };

        if (const auto plld = try_parent(LegacyGpioClockParent::PllD))
            return *plld;
        if (processor == LegacyGpioProcessorProfile::Bcm2711)
        {
            if (const auto oscillator =
                    try_parent(LegacyGpioClockParent::Oscillator))
                return *oscillator;
        }
        throw std::out_of_range(
            "Legacy GPIO frequency cannot be represented by an available parent.");
    }

    double deriveLegacyGpioIntrinsicDifferencePpm(
        double rf_parent_error_ppm,
        double frozen_system_clock_correction_ppm)
    {
        requireFinite(
            rf_parent_error_ppm,
            "RF parent error PPM must be finite.");
        requireFinite(
            frozen_system_clock_correction_ppm,
            "Frozen system-clock correction PPM must be finite.");

        const double difference =
            rf_parent_error_ppm - frozen_system_clock_correction_ppm;
        requireFinite(
            difference,
            "Derived intrinsic system-to-RF difference must be finite.");
        return difference;
    }

    double calibrateIssue429SdrFrequencyHz(double raw_detected_hz)
    {
        requirePositiveFrequency(
            raw_detected_hz,
            "Raw detected SDR frequency must be finite and positive.");

        const double calibrated =
            (raw_detected_hz - kIssue429SdrInterceptHz) /
            (1.0 + kIssue429SdrProportionalPpm * 1.0e-6);
        requirePositiveFrequency(
            calibrated,
            "Calibrated SDR frequency must be finite and positive.");
        return calibrated;
    }

    LegacyGpioSdrMeasurement analyzeIssue429SdrMeasurement(
        double requested_fundamental_hz,
        double raw_detected_hz,
        std::uint32_t authenticated_harmonic_number)
    {
        requirePositiveFrequency(
            requested_fundamental_hz,
            "Requested fundamental frequency must be finite and positive.");
        if (authenticated_harmonic_number == 0)
        {
            throw std::invalid_argument(
                "Authenticated harmonic number must be positive.");
        }

        const double calibrated_detected_hz =
            calibrateIssue429SdrFrequencyHz(raw_detected_hz);
        const double calibrated_fundamental_hz =
            calibrated_detected_hz /
            static_cast<double>(authenticated_harmonic_number);
        requirePositiveFrequency(
            calibrated_fundamental_hz,
            "Calibrated fundamental frequency must be finite and positive.");

        const double calibrated_rf_error_hz =
            calibrated_fundamental_hz - requested_fundamental_hz;
        const double calibrated_rf_error_ppm =
            calibrated_rf_error_hz / requested_fundamental_hz * 1.0e6;
        requireFinite(
            calibrated_rf_error_hz,
            "Calibrated RF error must be finite.");
        requireFinite(
            calibrated_rf_error_ppm,
            "Calibrated RF error PPM must be finite.");

        return {
            raw_detected_hz,
            calibrated_detected_hz,
            calibrated_fundamental_hz,
            calibrated_rf_error_hz,
            calibrated_rf_error_ppm,
            authenticated_harmonic_number};
    }
}
