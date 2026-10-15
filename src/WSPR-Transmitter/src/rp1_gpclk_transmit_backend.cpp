#include "rp1_gpclk_transmit_backend.hpp"
#include "chipset_offsets.hpp"

#include "wspr_transmit.hpp"
#include "rp1_gpclk_uapi.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace
{
std::mutex operation_record_mutex;
wsprrypi::Rp1GpclkOperationRecord operation_record;
std::uint64_t operation_record_owner{0};

std::uint64_t begin_record(const wsprrypi::Rp1GpclkOperationRecord& value)
{
    std::lock_guard<std::mutex> lock(operation_record_mutex);
    if (operation_record_owner == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("RP1 GPCLK operation record generation exhausted.");
    ++operation_record_owner;
    operation_record = value;
    return operation_record_owner;
}

void update_record(const wsprrypi::Rp1GpclkOperationRecord& value, std::uint64_t owner)
{
    std::lock_guard<std::mutex> lock(operation_record_mutex);
    if (owner != 0 && owner == operation_record_owner) operation_record = value;
}

const char* terminal_reason_name(std::uint32_t reason) noexcept
{
    switch (reason)
    {
    case RP1_GPCLK_REASON_NONE: return "none";
    case RP1_GPCLK_REASON_COMPLETE: return "complete";
    case RP1_GPCLK_REASON_STOPPED: return "stopped";
    case RP1_GPCLK_REASON_OWNER_CLOSED: return "owner-closed";
    case RP1_GPCLK_REASON_PROVIDER_REMOVED: return "provider-removed";
    case RP1_GPCLK_REASON_DEADLINE_MISSED: return "deadline-missed";
    case RP1_GPCLK_REASON_INVALID_REQUEST: return "invalid-request";
    case RP1_GPCLK_REASON_RESOURCE_UNAVAILABLE: return "resource-unavailable";
    case RP1_GPCLK_REASON_STARTUP_CONFLICT: return "startup-conflict";
    case RP1_GPCLK_REASON_DMA_FAILED: return "dma-failed";
    case RP1_GPCLK_REASON_CLOCK_FAILED: return "clock-failed";
    case RP1_GPCLK_REASON_PINCTRL_FAILED: return "pinctrl-failed";
    case RP1_GPCLK_REASON_READBACK_FAILED: return "readback-failed";
    case RP1_GPCLK_REASON_CLEANUP_FAILED: return "cleanup-failed";
    case RP1_GPCLK_REASON_COMPATIBILITY_REJECTED: return "compatibility-rejected";
    case RP1_GPCLK_REASON_INTERNAL_ERROR: return "internal-error";
    default: return "unknown";
    }
}
}

namespace wsprrypi
{
Rp1GpclkOperationRecord rp1GpclkOperationRecordSnapshot()
{
    std::lock_guard<std::mutex> lock(operation_record_mutex);
    return operation_record;
}
}

WsprRp1GpclkBackend::WsprRp1GpclkBackend(IControllerBridge& owner)
    : owner_(owner),
      io_(std::make_unique<wsprrypi::Rp1GpclkPosixIo>()),
      provider_(std::make_unique<wsprrypi::Rp1GpclkLinuxProvider>(*io_)),
      backend_(std::make_unique<wsprrypi::Rp1GpclkBackend>(*provider_))
{
}

WsprRp1GpclkBackend::WsprRp1GpclkBackend(
    IControllerBridge& owner,
    std::unique_ptr<wsprrypi::Rp1GpclkProvider> provider)
    : WsprRp1GpclkBackend(owner, std::move(provider), ExecutionClock{})
{
}

WsprRp1GpclkBackend::WsprRp1GpclkBackend(IControllerBridge& owner,
    std::unique_ptr<wsprrypi::Rp1GpclkProvider> provider, ExecutionClock clock)
    : owner_(owner),
      provider_(std::move(provider)),
      backend_(std::make_unique<wsprrypi::Rp1GpclkBackend>(*provider_)),
      clock_(std::move(clock))
{
}

WsprRp1GpclkBackend::~WsprRp1GpclkBackend()
{
    cleanup();
}

wsprrypi::BackendInfo WsprRp1GpclkBackend::info() const
{
    return {wsprrypi::BackendKind::RP1_GPCLK, "rp1-gpclk",
        "RP1 kernel-owned GPCLK0 WSPR backend"};
}

wsprrypi::BackendCapabilities WsprRp1GpclkBackend::capabilities() const
{
    wsprrypi::BackendCapabilities caps;
    caps.output_class = wsprrypi::BackendOutputClass::PHYSICAL_GPIO_RF;
    caps.supported_modes =
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::WSPR) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::TONE) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::QRSS) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::FSKCW) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::DFCW);
    caps.supports_precomputed_execution = true;
    caps.supports_frequency_switching = true;
    caps.supports_rf_gating = true;
    caps.min_event_duration = std::chrono::nanoseconds{RP1_GPCLK_EVENT_DURATION_NS_MIN};
    caps.min_frequency_hz = wsprrypi::kRp1GpclkNominalParentFrequencyHz / 65535.99998474121;
    caps.max_frequency_hz = wsprrypi::kRp1GpclkMaximumDirectOutputHz;
    return caps;
}

bool WsprRp1GpclkBackend::setScheduledExecutionHooks(wsprrypi::ScheduledExecutionHooks hooks)
{
    execution_hooks_ = std::move(hooks);
    return true;
}

std::vector<double> WsprRp1GpclkBackend::realizedEventFrequencies() const
{
    return configured_ ? configured_->realized_frequencies : std::vector<double>{};
}

wsprrypi::BackendCompileResult WsprRp1GpclkBackend::configure(
    const wsprrypi::ExecutionPlan& plan,
    const wsprrypi::BackendExecutionInputs& inputs)
{
    wsprrypi::BackendCompileResult result;
    configured_.reset();
    if (plan.backend != wsprrypi::BackendKind::RP1_GPCLK)
    {
        result.error = "Execution plan is not targeted for RP1 GPCLK.";
        return result;
    }
    if (!wsprrypi::Rp1GpclkBackend::validDrive(inputs.power_level))
    {
        result.error = "RP1 GPIO drive must be 2, 4, 8, or 12 mA.";
        return result;
    }
    const std::uint32_t route = inputs.tx_gpio == 4 ? RP1_GPCLK_ROUTE_GPIO4 :
        inputs.tx_gpio == 20 ? RP1_GPCLK_ROUTE_GPIO20 : RP1_GPCLK_ROUTE_INVALID;
    if (route == RP1_GPCLK_ROUTE_INVALID)
    {
        result.error = "RP1 GPCLK requires the independently selected GPIO4 or GPIO20 route.";
        return result;
    }

    const auto compiled = wsprrypi::compileRp1GpclkEventProgram(plan,
        static_cast<bool>(execution_hooks_.admit_output_enable));
    if (!compiled.ok)
    {
        result.error = compiled.error;
        return result;
    }

    const auto map_route = [](int gpio) {
        return gpio == 4 ? RP1_GPCLK_ROUTE_GPIO4 :
            gpio == 20 ? RP1_GPCLK_ROUTE_GPIO20 : RP1_GPCLK_ROUTE_INVALID;
    };
    wsprrypi::Rp1GpclkDevelopmentPolicyInputs policy;
    const auto& source = inputs.rp1_development;
    policy.development_testing_enabled = source.enabled;
    policy.rp1_backend_selected = true;
    policy.requested_route = route;
    policy.persisted_route = map_route(source.persisted_gpio);
    policy.configured_route = map_route(inputs.configured_tx_gpio);
    policy.active_route = map_route(source.active_gpio);
    policy.module_route = map_route(source.module_gpio);
    policy.active_route_count = source.active_route_count;
    policy.route_transaction_resolved = source.route_transaction_resolved;
    policy.scheduler_idle = source.scheduler_idle;
    policy.application_owns_operation = source.application_owns_operation;
    policy.endpoint_available = source.endpoint_available;
    policy.endpoint_closed = source.endpoint_closed;
    policy.endpoint_exclusively_acquirable = source.endpoint_exclusively_acquirable;
    policy.cleanup_fault = source.cleanup_fault;
    policy.physical_connection_confirmed = source.physical_connection_confirmed;
    policy.attenuation_and_load_confirmed = source.attenuation_and_load_confirmed;
    policy.bounded_operation_confirmed = source.bounded_operation_confirmed;
    policy.non_radiating_topology_confirmed = source.non_radiating_topology_confirmed;
    policy.experimental_status_acknowledged = source.experimental_status_acknowledged;
    policy.confirmation_current = source.confirmation_current;
    policy.route_transaction_generation = source.route_transaction_generation;
    policy.confirmation_route_transaction_generation =
        source.confirmation_route_transaction_generation;
    policy.operation_id = source.operation_id;
    policy.confirmation_operation_id = source.confirmation_operation_id;
    policy.confirmation_route = map_route(source.confirmation_gpio);

    ConfiguredFrame frame;
    frame.plan_id = plan.id;
    frame.event_program = compiled.program;
    frame.realized_frequencies = compiled.realized_frequencies;
    frame.continuous_tone =
        plan.mode == wsprrypi::TransmissionMode::TONE &&
        !plan.duration_was_explicit;
    frame.drive_ma = static_cast<std::uint32_t>(inputs.power_level);
    frame.route = route;
    frame.required_capabilities = RP1_GPCLK_CAP_SUBMIT_EVENTS |
        RP1_GPCLK_CAP_STOP_DRAIN | RP1_GPCLK_CAP_STABLE_STATE |
        RP1_GPCLK_CAP_ROUTE_IDENTITY | RP1_GPCLK_CAP_COMPAT_IDENTITY |
        RP1_GPCLK_CAP_CLEANUP_FAULT_LATCH | RP1_GPCLK_CAP_OUTPUT_INHIBIT |
        RP1_GPCLK_CAP_PASSIVE_SNAPSHOT | RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS;
    frame.development_policy = std::move(policy);
    if (frame.development_policy.development_testing_enabled)
        wsprrypi::armRp1GpclkDevelopmentOperation(frame.development_policy);
    configured_ = std::move(frame);
    stop_requested_.store(false, std::memory_order_release);
    execution_consumed_.store(false, std::memory_order_release);
    result.ok = true;
    return result;
}

wsprrypi::ExecutionResult WsprRp1GpclkBackend::execute(
    const wsprrypi::ExecutionPlan& plan) try
{
    wsprrypi::ExecutionResult result;
    if (!configured_ || configured_->plan_id.value != plan.id.value)
    {
        result.error = "RP1 GPCLK execution plan is not configured.";
        return result;
    }

    if (execution_consumed_.exchange(true, std::memory_order_acq_rel)) {
        result.error = "RP1 GPCLK execution requires a freshly configured operation.";
        return result;
    }
    std::string error;
    wsprrypi::Rp1GpclkOperationRecord initial;
    initial.operation_id = configured_->development_policy.operation_id;
    initial.route = configured_->route;
    initial.state = "preparing";
    initial.process_id = static_cast<std::uint64_t>(::getpid());
    initial.started_monotonic_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_.now().time_since_epoch()).count());
    record_owner_ = begin_record(initial);
    {
        std::lock_guard<std::mutex> lock(backend_mutex_);
        wsprrypi::Rp1GpclkProviderIdentity identity;
        const auto refused = [&](const std::string& detail) {
            wsprrypi::invalidateRp1GpclkDevelopmentOperation();
            std::string cleanup_error;
            const bool cleaned = backend_->closeAfterFailure(cleanup_error);
            initial.state = cleaned ? "provider-rejected" : "cleanup-fault";
            initial.provider_detail = detail;
            if (!cleaned) initial.provider_detail += " Cleanup failed: " + cleanup_error;
            initial.cleanup_attempted = true;
            initial.cleanup_complete = cleaned;
            initial.cleanup_fault = !cleaned;
            initial.endpoint_closed = provider_->endpointClosed();
            initial.finished_monotonic_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(clock_.now().time_since_epoch()).count());
            update_record(initial, record_owner_);
            result.error = initial.provider_detail;
            result.cleanup_attempted = true;
            result.cleanup = {cleaned, cleanup_error};
            result.faulted = !cleaned;
            return result;
        };
        if (!provider_->query(configured_->route, configured_->required_capabilities,
                true, identity, error))
        {
            return refused(error);
        }
        auto consumed = wsprrypi::consumeRp1GpclkDevelopmentOperation(
            configured_->development_policy.operation_id,
            configured_->route, identity);
        if (!consumed)
        {
            return refused("stale-operator-confirmation: Development authorization was not armed for this exact operation and route.");
        }
        auto current_policy = std::move(*consumed);
        current_policy.identity = std::move(identity);
        current_policy.module_route = current_policy.identity.route;
        const auto decision = wsprrypi::decideRp1GpclkDevelopmentUse(current_policy);
        if (!decision.allowed)
        {
            return refused(std::string(decision.code) + ": " + decision.explanation);
        }
        wsprrypi::Rp1GpclkOperationRecord record;
        record.operation_id = current_policy.operation_id;
        record.module_id = current_policy.identity.module_id;
        record.module_version = current_policy.identity.build_id;
        record.compatibility_id = current_policy.identity.compatibility_id;
        record.route = configured_->route;
        record.endpoint = provider_->endpoint();
        record.state = "authorized";
        record.execution_authorized = true;
        record.process_id = static_cast<std::uint64_t>(::getpid());
        record.started_monotonic_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock_.now().time_since_epoch()).count());
        record.endpoint_closed = true;
        update_record(record, record_owner_);
        const auto record_failure = [&](const std::string& state,
                                        bool cleanup_attempted,
                                        bool cleanup_complete) {
            record.state = state;
            record.execution_authorized = false;
            record.provider_detail = error;
            record.cleanup_attempted = cleanup_attempted;
            record.cleanup_complete = cleanup_complete;
            record.cleanup_fault = !cleanup_complete;
            record.endpoint_closed = provider_->endpointClosed();
            record.lease = provider_->leaseId();
            record.finished_monotonic_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    clock_.now().time_since_epoch()).count());
            update_record(record, record_owner_);
        };
        if (!backend_->prepare(
                configured_->drive_ma,
                configured_->route,
                configured_->required_capabilities,
                error))
        {
            std::string cleanup_error;
            const bool cleaned = backend_->closeAfterFailure(cleanup_error);
            record_failure(cleaned ? "acquire-failed" : "cleanup-fault", true, cleaned);
            record.cleanup_fault = !cleaned;
            update_record(record, record_owner_);
            result.error = error;
            result.cleanup_attempted = true;
            result.cleanup = {cleaned, cleanup_error};
            result.faulted = !cleaned;
            return result;
        }
        record.lease = provider_->leaseId();
        record.endpoint_closed = false;
        record.state = "acquired";
        update_record(record, record_owner_);
        bool admitted = true;
        if (execution_hooks_.admit_output_enable) {
            admitted = plan.events.front().rf_on ? execution_hooks_.admit_output_enable() :
                execution_hooks_.admit_silent_start && execution_hooks_.admit_silent_start();
        }
        const bool submitted = admitted && backend_->emitEvents(
            configured_->event_program, error);
        if (!admitted) error = "Scheduled RP1 launch was not admitted.";
        if (!submitted)
        {
            const std::string submit_error = error;
            std::string cleanup_error;
            const bool cleanup_ok = backend_->cleanup(cleanup_error);
            record_failure(cleanup_ok ? "submit-failed" : "cleanup-fault",
                true, cleanup_ok);
            if (!cleanup_ok && !cleanup_error.empty())
                result.error = submit_error + " Cleanup failed: " + cleanup_error;
            else
                result.error = submit_error;
            result.cleanup_attempted = true;
            result.cleanup = {cleanup_ok, cleanup_error};
            return result;
        }
        record.generation = backend_->generation();
        record.state = "running";
        update_record(record, record_owner_);
        if (plan.events.front().rf_on && execution_hooks_.observe_output_enable)
            execution_hooks_.observe_output_enable();
    }

    bool stop_sent = false;
    bool deadline_expired = false;
    std::optional<std::chrono::steady_clock::time_point> execution_deadline;
    if (!configured_->continuous_tone)
    {
        const auto now = clock_.now();
        const auto margin = std::chrono::seconds{5};
        const auto available = std::chrono::steady_clock::time_point::max() - now;
        execution_deadline = plan.summary.total_duration <= available - margin
            ? now + plan.summary.total_duration + margin
            : std::chrono::steady_clock::time_point::max();
    }
    std::optional<std::chrono::steady_clock::time_point> drain_deadline;
    for (;;)
    {
        if ((stop_requested_.load(std::memory_order_acquire) ||
                owner_.backendShouldStop()) && !stop_sent)
        {
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(backend_mutex_);
                cancelled = backend_->cancel(error);
            }
            if (!cancelled) return closeFailedOperation("stop-failed", error, true);
            stop_sent = true;
            auto record = wsprrypi::rp1GpclkOperationRecordSnapshot();
            record.cancellation_requested = true;
            record.state = "draining";
            update_record(record, record_owner_);
            drain_deadline = clock_.now() +
                std::chrono::seconds{5};
        }

        if (!stop_sent && execution_deadline &&
            clock_.now() >= *execution_deadline)
        {
            deadline_expired = true;
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(backend_mutex_);
                cancelled = backend_->timedOut(error);
            }
            if (!cancelled) return closeFailedOperation("deadline-stop-failed", error, true);
            stop_sent = true;
            auto record = wsprrypi::rp1GpclkOperationRecordSnapshot();
            record.cancellation_requested = true;
            record.state = "draining";
            update_record(record, record_owner_);
            drain_deadline = clock_.now() +
                std::chrono::seconds{5};
        }

        {
            std::lock_guard<std::mutex> lock(backend_mutex_);
            const auto event_state = provider_->eventState(backend_->generation());
            const auto state = event_state.completion;
            if (event_state.current_event < plan.events.size())
                owner_.backendReportExecutionProgress(event_state.current_event);
            if (state == wsprrypi::Rp1GpclkCompletionState::complete ||
                state == wsprrypi::Rp1GpclkCompletionState::failed)
            {
                auto record = wsprrypi::rp1GpclkOperationRecordSnapshot();
                const bool cleaned = backend_->cleanup(error, event_state.cleanup_fault != 0);
                record.execution_authorized = false;
                record.terminal_reason = event_state.terminal_reason;
                record.terminal_reason_name =
                    terminal_reason_name(event_state.terminal_reason);
                record.provider_detail = event_state.error;
                if (!cleaned && !error.empty()) record.provider_detail += " Cleanup failed: " + error;
                record.cleanup_fault = event_state.cleanup_fault != 0 || !cleaned;
                record.elapsed_ns = event_state.elapsed_ns;
                record.remaining_ns = event_state.remaining_ns;
                record.state = !cleaned ? "cleanup-fault" :
                    state == wsprrypi::Rp1GpclkCompletionState::complete
                        ? "complete" : "failed";
                record.cleanup_attempted = true;
                record.cleanup_complete = cleaned;
                record.endpoint_closed = provider_->endpointClosed();
                record.lease = provider_->leaseId();
                record.finished_monotonic_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        clock_.now().time_since_epoch()).count());
                update_record(record, record_owner_);
                result.cleanup_attempted = true;
                result.cleanup = {cleaned, cleaned ? std::string{} : error};
                if (!cleaned)
                {
                    result.error = "RP1 GPCLK provider failed: " +
                        record.terminal_reason_name + " (" +
                        std::to_string(record.terminal_reason) +
                        "). Cleanup failed: " + error;
                    if (!event_state.error.empty()) result.error += " " + event_state.error;
                    result.faulted = true;
                    return result;
                }
                if (state == wsprrypi::Rp1GpclkCompletionState::failed)
                {
                    result.error = "RP1 GPCLK provider failed: " +
                        record.terminal_reason_name + " (" +
                        std::to_string(record.terminal_reason) + ").";
                    if (!record.provider_detail.empty())
                        result.error += " " + record.provider_detail;
                    if (record.cleanup_fault)
                        result.error += " The provider also reported a cleanup fault.";
                    result.faulted = true;
                    return result;
                }
                if (deadline_expired) {
                    record.state = "timeout";
                    record.provider_detail = "RP1 GPCLK finite execution deadline expired; bounded stop completed.";
                    update_record(record, record_owner_);
                    result.error = record.provider_detail;
                    result.faulted = true;
                    return result;
                }
                result.ok = true;
                result.stopped = stop_sent || record.terminal_reason == RP1_GPCLK_REASON_STOPPED ||
                    record.terminal_reason == RP1_GPCLK_REASON_OWNER_CLOSED;
                return result;
            }
            auto running = wsprrypi::rp1GpclkOperationRecordSnapshot();
            running.elapsed_ns = event_state.elapsed_ns;
            running.remaining_ns = event_state.remaining_ns;
            update_record(running, record_owner_);
        }
        if (drain_deadline && clock_.now() >= *drain_deadline)
        {
            return closeFailedOperation("drain-timeout",
                "RP1 GPCLK provider did not reach a terminal state within the bounded drain interval.", true);
        }
        if (clock_.wait) clock_.wait(std::chrono::milliseconds{10});
        else std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}
catch (const std::exception& error)
{
    return closeFailedOperation("provider-rejected", error.what());
}

wsprrypi::ExecutionResult WsprRp1GpclkBackend::closeFailedOperation(
    const std::string& state, const std::string& detail, bool unproven_cleanup)
{
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    std::lock_guard<std::mutex> lock(backend_mutex_);
    std::string cleanup_error;
    const bool cleaned = backend_->closeAfterFailure(cleanup_error, unproven_cleanup);
    auto record = wsprrypi::rp1GpclkOperationRecordSnapshot();
    record.state = state;
    record.provider_detail = detail;
    record.execution_authorized = false;
    record.cancellation_requested = record.cancellation_requested || unproven_cleanup;
    record.cleanup_attempted = true;
    record.cleanup_complete = cleaned;
    record.cleanup_fault = record.cleanup_fault || !cleaned;
    record.endpoint_closed = provider_->endpointClosed();
    record.lease = provider_->leaseId();
    record.finished_monotonic_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_.now().time_since_epoch()).count());
    if (!cleaned) record.provider_detail += " Cleanup failed: " + cleanup_error;
    update_record(record, record_owner_);
    wsprrypi::ExecutionResult result;
    result.error = record.provider_detail;
    result.faulted = !cleaned;
    result.cleanup_attempted = true;
    result.cleanup = {cleaned, cleanup_error};
    return result;
}

wsprrypi::StartupQuiesceResult WsprRp1GpclkBackend::quiesceForStartup() try
{
    std::string error;
    std::lock_guard<std::mutex> lock(backend_mutex_);
    wsprrypi::Rp1GpclkProviderIdentity identity;
    constexpr std::uint64_t required = RP1_GPCLK_CAP_STABLE_STATE |
        RP1_GPCLK_CAP_ROUTE_IDENTITY | RP1_GPCLK_CAP_COMPAT_IDENTITY |
        RP1_GPCLK_CAP_CLEANUP_FAULT_LATCH;
    if (!provider_->query(
            RP1_GPCLK_ROUTE_INVALID, required, false, identity, error))
        return {false, error};
    return {true, {}};
}
catch (const std::exception& error)
{
    return {false, error.what()};
}

void WsprRp1GpclkBackend::stop() noexcept
{
    stop_requested_.store(true, std::memory_order_release);
}

wsprrypi::CleanupResult WsprRp1GpclkBackend::cleanup() noexcept
{
    stop();
    std::string error;
    std::lock_guard<std::mutex> lock(backend_mutex_);
    const bool ok = backend_->cleanup(error);
    auto record = wsprrypi::rp1GpclkOperationRecordSnapshot();
    record.cleanup_attempted = true;
    record.execution_authorized = false;
    record.cleanup_fault = record.cleanup_fault || !ok;
    if (!ok && !error.empty()) record.provider_detail = error;
    record.cleanup_complete = ok;
    record.endpoint_closed = provider_->endpointClosed();
    record.lease = provider_->leaseId();
    if (!ok) record.state = "cleanup-fault";
    update_record(record, record_owner_);
    configured_.reset();
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    return {ok, error};
}
