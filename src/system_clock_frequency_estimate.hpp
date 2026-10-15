#ifndef SYSTEM_CLOCK_FREQUENCY_ESTIMATE_HPP
#define SYSTEM_CLOCK_FREQUENCY_ESTIMATE_HPP

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "WSPR-Transmitter/src/legacy_gpio_clock_model.hpp"

struct PPMProviderSnapshot;

enum class FrequencyEstimateQualification
{
    Qualified,
    Converging,
    Unavailable,
    Stale
};

enum class GpioCorrectionMode
{
    QualifiedEstimate,
    StaleEstimate,
    FixedManual,
    Uncalibrated
};

inline constexpr double kMaximumGpioFrequencyCorrectionPpm =
    wsprrypi::kMaximumLegacyGpioCorrectionPpm;

struct SystemClockFrequencyEstimate
{
    std::string provider_name;
    FrequencyEstimateQualification qualification = FrequencyEstimateQualification::Unavailable;
    std::optional<double> frequency_ppm;
    std::optional<double> last_qualified_frequency_ppm;
    double last_qualified_age_seconds = 0.0;
    bool synchronized = false;
    double age_seconds = 0.0;
    std::optional<double> residual_frequency_ppm;
    std::optional<double> skew_ppm;
    bool selected_source = false;
    bool combined_sources = false;
    bool leap_normal = false;
    std::string source_provenance;
    std::string source_signature;
    std::chrono::system_clock::time_point snapshot_time{};
    std::size_t retained_source_samples = 0;
    double source_stability_span_seconds = 0.0;
    std::string reason;
};

struct GpioFrequencyCorrection
{
    bool valid = true;
    FrequencyEstimateQualification qualification = FrequencyEstimateQualification::Unavailable;
    GpioCorrectionMode mode = GpioCorrectionMode::Uncalibrated;
    std::string provider_name;
    std::string source_provenance;
    std::string source_signature;
    std::string reason;
    std::optional<double> estimate_ppm;
    double residual_ppm = 0.0;
    double intrinsic_ppm = 0.0;
    double additional_ppm = 0.0;
    double effective_ppm = 0.0;
    double estimate_age_seconds = 0.0;
    std::chrono::system_clock::time_point snapshot_time{};
};

struct GpioFrequencyCorrectionComposition
{
    bool valid = false;
    double intrinsic_ppm = 0.0;
    double additional_ppm = 0.0;
    double effective_ppm = 0.0;
    std::string reason;
};

class FrequencyEstimateQualifier
{
public:
    static constexpr std::size_t kRequiredStableSamples = 3;
    static constexpr double kMaximumSkewPpm = 1.0;
    static constexpr double kMaximumResidualFrequencyPpm = 0.5;
    static constexpr double kMaximumSampleSpreadPpm = 0.1;
    static constexpr std::size_t kMinimumProviderRetainedSamples = 3;
    static constexpr double kCurrentMaximumAgeSeconds = 300.0;
    static constexpr double kStaleMaximumAgeSeconds = 900.0;

    SystemClockFrequencyEstimate evaluate(
        SystemClockFrequencyEstimate sample,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void reset();

private:
    std::string source_signature_;
    std::deque<double> frequency_history_;
    std::optional<SystemClockFrequencyEstimate> last_qualified_;
    std::chrono::steady_clock::time_point last_qualified_at_{};
};

SystemClockFrequencyEstimate system_clock_frequency_estimate_from_provider(
    const PPMProviderSnapshot &provider,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
    std::chrono::system_clock::time_point utc_now = std::chrono::system_clock::now());

GpioFrequencyCorrection select_gpio_frequency_correction(
    bool use_estimate,
    double residual_ppm,
    double manual_ppm,
    const SystemClockFrequencyEstimate &estimate);

GpioFrequencyCorrectionComposition compose_gpio_frequency_correction(
    double intrinsic_ppm,
    const GpioFrequencyCorrection &selected_additional);

const char *to_string(FrequencyEstimateQualification value) noexcept;
const char *to_string(GpioCorrectionMode value) noexcept;

#endif
