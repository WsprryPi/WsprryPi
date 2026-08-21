#include "execution_plan_compiler.hpp"
#include "rpi_standard_feld_dry_run.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
using namespace wsprrypi;
using namespace wsprrypi::testing;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

TransmissionRequest request_for(std::string message)
{
    TransmissionRequest request;
    request.id.value = 53;
    request.mode = TransmissionMode::STANDARD_FELD;
    request.output.backend = BackendKind::RPI_CLOCK_GPIO;
    StandardFeldPayload payload;
    payload.message = std::move(message);
    payload.frequency_hz = 14'096'900.0;
    request.payload = std::move(payload);
    return request;
}

std::chrono::nanoseconds boundary(std::uint64_t position)
{
    std::chrono::nanoseconds result{};
    require(RpiStandardFeldDryRunInterpreter::exact_boundary(position, result),
            "test boundary must be representable");
    return result;
}

void require_safe_idle(const RpiDryRunResult& result)
{
    require(!result.trace.empty(), "every terminal path must be observable");
    const auto& terminal = result.trace.back();
    require(terminal.kind == RpiDryRunTraceKind::SAFE_IDLE &&
                !terminal.event_index.has_value() &&
                terminal.event_type == RfEventType::RF_OFF && !terminal.rf_on,
            "every terminal path must end in unambiguous safe-idle intent");
}

void require_atomic_rejection(
    const ExecutionPlan& plan,
    const std::string& expected_detail)
{
    const auto result = RpiStandardFeldDryRunInterpreter::interpret(plan);
    require(!result.accepted &&
                result.terminal_reason == RpiDryRunTerminalReason::REJECTED &&
                result.trace.size() == 1U && result.detail == expected_detail,
            "malformed plan must be rejected atomically with stable diagnostics: " +
                expected_detail);
    require_safe_idle(result);
}

void require_cancel(const ExecutionPlan& plan, std::uint64_t position)
{
    RpiDryRunOptions options;
    options.cancel_before_position = position;
    const auto result =
        RpiStandardFeldDryRunInterpreter::interpret(plan, options);
    require(result.accepted &&
                result.terminal_reason == RpiDryRunTerminalReason::CANCELLED &&
                result.trace.size() == position + 1U,
            "legal cancellation boundary must stop deterministically");
    require_safe_idle(result);
    require(result.trace.back().offset_from_start == boundary(position),
            "cancellation safe idle must retain the exact logical boundary");
    require(result.last_completed_position ==
                (position == 0U ? std::optional<std::uint64_t>{}
                                : std::optional<std::uint64_t>{position - 1U}),
            "cancellation must report the correct last completed position");
    require(result.next_pending_position ==
                (position == plan.events.size()
                     ? std::optional<std::uint64_t>{}
                     : std::optional<std::uint64_t>{position}),
            "cancellation must report the correct next pending position");
    require(result.detail.find("latency") == std::string::npos,
            "logical cancellation must not claim physical latency");
}

void require_failure(const ExecutionPlan& plan, std::uint64_t position)
{
    RpiDryRunOptions options;
    options.fail_before_position = position;
    const auto result =
        RpiStandardFeldDryRunInterpreter::interpret(plan, options);
    require(result.accepted &&
                result.terminal_reason ==
                    RpiDryRunTerminalReason::INJECTED_FAILURE &&
                result.trace.size() == position + 1U,
            "legal failure boundary must stop deterministically");
    require_safe_idle(result);
    require(result.trace.back().offset_from_start == boundary(position),
            "failure safe idle must retain the exact logical boundary");
    require(result.last_completed_position ==
                (position == 0U ? std::optional<std::uint64_t>{}
                                : std::optional<std::uint64_t>{position - 1U}),
            "failure must report the correct last completed position");
    require(result.next_pending_position ==
                (position == plan.events.size()
                     ? std::optional<std::uint64_t>{}
                     : std::optional<std::uint64_t>{position}),
            "failure must report the correct next pending position");
}
} // namespace

int main()
{
    const ExecutionPlan plan =
        ExecutionPlanCompiler{}.compile(request_for("a"));
    const auto result = RpiStandardFeldDryRunInterpreter::interpret(plan);
    require(result.accepted &&
                result.terminal_reason == RpiDryRunTerminalReason::COMPLETED,
            "GPIO planning seam must accept an authoritative Standard Feld plan");
    require(result.trace.size() == 295U,
            "294 frozen physical positions plus terminal safe idle are required");
    require_safe_idle(result);
    require(result.trace.back().offset_from_start == std::chrono::milliseconds(1200),
            "normal completion must idle at the exact compiled duration");

    // Independent frozen expectation: one blank leader cell, the retained
    // single-A 98-position raster, and one blank trailer cell.
    const std::string glyph_a =
        "00000000000000001111111111000000001100110000000011001100000000110011000011111111110000000000000000";
    const std::string expected_bits =
        std::string(98U, '0') + glyph_a + std::string(98U, '0');
    require(expected_bits.size() == plan.events.size(),
            "independent frozen expectation must contain every position");

    for (std::size_t i = 0; i < expected_bits.size(); ++i)
    {
        const auto& traced = result.trace[i];
        const bool expected_on = expected_bits[i] == '1';
        const std::size_t cell = i / 98U;
        const auto expected_kind =
            cell == 0U
                ? RfEvent::RasterProgress::CellKind::LEADER
                : (cell == 2U
                       ? RfEvent::RasterProgress::CellKind::TRAILER
                       : RfEvent::RasterProgress::CellKind::MESSAGE);
        const int expected_char_index = cell == 1U ? 0 : -1;
        require(traced.kind == RpiDryRunTraceKind::PLAN_EVENT &&
                    traced.event_index == i,
                "event sequence identity must be stable");
        require(traced.offset_from_start == boundary(i) &&
                    traced.duration == boundary(i + 1U) - boundary(i),
                "trace must preserve exact boundary conversion");
        require(traced.rf_on == expected_on &&
                    traced.event_type ==
                        (expected_on ? RfEventType::RF_ON : RfEventType::RF_OFF),
                "trace RF intent must match the independent frozen raster");
        require(traced.frequency_hz == 14'096'900.0,
                "trace must retain carrier intent without quantization");
        require(traced.raster_progress.has_value(),
                "every traced plan event must retain progress");
        const auto& progress = *traced.raster_progress;
        require(progress.absolute_position == i &&
                    progress.cell_kind == expected_kind &&
                    progress.normalized_char_index == expected_char_index &&
                    traced.message_char_index == expected_char_index &&
                    progress.cell_column == (i % 98U) / 14U &&
                    progress.physical_position == i % 14U,
                "absolute, cell, column, position, and character identity must agree");
    }

    // Every structural progress rule receives an executable rejection case.
    ExecutionPlan malformed = plan;
    malformed.events[10].raster_progress.reset();
    require_atomic_rejection(
        malformed, "Standard Feld plan event lacks raster progress identity.");

    malformed = plan;
    malformed.events[10].raster_progress->absolute_position = 11U;
    require_atomic_rejection(
        malformed, "Standard Feld plan physical positions are not contiguous.");

    malformed = plan;
    malformed.events[10].raster_progress->cell_column = 7U;
    require_atomic_rejection(
        malformed, "Standard Feld plan raster column is out of range.");

    malformed = plan;
    malformed.events[10].raster_progress->physical_position = 14U;
    require_atomic_rejection(
        malformed, "Standard Feld plan physical position is out of range.");

    malformed = plan;
    malformed.events[14].raster_progress->cell_column = 0U;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan raster column disagrees with its absolute position.");

    malformed = plan;
    malformed.events[1].raster_progress->physical_position = 0U;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan physical position disagrees with its absolute position.");

    malformed = plan;
    malformed.events[0].raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::MESSAGE;
    require_atomic_rejection(
        malformed, "Standard Feld plan raster cell kind is inconsistent.");

    malformed = plan;
    malformed.events.back().raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::MESSAGE;
    require_atomic_rejection(
        malformed, "Standard Feld plan raster cell kind is inconsistent.");

    malformed = plan;
    malformed.events[98].raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::LEADER;
    require_atomic_rejection(
        malformed, "Standard Feld plan raster cell kind is inconsistent.");

    malformed = plan;
    malformed.events[0].raster_progress->normalized_char_index = 0;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan normalized character index is inconsistent.");

    malformed = plan;
    malformed.events.back().raster_progress->normalized_char_index = 0;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan normalized character index is inconsistent.");

    const ExecutionPlan two_message_cells =
        ExecutionPlanCompiler{}.compile(request_for("AB"));
    malformed = two_message_cells;
    malformed.events[196].raster_progress->normalized_char_index = 0;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan normalized character index is inconsistent.");
    malformed = two_message_cells;
    malformed.events[196].raster_progress->normalized_char_index = 2;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan normalized character index is inconsistent.");

    malformed = plan;
    malformed.events[98].message_char_index = 1;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan event and raster character indexes disagree.");

    malformed = plan;
    malformed.events.pop_back();
    require_atomic_rejection(
        malformed, "Standard Feld plan does not contain complete raster cells.");

    malformed = plan;
    malformed.events[0].raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::MESSAGE;
    require_atomic_rejection(
        malformed, "Standard Feld plan raster cell kind is inconsistent.");
    malformed = plan;
    malformed.events.back().raster_progress->cell_kind =
        RfEvent::RasterProgress::CellKind::MESSAGE;
    require_atomic_rejection(
        malformed, "Standard Feld plan raster cell kind is inconsistent.");

    malformed = plan;
    malformed.events.resize(196U);
    malformed.summary.event_count = malformed.events.size();
    malformed.summary.total_duration = boundary(malformed.events.size());
    require_atomic_rejection(
        malformed,
        "Standard Feld plan must contain leader, message, and trailer cells.");

    malformed = plan;
    malformed.events[10].offset_from_start += std::chrono::nanoseconds{1};
    require_atomic_rejection(
        malformed,
        "Standard Feld plan does not preserve the exact 245-position/s timebase.");
    malformed = plan;
    malformed.events[1].offset_from_start = malformed.events[0].offset_from_start;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan does not preserve the exact 245-position/s timebase.");

    for (const auto mode : {TransmissionMode::WSPR, TransmissionMode::QRSS,
                            TransmissionMode::FSKCW, TransmissionMode::DFCW,
                            TransmissionMode::TONE})
    {
        malformed = plan;
        malformed.mode = mode;
        const auto rejected = RpiStandardFeldDryRunInterpreter::interpret(malformed);
        require(!rejected.accepted && rejected.trace.size() == 1U,
                "focused seam must reject every other production mode");
        require_safe_idle(rejected);
    }
    malformed = plan;
    malformed.backend = BackendKind::SI5351;
    require_atomic_rejection(
        malformed,
        "Standard Feld plan is not targeted for the Raspberry Pi GPIO backend.");

    require_cancel(plan, 0U);
    require_cancel(plan, plan.events.size() / 2U);
    require_cancel(plan, plan.events.size());
    require_failure(plan, 0U);
    require_failure(plan, plan.events.size() / 2U);
    require_failure(plan, plan.events.size());

    RpiDryRunOptions invalid_options;
    invalid_options.cancel_before_position = plan.events.size() + 1U;
    auto rejected_options =
        RpiStandardFeldDryRunInterpreter::interpret(plan, invalid_options);
    require(!rejected_options.accepted && rejected_options.trace.size() == 1U &&
                rejected_options.detail ==
                    "Dry-run cancellation position is outside the plan boundaries.",
            "out-of-range cancellation must be rejected rather than complete");
    require_safe_idle(rejected_options);

    invalid_options = {};
    invalid_options.fail_before_position = plan.events.size() + 1U;
    rejected_options =
        RpiStandardFeldDryRunInterpreter::interpret(plan, invalid_options);
    require(!rejected_options.accepted && rejected_options.trace.size() == 1U &&
                rejected_options.detail ==
                    "Dry-run failure position is outside the plan boundaries.",
            "out-of-range failure must be rejected rather than complete");
    require_safe_idle(rejected_options);

    invalid_options.cancel_before_position = 0U;
    invalid_options.fail_before_position = 0U;
    rejected_options =
        RpiStandardFeldDryRunInterpreter::interpret(plan, invalid_options);
    require(!rejected_options.accepted && rejected_options.trace.size() == 1U &&
                rejected_options.detail ==
                    "Dry-run cancellation and injected failure cannot both be requested.",
            "conflicting terminal controls must be rejected deterministically");
    require_safe_idle(rejected_options);

    const std::uint64_t maximum_position =
        RpiStandardFeldDryRunInterpreter::maximum_representable_position();
    std::chrono::nanoseconds maximum_boundary{};
    require(RpiStandardFeldDryRunInterpreter::exact_boundary(
                maximum_position, maximum_boundary) &&
                maximum_boundary.count() <=
                    std::numeric_limits<std::chrono::nanoseconds::rep>::max(),
            "largest representable position boundary must be accepted");
    std::chrono::nanoseconds overflow_boundary{};
    require(!RpiStandardFeldDryRunInterpreter::exact_boundary(
                maximum_position + 1U, overflow_boundary) &&
                !RpiStandardFeldDryRunInterpreter::exact_boundary(
                    std::numeric_limits<std::uint64_t>::max(), overflow_boundary),
            "unrepresentable position boundaries must be rejected without overflow");

    std::cout << "PASS: Standard Feld GPIO non-transmitting dry-run trace\n";
}
