#include "system_clock_frequency_estimate.hpp"
#include "PPM-Manager/src/ppm_manager.hpp"

#include <algorithm>
#include <cmath>

namespace
{
bool finite_bounded(double value)
{
    return std::isfinite(value) &&
        value >= -kMaximumGpioFrequencyCorrectionPpm &&
        value <= kMaximumGpioFrequencyCorrectionPpm;
}

GpioFrequencyCorrection invalid_correction(std::string reason)
{
    GpioFrequencyCorrection result;
    result.valid = false;
    result.reason = std::move(reason);
    return result;
}
}

SystemClockFrequencyEstimate system_clock_frequency_estimate_from_provider(
    const PPMProviderSnapshot &provider,
    std::chrono::steady_clock::time_point now,
    std::chrono::system_clock::time_point utc_now)
{
    const auto aged = provider.withAgeAt(now);
    SystemClockFrequencyEstimate sample;
    sample.provider_name = aged.provider_name;
    sample.frequency_ppm = aged.frequency_ppm;
    sample.synchronized = aged.synchronized;
    sample.age_seconds = aged.age_seconds;
    sample.residual_frequency_ppm = aged.residual_frequency_ppm;
    sample.skew_ppm = aged.skew_ppm;
    sample.selected_source = aged.selected_source;
    sample.combined_sources = aged.combined_sources;
    sample.leap_normal = aged.leap_normal;
    sample.source_provenance = aged.source_provenance;
    sample.source_signature = aged.source_signature;
    if (std::isfinite(aged.age_seconds) && aged.age_seconds >= 0.0 &&
        aged.age_seconds <= FrequencyEstimateQualifier::kStaleMaximumAgeSeconds)
    {
        sample.snapshot_time = utc_now -
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(aged.age_seconds));
    }
    sample.retained_source_samples = aged.retained_source_samples;
    sample.source_stability_span_seconds = aged.source_stability_span_seconds;
    sample.reason = aged.error_reason;
    return sample;
}

SystemClockFrequencyEstimate FrequencyEstimateQualifier::evaluate(
    SystemClockFrequencyEstimate sample,
    std::chrono::steady_clock::time_point now)
{
    const auto attach_stale_fallback = [&]()
    {
        if (!last_qualified_.has_value() || now < last_qualified_at_)
            return;
        const double age = std::chrono::duration<double>(
            now - last_qualified_at_).count() +
            last_qualified_->age_seconds;
        if (age <= kStaleMaximumAgeSeconds)
        {
            sample.last_qualified_frequency_ppm = last_qualified_->frequency_ppm;
            sample.last_qualified_age_seconds = age;
            sample.provider_name = last_qualified_->provider_name;
            sample.source_provenance = last_qualified_->source_provenance;
            sample.source_signature = last_qualified_->source_signature;
            sample.snapshot_time = last_qualified_->snapshot_time;
        }
    };
    sample.qualification = FrequencyEstimateQualification::Unavailable;

    if (!sample.frequency_ppm.has_value() || !finite_bounded(*sample.frequency_ppm))
    {
        sample.reason = "Provider did not return a valid frequency estimate.";
        attach_stale_fallback();
        return sample;
    }
    if (!std::isfinite(sample.age_seconds) || sample.age_seconds < 0.0)
    {
        sample.reason = "Provider estimate age is malformed.";
        attach_stale_fallback();
        return sample;
    }
    if (!std::isfinite(sample.source_stability_span_seconds) ||
        sample.source_stability_span_seconds < 0.0)
    {
        sample.reason = "Provider source stability span is malformed.";
        attach_stale_fallback();
        return sample;
    }
    if (!sample.skew_ppm.has_value() || !std::isfinite(*sample.skew_ppm) ||
        *sample.skew_ppm < 0.0)
    {
        sample.reason = "Provider skew metadata is malformed.";
        attach_stale_fallback();
        return sample;
    }
    if (!sample.residual_frequency_ppm.has_value() ||
        !std::isfinite(*sample.residual_frequency_ppm))
    {
        sample.reason = "Provider residual-frequency metadata is malformed.";
        attach_stale_fallback();
        return sample;
    }

    if (!sample.synchronized || !sample.selected_source || !sample.leap_normal)
    {
        sample.reason = "Provider is not synchronized to a selected source with normal leap status.";
        attach_stale_fallback();
        return sample;
    }
    if (sample.source_signature.empty())
    {
        sample.reason = "Provider source signature is missing.";
        attach_stale_fallback();
        return sample;
    }
    if (sample.age_seconds > kStaleMaximumAgeSeconds)
    {
        sample.reason = "The last provider estimate is older than the stale fallback interval.";
        attach_stale_fallback();
        return sample;
    }
    if (*sample.skew_ppm > kMaximumSkewPpm)
    {
        sample.qualification = FrequencyEstimateQualification::Converging;
        sample.reason = "Provider skew exceeds the qualification limit.";
        attach_stale_fallback();
        return sample;
    }
    if (std::abs(*sample.residual_frequency_ppm) > kMaximumResidualFrequencyPpm)
    {
        sample.qualification = FrequencyEstimateQualification::Converging;
        sample.reason = "Provider residual frequency exceeds the qualification limit.";
        attach_stale_fallback();
        return sample;
    }
    if (sample.retained_source_samples < kMinimumProviderRetainedSamples)
    {
        sample.qualification = FrequencyEstimateQualification::Converging;
        sample.reason = "The selected provider sources do not yet retain enough observations.";
        attach_stale_fallback();
        return sample;
    }

    if (sample.age_seconds > kCurrentMaximumAgeSeconds)
    {
        // Age alone cannot promote an observation that never qualified.
        sample.reason = "The provider estimate is stale; only a previously qualified snapshot may be reused.";
        attach_stale_fallback();
        return sample;
    }

    if (sample.source_signature != source_signature_)
    {
        source_signature_ = sample.source_signature;
        frequency_history_.clear();
    }
    frequency_history_.push_back(*sample.frequency_ppm);
    while (frequency_history_.size() > kRequiredStableSamples)
    {
        frequency_history_.pop_front();
    }

    const auto bounds = std::minmax_element(
        frequency_history_.begin(), frequency_history_.end());
    if (frequency_history_.size() < kRequiredStableSamples ||
        *bounds.second - *bounds.first > kMaximumSampleSpreadPpm)
    {
        sample.qualification = FrequencyEstimateQualification::Converging;
        sample.reason = "Provider estimate has not completed the stability observation window.";
        attach_stale_fallback();
        return sample;
    }

    sample.qualification = FrequencyEstimateQualification::Qualified;
    sample.reason = "Provider estimate meets the configured quality and stability criteria.";
    last_qualified_ = sample;
    last_qualified_at_ = now;
    return sample;
}

void FrequencyEstimateQualifier::reset()
{
    source_signature_.clear();
    frequency_history_.clear();
    last_qualified_.reset();
    last_qualified_at_ = {};
}

GpioFrequencyCorrection select_gpio_frequency_correction(
    bool use_estimate,
    double residual_ppm,
    double manual_ppm,
    const SystemClockFrequencyEstimate &estimate)
{
    if (!finite_bounded(residual_ppm))
    {
        return invalid_correction(
            "Configured GPIO conducted residual PPM is non-finite or outside +/-200.");
    }
    if (!finite_bounded(manual_ppm))
    {
        return invalid_correction(
            "Configured GPIO manual PPM is non-finite or outside +/-200.");
    }

    GpioFrequencyCorrection result;
    result.qualification = estimate.qualification;
    result.provider_name = estimate.provider_name;
    result.source_provenance = estimate.source_provenance;
    result.source_signature = estimate.source_signature;
    result.reason = estimate.reason;
    result.estimate_age_seconds = estimate.age_seconds;
    result.snapshot_time = estimate.snapshot_time;
    result.residual_ppm = residual_ppm;

    if (use_estimate && estimate.frequency_ppm.has_value() &&
        estimate.qualification == FrequencyEstimateQualification::Qualified)
    {
        if (!finite_bounded(*estimate.frequency_ppm))
        {
            return invalid_correction(
                "Qualified provider estimate PPM is non-finite or outside +/-200.");
        }
        result.estimate_ppm = estimate.frequency_ppm;
        result.effective_ppm = *estimate.frequency_ppm + residual_ppm;
        if (!finite_bounded(result.effective_ppm))
        {
            return invalid_correction(
                "Qualified provider estimate plus conducted residual is outside +/-200 PPM.");
        }
        result.mode = GpioCorrectionMode::QualifiedEstimate;
        return result;
    }

    const std::optional<double> stale_estimate =
        estimate.qualification == FrequencyEstimateQualification::Stale
            ? estimate.frequency_ppm
            : estimate.last_qualified_frequency_ppm;
    if (use_estimate && stale_estimate.has_value())
    {
        if (!finite_bounded(*stale_estimate))
        {
            return invalid_correction(
                "Stale provider estimate PPM is non-finite or outside +/-200.");
        }
        const double stale_age =
            estimate.qualification == FrequencyEstimateQualification::Stale
                ? estimate.age_seconds
                : estimate.last_qualified_age_seconds;
        if (!std::isfinite(stale_age) || stale_age < 0.0 ||
            stale_age > FrequencyEstimateQualifier::kStaleMaximumAgeSeconds)
        {
            return invalid_correction(
                "Stale provider estimate age is malformed or expired.");
        }
        result.estimate_ppm = stale_estimate;
        result.estimate_age_seconds = stale_age;
        result.effective_ppm = *stale_estimate + residual_ppm;
        if (!finite_bounded(result.effective_ppm))
        {
            return invalid_correction(
                "Stale provider estimate plus conducted residual is outside +/-200 PPM.");
        }
        result.mode = GpioCorrectionMode::StaleEstimate;
        return result;
    }

    if (finite_bounded(manual_ppm) && (!use_estimate || manual_ppm != 0.0))
    {
        result.effective_ppm = manual_ppm;
        result.mode = GpioCorrectionMode::FixedManual;
        if (use_estimate)
        {
            result.reason = estimate.reason.empty()
                ? "Provider estimate is not qualified; using fixed manual correction."
                : estimate.reason + " Using fixed manual correction.";
        }
        return result;
    }

    result.effective_ppm = 0.0;
    result.mode = GpioCorrectionMode::Uncalibrated;
    result.reason = "No valid provider estimate or fixed manual correction is available.";
    return result;
}

GpioFrequencyCorrectionComposition compose_gpio_frequency_correction(
    double intrinsic_ppm,
    const GpioFrequencyCorrection &selected_additional)
{
    GpioFrequencyCorrectionComposition result;
    result.intrinsic_ppm = intrinsic_ppm;
    result.additional_ppm = selected_additional.effective_ppm;

    if (!selected_additional.valid)
    {
        result.reason = selected_additional.reason.empty()
            ? "Selected additional GPIO correction is invalid."
            : selected_additional.reason;
        return result;
    }
    if (!finite_bounded(intrinsic_ppm))
    {
        result.reason =
            "Intrinsic GPIO system-to-RF difference is non-finite or outside +/-200 PPM.";
        return result;
    }
    if (!finite_bounded(selected_additional.effective_ppm))
    {
        result.reason =
            "Selected additional GPIO correction is non-finite or outside +/-200 PPM.";
        return result;
    }

    const auto composition = wsprrypi::composeLegacyGpioCorrection(
        intrinsic_ppm,
        selected_additional.effective_ppm);
    result.effective_ppm = composition.effective_ppm;
    if (!composition.valid)
    {
        result.reason =
            "Intrinsic plus additional GPIO correction is invalid.";
        return result;
    }

    result.valid = true;
    return result;
}

const char *to_string(FrequencyEstimateQualification value) noexcept
{
    switch (value)
    {
    case FrequencyEstimateQualification::Qualified: return "qualified";
    case FrequencyEstimateQualification::Converging: return "converging";
    case FrequencyEstimateQualification::Stale: return "stale";
    case FrequencyEstimateQualification::Unavailable: return "unavailable";
    }
    return "unavailable";
}

const char *to_string(GpioCorrectionMode value) noexcept
{
    switch (value)
    {
    case GpioCorrectionMode::QualifiedEstimate: return "qualified_estimate_plus_residual";
    case GpioCorrectionMode::StaleEstimate: return "stale_estimate_plus_residual";
    case GpioCorrectionMode::FixedManual: return "fixed_manual";
    case GpioCorrectionMode::Uncalibrated: return "uncalibrated";
    }
    return "uncalibrated";
}
