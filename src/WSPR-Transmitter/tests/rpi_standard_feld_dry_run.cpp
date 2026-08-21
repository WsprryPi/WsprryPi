#include "rpi_standard_feld_dry_run.hpp"

#include <limits>
#include <utility>

#include "rpi_standard_feld_execution.hpp"
#include "standard_feld_asset.hpp"

namespace wsprrypi::testing
{
namespace
{
RpiDryRunTraceEvent safe_idle(std::chrono::nanoseconds offset)
{
    RpiDryRunTraceEvent event;
    event.kind = RpiDryRunTraceKind::SAFE_IDLE;
    event.offset_from_start = offset;
    event.event_type = RfEventType::RF_OFF;
    event.rf_on = false;
    return event;
}

RpiDryRunResult rejection(std::string detail)
{
    RpiDryRunResult result;
    result.detail = std::move(detail);
    result.trace.push_back(safe_idle(std::chrono::nanoseconds::zero()));
    return result;
}
} // namespace

bool RpiStandardFeldDryRunInterpreter::exact_boundary(
    std::uint64_t position,
    std::chrono::nanoseconds& result) noexcept
{
    return RpiStandardFeldExecution::exact_boundary(position, result);
}

std::uint64_t
RpiStandardFeldDryRunInterpreter::maximum_representable_position() noexcept
{
    constexpr std::uint64_t rate = standard_feld::kPositionsPerSecond;
    constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
    constexpr std::uint64_t maximum = static_cast<std::uint64_t>(
        std::numeric_limits<std::chrono::nanoseconds::rep>::max());
    const std::uint64_t whole_seconds = maximum / nanoseconds_per_second;
    const std::uint64_t remaining_nanoseconds = maximum % nanoseconds_per_second;
    std::uint64_t remainder_positions = 0;
    for (std::uint64_t candidate = 1; candidate < rate; ++candidate)
    {
        const std::uint64_t rounded =
            (candidate * nanoseconds_per_second + rate / 2U) / rate;
        if (rounded > remaining_nanoseconds)
            break;
        remainder_positions = candidate;
    }
    return whole_seconds * rate + remainder_positions;
}

std::string RpiStandardFeldDryRunInterpreter::validate_plan(
    const ExecutionPlan& plan)
{
    const auto validation = RpiStandardFeldExecution::validate(plan);
    return validation.error;
}

std::string RpiStandardFeldDryRunInterpreter::validate_options(
    const RpiDryRunOptions& options,
    std::uint64_t event_count)
{
    if (options.cancel_before_position.has_value() &&
        options.fail_before_position.has_value())
        return "Dry-run cancellation and injected failure cannot both be requested.";
    if (options.cancel_before_position > event_count)
        return "Dry-run cancellation position is outside the plan boundaries.";
    if (options.fail_before_position > event_count)
        return "Dry-run failure position is outside the plan boundaries.";
    return {};
}

RpiDryRunResult RpiStandardFeldDryRunInterpreter::interpret(
    const ExecutionPlan& plan,
    const RpiDryRunOptions& options)
{
    const std::string plan_error = validate_plan(plan);
    if (!plan_error.empty())
        return rejection(plan_error);

    const std::uint64_t event_count = plan.events.size();
    const std::string option_error = validate_options(options, event_count);
    if (!option_error.empty())
        return rejection(option_error);

    RpiDryRunResult result;
    result.accepted = true;
    result.trace.reserve(plan.events.size() + 1U);

    for (std::uint64_t position = 0; position < event_count; ++position)
    {
        if (options.cancel_before_position == position)
        {
            result.terminal_reason = RpiDryRunTerminalReason::CANCELLED;
            result.next_pending_position = position;
            result.detail = "Cancellation requested before physical position " +
                std::to_string(position) + ".";
            result.trace.push_back(safe_idle(plan.events[position].offset_from_start));
            return result;
        }
        if (options.fail_before_position == position)
        {
            result.terminal_reason = RpiDryRunTerminalReason::INJECTED_FAILURE;
            result.next_pending_position = position;
            result.detail = "Injected interpretation failure before physical position " +
                std::to_string(position) + ".";
            result.trace.push_back(safe_idle(plan.events[position].offset_from_start));
            return result;
        }

        const auto& source = plan.events[static_cast<std::size_t>(position)];
        RpiDryRunTraceEvent traced;
        traced.kind = RpiDryRunTraceKind::PLAN_EVENT;
        traced.event_index = static_cast<std::size_t>(position);
        traced.offset_from_start = source.offset_from_start;
        traced.duration = source.duration;
        traced.event_type = source.type;
        traced.rf_on = source.rf_on;
        traced.frequency_hz = source.frequency_hz;
        traced.message_char_index = source.message_char_index;
        traced.raster_progress = source.raster_progress;
        result.trace.push_back(traced);
        result.last_completed_position = position;
    }

    if (options.cancel_before_position == event_count)
    {
        result.terminal_reason = RpiDryRunTerminalReason::CANCELLED;
        result.detail = "Cancellation requested at the final physical-position boundary.";
        result.trace.push_back(safe_idle(plan.summary.total_duration));
        return result;
    }
    if (options.fail_before_position == event_count)
    {
        result.terminal_reason = RpiDryRunTerminalReason::INJECTED_FAILURE;
        result.detail = "Injected interpretation failure at the final physical-position boundary.";
        result.trace.push_back(safe_idle(plan.summary.total_duration));
        return result;
    }

    result.terminal_reason = RpiDryRunTerminalReason::COMPLETED;
    result.detail = "Standard Feld dry-run interpretation completed.";
    result.trace.push_back(safe_idle(plan.summary.total_duration));
    return result;
}

} // namespace wsprrypi::testing
