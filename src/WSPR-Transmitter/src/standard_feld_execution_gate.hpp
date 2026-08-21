#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

namespace wsprrypi
{
// Production-only synchronization for the Standard Feld route.  It has no
// device dependency and deliberately owns only lifecycle, stop/fault state,
// and the final RF-enable authorization edge.
class StandardFeldExecutionGate final
{
public:
    struct Generation { std::uint64_t value{0}; };
    enum class RfAuthorization { GRANTED, INACTIVE, STOPPED };

    Generation activate(std::uint64_t plan_id);
    void deactivate(Generation generation) noexcept;
    bool publish_stop(bool watchdog_fault = false) noexcept;
    bool publish_stop(Generation generation, bool watchdog_fault = false) noexcept;
    bool stop_requested(Generation generation) const noexcept;
    bool watchdog_faulted(Generation generation) const noexcept;

#ifdef STANDARD_FELD_GATE_TEST_HOOKS
    // Focused-test-only lock-ownership probes.  The production build does not
    // declare or compile these methods.
    Generation test_activate_while_locked(
        std::uint64_t plan_id, const std::function<void()>& while_locked);
    void test_deactivate_while_locked(
        Generation generation, const std::function<void()>& while_locked);
    bool test_publish_stop_while_locked(
        Generation generation, bool watchdog_fault,
        const std::function<void()>& while_locked);
#endif

    template<typename Enable>
    RfAuthorization authorize_rf_on(Generation generation, Enable&& enable)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!active_ || generation.value != generation_.value)
            return RfAuthorization::INACTIVE;
        if (stop_requested_ || watchdog_faulted_)
            return RfAuthorization::STOPPED;
        enable(); // The physical enable edge is the only callback under lock.
        return RfAuthorization::GRANTED;
    }

private:
    bool matches_locked(Generation generation) const noexcept;
    Generation activate_locked(std::uint64_t plan_id);
    mutable std::mutex mutex_{};
    Generation generation_{};
    bool active_{false};
    bool stop_requested_{false};
    bool watchdog_faulted_{false};
};
} // namespace wsprrypi
