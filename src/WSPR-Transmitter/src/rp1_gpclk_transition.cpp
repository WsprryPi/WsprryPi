#include "rp1_gpclk_transition.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace wsprrypi
{
namespace
{
bool validPlan(const Rp1GpclkPlan& plan)
{
    if (!std::isfinite(plan.corrected_parent_frequency_hz) ||
        plan.corrected_parent_frequency_hz <= 0.0 ||
        plan.fractional_bits == 0 || plan.fractional_bits >= 64 ||
        plan.shared_integer_divider == 0 || !plan.average_tones_are_ordered)
    {
        return false;
    }

    double previous_frequency = 0.0;
    for (const auto& tone : plan.tones)
    {
        if (tone.lower_divider_word == 0 || tone.upper_divider_word == 0 ||
            (tone.lower_divider_word >> plan.fractional_bits) !=
                plan.shared_integer_divider ||
            (tone.upper_divider_word >> plan.fractional_bits) !=
                plan.shared_integer_divider ||
            tone.lower_word_count + tone.upper_word_count !=
                plan.dither_sequence_length ||
            !std::isfinite(tone.nearest_frequency_hz) ||
            !std::isfinite(tone.average_frequency_hz) ||
            tone.average_frequency_hz <= previous_frequency)
        {
            return false;
        }
        previous_frequency = tone.average_frequency_hz;
    }
    return true;
}

bool validSchedule(const std::vector<Rp1GpclkTransitionEvent>& schedule)
{
    if (schedule.empty())
        return false;

    std::uint64_t previous = 0;
    bool first = true;
    for (const auto& event : schedule)
    {
        if (event.tone_index >= 4 || (!first && event.offset_ns < previous))
            return false;
        first = false;
        previous = event.offset_ns;
    }
    return true;
}
}

Rp1GpclkTransitionSequence::Rp1GpclkTransitionSequence(
    Rp1GpclkTransitionAdapter& adapter) noexcept
    : adapter_(adapter)
{
}

Rp1GpclkTransitionSequence::~Rp1GpclkTransitionSequence()
{
    stop();
}

Rp1GpclkTransitionStartResult Rp1GpclkTransitionSequence::start(
    const Rp1GpclkPlan& plan,
    std::vector<Rp1GpclkTransitionEvent> schedule)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_)
        return {false, generation_, "RP1 GPCLK transition sequence is already active."};
    if (!validPlan(plan))
        return {false, generation_, "RP1 GPCLK transition sequence requires a valid four-tone plan."};
    if (!validSchedule(schedule))
        return {false, generation_, "RP1 GPCLK transition schedule is invalid."};
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
        return {false, generation_, "RP1 GPCLK transition generation is exhausted."};

    ++generation_;
    plan_ = plan;
    schedule_ = std::move(schedule);
    next_event_ = 0;
    active_ = true;
    return {true, generation_, {}};
}

Rp1GpclkTransitionResult Rp1GpclkTransitionSequence::advance(
    std::uint64_t generation,
    std::uint64_t elapsed_ns)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || generation != generation_)
        return {false, true, false, 0, "Stale RP1 GPCLK transition callback rejected."};

    std::size_t applied = 0;
    while (next_event_ < schedule_.size() &&
           schedule_[next_event_].offset_ns <= elapsed_ns)
    {
        const auto& event = schedule_[next_event_];
        std::string error;
        if (!adapter_.applyToneProgram(
                plan_.tones[event.tone_index],
                event.tone_index,
                generation_,
                error))
        {
            return failLocked(error.empty()
                ? "Could not apply RP1 GPCLK divider transition."
                : error);
        }
        ++next_event_;
        ++applied;
    }
    return {true, false, next_event_ == schedule_.size(), applied, {}};
}

void Rp1GpclkTransitionSequence::cancel(std::uint64_t generation) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && generation == generation_)
        stopLocked();
}

void Rp1GpclkTransitionSequence::cutoff(std::uint64_t generation) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && generation == generation_)
        stopLocked();
}

void Rp1GpclkTransitionSequence::stop() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
}

bool Rp1GpclkTransitionSequence::active() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

std::uint64_t Rp1GpclkTransitionSequence::generation() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

std::size_t Rp1GpclkTransitionSequence::nextEvent() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return next_event_;
}

void Rp1GpclkTransitionSequence::stopLocked() noexcept
{
    if (!active_)
        return;
    active_ = false;
    adapter_.failClosed();
}

Rp1GpclkTransitionResult Rp1GpclkTransitionSequence::failLocked(
    const std::string& error) noexcept
{
    stopLocked();
    return {false, false, false, 0, error};
}

} // namespace wsprrypi
