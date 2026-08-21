#include "standard_feld_execution_gate.hpp"

#include <stdexcept>

namespace wsprrypi
{
StandardFeldExecutionGate::Generation StandardFeldExecutionGate::activate(
    std::uint64_t plan_id)
{
    std::lock_guard<std::mutex> lock{mutex_};
    return activate_locked(plan_id);
}

StandardFeldExecutionGate::Generation StandardFeldExecutionGate::activate_locked(
    std::uint64_t plan_id)
{
    if (active_)
        throw std::logic_error(
            "Standard Feld execution gate cannot replace an active generation.");
    ++generation_.value;
    if (generation_.value == 0)
        ++generation_.value;
    (void)plan_id; // Generation is deliberately monotonic even if plan IDs repeat.
    active_ = true;
    stop_requested_ = false;
    watchdog_faulted_ = false;
    return generation_;
}

#ifdef STANDARD_FELD_GATE_TEST_HOOKS
StandardFeldExecutionGate::Generation
StandardFeldExecutionGate::test_activate_while_locked(
    std::uint64_t plan_id, const std::function<void()>& while_locked)
{
    std::lock_guard<std::mutex> lock{mutex_};
    while_locked();
    return activate_locked(plan_id);
}

void StandardFeldExecutionGate::test_deactivate_while_locked(
    Generation generation, const std::function<void()>& while_locked)
{
    std::lock_guard<std::mutex> lock{mutex_};
    while_locked();
    if (matches_locked(generation))
        active_ = false;
}

bool StandardFeldExecutionGate::test_publish_stop_while_locked(
    Generation generation, bool watchdog_fault,
    const std::function<void()>& while_locked)
{
    std::lock_guard<std::mutex> lock{mutex_};
    while_locked();
    if (!matches_locked(generation))
        return false;
    stop_requested_ = true;
    watchdog_faulted_ = watchdog_faulted_ || watchdog_fault;
    return true;
}
#endif

void StandardFeldExecutionGate::deactivate(Generation generation) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (matches_locked(generation))
            active_ = false;
    }
    catch (...)
    {
        // A mutex-runtime failure is unrecoverable for synchronization, but it
        // must not escape a destruction/finalization boundary.
    }
}

bool StandardFeldExecutionGate::publish_stop(bool watchdog_fault) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!active_)
            return false;
        stop_requested_ = true;
        watchdog_faulted_ = watchdog_faulted_ || watchdog_fault;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool StandardFeldExecutionGate::publish_stop(
    Generation generation, bool watchdog_fault) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!matches_locked(generation))
            return false;
        stop_requested_ = true;
        watchdog_faulted_ = watchdog_faulted_ || watchdog_fault;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool StandardFeldExecutionGate::stop_requested(Generation generation) const noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return matches_locked(generation) && stop_requested_;
    }
    catch (...)
    {
        return true;
    }
}

bool StandardFeldExecutionGate::watchdog_faulted(Generation generation) const noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return matches_locked(generation) && watchdog_faulted_;
    }
    catch (...)
    {
        return true;
    }
}

bool StandardFeldExecutionGate::matches_locked(Generation generation) const noexcept
{
    return active_ && generation.value == generation_.value;
}
} // namespace wsprrypi
