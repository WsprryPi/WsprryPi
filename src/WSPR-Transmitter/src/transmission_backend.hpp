#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "execution_plan.hpp"
#include "rp1_gpclk_development_inputs.hpp"
#include "scheduled_execution.hpp"

namespace wsprrypi
{

class IExecutionContext
{
public:
    virtual ~IExecutionContext() = default;
    virtual bool stopRequested() const noexcept = 0;
    virtual bool waitInterruptibleFor(std::chrono::nanoseconds duration) = 0;
    virtual void reportExecutionProgress(std::size_t event_index) noexcept = 0;
    virtual std::chrono::nanoseconds logicalNow() const noexcept = 0;
};

enum class BackendOutputClass
{
    PHYSICAL_GPIO_RF,
    EXTERNAL_CLOCK_RF,
    NON_RF_SIMULATION
};

struct BackendInfo
{
    BackendKind kind{BackendKind::RPI_CLOCK_GPIO};
    std::string name;
    std::string description;
};

struct BackendCapabilities
{
    BackendOutputClass output_class{BackendOutputClass::PHYSICAL_GPIO_RF};
    std::uint32_t supported_modes{0};
    bool supports_frequency_switching{true};
    bool supports_rf_gating{true};
    bool supports_fade_shape{false};
    bool supports_continuous_phase{false};
    bool supports_precomputed_execution{false};

    std::chrono::nanoseconds min_event_duration{};
    double min_frequency_hz{0.0};
    double max_frequency_hz{0.0};
    double nominal_frequency_resolution_hz{0.0};
};

constexpr std::uint32_t transmission_mode_bit(TransmissionMode mode) noexcept
{
    return std::uint32_t{1} << static_cast<unsigned>(mode);
}

inline bool supports_mode(
    const BackendCapabilities& capabilities,
    TransmissionMode mode) noexcept
{
    return (capabilities.supported_modes & transmission_mode_bit(mode)) != 0;
}

struct BackendAdjustment
{
    std::size_t event_index{0};
    double requested_frequency_hz{0.0};
    double actual_frequency_hz{0.0};
    std::string note;
};

struct BackendCompileResult
{
    bool ok{false};
    std::vector<BackendAdjustment> adjustments{};
    std::string error;
};

struct BackendExecutionInputs
{
    int power_level{0};
    int tx_gpio{0};
    int configured_tx_gpio{0};
    Rp1GpclkDevelopmentInputs rp1_development;
};

struct CleanupResult
{
    bool ok{false};
    std::string error;
};

struct ExecutionResult
{
    bool ok{false};
    bool stopped{false};
    bool faulted{false};
    std::string error;
    bool cleanup_attempted{false};
    CleanupResult cleanup{};
};

/** Result of placing a backend into its safe startup state. */
struct StartupQuiesceResult
{
    bool ok{false};
    std::string error;
};

class ITransmissionBackend
{
public:
    virtual ~ITransmissionBackend() = default;

    virtual BackendInfo info() const = 0;
    virtual BackendCapabilities capabilities() const = 0;

    // Optional native scheduling contract; adapters must reject routes without
    // these hooks rather than claiming a physical launch before preparation.
    virtual bool setScheduledExecutionHooks(ScheduledExecutionHooks) { return false; }
    virtual std::vector<double> realizedEventFrequencies() const { return {}; }

    virtual BackendCompileResult configure(
        const ExecutionPlan& plan,
        const BackendExecutionInputs& inputs) = 0;
    virtual ExecutionResult execute(const ExecutionPlan& plan) = 0;
    virtual StartupQuiesceResult quiesceForStartup() = 0;

    virtual void stop() noexcept = 0;
    virtual CleanupResult cleanup() noexcept = 0;
};

} // namespace wsprrypi
