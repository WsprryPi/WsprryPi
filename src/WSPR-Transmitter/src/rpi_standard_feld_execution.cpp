#include "rpi_standard_feld_execution.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <utility>
#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
#include <condition_variable>
#endif

#include "standard_feld_asset.hpp"

namespace wsprrypi
{
namespace
{
#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
thread_local std::optional<RpiStandardFeldProgressTestFault> progress_test_fault;
thread_local std::size_t progress_test_after{0U};
std::mutex progress_pause_mutex;
std::condition_variable progress_pause_cv;
std::optional<std::pair<ProgressTestOperation, ProgressTestPhase>> progress_pause;
bool progress_paused{false}; bool progress_released{false};
void progress_test_pause(const ProgressTestOperation op, const ProgressTestPhase phase)
{
    std::unique_lock<std::mutex> lock{progress_pause_mutex};
    if (!progress_pause || *progress_pause != std::make_pair(op, phase)) return;
    progress_paused = true; progress_pause_cv.notify_all();
    progress_pause_cv.wait(lock, [] { return progress_released; });
    progress_pause.reset(); progress_paused = false; progress_released = false;
}
void progress_test_inject(const RpiStandardFeldProgressTestFault fault)
{
    if (progress_test_fault == fault && progress_test_after-- == 0U)
    {
        progress_test_fault.reset();
        throw std::bad_alloc{};
    }
}
#define WSPRRYPI_PROGRESS_TEST_PAUSE(operation, phase) \
    progress_test_pause(operation, phase)
#define WSPRRYPI_PROGRESS_TEST_INJECT(fault) progress_test_inject(fault)
#else
#define WSPRRYPI_PROGRESS_TEST_PAUSE(operation, phase) do { } while (false)
#define WSPRRYPI_PROGRESS_TEST_INJECT(fault) do { } while (false)
#endif
using AdapterCall = RpiStandardFeldAdapterResult;

template<typename Function>
AdapterCall call_adapter(const char* operation, Function&& function)
{
    try
    {
        AdapterCall result = function();
        if (result.status == RpiStandardFeldAdapterStatus::FAILED &&
            result.error.empty())
            result.error = std::string(operation) + " failed.";
        return result;
    }
    catch (const std::exception& exception)
    {
        return AdapterCall::failure(
            std::string(operation) + " threw: " + exception.what());
    }
    catch (...)
    {
        return AdapterCall::failure(
            std::string(operation) + " threw an unknown exception.");
    }
}

void compose_error(RpiStandardFeldExecutionResult& result)
{
    result.error = result.primary_error;
    if (!result.cleanup_error.empty())
    {
        if (!result.error.empty())
            result.error += " ";
        result.error += "Complete terminal shutdown failed: " +
            result.cleanup_error;
    }
}

bool watchdog_wins(
    IRpiStandardFeldExecutionAdapter& adapter,
    std::string& diagnostic);

void finalize_noexcept(
    IRpiStandardFeldExecutionAdapter& adapter,
    RpiStandardFeldExecutionResult& result) noexcept
{
    try
    {
        result.cleanup_attempted = true;
        const auto cleanup_result = call_adapter(
            "complete terminal shutdown",
            [&adapter]() { return adapter.complete_terminal_shutdown(); });
        result.safe_idle_confirmed =
            cleanup_result.status == RpiStandardFeldAdapterStatus::OK;
        if (!result.safe_idle_confirmed)
            result.cleanup_error = cleanup_result.error.empty()
                    ? "adapter did not confirm safe idle."
                    : cleanup_result.error;
    }
    catch (const std::exception& exception)
    {
        result.cleanup_attempted = true;
        result.safe_idle_confirmed = false;
        try { result.cleanup_error = std::string("terminal finalization threw: ") + exception.what(); }
        catch (...) { try { result.cleanup_error = "terminal finalization threw."; } catch (...) {} }
    }
    catch (...)
    {
        result.cleanup_attempted = true;
        result.safe_idle_confirmed = false;
        try { result.cleanup_error = "terminal finalization threw an unknown exception."; }
        catch (...) {}
    }
}

void classify_terminal_result(
    IRpiStandardFeldExecutionAdapter& adapter,
    RpiStandardFeldExecutionResult& result) noexcept
{
    try
    {
        std::string watchdog_detail;
        if (watchdog_wins(adapter, watchdog_detail))
        {
            result.terminal = RpiStandardFeldExecutionTerminal::FAILED;
            result.watchdog_faulted = true;
            result.primary_error = std::move(watchdog_detail);
        }
        if (!result.cleanup_error.empty())
            result.safe_idle_confirmed = false;
        if (result.terminal == RpiStandardFeldExecutionTerminal::COMPLETED &&
            !result.safe_idle_confirmed)
            result.terminal = RpiStandardFeldExecutionTerminal::FAILED;
        compose_error(result);
    }
    catch (...)
    {
        result.terminal = RpiStandardFeldExecutionTerminal::FAILED;
        result.safe_idle_confirmed = false;
        try { result.primary_error = "terminal result classification failed."; }
        catch (...) {}
    }
}

RpiStandardFeldExecutionResult failure(
    IRpiStandardFeldExecutionAdapter& adapter,
    std::string detail,
    std::optional<std::size_t> last_completed,
    std::optional<std::size_t> next_pending)
{
    RpiStandardFeldExecutionResult result;
    result.terminal = RpiStandardFeldExecutionTerminal::FAILED;
    result.last_completed_position = last_completed;
    result.next_pending_position = next_pending;
    result.primary_error = std::move(detail);
    return result;
}

bool watchdog_wins(
    IRpiStandardFeldExecutionAdapter& adapter,
    std::string& diagnostic)
{
    try
    {
        if (!adapter.watchdog_faulted())
            return false;
    }
    catch (const std::exception& exception)
    {
        diagnostic = std::string("Standard Feld watchdog state query threw: ") +
            exception.what();
        return true;
    }
    catch (...)
    {
        diagnostic = "Standard Feld watchdog state query threw an unknown exception.";
        return true;
    }
    try
    {
        diagnostic = adapter.watchdog_diagnostic();
    }
    catch (const std::exception& exception)
    {
        diagnostic = std::string("Standard Feld watchdog diagnostic threw: ") +
            exception.what();
    }
    catch (...)
    {
        diagnostic = "Standard Feld watchdog diagnostic threw an unknown exception.";
    }
    if (diagnostic.empty())
        diagnostic = "Standard Feld watchdog fault latched.";
    return true;
}

RpiStandardFeldExecutionResult cancelled(
    IRpiStandardFeldExecutionAdapter& adapter,
    std::optional<std::size_t> last_completed,
    std::optional<std::size_t> next_pending);

RpiStandardFeldExecutionResult stopped_or_faulted(
    IRpiStandardFeldExecutionAdapter& adapter,
    std::optional<std::size_t> last_completed,
    std::optional<std::size_t> next_pending)
{
    std::string diagnostic;
    if (watchdog_wins(adapter, diagnostic))
        return failure(adapter, std::move(diagnostic), last_completed, next_pending);
    return cancelled(adapter, last_completed, next_pending);
}

RpiStandardFeldExecutionResult cancelled(
    IRpiStandardFeldExecutionAdapter& adapter,
    std::optional<std::size_t> last_completed,
    std::optional<std::size_t> next_pending)
{
    RpiStandardFeldExecutionResult result;
    result.terminal = RpiStandardFeldExecutionTerminal::CANCELLED;
    result.last_completed_position = last_completed;
    result.next_pending_position = next_pending;
    return result;
}

std::string adapter_failure(
    const char* operation,
    std::size_t boundary,
    const AdapterCall& result)
{
    std::string detail = std::string(operation) + " failed at boundary " +
        std::to_string(boundary) + ".";
    if (!result.error.empty())
        detail += " " + result.error;
    return detail;
}
} // namespace

#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
void set_rpi_standard_feld_progress_test_fault(
    const RpiStandardFeldProgressTestFault fault, const std::size_t after)
{ progress_test_fault = fault; progress_test_after = after; }
void clear_rpi_standard_feld_progress_test_fault() noexcept
{ progress_test_fault.reset(); progress_test_after = 0U; }
void arm_rpi_standard_feld_progress_pause(ProgressTestOperation op, ProgressTestPhase phase)
{ std::lock_guard<std::mutex> lock{progress_pause_mutex}; progress_pause = {op, phase}; progress_paused=false; progress_released=false; }
bool wait_rpi_standard_feld_progress_pause()
{ std::unique_lock<std::mutex> lock{progress_pause_mutex}; return progress_pause_cv.wait_for(lock, std::chrono::seconds{2}, [] { return progress_paused; }); }
void release_rpi_standard_feld_progress_pause() noexcept
{ std::lock_guard<std::mutex> lock{progress_pause_mutex}; progress_released=true; progress_pause_cv.notify_all(); }
#endif

RpiStandardFeldAdapterResult RpiStandardFeldAdapterResult::success()
{
    return {};
}

RpiStandardFeldAdapterResult RpiStandardFeldAdapterResult::cancelled()
{
    return {RpiStandardFeldAdapterStatus::CANCELLED, {}};
}

RpiStandardFeldAdapterResult RpiStandardFeldAdapterResult::failure(
    std::string detail)
{
    return {RpiStandardFeldAdapterStatus::FAILED, std::move(detail)};
}

bool RpiStandardFeldProgressStore::reset(
    const ExecutionPlan& plan, const std::uint64_t generation)
{
    if (generation == 0U || plan.events.empty())
        return false;
    try
    {
        std::vector<RpiStandardFeldCompletedProgress> replacement;
        std::vector<ExpectedProgress> expected;
        WSPRRYPI_PROGRESS_TEST_INJECT(
            RpiStandardFeldProgressTestFault::COMPLETED_RESERVE);
        replacement.reserve(plan.events.size());
        WSPRRYPI_PROGRESS_TEST_INJECT(
            RpiStandardFeldProgressTestFault::EXPECTED_RESERVE);
        expected.reserve(plan.events.size());
        for (const auto& event : plan.events)
        {
            if (!event.raster_progress.has_value())
                return false;
            WSPRRYPI_PROGRESS_TEST_INJECT(
                RpiStandardFeldProgressTestFault::EXPECTED_COPY);
            expected.push_back({event.message_char_index, *event.raster_progress});
        }
        WSPRRYPI_PROGRESS_TEST_INJECT(
            RpiStandardFeldProgressTestFault::BEFORE_INSTALL);
        std::lock_guard<std::mutex> lock{mutex_};
        WSPRRYPI_PROGRESS_TEST_PAUSE(
            ProgressTestOperation::PREPARE_INSTALL,
            ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
        plan_id_ = plan.id;
        total_positions_ = plan.events.size();
        active_ = true;
        generation_ = generation;
        state_ = RpiStandardFeldProgressState::ACTIVE;
        expected_.swap(expected);
        completed_.swap(replacement);
        WSPRRYPI_PROGRESS_TEST_PAUSE(
            ProgressTestOperation::PREPARE_INSTALL,
            ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK);
        return true;
    }
    catch (...)
    {
        // Construct the replacement before taking the lock: failed setup
        // leaves the previously committed identity and history unchanged.
        return false;
    }
}

void RpiStandardFeldProgressStore::clear()
{
    std::lock_guard<std::mutex> lock{mutex_};
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::CLEAR,
        ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
    plan_id_ = {};
    total_positions_ = 0;
    active_ = false;
    generation_ = 0;
    state_ = RpiStandardFeldProgressState::EMPTY;
    expected_.clear();
    completed_.clear();
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::CLEAR,
        ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK);
}

bool RpiStandardFeldProgressStore::report(
    const std::uint64_t generation,
    std::size_t event_index, const RfEvent::RasterProgress& progress)
{
    std::lock_guard<std::mutex> lock{mutex_};
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::REPORT,
        ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
    if (!active_ || state_ != RpiStandardFeldProgressState::ACTIVE ||
        generation == 0U || generation != generation_ ||
        event_index != completed_.size() ||
        event_index >= total_positions_ || progress.absolute_position != event_index ||
        expected_.size() != total_positions_ ||
        completed_.capacity() < total_positions_)
        return false;
    const auto& expected = expected_[event_index];
    if (expected.raster.absolute_position != progress.absolute_position ||
        expected.raster.cell_kind != progress.cell_kind ||
        expected.raster.normalized_char_index != progress.normalized_char_index ||
        expected.raster.cell_column != progress.cell_column ||
        expected.raster.physical_position != progress.physical_position)
        return false;
    try
    {
        WSPRRYPI_PROGRESS_TEST_INJECT(
            RpiStandardFeldProgressTestFault::REPORT_APPEND);
        completed_.push_back({generation_, plan_id_, total_positions_, event_index,
                              expected.message_char_index, progress});
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::vector<RpiStandardFeldCompletedProgress>
RpiStandardFeldProgressStore::snapshot() const
{
    std::lock_guard<std::mutex> lock{mutex_};
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::SNAPSHOT,
        ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
    WSPRRYPI_PROGRESS_TEST_INJECT(
        RpiStandardFeldProgressTestFault::SNAPSHOT_COPY);
    auto copy = completed_;
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::SNAPSHOT,
        ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK);
    return copy;
}

RpiStandardFeldProgressSnapshot
RpiStandardFeldProgressStore::lifecycle_snapshot() const
{
    std::lock_guard<std::mutex> lock{mutex_};
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::SNAPSHOT,
        ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
    WSPRRYPI_PROGRESS_TEST_INJECT(
        RpiStandardFeldProgressTestFault::SNAPSHOT_COPY);
    auto copy = RpiStandardFeldProgressSnapshot{state_, generation_, plan_id_, total_positions_, completed_};
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::SNAPSHOT,
        ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK);
    return copy;
}

bool RpiStandardFeldProgressStore::terminal(
    const std::uint64_t generation, const RpiStandardFeldProgressState state)
{
    if (state != RpiStandardFeldProgressState::COMPLETED &&
        state != RpiStandardFeldProgressState::CANCELLED &&
        state != RpiStandardFeldProgressState::FAILED &&
        state != RpiStandardFeldProgressState::WATCHDOG_FAULT)
        return false;

    std::lock_guard<std::mutex> lock{mutex_};
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::TERMINAL,
        ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
    if (!active_ || generation == 0U || generation != generation_)
        return false;
    if (state_ == state)
        return true;
    if (state_ != RpiStandardFeldProgressState::ACTIVE)
        return false;
    if (state == RpiStandardFeldProgressState::COMPLETED &&
        completed_.size() != total_positions_)
        return false;
    if (completed_.size() > total_positions_)
        return false;
    state_ = state;
    WSPRRYPI_PROGRESS_TEST_PAUSE(
        ProgressTestOperation::TERMINAL,
        ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK);
    return true;
}

#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
std::size_t RpiStandardFeldProgressStore::completed_capacity_for_test() const
{
    std::lock_guard<std::mutex> lock{mutex_};
    return completed_.capacity();
}
#endif

bool RpiStandardFeldExecution::exact_boundary(
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

RpiStandardFeldPlanValidation RpiStandardFeldExecution::validate(
    const ExecutionPlan& plan)
{
    const auto invalid = [](std::string error) {
        return RpiStandardFeldPlanValidation{false, std::move(error)};
    };
    if (plan.mode != TransmissionMode::STANDARD_FELD)
        return invalid("Raspberry Pi Standard Feld execution requires a STANDARD_FELD plan.");
    if (plan.backend != BackendKind::RPI_CLOCK_GPIO)
        return invalid("Standard Feld plan is not targeted for the Raspberry Pi GPIO backend.");
    if (plan.events.empty())
        return invalid("Standard Feld plan has no events.");
    if (plan.events.size() % standard_feld::kPositionsPerCell != 0U)
        return invalid("Standard Feld plan does not contain complete raster cells.");
    const std::size_t cell_count =
        plan.events.size() / standard_feld::kPositionsPerCell;
    if (cell_count < 3U)
        return invalid("Standard Feld plan must contain leader, message, and trailer cells.");
    if (!std::isfinite(plan.reference_frequency_hz) ||
        plan.reference_frequency_hz <= 0.0)
        return invalid("Standard Feld plan has an invalid carrier frequency.");
    if (plan.summary.event_count != plan.events.size())
        return invalid("Standard Feld plan event count does not match its summary.");
    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const auto& event = plan.events[i];
        if (!event.raster_progress.has_value())
            return invalid("Standard Feld plan event lacks raster progress identity.");
        const auto& progress = *event.raster_progress;
        if (progress.absolute_position != i)
            return invalid("Standard Feld plan physical positions are not contiguous.");

        const std::size_t within_cell =
            i % standard_feld::kPositionsPerCell;
        const auto expected_column = static_cast<std::uint8_t>(
            within_cell / standard_feld::kPhysicalPositionsPerColumn);
        const auto expected_physical_position = static_cast<std::uint8_t>(
            within_cell % standard_feld::kPhysicalPositionsPerColumn);
        if (progress.cell_column >= standard_feld::kColumnsPerCell)
            return invalid("Standard Feld plan raster column is out of range.");
        if (progress.physical_position >=
            standard_feld::kPhysicalPositionsPerColumn)
            return invalid("Standard Feld plan physical position is out of range.");
        if (progress.cell_column != expected_column)
            return invalid("Standard Feld plan raster column disagrees with its absolute position.");
        if (progress.physical_position != expected_physical_position)
            return invalid("Standard Feld plan physical position disagrees with its absolute position.");

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
                return invalid("Standard Feld plan message character index is not representable.");
            expected_char_index = static_cast<int>(message_cell);
        }
        if (progress.cell_kind != expected_kind)
            return invalid("Standard Feld plan raster cell kind is inconsistent.");
        if (progress.normalized_char_index != expected_char_index)
            return invalid("Standard Feld plan normalized character index is inconsistent.");
        if (event.message_char_index != expected_char_index)
            return invalid("Standard Feld plan event and raster character indexes disagree.");

        std::chrono::nanoseconds start{};
        std::chrono::nanoseconds end{};
        if (!exact_boundary(i, start) || !exact_boundary(i + 1U, end))
            return invalid("Standard Feld plan timing exceeds nanosecond representation.");
        if (event.offset_from_start != start || event.duration != end - start)
            return invalid("Standard Feld plan does not preserve the exact 245-position/s timebase.");
        if (i > 0U &&
            event.offset_from_start <= plan.events[i - 1U].offset_from_start)
            return invalid("Standard Feld plan deadlines are not strictly increasing.");
        if (event.duration <= std::chrono::nanoseconds::zero())
            return invalid("Standard Feld plan contains a non-positive event duration.");
        if (event.type != (event.rf_on ? RfEventType::RF_ON
                                       : RfEventType::RF_OFF))
            return invalid("Standard Feld plan RF event type and intent disagree.");
        if (!std::isfinite(event.frequency_hz) ||
            event.frequency_hz != plan.reference_frequency_hz)
            return invalid("Standard Feld plan event carrier intent is invalid.");
    }

    if (plan.events.back().rf_on)
        return invalid("Standard Feld plan does not end with RF off.");
    std::chrono::nanoseconds total_duration{};
    if (!exact_boundary(plan.events.size(), total_duration))
        return invalid("Standard Feld plan timing exceeds nanosecond representation.");
    if (plan.summary.total_duration != total_duration)
        return invalid("Standard Feld plan duration does not match its physical positions.");
    if (plan.summary.min_frequency_hz != plan.reference_frequency_hz ||
        plan.summary.max_frequency_hz != plan.reference_frequency_hz)
        return invalid("Standard Feld plan frequency summary disagrees with its carrier.");
    return {true, {}};
}

RpiStandardFeldExecutionResult RpiStandardFeldExecution::execute(
    const ExecutionPlan& plan,
    IRpiStandardFeldExecutionAdapter& adapter)
{
    const auto validation = validate(plan);
    if (!validation.ok)
    {
        RpiStandardFeldExecutionResult result;
        result.error = validation.error;
        return result;
    }

    // Startup begins immediately before initial safe-state establishment: that
    // adapter call may have touched backend state, so every body outcome below
    // is returned to this outer scope for exactly one finalization attempt.
    const auto execute_started = [&]() -> RpiStandardFeldExecutionResult {
    std::optional<std::size_t> last_completed;
    auto next_pending = std::optional<std::size_t>{0U};
    const auto initial_idle = call_adapter(
        "initial safe idle",
        [&adapter]() { return adapter.establish_initial_safe_state(); });
    if (initial_idle.status != RpiStandardFeldAdapterStatus::OK)
        return failure(
            adapter,
            "Initial safe-idle setup failed. " + initial_idle.error,
            last_completed,
            next_pending);

    const auto initial_wait = call_adapter(
        "logical deadline wait",
        [&adapter]() {
            return adapter.wait_until(std::chrono::nanoseconds::zero());
        });
    if (initial_wait.status == RpiStandardFeldAdapterStatus::CANCELLED ||
        adapter.watchdog_faulted())
        return stopped_or_faulted(adapter, last_completed, next_pending);
    if (initial_wait.status == RpiStandardFeldAdapterStatus::FAILED)
        return failure(
            adapter,
            adapter_failure("Logical deadline wait", 0U, initial_wait),
            last_completed,
            next_pending);

    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        next_pending = i;
        if (adapter.watchdog_faulted() || adapter.cancellation_requested())
            return stopped_or_faulted(adapter, last_completed, next_pending);

        const auto& event = plan.events[i];
        const auto carrier_result = call_adapter(
            "carrier application",
            [&adapter, &event, i]() {
                return adapter.apply_carrier(event.frequency_hz, i);
            });
        if (carrier_result.status == RpiStandardFeldAdapterStatus::CANCELLED)
            return stopped_or_faulted(adapter, last_completed, next_pending);
        if (carrier_result.status == RpiStandardFeldAdapterStatus::FAILED)
            return failure(
                adapter,
                adapter_failure("Carrier application", i, carrier_result),
                last_completed,
                next_pending);

        // Recheck after carrier application.  apply_rf_checked() repeats this
        // classification at the adapter's last safe point before RF enable.
        if (adapter.watchdog_faulted() || adapter.cancellation_requested())
            return stopped_or_faulted(adapter, last_completed, next_pending);
        const auto rf_result = call_adapter(
            event.rf_on ? "RF-on application" : "RF-off application",
            [&adapter, &event, i]() {
                return adapter.apply_rf_checked(event, i);
            });
        if (rf_result.status == RpiStandardFeldAdapterStatus::CANCELLED)
            return stopped_or_faulted(adapter, last_completed, next_pending);
        if (rf_result.status == RpiStandardFeldAdapterStatus::FAILED)
            return failure(
                adapter,
                adapter_failure(
                    event.rf_on ? "RF-on application" : "RF-off application",
                    i,
                    rf_result),
                last_completed,
                next_pending);

        const auto end_deadline =
            event.offset_from_start + event.duration;
        const auto wait_result = call_adapter(
            "logical deadline wait",
            [&adapter, end_deadline]() {
                return adapter.wait_until(end_deadline);
            });
        if (wait_result.status == RpiStandardFeldAdapterStatus::CANCELLED ||
            adapter.watchdog_faulted())
            return stopped_or_faulted(adapter, last_completed, next_pending);
        if (wait_result.status == RpiStandardFeldAdapterStatus::FAILED)
            return failure(
                adapter,
                adapter_failure("Logical deadline wait", i + 1U, wait_result),
                last_completed,
                next_pending);

        const auto progress_result = call_adapter(
            "progress reporting",
            [&adapter, &event, i]() {
                return adapter.report_progress(i, *event.raster_progress);
            });
        if (progress_result.status == RpiStandardFeldAdapterStatus::CANCELLED)
            return stopped_or_faulted(adapter, last_completed, next_pending);
        if (progress_result.status == RpiStandardFeldAdapterStatus::FAILED)
            return failure(
                adapter,
                adapter_failure("Progress reporting", i + 1U, progress_result),
                last_completed,
                next_pending);

        last_completed = i;
        next_pending = i + 1U < plan.events.size()
            ? std::optional<std::size_t>{i + 1U}
            : std::nullopt;
    }

    // Completion is the final physical-position boundary and receives the
    // same single boundary cancellation opportunity as pending positions.
    if (adapter.watchdog_faulted() || adapter.cancellation_requested())
        return stopped_or_faulted(adapter, last_completed, next_pending);

    RpiStandardFeldExecutionResult result;
    result.terminal = RpiStandardFeldExecutionTerminal::COMPLETED;
    result.last_completed_position = last_completed;
    return result;
    };

    RpiStandardFeldExecutionResult result;
    try
    {
        result = execute_started();
    }
    catch (const std::exception& exception)
    {
        result.terminal = RpiStandardFeldExecutionTerminal::FAILED;
        try { result.primary_error = std::string("Standard Feld execution threw: ") + exception.what(); }
        catch (...) { result.primary_error = "Standard Feld execution threw."; }
    }
    catch (...)
    {
        result.terminal = RpiStandardFeldExecutionTerminal::FAILED;
        try { result.primary_error = "Standard Feld execution threw an unknown exception."; }
        catch (...) {}
    }
    finalize_noexcept(adapter, result);
    classify_terminal_result(adapter, result);
    return result;
}

} // namespace wsprrypi
