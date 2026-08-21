#include "rpi_standard_feld_progress_bridge.hpp"

namespace wsprrypi
{
bool RpiStandardFeldProgressBridge::prepare(
    const ExecutionPlan& plan, const std::uint64_t generation)
{
    if (generation == 0U ||
        plan.mode != TransmissionMode::STANDARD_FELD ||
        plan.backend != BackendKind::RPI_CLOCK_GPIO ||
        plan.summary.event_count != plan.events.size() ||
        !RpiStandardFeldExecution::validate(plan).ok)
        return false;
    return store_.reset(plan, generation);
}

bool RpiStandardFeldProgressBridge::report(
    const std::uint64_t generation, const std::size_t event_index,
    const RfEvent::RasterProgress& progress)
{
    return store_.report(generation, event_index, progress);
}

bool RpiStandardFeldProgressBridge::finalize(
    const std::uint64_t generation, const RpiStandardFeldExecutionResult& result)
{
    const auto state = result.watchdog_faulted
        ? RpiStandardFeldProgressState::WATCHDOG_FAULT
        : result.terminal == RpiStandardFeldExecutionTerminal::COMPLETED &&
                result.safe_idle_confirmed
            ? RpiStandardFeldProgressState::COMPLETED
        : result.terminal == RpiStandardFeldExecutionTerminal::CANCELLED
            ? RpiStandardFeldProgressState::CANCELLED
            : RpiStandardFeldProgressState::FAILED;
    return store_.terminal(generation, state);
}

void RpiStandardFeldProgressBridge::clear() { store_.clear(); }

RpiStandardFeldProgressSnapshot RpiStandardFeldProgressBridge::snapshot() const
{
    return store_.lifecycle_snapshot();
}
#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
std::size_t RpiStandardFeldProgressBridge::completed_capacity_for_test() const
{
    return store_.completed_capacity_for_test();
}
#endif
} // namespace wsprrypi
