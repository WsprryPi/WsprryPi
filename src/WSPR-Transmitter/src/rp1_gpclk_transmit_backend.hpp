#pragma once

#include "rp1_gpclk_backend.hpp"
#include "rp1_gpclk_linux_provider.hpp"
#include "rp1_gpclk_event_program.hpp"
#include "rp1_gpclk_development_policy.hpp"
#include "transmission_backend.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

class IControllerBridge;

namespace wsprrypi
{
struct Rp1GpclkOperationRecord
{
    std::uint32_t schema_version{2};
    std::string operation_id;
    std::string module_id;
    std::string module_version;
    std::string compatibility_id;
    std::uint32_t route{0};
    std::string endpoint{"/dev/rp1-gpclk"};
    std::uint64_t lease{0};
    std::uint64_t generation{0};
    std::string state{"idle"};
    std::uint32_t terminal_reason{0};
    std::string terminal_reason_name{"none"};
    bool cleanup_fault{false};
    std::uint64_t elapsed_ns{0};
    std::uint64_t remaining_ns{0};
    bool cancellation_requested{false};
    bool cleanup_attempted{false};
    bool cleanup_complete{false};
    bool endpoint_closed{true};
    bool execution_authorized{false};
    bool qualification_claim{false};
    std::uint64_t process_id{0};
    std::string executable{"wsprrypi"};
    std::uint64_t started_monotonic_ns{0};
    std::uint64_t finished_monotonic_ns{0};
};

Rp1GpclkOperationRecord rp1GpclkOperationRecordSnapshot();
}

class WsprRp1GpclkBackend final : public wsprrypi::ITransmissionBackend
{
public:
    explicit WsprRp1GpclkBackend(IControllerBridge& owner);
    WsprRp1GpclkBackend(
        IControllerBridge& owner,
        std::unique_ptr<wsprrypi::Rp1GpclkProvider> provider);
    ~WsprRp1GpclkBackend() override;

    wsprrypi::BackendInfo info() const override;
    wsprrypi::BackendCapabilities capabilities() const override;
    bool setScheduledExecutionHooks(wsprrypi::ScheduledExecutionHooks hooks) override;
    std::vector<double> realizedEventFrequencies() const override;
    wsprrypi::BackendCompileResult configure(
        const wsprrypi::ExecutionPlan& plan,
        const wsprrypi::BackendExecutionInputs& inputs) override;
    wsprrypi::ExecutionResult execute(
        const wsprrypi::ExecutionPlan& plan) override;
    wsprrypi::StartupQuiesceResult quiesceForStartup() override;
    void stop() noexcept override;
    wsprrypi::CleanupResult cleanup() noexcept override;

private:
    struct ConfiguredFrame
    {
        wsprrypi::PlanId plan_id{};
        wsprrypi::Rp1GpclkProviderEventProgram event_program{};
        bool continuous_tone{false};
        std::uint32_t drive_ma{2};
        std::uint32_t route{0};
        std::uint64_t required_capabilities{0};
        wsprrypi::Rp1GpclkDevelopmentPolicyInputs development_policy{};
        std::vector<double> realized_frequencies;
    };

    IControllerBridge& owner_;
    wsprrypi::ScheduledExecutionHooks execution_hooks_;
    std::unique_ptr<wsprrypi::Rp1GpclkIo> io_;
    std::unique_ptr<wsprrypi::Rp1GpclkProvider> provider_;
    std::unique_ptr<wsprrypi::Rp1GpclkBackend> backend_;
    std::optional<ConfiguredFrame> configured_;
    std::atomic<bool> stop_requested_{false};
    std::mutex backend_mutex_;
};
