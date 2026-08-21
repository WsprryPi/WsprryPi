#include "transmission_controller.hpp"

#include "gpio_band_policy.hpp"

namespace wsprrypi
{

TransmissionController::TransmissionController(
    IExecutionPlanCompiler& compiler,
    ITransmissionBackend& backend)
    : compiler_(compiler),
      backend_(backend)
{
}

BackendCompileResult TransmissionController::prepare(
    const TransmissionRequest& request,
    const TransmissionPrepareOptions& options)
{
    prepared_plan_ = compiler_.compile(request);
    prepared_plan_->id.value = next_plan_id_++;
    const BackendCapabilities capabilities = backend_.capabilities();
    if (!supports_mode(capabilities, prepared_plan_->mode))
    {
        prepared_plan_.reset();
        return BackendCompileResult{false, {},
            "Selected backend does not support the requested transmission mode."};
    }
    if (capabilities.output_class != BackendOutputClass::NON_RF_SIMULATION)
    {
        const auto policy =
            evaluate_gpio_band_policy(*prepared_plan_);
        if (!policy.allowed)
        {
            prepared_plan_.reset();
            return BackendCompileResult{false, {}, policy.error};
        }
    }

    const BackendCompileResult configure_result = backend_.configure(
        *prepared_plan_,
        build_backend_inputs(request, options));
    if (!configure_result.ok)
    {
        BackendCompileResult failure = configure_result;
        const CleanupResult cleanup_result = backend_.cleanup();
        if (!cleanup_result.ok)
        {
            if (!failure.error.empty())
                failure.error += " ";
            failure.error += "Cleanup failed";
            if (!cleanup_result.error.empty())
                failure.error += ": " + cleanup_result.error;
        }
        prepared_plan_.reset();
        return failure;
    }

    apply_adjustments(configure_result);
    return configure_result;
}

ExecutionResult TransmissionController::execute_prepared()
{
    if (!prepared_plan_.has_value())
    {
        return ExecutionResult{
            false,
            false,
            false,
            "No prepared execution plan."};
    }

    ExecutionResult result = backend_.execute(*prepared_plan_);
    result.cleanup_attempted = true;
    result.cleanup = backend_.cleanup();
    if (!result.cleanup.ok)
    {
        result.ok = false;
        result.faulted = true;
        if (!result.error.empty())
            result.error += " ";
        result.error += "Cleanup failed";
        if (!result.cleanup.error.empty())
            result.error += ": " + result.cleanup.error;
    }
    return result;
}

ExecutionResult TransmissionController::transmit(
    const TransmissionRequest& request,
    const TransmissionPrepareOptions& options)
{
    const BackendCompileResult configure_result = prepare(request, options);
    if (!configure_result.ok)
    {
        return ExecutionResult{
            false,
            false,
            false,
            configure_result.error};
    }

    return execute_prepared();
}

StartupQuiesceResult TransmissionController::quiesceForStartup()
{
    return backend_.quiesceForStartup();
}

const ExecutionPlan* TransmissionController::prepared_plan() const noexcept
{
    return prepared_plan_.has_value() ? &*prepared_plan_ : nullptr;
}

void TransmissionController::reset() noexcept
{
    prepared_plan_.reset();
}

void TransmissionController::apply_adjustments(
    const BackendCompileResult& configure_result)
{
    if (!prepared_plan_.has_value() || configure_result.adjustments.empty())
        return;

    const auto& adjustment = configure_result.adjustments.front();
    const double delta_hz =
        adjustment.actual_frequency_hz - adjustment.requested_frequency_hz;
    if (delta_hz == 0.0)
        return;

    prepared_plan_->reference_frequency_hz += delta_hz;
    for (auto& event : prepared_plan_->events)
    {
        if (event.rf_on ||
            prepared_plan_->mode == TransmissionMode::STANDARD_FELD)
            event.frequency_hz += delta_hz;
    }
    prepared_plan_->summary.min_frequency_hz += delta_hz;
    prepared_plan_->summary.max_frequency_hz += delta_hz;
}

BackendExecutionInputs TransmissionController::build_backend_inputs(
    const TransmissionRequest& request,
    const TransmissionPrepareOptions& options) const noexcept
{
    BackendExecutionInputs inputs;
    inputs.power_level = options.power_level;
    inputs.tx_gpio = request.output.gpio;
    return inputs;
}

void TransmissionController::stop() noexcept
{
    backend_.stop();
}

} // namespace wsprrypi
