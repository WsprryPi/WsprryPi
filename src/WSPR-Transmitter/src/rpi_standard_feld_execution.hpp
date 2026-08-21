#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <mutex>
#include <vector>

#include "execution_plan.hpp"

namespace wsprrypi
{

struct RpiStandardFeldPlanValidation
{
    bool ok{false};
    std::string error;
};

enum class RpiStandardFeldAdapterStatus
{
    OK,
    CANCELLED,
    FAILED
};

struct RpiStandardFeldAdapterResult
{
    RpiStandardFeldAdapterStatus status{RpiStandardFeldAdapterStatus::OK};
    std::string error;

    static RpiStandardFeldAdapterResult success();
    static RpiStandardFeldAdapterResult cancelled();
    static RpiStandardFeldAdapterResult failure(std::string detail);
};

class IRpiStandardFeldExecutionAdapter
{
public:
    virtual ~IRpiStandardFeldExecutionAdapter() = default;

    virtual bool cancellation_requested() const = 0;
    virtual bool watchdog_faulted() const = 0;
    virtual std::string watchdog_diagnostic() const = 0;
    virtual RpiStandardFeldAdapterResult establish_initial_safe_state() = 0;
    virtual RpiStandardFeldAdapterResult wait_until(
        std::chrono::nanoseconds absolute_deadline) = 0;
    virtual RpiStandardFeldAdapterResult apply_carrier(
        double frequency_hz,
        std::size_t event_index) = 0;
    // This is the final cancellation classification point before an RF
    // transition.  Implementations must not enable RF after returning OK from
    // a stale, earlier cancellation observation: the stop classification and
    // enable action belong to this single checked transition operation.
    virtual RpiStandardFeldAdapterResult apply_rf_checked(
        const RfEvent& event,
        std::size_t event_index) = 0;
    virtual RpiStandardFeldAdapterResult report_progress(
        std::size_t event_index,
        const RfEvent::RasterProgress& progress) = 0;
    virtual RpiStandardFeldAdapterResult complete_terminal_shutdown() = 0;
};

enum class RpiStandardFeldExecutionTerminal
{
    COMPLETED,
    CANCELLED,
    FAILED,
    REJECTED
};

struct RpiStandardFeldExecutionResult
{
    RpiStandardFeldExecutionTerminal terminal{
        RpiStandardFeldExecutionTerminal::REJECTED};
    bool cleanup_attempted{false};
    bool safe_idle_confirmed{false};
    bool watchdog_faulted{false};
    std::optional<std::size_t> last_completed_position{};
    std::optional<std::size_t> next_pending_position{};
    std::string primary_error;
    std::string cleanup_error;
    std::string error;
};

// Internal-only completed-position history.  This is intentionally a small
// production component: the Raspberry Pi bridge and its tests share the same
// identity, bounds, ordering, and locking rules without exposing progress to
// any operator protocol.
struct RpiStandardFeldCompletedProgress
{
    std::uint64_t generation{0};
    PlanId plan_id{};
    std::size_t total_positions{0};
    std::size_t event_index{0};
    int message_char_index{-1};
    RfEvent::RasterProgress raster{};
};

enum class RpiStandardFeldProgressState
{
    EMPTY, ACTIVE, COMPLETED, CANCELLED, FAILED, WATCHDOG_FAULT
};

#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
enum class RpiStandardFeldProgressTestFault
{
    EXPECTED_RESERVE, COMPLETED_RESERVE, EXPECTED_COPY, BEFORE_INSTALL,
    REPORT_APPEND, SNAPSHOT_COPY
};
enum class ProgressTestOperation { REPORT, CLEAR, SNAPSHOT, TERMINAL, PREPARE_INSTALL };
enum class ProgressTestPhase { AFTER_LOCK_BEFORE_MUTATION, AFTER_MUTATION_BEFORE_UNLOCK };
void arm_rpi_standard_feld_progress_pause(ProgressTestOperation, ProgressTestPhase);
bool wait_rpi_standard_feld_progress_pause();
void release_rpi_standard_feld_progress_pause() noexcept;
void set_rpi_standard_feld_progress_test_fault(
    RpiStandardFeldProgressTestFault fault, std::size_t after = 0U);
void clear_rpi_standard_feld_progress_test_fault() noexcept;
#endif

struct RpiStandardFeldProgressSnapshot
{
    RpiStandardFeldProgressState state{RpiStandardFeldProgressState::EMPTY};
    std::uint64_t generation{0};
    PlanId plan_id{};
    std::size_t total_positions{0};
    std::vector<RpiStandardFeldCompletedProgress> completed{};
};

class RpiStandardFeldProgressStore final
{
public:
    // A replacement request clears the old, uncommitted history.  A failed
    // replacement therefore leaves no stale completed history to leak into a
    // later plan; this matches the parent committed-request replacement rule.
    bool reset(const ExecutionPlan& plan, std::uint64_t generation);
    void clear();
    bool report(std::uint64_t generation, std::size_t event_index,
                const RfEvent::RasterProgress& progress);
    std::vector<RpiStandardFeldCompletedProgress> snapshot() const;
    RpiStandardFeldProgressSnapshot lifecycle_snapshot() const;
    bool terminal(std::uint64_t generation, RpiStandardFeldProgressState state);
#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
    std::size_t completed_capacity_for_test() const;
#endif

private:
    mutable std::mutex mutex_{};
    PlanId plan_id_{};
    std::size_t total_positions_{0};
    bool active_{false};
    std::uint64_t generation_{0};
    RpiStandardFeldProgressState state_{RpiStandardFeldProgressState::EMPTY};
    struct ExpectedProgress
    {
        int message_char_index{-1};
        RfEvent::RasterProgress raster{};
    };
    std::vector<ExpectedProgress> expected_{};
    std::vector<RpiStandardFeldCompletedProgress> completed_{};
};

class RpiStandardFeldExecution final
{
public:
    static RpiStandardFeldPlanValidation validate(
        const ExecutionPlan& plan);

    static RpiStandardFeldExecutionResult execute(
        const ExecutionPlan& plan,
        IRpiStandardFeldExecutionAdapter& adapter);

    static bool exact_boundary(
        std::uint64_t position,
        std::chrono::nanoseconds& result) noexcept;
};

} // namespace wsprrypi
