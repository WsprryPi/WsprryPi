#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "execution_plan.hpp"

namespace wsprrypi::testing
{

enum class RpiDryRunTraceKind
{
    PLAN_EVENT,
    SAFE_IDLE
};

enum class RpiDryRunTerminalReason
{
    COMPLETED,
    CANCELLED,
    INJECTED_FAILURE,
    REJECTED
};

struct RpiDryRunTraceEvent
{
    RpiDryRunTraceKind kind{RpiDryRunTraceKind::PLAN_EVENT};
    std::optional<std::size_t> event_index{};
    std::chrono::nanoseconds offset_from_start{};
    std::chrono::nanoseconds duration{};
    RfEventType event_type{RfEventType::RF_OFF};
    bool rf_on{false};
    double frequency_hz{0.0};
    int message_char_index{-1};
    std::optional<RfEvent::RasterProgress> raster_progress{};
};

struct RpiDryRunOptions
{
    std::optional<std::uint64_t> cancel_before_position{};
    std::optional<std::uint64_t> fail_before_position{};
};

struct RpiDryRunResult
{
    bool accepted{false};
    RpiDryRunTerminalReason terminal_reason{RpiDryRunTerminalReason::REJECTED};
    std::vector<RpiDryRunTraceEvent> trace{};
    std::optional<std::uint64_t> last_completed_position{};
    std::optional<std::uint64_t> next_pending_position{};
    std::string detail;
};

class RpiStandardFeldDryRunInterpreter final
{
public:
    static RpiDryRunResult interpret(
        const ExecutionPlan& plan,
        const RpiDryRunOptions& options = {});

    static bool exact_boundary(
        std::uint64_t position,
        std::chrono::nanoseconds& result) noexcept;
    static std::uint64_t maximum_representable_position() noexcept;

private:
    static std::string validate_plan(const ExecutionPlan& plan);
    static std::string validate_options(
        const RpiDryRunOptions& options,
        std::uint64_t event_count);
};

} // namespace wsprrypi::testing
