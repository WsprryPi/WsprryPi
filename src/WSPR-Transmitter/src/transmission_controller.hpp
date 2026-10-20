#pragma once

#include <cstdint>
#include <optional>

#include "execution_plan_compiler.hpp"
#include "transmission_backend.hpp"
#include "transmission_request.hpp"

namespace wsprrypi
{

struct TransmissionPrepareOptions
{
    int power_level{0};
    Rp1GpclkDevelopmentInputs rp1_development{};
};

class TransmissionController
{
public:
    TransmissionController(IExecutionPlanCompiler& compiler,
                           ITransmissionBackend& backend);

    BackendCompileResult prepare(
        const TransmissionRequest& request,
        const TransmissionPrepareOptions& options = {});
    ExecutionResult execute_prepared();
    ExecutionResult transmit(
        const TransmissionRequest& request,
        const TransmissionPrepareOptions& options = {});
    StartupQuiesceResult quiesceForStartup();
    const ExecutionPlan* prepared_plan() const noexcept;
    void reset() noexcept;
    void stop() noexcept;

private:
    void apply_adjustments(const BackendCompileResult& configure_result);
    BackendExecutionInputs build_backend_inputs(
        const TransmissionRequest& request,
        const TransmissionPrepareOptions& options) const noexcept;

    IExecutionPlanCompiler& compiler_;
    ITransmissionBackend& backend_;
    std::uint64_t next_plan_id_{1};
    std::optional<ExecutionPlan> prepared_plan_{};
};

} // namespace wsprrypi
