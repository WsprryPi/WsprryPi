#pragma once

#include "rp1_gpclk_planner.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace wsprrypi
{

struct Rp1GpclkTransitionEvent
{
    std::uint64_t offset_ns{0};
    std::size_t tone_index{0};
};

struct Rp1GpclkTransitionStartResult
{
    bool ok{false};
    std::uint64_t generation{0};
    std::string error;
};

struct Rp1GpclkTransitionResult
{
    bool ok{false};
    bool stale{false};
    bool complete{false};
    std::size_t applied{0};
    std::string error;
};

/**
 * Hardware boundary for one already-owned RP1 GPCLK clock.
 *
 * Phase 6 passes planner-supplied finite two-word dither programs through this
 * boundary.  A later production adapter must provide the kernel-owned timed
 * implementation.
 */
class Rp1GpclkTransitionAdapter
{
public:
    virtual ~Rp1GpclkTransitionAdapter() = default;

    virtual bool applyToneProgram(
        const Rp1GpclkTonePlan& tone,
        std::size_t tone_index,
        std::uint64_t generation,
        std::string& error) = 0;
    virtual void failClosed() noexcept = 0;
};

/**
 * Generation-safe deterministic transition sequencer.
 *
 * Callers supply monotonic elapsed time.  Cancellation, cutoff, a transition
 * failure, or destruction invalidates the generation and invokes failClosed
 * exactly once.  Stale callbacks can never reach the adapter.
 */
class Rp1GpclkTransitionSequence
{
public:
    explicit Rp1GpclkTransitionSequence(
        Rp1GpclkTransitionAdapter& adapter) noexcept;
    ~Rp1GpclkTransitionSequence();

    Rp1GpclkTransitionSequence(const Rp1GpclkTransitionSequence&) = delete;
    Rp1GpclkTransitionSequence& operator=(
        const Rp1GpclkTransitionSequence&) = delete;

    Rp1GpclkTransitionStartResult start(
        const Rp1GpclkPlan& plan,
        std::vector<Rp1GpclkTransitionEvent> schedule);
    Rp1GpclkTransitionResult advance(
        std::uint64_t generation,
        std::uint64_t elapsed_ns);
    void cancel(std::uint64_t generation) noexcept;
    void cutoff(std::uint64_t generation) noexcept;
    void stop() noexcept;

    bool active() const noexcept;
    std::uint64_t generation() const noexcept;
    std::size_t nextEvent() const noexcept;

private:
    void stopLocked() noexcept;
    Rp1GpclkTransitionResult failLocked(const std::string& error) noexcept;

    Rp1GpclkTransitionAdapter& adapter_;
    mutable std::mutex mutex_;
    Rp1GpclkPlan plan_{};
    std::vector<Rp1GpclkTransitionEvent> schedule_;
    std::uint64_t generation_{0};
    std::size_t next_event_{0};
    bool active_{false};
};

} // namespace wsprrypi
