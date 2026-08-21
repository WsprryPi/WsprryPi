#include "rpi_standard_feld_dry_run.hpp"

#include <cmath>
#include <limits>

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
    constexpr std::uint64_t rate = standard_feld::kPositionsPerSecond;
    constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
    constexpr auto maximum_rep =
        std::numeric_limits<std::chrono::nanoseconds::rep>::max();
    constexpr std::uint64_t maximum =
        static_cast<std::uint64_t>(maximum_rep);

    const std::uint64_t whole_seconds = position / rate;
    const std::uint64_t remainder_positions = position % rate;
    if (whole_seconds > maximum / nanoseconds_per_second)
        return false;

    const std::uint64_t whole_nanoseconds =
        whole_seconds * nanoseconds_per_second;
    const std::uint64_t remainder_nanoseconds =
        (remainder_positions * nanoseconds_per_second + rate / 2U) / rate;
    if (remainder_nanoseconds > maximum - whole_nanoseconds)
        return false;

    result = std::chrono::nanoseconds{
        static_cast<std::chrono::nanoseconds::rep>(
            whole_nanoseconds + remainder_nanoseconds)};
    return true;
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
    if (plan.mode != TransmissionMode::STANDARD_FELD)
        return "Raspberry Pi Standard Feld dry-run requires a STANDARD_FELD plan.";
    if (plan.backend != BackendKind::RPI_CLOCK_GPIO)
        return "Standard Feld plan is not targeted for the Raspberry Pi GPIO backend.";
    if (plan.events.empty())
        return "Standard Feld plan has no events.";
    if (plan.events.size() % standard_feld::kPositionsPerCell != 0U)
        return "Standard Feld plan does not contain complete raster cells.";

    const std::size_t cell_count =
        plan.events.size() / standard_feld::kPositionsPerCell;
    if (cell_count < 3U)
        return "Standard Feld plan must contain leader, message, and trailer cells.";
    if (!std::isfinite(plan.reference_frequency_hz) ||
        plan.reference_frequency_hz <= 0.0)
        return "Standard Feld plan has an invalid carrier frequency.";
    if (plan.summary.event_count != plan.events.size())
        return "Standard Feld plan event count does not match its summary.";

    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const auto& event = plan.events[i];
        if (!event.raster_progress.has_value())
            return "Standard Feld plan event lacks raster progress identity.";

        const auto& progress = *event.raster_progress;
        if (progress.absolute_position != i)
            return "Standard Feld plan physical positions are not contiguous.";

        const std::size_t within_cell =
            i % standard_feld::kPositionsPerCell;
        const auto expected_column = static_cast<std::uint8_t>(
            within_cell / standard_feld::kPhysicalPositionsPerColumn);
        const auto expected_physical_position = static_cast<std::uint8_t>(
            within_cell % standard_feld::kPhysicalPositionsPerColumn);
        if (progress.cell_column >= standard_feld::kColumnsPerCell)
            return "Standard Feld plan raster column is out of range.";
        if (progress.physical_position >=
            standard_feld::kPhysicalPositionsPerColumn)
            return "Standard Feld plan physical position is out of range.";
        if (progress.cell_column != expected_column)
            return "Standard Feld plan raster column disagrees with its absolute position.";
        if (progress.physical_position != expected_physical_position)
            return "Standard Feld plan physical position disagrees with its absolute position.";

        const std::size_t cell = i / standard_feld::kPositionsPerCell;
        RfEvent::RasterProgress::CellKind expected_kind =
            RfEvent::RasterProgress::CellKind::MESSAGE;
        int expected_char_index = -1;
        if (cell == 0U)
            expected_kind = RfEvent::RasterProgress::CellKind::LEADER;
        else if (cell + 1U == cell_count)
            expected_kind = RfEvent::RasterProgress::CellKind::TRAILER;
        else
        {
            const std::size_t message_cell = cell - 1U;
            if (message_cell > static_cast<std::size_t>(
                    std::numeric_limits<int>::max()))
                return "Standard Feld plan message character index is not representable.";
            expected_char_index = static_cast<int>(message_cell);
        }
        if (progress.cell_kind != expected_kind)
            return "Standard Feld plan raster cell kind is inconsistent.";
        if (progress.normalized_char_index != expected_char_index)
            return "Standard Feld plan normalized character index is inconsistent.";
        if (event.message_char_index != expected_char_index)
            return "Standard Feld plan event and raster character indexes disagree.";

        std::chrono::nanoseconds start{};
        std::chrono::nanoseconds end{};
        if (!exact_boundary(i, start) || !exact_boundary(i + 1U, end))
            return "Standard Feld plan timing exceeds nanosecond representation.";
        if (event.offset_from_start != start || event.duration != end - start)
            return "Standard Feld plan does not preserve the exact 245-position/s timebase.";
        if (event.type != (event.rf_on ? RfEventType::RF_ON
                                       : RfEventType::RF_OFF))
            return "Standard Feld plan RF event type and intent disagree.";
        if (!std::isfinite(event.frequency_hz) ||
            event.frequency_hz != plan.reference_frequency_hz)
            return "Standard Feld plan event carrier intent is invalid.";
    }

    if (plan.events.back().rf_on)
        return "Standard Feld plan does not end with RF off.";
    std::chrono::nanoseconds total_duration{};
    if (!exact_boundary(plan.events.size(), total_duration))
        return "Standard Feld plan timing exceeds nanosecond representation.";
    if (plan.summary.total_duration != total_duration)
        return "Standard Feld plan duration does not match its physical positions.";
    return {};
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
