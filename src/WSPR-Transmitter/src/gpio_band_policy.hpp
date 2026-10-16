#pragma once

#include <array>
#include <cmath>
#include <string>
#include <string_view>

#include "../../Band-Lookup/include/amateur_band_catalog.hpp"
#include "execution_plan.hpp"
#include "transmission_request.hpp"

namespace wsprrypi
{

enum class QualificationState
{
    QUALIFIED,
    UNQUALIFIED,
    UNTESTED,
    UNAVAILABLE
};

struct FrequencyPolicyDecision
{
    bool allowed{true};
    bool amateur_allocation{true};
    QualificationState qualification{QualificationState::QUALIFIED};
    std::string band;
    std::string error;
};

inline const char* qualification_state_name(QualificationState state) noexcept
{
    switch (state)
    {
    case QualificationState::QUALIFIED: return "qualified";
    case QualificationState::UNQUALIFIED: return "unqualified";
    case QualificationState::UNTESTED: return "untested";
    case QualificationState::UNAVAILABLE: return "unavailable";
    }
    return "unqualified";
}

namespace detail
{
inline std::string display_band_name(std::string_view canonical_band)
{
    const std::size_t unit_length = canonical_band.ends_with("cm") ? 2U : 1U;
    if (canonical_band.size() <= unit_length)
        return std::string(canonical_band);
    return std::string(canonical_band.substr(0, canonical_band.size() - unit_length)) +
        " " + std::string(canonical_band.substr(canonical_band.size() - unit_length));
}

inline QualificationState qualification_for(
    BackendKind backend, HardwareProfile profile, TransmissionMode mode,
    std::string_view band) noexcept
{
    if (backend == BackendKind::SIMULATED)
        return QualificationState::QUALIFIED;

    if (backend == BackendKind::WTP)
        return QualificationState::UNTESTED; // CAPS is not RF qualification.

    if (backend == BackendKind::SI5351)
    {
        if (band == "1.25m" || band == "70cm")
            return QualificationState::UNAVAILABLE;
        if (band == "8m" || band == "5m")
            return QualificationState::UNTESTED;
        return QualificationState::QUALIFIED;
    }

    if (backend != BackendKind::RPI_CLOCK_GPIO && backend != BackendKind::RP1_GPCLK)
        return QualificationState::UNAVAILABLE;

    // Application-owned qualification from the maintained Pi 5 / PLL_SYS table.
    // These dispositions are independent of provider metadata and overrides.
    if (backend == BackendKind::RP1_GPCLK)
    {
        if (band == "2m" || band == "1.25m" || band == "70cm")
            return QualificationState::UNAVAILABLE;
        if (profile == HardwareProfile::RP1_GPCLK && (band == "6m" || band == "4m"))
            return QualificationState::UNQUALIFIED;
        if (profile == HardwareProfile::RP1_GPCLK && (band == "8m" || band == "5m"))
            return QualificationState::UNTESTED;
        if (profile == HardwareProfile::RP1_GPCLK && band == "12m" &&
            (mode == TransmissionMode::WSPR || mode == TransmissionMode::TONE ||
             mode == TransmissionMode::QRSS || mode == TransmissionMode::FSKCW ||
             mode == TransmissionMode::DFCW))
            return QualificationState::QUALIFIED;
    }

    if (band == "2200m")
    {
        if (profile == HardwareProfile::BCM2711 ||
            profile == HardwareProfile::RP1_GPCLK)
            return QualificationState::QUALIFIED;
        if (profile == HardwareProfile::BCM2835 ||
            profile == HardwareProfile::BCM2836_BCM2837)
        {
            if (mode == TransmissionMode::TONE ||
                mode == TransmissionMode::QRSS ||
                mode == TransmissionMode::FSKCW ||
                mode == TransmissionMode::DFCW)
                return QualificationState::QUALIFIED;
        }
        return QualificationState::UNQUALIFIED;
    }

    const bool questionable = band == "12m" || band == "8m" || band == "6m" ||
        band == "5m" || band == "4m" || band == "2m" ||
        band == "1.25m" || band == "70cm";
    if (!questionable)
        return QualificationState::QUALIFIED;

    if (profile == HardwareProfile::BCM2711)
    {
        if (band == "1.25m" || band == "70cm")
            return QualificationState::UNAVAILABLE;
        if (band == "6m" &&
            (mode == TransmissionMode::TONE || mode == TransmissionMode::QRSS ||
             mode == TransmissionMode::FSKCW || mode == TransmissionMode::DFCW))
            return QualificationState::QUALIFIED;
        return QualificationState::UNQUALIFIED;
    }

    // Profiles without a completed mode-specific record remain fail-closed.
    // CW states become qualified only after the applicable qualification.
    if (mode == TransmissionMode::QRSS || mode == TransmissionMode::TONE ||
        mode == TransmissionMode::FSKCW || mode == TransmissionMode::DFCW)
        return QualificationState::UNTESTED;
    return QualificationState::UNQUALIFIED;
}
} // namespace detail

inline FrequencyPolicyDecision evaluate_frequency_policy(
    BackendKind backend,
    TransmissionMode mode,
    double frequency_hz,
    bool allow_unqualified_frequency = false,
    bool allow_non_amateur_frequency = false,
    HardwareProfile profile = HardwareProfile::UNSPECIFIED)
{
    FrequencyPolicyDecision decision;
    if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0)
        return decision; // Numeric/backend validation remains authoritative.

    const auto* band = bands::find(frequency_hz);
    decision.amateur_allocation = band != nullptr;
    if (band == nullptr)
    {
        decision.qualification = QualificationState::UNTESTED;
        if (allow_unqualified_frequency && allow_non_amateur_frequency)
            return decision;
        decision.allowed = false;
        decision.error =
            "Transmission is outside the recognized worldwide amateur-band catalog. "
            "Both --allow-unqualified-frequency and --allow-non-amateur-frequency are required.";
        return decision;
    }

    decision.band = std::string(band->canonical_name);
    decision.qualification = detail::qualification_for(
        backend, profile, mode, band->canonical_name);
    if (decision.qualification == QualificationState::QUALIFIED)
        return decision;
    if (decision.qualification != QualificationState::UNAVAILABLE &&
        allow_unqualified_frequency)
        return decision;

    decision.allowed = false;
    decision.error = "Transmission on the " + detail::display_band_name(decision.band) + " band is " +
        qualification_state_name(decision.qualification) +
        " for the selected backend and mode.";
    if (decision.qualification == QualificationState::UNAVAILABLE)
        decision.error += " This backend cannot safely construct the requested output.";
    else
        decision.error += " Use --allow-unqualified-frequency only if you accept the experimental risk.";
    return decision;
}

// Compatibility name retained while callers migrate from the former coarse GPIO rule.
inline FrequencyPolicyDecision evaluate_gpio_band_policy(
    BackendKind backend, double frequency_hz,
    TransmissionMode mode = TransmissionMode::WSPR,
    bool allow_unqualified_frequency = false,
    bool allow_non_amateur_frequency = false,
    HardwareProfile profile = HardwareProfile::UNSPECIFIED)
{
    return evaluate_frequency_policy(backend, mode, frequency_hz,
        allow_unqualified_frequency, allow_non_amateur_frequency, profile);
}

inline FrequencyPolicyDecision evaluate_gpio_band_policy(const ExecutionPlan& plan)
{
    for (const auto& event : plan.events)
    {
        if (!event.rf_on && event.type != RfEventType::SET_FREQUENCY)
            continue;
        const auto decision = evaluate_frequency_policy(
            plan.backend, plan.mode, event.frequency_hz,
            plan.policy.allow_unqualified_frequency,
            plan.policy.allow_non_amateur_frequency,
            plan.policy.hardware_profile);
        if (!decision.allowed)
            return decision;
    }
    return {};
}

} // namespace wsprrypi
