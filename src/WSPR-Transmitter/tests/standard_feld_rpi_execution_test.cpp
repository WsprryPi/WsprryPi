#include "execution_plan_compiler.hpp"
#include "rpi_standard_feld_execution.hpp"
#include "standard_feld_asset.hpp"
#include "transmission_controller.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace wsprrypi;

void require(bool condition, const std::string& detail)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << detail << '\n';
        std::exit(1);
    }
}

ExecutionPlan single_a_plan()
{
    TransmissionRequest request;
    request.id.value = 54;
    request.mode = TransmissionMode::STANDARD_FELD;
    request.output.backend = BackendKind::RPI_CLOCK_GPIO;
    request.payload = StandardFeldPayload{"A", 14'096'900.0};
    return ExecutionPlanCompiler{}.compile(request);
}

class AdjustmentBackend final : public ITransmissionBackend
{
public:
    BackendInfo info() const override
    {
        return {BackendKind::RPI_CLOCK_GPIO, "adjustment-fake", {}};
    }
    BackendCapabilities capabilities() const override
    {
        return {};
    }
    BackendCompileResult configure(
        const ExecutionPlan& plan,
        const BackendExecutionInputs&) override
    {
        return {true,
                {{0U,
                  plan.reference_frequency_hz,
                  plan.reference_frequency_hz + 0.25,
                  "injected carrier quantization"}},
                {}};
    }
    ExecutionResult execute(const ExecutionPlan& plan) override
    {
        executed = plan;
        return {true, false, false, {}};
    }
    void stop() noexcept override {}
    void cleanup() noexcept override {}

    std::optional<ExecutionPlan> executed{};
};

void validate_controller_adjustment()
{
    TransmissionRequest request;
    request.mode = TransmissionMode::STANDARD_FELD;
    request.output.backend = BackendKind::RPI_CLOCK_GPIO;
    request.payload = StandardFeldPayload{"A", 14'096'900.0};
    ExecutionPlanCompiler compiler;
    AdjustmentBackend backend;
    TransmissionController controller{compiler, backend};
    require(controller.prepare(request).ok,
            "controller must accept the backend carrier adjustment");
    const auto* adjusted = controller.prepared_plan();
    require(adjusted != nullptr &&
                adjusted->reference_frequency_hz == 14'096'900.25,
            "controller must adjust the compiled Standard Feld carrier");
    for (const auto& event : adjusted->events)
        require(event.frequency_hz == adjusted->reference_frequency_hz,
                "controller must adjust RF-on and RF-off carrier identity together");
    require(RpiStandardFeldExecution::validate(*adjusted).ok,
            "quantized Standard Feld plan must retain its atomic contract");
}

enum class Operation
{
    INITIAL_SAFE,
    WAIT,
    CARRIER,
    CANCELLATION_GATE,
    RF_ON,
    RF_OFF,
    PROGRESS,
    TERMINAL_SHUTDOWN,
    WATCHDOG_STOP,
    RF_ENVELOPE_OFF,
    DMA_RESET,
    PWM_DISABLE,
    CLOCK_DISABLE,
    CLOCK_VERIFY
};

struct Call
{
    Operation operation;
    std::optional<std::size_t> event_index{};
    std::chrono::nanoseconds deadline{};
    double frequency_hz{0.0};
    std::optional<RfEvent::RasterProgress> progress{};
};

struct FailurePoint
{
    Operation operation;
    std::size_t occurrence{0};
};

class FakeAdapter final : public IRpiStandardFeldExecutionAdapter
{
public:
    std::optional<std::size_t> cancel_boundary{};
    std::vector<FailurePoint> failures{};
    std::vector<Call> calls{};
    bool logical_rf_on{false};
    bool safe_idle_confirmed{false};
    mutable bool watchdog_fault{false};
    bool throw_watchdog_query{false};
    bool throw_watchdog_diagnostic{false};
    std::string watchdog_detail{"injected watchdog fault"};
    std::optional<Operation> cancel_after{};
    std::size_t cancel_after_occurrence{0};
    std::optional<Operation> watchdog_after{};
    std::size_t watchdog_after_occurrence{0};
    std::optional<Operation> watchdog_on_outcome{};
    bool watchdog_on_cancel{false};
    std::optional<std::size_t> cancel_on_check{};
    mutable std::size_t cancellation_checks{0};
    std::optional<FailurePoint> throw_at{};
    std::chrono::nanoseconds fake_now{};
    std::vector<std::chrono::nanoseconds> original_deadlines{};

    bool cancellation_requested() const noexcept override
    {
        const bool injected = cancel_on_check.has_value() &&
            cancellation_checks == *cancel_on_check;
        ++cancellation_checks;
        const bool cancelled = (cancel_boundary.has_value() &&
                                boundary_ == *cancel_boundary) || injected;
        if (cancelled && watchdog_on_cancel)
            watchdog_fault = true;
        return cancelled;
    }

    bool watchdog_faulted() const override
    {
        if (throw_watchdog_query)
            throw std::runtime_error("injected watchdog state exception");
        return watchdog_fault;
    }
    std::string watchdog_diagnostic() const override
    {
        if (throw_watchdog_diagnostic)
            throw std::runtime_error("injected watchdog diagnostic exception");
        return watchdog_detail;
    }

    RpiStandardFeldAdapterResult establish_initial_safe_state() override
    {
        return shutdown_sequence(Operation::INITIAL_SAFE);
    }

    RpiStandardFeldAdapterResult wait_until(
        std::chrono::nanoseconds deadline) override
    {
        calls.push_back({Operation::WAIT, {}, deadline});
        original_deadlines.push_back(deadline);
        if (fake_now < deadline)
            fake_now = deadline;
        if (should_fail(Operation::WAIT))
            return RpiStandardFeldAdapterResult::failure(
                "injected wait failure");
        std::chrono::nanoseconds expected{};
        for (std::size_t candidate = boundary_;
             candidate <= 294U;
             ++candidate)
        {
            require(RpiStandardFeldExecution::exact_boundary(
                        candidate, expected),
                    "fake boundary must be representable");
            if (expected == deadline)
            {
                boundary_ = candidate;
                inject_after(Operation::WAIT);
                return RpiStandardFeldAdapterResult::success();
            }
        }
        return RpiStandardFeldAdapterResult::failure(
            "deadline is not a frozen physical-position boundary");
    }

    RpiStandardFeldAdapterResult apply_carrier(
        double frequency_hz,
        std::size_t event_index) override
    {
        calls.push_back(
            {Operation::CARRIER, event_index, {}, frequency_hz});
        if (should_fail(Operation::CARRIER))
            return RpiStandardFeldAdapterResult::failure(
                "injected carrier failure");
        inject_after(Operation::CARRIER);
        return RpiStandardFeldAdapterResult::success();
    }

    RpiStandardFeldAdapterResult apply_rf_checked(
        const RfEvent& event,
        std::size_t event_index) override
    {
        calls.push_back({Operation::CANCELLATION_GATE, event_index});
        inject_after(Operation::CANCELLATION_GATE);
        if (watchdog_fault || cancellation_requested())
            return RpiStandardFeldAdapterResult::cancelled();
        const auto operation =
            event.rf_on ? Operation::RF_ON : Operation::RF_OFF;
        calls.push_back({operation, event_index});
        if (should_fail(operation))
            return RpiStandardFeldAdapterResult::failure(
                event.rf_on
                    ? "injected RF-on failure"
                    : "injected RF-off failure");
        logical_rf_on = event.rf_on;
        return RpiStandardFeldAdapterResult::success();
    }

    RpiStandardFeldAdapterResult report_progress(
        std::size_t event_index,
        const RfEvent::RasterProgress& progress) override
    {
        Call call{Operation::PROGRESS, event_index};
        call.progress = progress;
        calls.push_back(call);
        if (should_fail(Operation::PROGRESS))
            return RpiStandardFeldAdapterResult::failure(
                "injected progress failure");
        inject_after(Operation::PROGRESS);
        return RpiStandardFeldAdapterResult::success();
    }

    RpiStandardFeldAdapterResult complete_terminal_shutdown() override
    {
        return shutdown_sequence(Operation::TERMINAL_SHUTDOWN);
    }

private:
    RpiStandardFeldAdapterResult shutdown_sequence(Operation marker)
    {
        calls.push_back({marker});
        std::string failed;
        const Operation components[]{
            Operation::WATCHDOG_STOP, Operation::RF_ENVELOPE_OFF,
            Operation::DMA_RESET, Operation::PWM_DISABLE,
            Operation::CLOCK_DISABLE, Operation::CLOCK_VERIFY};
        if (should_fail(marker))
            failed = "shutdown entry";
        for (const auto component : components)
        {
            calls.push_back({component});
            if (should_fail(component))
            {
                if (!failed.empty()) failed += ", ";
                failed += "component " +
                    std::to_string(static_cast<int>(component));
            }
        }
        logical_rf_on = false;
        safe_idle_confirmed = failed.empty();
        return failed.empty()
            ? RpiStandardFeldAdapterResult::success()
            : RpiStandardFeldAdapterResult::failure(
                "injected shutdown failure: " + failed);
    }

    void inject_after(Operation operation)
    {
        const auto occurrence = injections_[static_cast<int>(operation)]++;
        if (cancel_after == operation && occurrence == cancel_after_occurrence)
            cancel_boundary = boundary_;
        if (watchdog_after == operation &&
            occurrence == watchdog_after_occurrence)
            watchdog_fault = true;
    }

    bool should_fail(Operation operation)
    {
        const std::size_t occurrence = occurrences_[static_cast<int>(operation)]++;
        if (throw_at.has_value() && throw_at->operation == operation &&
            throw_at->occurrence == occurrence)
        {
            if (watchdog_on_outcome == operation)
                watchdog_fault = true;
            throw std::runtime_error("injected adapter exception");
        }
        for (const auto& failure : failures)
            if (failure.operation == operation && failure.occurrence == occurrence)
            {
                if (watchdog_on_outcome == operation)
                    watchdog_fault = true;
                return true;
            }
        return false;
    }

    std::size_t boundary_{0};
    std::size_t occurrences_[14]{};
    std::size_t injections_[14]{};
};

std::size_t count(const FakeAdapter& adapter, Operation operation)
{
    std::size_t result = 0;
    for (const auto& call : adapter.calls)
        result += call.operation == operation ? 1U : 0U;
    return result;
}

void require_no_calls(const ExecutionPlan& plan, const std::string& expected)
{
    FakeAdapter adapter;
    const auto result = RpiStandardFeldExecution::execute(plan, adapter);
    require(result.terminal == RpiStandardFeldExecutionTerminal::REJECTED,
            expected + " must reject");
    require(adapter.calls.empty(), expected + " must reject atomically");
    require(result.error.find(expected) != std::string::npos,
            expected + " must have stable diagnostic");
}

void validate_atomic_rejections(const ExecutionPlan& valid)
{
    auto malformed = valid;
    malformed.mode = TransmissionMode::QRSS;
    require_no_calls(malformed, "STANDARD_FELD");
    malformed = valid;
    malformed.backend = BackendKind::SI5351;
    require_no_calls(malformed, "not targeted");
    malformed = valid;
    malformed.events.clear();
    malformed.summary.event_count = 0;
    require_no_calls(malformed, "no events");
    malformed = valid;
    malformed.events[1].offset_from_start = malformed.events[0].offset_from_start;
    require_no_calls(malformed, "245-position/s");
    malformed = valid;
    malformed.events[2].offset_from_start =
        malformed.events[1].offset_from_start - std::chrono::nanoseconds{1};
    require_no_calls(malformed, "245-position/s");
    malformed = valid;
    malformed.events[0].duration = std::chrono::nanoseconds::zero();
    require_no_calls(malformed, "245-position/s");
    malformed = valid;
    malformed.events[1].offset_from_start =
        std::chrono::nanoseconds::max();
    require_no_calls(malformed, "245-position/s");
    malformed = valid;
    malformed.events[5].raster_progress.reset();
    require_no_calls(malformed, "lacks raster progress");
    malformed = valid;
    malformed.events[98].raster_progress->normalized_char_index = 1;
    require_no_calls(malformed, "normalized character index");
    malformed = valid;
    malformed.events[40].frequency_hz = 0.0;
    require_no_calls(malformed, "carrier intent");
    malformed = valid;
    malformed.events[40].type = RfEventType::RF_ON;
    malformed.events[40].rf_on = false;
    require_no_calls(malformed, "event type and intent");
    malformed = valid;
    malformed.events.back().rf_on = true;
    malformed.events.back().type = RfEventType::RF_ON;
    require_no_calls(malformed, "does not end with RF off");
    malformed = valid;
    malformed.summary.total_duration += std::chrono::nanoseconds{1};
    require_no_calls(malformed, "duration");

    malformed = valid;
    ++malformed.summary.event_count;
    require_no_calls(malformed, "event count");
    malformed = valid;
    malformed.reference_frequency_hz = 0.0;
    require_no_calls(malformed, "invalid carrier");
    malformed.reference_frequency_hz = -1.0;
    require_no_calls(malformed, "invalid carrier");
    malformed.reference_frequency_hz =
        std::numeric_limits<double>::quiet_NaN();
    require_no_calls(malformed, "invalid carrier");
    malformed.reference_frequency_hz =
        std::numeric_limits<double>::infinity();
    require_no_calls(malformed, "invalid carrier");
    malformed = valid;
    malformed.summary.min_frequency_hz += 1.0;
    require_no_calls(malformed, "frequency summary");
    malformed = valid;
    malformed.events[3].frequency_hz =
        std::numeric_limits<double>::quiet_NaN();
    require_no_calls(malformed, "carrier intent");
    malformed.events[3].frequency_hz =
        std::numeric_limits<double>::infinity();
    require_no_calls(malformed, "carrier intent");
    malformed = valid;
    malformed.events.pop_back();
    malformed.summary.event_count = malformed.events.size();
    require_no_calls(malformed, "complete raster cells");
    malformed = valid;
    malformed.events.resize(196U);
    malformed.summary.event_count = malformed.events.size();
    require_no_calls(malformed, "leader, message, and trailer");
    malformed = valid;
    malformed.events[0].raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::MESSAGE;
    require_no_calls(malformed, "cell kind");
    malformed = valid;
    malformed.events[98].raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::LEADER;
    require_no_calls(malformed, "cell kind");
    malformed = valid;
    malformed.events[196].raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::MESSAGE;
    require_no_calls(malformed, "cell kind");
    malformed = valid;
    malformed.events[9].raster_progress->absolute_position = 10U;
    require_no_calls(malformed, "not contiguous");
    malformed = valid;
    malformed.events[9].raster_progress->cell_column = 7U;
    require_no_calls(malformed, "column is out of range");
    malformed.events[9].raster_progress->cell_column = 1U;
    require_no_calls(malformed, "column disagrees");
    malformed = valid;
    malformed.events[9].raster_progress->physical_position = 14U;
    require_no_calls(malformed, "physical position is out of range");
    malformed.events[9].raster_progress->physical_position = 8U;
    require_no_calls(malformed, "physical position disagrees");
    malformed = valid;
    malformed.events[0].raster_progress->normalized_char_index = 0;
    require_no_calls(malformed, "normalized character index");
    malformed = valid;
    malformed.events[196].message_char_index = 0;
    require_no_calls(malformed, "event and raster character indexes");
    malformed = valid;
    malformed.events[98].message_char_index = 1;
    require_no_calls(malformed, "event and raster character indexes");
    malformed = valid;
    malformed.events[17].offset_from_start += std::chrono::nanoseconds{1};
    require_no_calls(malformed, "245-position/s");
    malformed = valid;
    malformed.events[17].duration = std::chrono::nanoseconds{-1};
    require_no_calls(malformed, "245-position/s");

    std::chrono::nanoseconds overflow{};
    require(!RpiStandardFeldExecution::exact_boundary(
                std::numeric_limits<std::uint64_t>::max(), overflow),
            "unconstructible plan-size timing overflow must reject at boundary conversion");
}

void validate_complete_trace(const ExecutionPlan& plan)
{
    const std::string frozen_states =
        std::string(98U, '0') +
        "00000000000000001111111111000000001100110000000011001100000000110011000011111111110000000000000000" +
        std::string(98U, '0');
    require(frozen_states.size() == 294U,
            "independent frozen raster must contain 294 positions");

    FakeAdapter adapter;
    const auto result = RpiStandardFeldExecution::execute(plan, adapter);
    require(result.terminal == RpiStandardFeldExecutionTerminal::COMPLETED,
            "valid plan must complete");
    require(result.safe_idle_confirmed && !adapter.logical_rf_on,
            "completion must confirm terminal safe idle");
    require(result.last_completed_position == 293U &&
                !result.next_pending_position.has_value(),
            "completion identity must name final completed position");
    require(count(adapter, Operation::INITIAL_SAFE) == 1U &&
                count(adapter, Operation::TERMINAL_SHUTDOWN) == 1U,
            "execution must establish initial safety and perform terminal shutdown");
    require(count(adapter, Operation::WAIT) == 295U,
            "every physical boundary must use an absolute logical deadline");
    require(count(adapter, Operation::CARRIER) == 294U &&
                count(adapter, Operation::PROGRESS) == 294U,
            "each position must preserve carrier and completed progress");
    require(adapter.calls.size() == 14U + 1U + 294U * 5U,
            "complete operation trace must contain every operation class");
    const Operation shutdown_components[]{
        Operation::WATCHDOG_STOP, Operation::RF_ENVELOPE_OFF,
        Operation::DMA_RESET, Operation::PWM_DISABLE,
        Operation::CLOCK_DISABLE, Operation::CLOCK_VERIFY};
    require(adapter.calls[0].operation == Operation::INITIAL_SAFE,
            "trace must begin with initial safety");
    for (std::size_t i = 0; i < 6U; ++i)
        require(adapter.calls[1U + i].operation == shutdown_components[i],
                "initial safety must use the complete ordered sequence");
    require(adapter.calls[7].operation == Operation::WAIT,
            "initial safety must precede boundary zero wait");
    for (std::size_t i = 0; i < 294U; ++i)
    {
        const std::size_t base = 8U + i * 5U;
        require(adapter.calls[base].operation == Operation::CARRIER &&
                    adapter.calls[base + 1U].operation ==
                        Operation::CANCELLATION_GATE &&
                    (adapter.calls[base + 2U].operation == Operation::RF_ON ||
                     adapter.calls[base + 2U].operation == Operation::RF_OFF) &&
                    adapter.calls[base + 3U].operation == Operation::WAIT &&
                    adapter.calls[base + 4U].operation == Operation::PROGRESS,
                "each position must preserve carrier/gate/RF/wait/progress interleaving");
    }
    const std::size_t terminal = 8U + 294U * 5U;
    require(adapter.calls[terminal].operation == Operation::TERMINAL_SHUTDOWN,
            "trace must enter terminal shutdown after final progress");
    for (std::size_t i = 0; i < 6U; ++i)
        require(adapter.calls[terminal + 1U + i].operation ==
                    shutdown_components[i],
                "terminal shutdown must use the complete ordered sequence");

    std::size_t carrier_index = 0;
    std::size_t rf_index = 0;
    std::size_t progress_index = 0;
    std::size_t wait_index = 0;
    for (const auto& call : adapter.calls)
    {
        if (call.operation == Operation::WAIT)
        {
            std::chrono::nanoseconds expected{};
            require(RpiStandardFeldExecution::exact_boundary(
                        wait_index, expected) && call.deadline == expected,
                    "logical deadlines must be absolute and exact");
            ++wait_index;
        }
        else if (call.operation == Operation::CARRIER)
        {
            require(call.event_index == carrier_index &&
                        call.frequency_hz == 14'096'900.0,
                    "carrier intent must be unchanged and ordered");
            ++carrier_index;
        }
        else if (call.operation == Operation::RF_ON ||
                 call.operation == Operation::RF_OFF)
        {
            const bool expected_on = frozen_states[rf_index] == '1';
            require(call.event_index == rf_index &&
                        (call.operation == Operation::RF_ON) == expected_on,
                    "RF intent must match the independent frozen raster");
            ++rf_index;
        }
        else if (call.operation == Operation::PROGRESS)
        {
            require(call.event_index == progress_index &&
                        call.progress.has_value(),
                    "progress must be ordered and present");
            const auto& progress = *call.progress;
            const std::size_t cell = progress_index / 98U;
            require(progress.absolute_position == progress_index &&
                        progress.cell_column == (progress_index % 98U) / 14U &&
                        progress.physical_position == progress_index % 14U,
                    "progress identity must preserve position, column, and row");
            require(progress.cell_kind ==
                        (cell == 0U
                            ? RfEvent::RasterProgress::CellKind::LEADER
                            : cell == 1U
                                ? RfEvent::RasterProgress::CellKind::MESSAGE
                                : RfEvent::RasterProgress::CellKind::TRAILER) &&
                        progress.normalized_char_index ==
                            (cell == 1U ? 0 : -1),
                    "progress must preserve leader/message/trailer identity");
            ++progress_index;
        }
    }
    require(rf_index == 294U && progress_index == 294U &&
                wait_index == 295U,
            "no event, deadline, or progress update may be omitted");
}

void validate_cancellation(const ExecutionPlan& plan)
{
    const std::vector<std::size_t> boundaries{0U, 114U, 125U, 147U, 294U};
    for (const auto boundary : boundaries)
    {
        FakeAdapter adapter;
        adapter.cancel_boundary = boundary;
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::CANCELLED,
                "representative boundary cancellation must be reported");
        require(count(adapter, Operation::TERMINAL_SHUTDOWN) == 1U,
                "every cancellation must finalize exactly once");
        require(result.safe_idle_confirmed && !adapter.logical_rf_on,
                "cancellation must end in safe idle");
        require(count(adapter, Operation::PROGRESS) == boundary,
                "cancellation must not fabricate later progress");
        require(count(adapter, Operation::CARRIER) == boundary &&
                    count(adapter, Operation::RF_ON) +
                        count(adapter, Operation::RF_OFF) == boundary,
                "cancellation must not apply an event beyond its boundary");
        require(boundary == 0U
                    ? !result.last_completed_position.has_value()
                    : result.last_completed_position == boundary - 1U,
                "cancellation must report the last completed position");
        require(boundary == plan.events.size()
                    ? !result.next_pending_position.has_value()
                    : result.next_pending_position == boundary,
                "cancellation must report the next pending position");
    }
}

void validate_failures(const ExecutionPlan& plan)
{
    const std::vector<FailurePoint> failures{
        {Operation::INITIAL_SAFE, 0U},
        {Operation::CARRIER, 114U},
        {Operation::RF_ON, 0U},
        {Operation::RF_OFF, 114U},
        {Operation::WAIT, 148U},
        {Operation::PROGRESS, 147U},
        {Operation::TERMINAL_SHUTDOWN, 0U},
        {Operation::WATCHDOG_STOP, 1U},
        {Operation::RF_ENVELOPE_OFF, 1U},
        {Operation::DMA_RESET, 1U},
        {Operation::PWM_DISABLE, 1U},
        {Operation::CLOCK_DISABLE, 1U},
        {Operation::CLOCK_VERIFY, 1U}};
    for (const auto failure : failures)
    {
        FakeAdapter adapter;
        adapter.failures = {failure};
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED,
                "injected adapter failure must not report completion");
        require(result.cleanup_attempted,
                "adapter failure must attempt safe-idle cleanup");
        require(count(adapter, Operation::TERMINAL_SHUTDOWN) == 1U,
                "every adapter failure must finalize exactly once");
        require(!result.error.empty(),
                "adapter failure must have a stable diagnostic");
        if (failure.operation == Operation::TERMINAL_SHUTDOWN ||
            (static_cast<int>(failure.operation) >=
                 static_cast<int>(Operation::WATCHDOG_STOP) &&
             failure.occurrence == 1U))
        {
            require(!result.safe_idle_confirmed,
                    "terminal cleanup failure must not claim safe idle");
            require(result.error.find("Complete terminal shutdown failed") !=
                        std::string::npos,
                    "terminal cleanup failure must remain visible");
        }
        else
        {
            require(result.safe_idle_confirmed && !adapter.logical_rf_on,
                    "failure cleanup must confirm safe idle when it succeeds");
        }
    }

    FakeAdapter multiple;
    multiple.failures = {{Operation::DMA_RESET, 1U},
                         {Operation::CLOCK_VERIFY, 1U}};
    const auto multiple_result = RpiStandardFeldExecution::execute(plan, multiple);
    require(multiple_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                !multiple_result.safe_idle_confirmed &&
                multiple_result.cleanup_error.find(",") != std::string::npos,
            "multiple shutdown component failures must all remain visible");
}

void validate_cancellation_race_gates(const ExecutionPlan& plan)
{
    std::size_t rf_on = 0;
    std::size_t rf_off = 0;
    while (!plan.events[rf_on].rf_on) ++rf_on;
    while (rf_off == rf_on || plan.events[rf_off].rf_on) ++rf_off;

    for (const auto event_index : {rf_on, rf_off})
    {
        // Core boundary observation, post-carrier observation, and the
        // adapter's last-safe-point checked transition are independently
        // injectable and deterministic.
        for (int phase = 0; phase < 3; ++phase)
        {
            FakeAdapter adapter;
            adapter.cancel_boundary = event_index;
            if (phase == 0)
            {
                // Cancel at the physical-position boundary.
            }
            else
            {
                adapter.cancel_boundary.reset();
                adapter.cancel_after = phase == 1
                    ? Operation::CARRIER
                    : Operation::CANCELLATION_GATE;
                adapter.cancel_after_occurrence = event_index;
            }
            const auto result = RpiStandardFeldExecution::execute(plan, adapter);
            require(result.terminal == RpiStandardFeldExecutionTerminal::CANCELLED,
                    "each cancellation injection phase must cancel");
            const auto operation = plan.events[event_index].rf_on
                ? Operation::RF_ON : Operation::RF_OFF;
            for (const auto& call : adapter.calls)
                require(!(call.operation == operation &&
                          call.event_index == event_index),
                        "RF operation must not occur after cancellation");
            require(result.safe_idle_confirmed,
                    "cancellation race cleanup must confirm safe idle");
        }
    }
}

void validate_watchdog_precedence(const ExecutionPlan& plan)
{
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        FakeAdapter adapter;
        adapter.watchdog_detail = "stable injected watchdog diagnostic";
        if (scenario == 0)
        {
            adapter.watchdog_fault = true;
        }
        else
        {
            adapter.watchdog_after = Operation::WAIT;
            adapter.watchdog_after_occurrence = scenario == 1 ? 4U : 8U;
        }
        if (scenario == 2)
        {
            adapter.cancel_after = Operation::WAIT;
            adapter.cancel_after_occurrence = 8U;
        }
        if (scenario == 3)
            adapter.failures.push_back({Operation::TERMINAL_SHUTDOWN, 0U});
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    result.primary_error.find("stable injected watchdog") !=
                        std::string::npos,
                "watchdog fault must outrank cancellation");
        if (scenario == 3)
            require(!result.safe_idle_confirmed && !result.cleanup_error.empty(),
                    "watchdog plus cleanup failure must preserve both statuses");
    }
}

void validate_late_absolute_deadlines(const ExecutionPlan& plan)
{
    FakeAdapter adapter;
    adapter.fake_now = std::chrono::seconds{1};
    const auto result = RpiStandardFeldExecution::execute(plan, adapter);
    require(result.terminal == RpiStandardFeldExecutionTerminal::COMPLETED,
            "late fake clock must still complete");
    require(adapter.original_deadlines.size() == 295U,
            "late execution must retain every absolute deadline");
    for (std::size_t i = 0; i < adapter.original_deadlines.size(); ++i)
    {
        std::chrono::nanoseconds expected{};
        require(RpiStandardFeldExecution::exact_boundary(i, expected) &&
                    adapter.original_deadlines[i] == expected,
                "lateness must not create relative-wait drift");
    }
    require(count(adapter, Operation::RF_ON) > 0U &&
                count(adapter, Operation::RF_OFF) > 0U &&
                count(adapter, Operation::PROGRESS) == 294U,
            "late RF-on/off positions must retain gates and progress");
}

void validate_combined_failures(const ExecutionPlan& plan)
{
    const std::vector<FailurePoint> primaries{
        {Operation::CARRIER, 2U}, {Operation::RF_ON, 0U},
        {Operation::WAIT, 3U}, {Operation::PROGRESS, 2U}};
    for (const auto primary : primaries)
    {
        FakeAdapter adapter;
        adapter.failures = {primary, {Operation::TERMINAL_SHUTDOWN, 0U}};
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    !result.primary_error.empty() &&
                    !result.cleanup_error.empty() &&
                    !result.safe_idle_confirmed,
                "primary and cleanup failures must remain structured and failed");
        require(result.error.find(result.primary_error) != std::string::npos &&
                    result.error.find(result.cleanup_error) != std::string::npos,
                "combined diagnostic must expose primary and cleanup failures");
    }

    FakeAdapter exception_adapter;
    exception_adapter.throw_at = FailurePoint{Operation::CARRIER, 1U};
    const auto exception_result =
        RpiStandardFeldExecution::execute(plan, exception_adapter);
    require(exception_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                exception_result.primary_error.find("threw") != std::string::npos &&
                exception_result.safe_idle_confirmed,
            "adapter exception must still run complete terminal shutdown");

    const Operation throwing_operations[]{
        Operation::CARRIER, Operation::RF_ON, Operation::RF_OFF,
        Operation::WAIT, Operation::PROGRESS};
    for (const auto operation : throwing_operations)
    {
        FakeAdapter throwing_cleanup;
        throwing_cleanup.throw_at = FailurePoint{operation, 0U};
        throwing_cleanup.failures = {{Operation::TERMINAL_SHUTDOWN, 0U}};
        const auto outcome = RpiStandardFeldExecution::execute(plan, throwing_cleanup);
        require(outcome.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    outcome.cleanup_attempted && !outcome.safe_idle_confirmed &&
                    outcome.primary_error.find("threw") != std::string::npos &&
                    !outcome.cleanup_error.empty() &&
                    count(throwing_cleanup, Operation::TERMINAL_SHUTDOWN) == 1U,
                "every adapter exception plus cleanup failure must finalize once");
    }
}

void validate_production_progress_store(const ExecutionPlan& plan)
{
    // Sequential C1 matrix: M01..M31 labels below deliberately map one-to-one
    // to the requested lifecycle checklist.  Field-level rejection labels use
    // I01..I13 and prove rejected reports leave the snapshot untouched.
    RpiStandardFeldProgressStore store;
    constexpr std::uint64_t generation_a = 41U;
    constexpr std::uint64_t generation_b = 42U;
    const auto empty = store.lifecycle_snapshot();
    require(empty.state == RpiStandardFeldProgressState::EMPTY &&
                empty.generation == 0U && empty.completed.empty(),
            "M01 initial EMPTY snapshot");
    require(store.reset(plan, generation_a),
            "M02 setup generation A");
    require(store.lifecycle_snapshot().state == RpiStandardFeldProgressState::ACTIVE &&
                store.lifecycle_snapshot().generation == generation_a &&
                store.lifecycle_snapshot().completed.empty(),
            "M03 ACTIVE with zero completed");
    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const auto& event = plan.events[i];
        require(store.report(generation_a, i, *event.raster_progress),
                "M04 all 294 updates accepted");
    }
    const auto completed = store.snapshot();
    require(completed.size() == 294U,
            "M04 all 294 updates retain one position each");
    for (std::size_t i = 0; i < completed.size(); ++i)
        require(completed[i].event_index == i &&
                    completed[i].generation == generation_a &&
                    completed[i].raster.absolute_position == i &&
                    completed[i].plan_id.value == plan.id.value &&
                    completed[i].total_positions == plan.events.size(),
                "M04 complete immutable identity retained");
    require(store.terminal(generation_a, RpiStandardFeldProgressState::COMPLETED),
            "M05 COMPLETED only after 294");
    require(store.lifecycle_snapshot().state == RpiStandardFeldProgressState::COMPLETED,
            "M05 COMPLETED state recorded");
    require(!store.report(generation_a, 294U, *plan.events.back().raster_progress),
            "M06 report after completion rejected");
    require(!store.terminal(generation_a, RpiStandardFeldProgressState::CANCELLED),
            "M19 conflicting terminal transition rejected");
    require(store.terminal(generation_a, RpiStandardFeldProgressState::COMPLETED),
            "M18 repeated identical terminal transition is idempotent");
    store.clear();
    require(store.lifecycle_snapshot().state == RpiStandardFeldProgressState::EMPTY &&
                store.snapshot().empty(),
            "M27 explicit reset returns EMPTY");
    auto replacement = plan;
    // Reuse the plan ID deliberately: only the new generation can own it.
    replacement.id = plan.id;
    replacement.events.resize(1U);
    replacement.summary.event_count = 1U;
    replacement.events[0].raster_progress->normalized_char_index = 77;
    require(store.reset(replacement, generation_b),
            "M24 valid replacement starts with zero completed history");
    require(!store.report(generation_a, 0U, *plan.events[0].raster_progress),
            "M22/M23 stale generation-A report rejected after reused-ID generation-B replacement");
    require(store.lifecycle_snapshot().generation == generation_b &&
                store.lifecycle_snapshot().completed.empty(),
            "M24 replacement generation B has no completed history");
    require(!store.terminal(generation_a, RpiStandardFeldProgressState::FAILED),
            "M21 wrong-generation terminal transition rejected");
    require(store.terminal(generation_b, RpiStandardFeldProgressState::CANCELLED),
            "M08 cancellation after zero updates");
    require(!store.report(generation_b, 0U, *replacement.events[0].raster_progress),
            "M10 report after cancellation rejected");
    require(!store.terminal(generation_b, RpiStandardFeldProgressState::COMPLETED),
            "M16 cancellation cannot become completion");

    require(store.reset(plan, generation_a), "progress setup for cancellation-prefix case");
    require(store.report(generation_a, 0U, *plan.events[0].raster_progress),
            "progress completed prefix before cancellation");
    require(store.terminal(generation_a, RpiStandardFeldProgressState::CANCELLED) &&
                store.lifecycle_snapshot().completed.size() == 1U,
            "M09 cancellation after completed prefix");

    require(store.reset(plan, generation_a), "progress setup for failure-prefix case");
    require(store.report(generation_a, 0U, *plan.events[0].raster_progress),
            "progress completed prefix before failure");
    require(store.terminal(generation_a, RpiStandardFeldProgressState::FAILED),
            "M11 failure after completed prefix");
    require(store.lifecycle_snapshot().completed.size() == 1U &&
                !store.report(generation_a, 1U, *plan.events[1].raster_progress),
            "M12 report after failure rejected with completed prefix retained");
    require(!store.terminal(generation_a, RpiStandardFeldProgressState::COMPLETED),
            "M17 failure cannot become completion");

    require(store.reset(plan, generation_b), "progress setup for watchdog-prefix case");
    require(store.report(generation_b, 0U, *plan.events[0].raster_progress),
            "progress completed prefix before watchdog");
    require(store.terminal(generation_b, RpiStandardFeldProgressState::WATCHDOG_FAULT),
            "M13 watchdog fault after completed prefix");
    require(!store.terminal(generation_b, RpiStandardFeldProgressState::FAILED) &&
                !store.report(generation_b, 1U, *plan.events[1].raster_progress),
            "M14/M15 report after watchdog rejected and watchdog cannot be downgraded");

    require(store.reset(plan, generation_a), "progress setup for premature-completion case");
    require(!store.terminal(generation_a, RpiStandardFeldProgressState::COMPLETED),
            "M07 completion before 294 rejected");
    require(store.report(generation_a, 0U, *plan.events[0].raster_progress),
            "I01 baseline report before identity rejection checks");
    auto wrong_total = *plan.events[1].raster_progress;
    wrong_total.absolute_position = 2U;
    require(!store.report(generation_a, 1U, wrong_total),
            "I04 wrong absolute position rejected");
    require(!store.report(generation_b, 1U, *plan.events[1].raster_progress),
            "M20 wrong-generation report rejected");

    const auto before_rejections = store.lifecycle_snapshot();
    const auto same_history = [](const auto& left, const auto& right) {
        if (left.size() != right.size())
            return false;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (left[i].generation != right[i].generation ||
                left[i].plan_id.value != right[i].plan_id.value ||
                left[i].total_positions != right[i].total_positions ||
                left[i].event_index != right[i].event_index ||
                left[i].raster.absolute_position != right[i].raster.absolute_position)
                return false;
        }
        return true;
    };
    const auto reject_preserving_snapshot = [&](const char* label,
                                                std::size_t event_index,
                                                const RfEvent::RasterProgress& progress) {
        require(!store.report(generation_a, event_index, progress), label);
        const auto after = store.lifecycle_snapshot();
        require(after.state == before_rejections.state &&
                    after.generation == before_rejections.generation &&
                    after.plan_id.value == before_rejections.plan_id.value &&
                    after.total_positions == before_rejections.total_positions &&
                    same_history(after.completed, before_rejections.completed),
                std::string(label) + ": rejected report preserves snapshot");
    };
    auto identity = *plan.events[1].raster_progress;
    identity.normalized_char_index += 1;
    reject_preserving_snapshot("I06 wrong normalized character index rejected", 1U, identity);
    identity = *plan.events[1].raster_progress;
    identity.cell_kind = static_cast<RfEvent::RasterProgress::CellKind>(
        static_cast<int>(identity.cell_kind) + 1);
    reject_preserving_snapshot("I07 wrong cell kind rejected", 1U, identity);
    identity = *plan.events[1].raster_progress;
    identity.cell_column += 1U;
    reject_preserving_snapshot("I08 wrong cell column rejected", 1U, identity);
    identity = *plan.events[1].raster_progress;
    identity.physical_position += 1U;
    reject_preserving_snapshot("I09 wrong physical position rejected", 1U, identity);
    reject_preserving_snapshot("I10 duplicate update rejected", 0U, *plan.events[0].raster_progress);
    reject_preserving_snapshot("I11 skipped update rejected", 2U, *plan.events[2].raster_progress);
    reject_preserving_snapshot("I12 out-of-order update rejected", 3U, *plan.events[3].raster_progress);
    reject_preserving_snapshot("I13 update beyond total rejected", plan.events.size(), *plan.events.back().raster_progress);

    const auto before_failed_replacement = store.lifecycle_snapshot();
    require(!store.reset(plan, 0U),
            "M25 replacement preparation failure reported");
    const auto after_failed_replacement = store.lifecycle_snapshot();
    require(after_failed_replacement.state == before_failed_replacement.state &&
                after_failed_replacement.generation == before_failed_replacement.generation &&
                after_failed_replacement.plan_id.value == before_failed_replacement.plan_id.value &&
                after_failed_replacement.completed.size() == before_failed_replacement.completed.size(),
            "M25 replacement preparation failure preserves complete prior snapshot");
    auto malformed_replacement = plan;
    malformed_replacement.events[0].raster_progress.reset();
    require(!store.reset(malformed_replacement, generation_b),
            "M26 validation/setup failure reported");
    const auto after_malformed_replacement = store.lifecycle_snapshot();
    require(after_malformed_replacement.generation == before_failed_replacement.generation &&
                after_malformed_replacement.completed.size() == before_failed_replacement.completed.size(),
            "M26 validation/setup failure preserves prior snapshot");

    constexpr std::uint64_t generation_c = 43U;
    auto plan_c = replacement;
    plan_c.id.value += 1U;
    require(store.reset(plan_c, generation_c), "M29 consecutive plan C setup succeeds");
    const auto snapshot_c = store.lifecycle_snapshot();
    require(snapshot_c.generation == generation_c &&
                snapshot_c.plan_id.value == plan_c.id.value &&
                snapshot_c.completed.empty() &&
                !store.report(generation_a, 0U, *plan.events[0].raster_progress) &&
                !store.report(generation_b, 0U, *replacement.events[0].raster_progress) &&
                store.report(generation_c, 0U, *plan_c.events[0].raster_progress),
            "M29 consecutive A/B/C plans expose no stale identity or history");
    store.clear();
    require(!store.report(generation_b, 0U, *plan.events[0].raster_progress),
            "M28 report after reset rejected");
}

void validate_watchdog_diagnostic_exception(const ExecutionPlan& plan)
{
    FakeAdapter single;
    single.watchdog_fault = true;
    single.throw_watchdog_diagnostic = true;
    const auto single_result = RpiStandardFeldExecution::execute(plan, single);
    require(single_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                single_result.cleanup_attempted && single_result.safe_idle_confirmed &&
                single_result.primary_error.find("diagnostic threw") != std::string::npos &&
                single_result.cleanup_error.empty() &&
                count(single, Operation::TERMINAL_SHUTDOWN) == 1U,
            "watchdog diagnostic exception must finalize once with successful cleanup");

    FakeAdapter adapter;
    adapter.watchdog_fault = true;
    adapter.throw_watchdog_diagnostic = true;
    adapter.failures = {{Operation::TERMINAL_SHUTDOWN, 0U}};
    const auto result = RpiStandardFeldExecution::execute(plan, adapter);
    require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                result.cleanup_attempted && !result.safe_idle_confirmed &&
                result.primary_error.find("diagnostic threw") != std::string::npos &&
                !result.cleanup_error.empty(),
            "watchdog diagnostic exception plus cleanup failure must retain both diagnostics");
}

void validate_watchdog_query_exception(const ExecutionPlan& plan)
{
    FakeAdapter adapter;
    adapter.throw_watchdog_query = true;
    const auto result = RpiStandardFeldExecution::execute(plan, adapter);
    require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                result.cleanup_attempted &&
                result.primary_error.find("state query threw") != std::string::npos,
            "watchdog state exception must be normalized and finalized once");
    require(count(adapter, Operation::TERMINAL_SHUTDOWN) == 1U,
            "watchdog state exception must finalize exactly once");
}

void require_finalized_once(
    const std::string& label,
    const RpiStandardFeldExecutionResult& result,
    const FakeAdapter& adapter,
    bool safe_idle)
{
    require(result.cleanup_attempted &&
                count(adapter, Operation::TERMINAL_SHUTDOWN) == 1U,
            label + ": terminal finalization must occur exactly once");
    require(result.safe_idle_confirmed == safe_idle,
            label + ": safe-idle confirmation must match cleanup outcome");
    require(count(adapter, Operation::PROGRESS) <= 294U,
            label + ": no progress may execute after a terminal outcome");
}

void validate_finalization_matrix(const ExecutionPlan& plan)
{
    struct PrimaryCase { const char* name; Operation operation; bool exception; };
    const PrimaryCase primary_cases[]{
        {"initial safe failure", Operation::INITIAL_SAFE, false},
        {"carrier failure", Operation::CARRIER, false},
        {"RF-on failure", Operation::RF_ON, false},
        {"RF-off failure", Operation::RF_OFF, false},
        {"wait failure", Operation::WAIT, false},
        {"progress failure", Operation::PROGRESS, false},
        {"carrier exception", Operation::CARRIER, true},
        {"RF-on exception", Operation::RF_ON, true},
        {"RF-off exception", Operation::RF_OFF, true},
        {"wait exception", Operation::WAIT, true},
        {"progress exception", Operation::PROGRESS, true}};

    for (const auto& item : primary_cases)
    {
        FakeAdapter single;
        if (item.exception)
            single.throw_at = FailurePoint{item.operation, 0U};
        else
            single.failures = {{item.operation, 0U}};
        const auto single_result = RpiStandardFeldExecution::execute(plan, single);
        require(single_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    !single_result.primary_error.empty() &&
                    single_result.cleanup_error.empty(),
            std::string(item.name) + ": primary failure must remain visible");
        require_finalized_once(std::string(item.name) + " single", single_result,
                               single, true);

        FakeAdapter combined;
        if (item.exception)
            combined.throw_at = FailurePoint{item.operation, 0U};
        else
            combined.failures.push_back({item.operation, 0U});
        combined.failures.push_back({Operation::TERMINAL_SHUTDOWN, 0U});
        const auto combined_result = RpiStandardFeldExecution::execute(plan, combined);
        require(combined_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    !combined_result.primary_error.empty() &&
                    !combined_result.cleanup_error.empty(),
            std::string(item.name) + ": primary and cleanup diagnostics must remain separate");
        require_finalized_once(std::string(item.name) + " combined", combined_result,
                               combined, false);
    }

    for (const auto boundary : {0U, 147U, 294U})
    {
        FakeAdapter cancellation;
        cancellation.cancel_boundary = boundary;
        const auto outcome = RpiStandardFeldExecution::execute(plan, cancellation);
        require(outcome.terminal == RpiStandardFeldExecutionTerminal::CANCELLED &&
                    outcome.primary_error.empty() && outcome.cleanup_error.empty(),
            "cancellation matrix: cancellation must remain primary with successful cleanup");
        require_finalized_once("cancellation matrix", outcome, cancellation, true);

        FakeAdapter combined;
        combined.cancel_boundary = boundary;
        combined.failures = {{Operation::TERMINAL_SHUTDOWN, 0U}};
        const auto combined_outcome = RpiStandardFeldExecution::execute(plan, combined);
        require(combined_outcome.terminal != RpiStandardFeldExecutionTerminal::COMPLETED &&
                    !combined_outcome.cleanup_error.empty(),
            "cancellation plus cleanup failure must not report success");
        require_finalized_once("cancellation cleanup matrix", combined_outcome,
                               combined, false);
    }

    FakeAdapter completion_cleanup;
    completion_cleanup.failures = {{Operation::TERMINAL_SHUTDOWN, 0U}};
    const auto completion_outcome =
        RpiStandardFeldExecution::execute(plan, completion_cleanup);
    require(completion_outcome.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                !completion_outcome.cleanup_error.empty(),
        "completion plus cleanup failure must not survive as completion");
    require_finalized_once("completion cleanup matrix", completion_outcome,
                           completion_cleanup, false);

    const FailurePoint cleanup_exceptions[]{
        {Operation::TERMINAL_SHUTDOWN, 0U}, {Operation::CLOCK_VERIFY, 1U}};
    for (const auto point : cleanup_exceptions)
    {
        FakeAdapter adapter;
        adapter.throw_at = point;
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    !result.cleanup_error.empty(),
            "terminal shutdown and verification exceptions must fail finalization");
        require_finalized_once("cleanup exception matrix", result, adapter, false);
    }

    FakeAdapter watchdog_query_cleanup;
    watchdog_query_cleanup.throw_watchdog_query = true;
    watchdog_query_cleanup.failures = {{Operation::TERMINAL_SHUTDOWN, 0U}};
    const auto watchdog_query_outcome =
        RpiStandardFeldExecution::execute(plan, watchdog_query_cleanup);
    require(watchdog_query_outcome.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                !watchdog_query_outcome.primary_error.empty() &&
                !watchdog_query_outcome.cleanup_error.empty(),
        "watchdog query exception plus cleanup failure must retain both diagnostics");
    require_finalized_once("watchdog query cleanup matrix", watchdog_query_outcome,
                           watchdog_query_cleanup, false);
}

void validate_watchdog_outcome_matrix(const ExecutionPlan& plan)
{
    const Operation outcomes[]{Operation::INITIAL_SAFE, Operation::CARRIER,
        Operation::RF_ON, Operation::RF_OFF, Operation::WAIT,
        Operation::PROGRESS, Operation::TERMINAL_SHUTDOWN};
    for (const auto operation : outcomes)
    {
        FakeAdapter adapter;
        adapter.failures = {{operation, 0U}};
        adapter.watchdog_on_outcome = operation;
        adapter.watchdog_detail = "outcome-edge watchdog";
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    result.primary_error.find("outcome-edge watchdog") != std::string::npos,
            "watchdog outcome edge must outrank the named adapter failure");
        require_finalized_once("watchdog outcome edge", result, adapter,
                               operation != Operation::TERMINAL_SHUTDOWN);
    }

    const Operation exceptions[]{Operation::CARRIER, Operation::RF_ON, Operation::RF_OFF,
        Operation::WAIT, Operation::PROGRESS};
    for (const auto operation : exceptions)
    {
        FakeAdapter adapter;
        adapter.throw_at = FailurePoint{operation, 0U};
        adapter.watchdog_on_outcome = operation;
        adapter.watchdog_detail = "outcome-edge watchdog";
        const auto result = RpiStandardFeldExecution::execute(plan, adapter);
        require(result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                    result.primary_error.find("outcome-edge watchdog") != std::string::npos,
            "watchdog outcome edge must outrank the named adapter exception");
        require_finalized_once("watchdog exception edge", result, adapter, true);
    }

    FakeAdapter cancellation;
    cancellation.cancel_boundary = 147U;
    cancellation.watchdog_on_cancel = true;
    cancellation.watchdog_detail = "outcome-edge watchdog";
    const auto cancellation_result = RpiStandardFeldExecution::execute(plan, cancellation);
    require(cancellation_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                cancellation_result.primary_error.find("outcome-edge watchdog") != std::string::npos,
        "watchdog at cancellation edge must win");
    require_finalized_once("watchdog cancellation edge", cancellation_result,
                           cancellation, true);

    FakeAdapter completion;
    completion.watchdog_after = Operation::PROGRESS;
    completion.watchdog_after_occurrence = 293U;
    completion.watchdog_detail = "outcome-edge watchdog";
    const auto completion_result = RpiStandardFeldExecution::execute(plan, completion);
    require(completion_result.terminal == RpiStandardFeldExecutionTerminal::FAILED &&
                completion_result.primary_error.find("outcome-edge watchdog") != std::string::npos,
        "watchdog at completion boundary must win");
    require_finalized_once("watchdog completion edge", completion_result,
                           completion, true);
}
} // namespace

int main()
{
    const auto plan = single_a_plan();
    require(plan.events.size() == 294U,
            "authoritative compiler must produce 294 single-A positions");
    validate_atomic_rejections(plan);
    validate_controller_adjustment();
    validate_complete_trace(plan);
    validate_cancellation(plan);
    validate_cancellation_race_gates(plan);
    validate_failures(plan);
    validate_watchdog_precedence(plan);
    validate_late_absolute_deadlines(plan);
    validate_combined_failures(plan);
    validate_production_progress_store(plan);
    validate_watchdog_diagnostic_exception(plan);
    validate_watchdog_query_exception(plan);
    validate_finalization_matrix(plan);
    validate_watchdog_outcome_matrix(plan);
    std::cout << "PASS: Standard Feld Raspberry Pi production execution core\n";
}
