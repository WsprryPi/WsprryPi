#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "transmission_payloads.hpp"
#include "legacy_gpio_clock_model.hpp"

namespace wsprrypi
{

enum class BackendKind
{
    RPI_CLOCK_GPIO,
    RP1_GPCLK,
    SI5351,
    SIMULATED,
    WTP // Explicit complete-plan construction; production selection is deferred.
};

enum class ClockSource
{
    UNSPECIFIED,
    GPIO_CLK,
    SI5351_CLK0,
    SI5351_CLK1,
    SI5351_CLK2
};

enum class HardwareProfile
{
    UNSPECIFIED,
    BCM2835,
    BCM2836_BCM2837,
    BCM2711,
    RP1_GPCLK,
    SI5351
};

inline HardwareProfile legacyHardwareProfile(
    LegacyGpioProcessorProfile processor) noexcept
{
    switch (processor)
    {
    case LegacyGpioProcessorProfile::Bcm2835:
        return HardwareProfile::BCM2835;
    case LegacyGpioProcessorProfile::Bcm2836Bcm2837:
        return HardwareProfile::BCM2836_BCM2837;
    case LegacyGpioProcessorProfile::Bcm2711:
        return HardwareProfile::BCM2711;
    }
    return HardwareProfile::UNSPECIFIED;
}

inline bool legacyHardwareProfileMatches(
    HardwareProfile committed_profile,
    LegacyGpioProcessorProfile detected_processor) noexcept
{
    return committed_profile == legacyHardwareProfile(detected_processor);
}

struct RequestId
{
    std::uint64_t value{0};
};

struct ScheduledSlot
{
    std::chrono::system_clock::time_point start_time{};
    std::chrono::nanoseconds start_offset{};
};

struct CalibrationSnapshot
{
    double ppm{0.0};
    std::optional<double> reference_frequency_hz{};
};

struct OutputSelection
{
    BackendKind backend{BackendKind::RPI_CLOCK_GPIO};
    ClockSource output{ClockSource::UNSPECIFIED};
    int gpio{0};
};

struct ExecutionPolicy
{
    bool allow_quantization{false};
    bool allow_backend_approximation{false};
    bool allow_truncation_on_stop{true};
    bool allow_unqualified_frequency{false};
    bool allow_non_amateur_frequency{false};
    HardwareProfile hardware_profile{HardwareProfile::UNSPECIFIED};
};

struct RequestMetadata
{
    std::string label;
    std::string origin;
    std::string note;
};

struct TransmissionRequest
{
    RequestId id{};
    ScheduledSlot slot{};

    TransmissionMode mode{TransmissionMode::WSPR};
    TransmissionPayload payload{};

    OutputSelection output{};
    CalibrationSnapshot calibration{};
    ExecutionPolicy policy{};
    RequestMetadata metadata{};
};

} // namespace wsprrypi
