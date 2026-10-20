#include "rp1_gpclk_lifecycle.hpp"

#include <cmath>

namespace wsprrypi
{
namespace
{
bool validPlan(const Rp1GpclkPlan& plan)
{
    if (!std::isfinite(plan.corrected_parent_frequency_hz) ||
        plan.corrected_parent_frequency_hz <= 0.0 ||
        plan.integer_bits == 0 || plan.fractional_bits == 0 ||
        plan.dither_sequence_length == 0 ||
        plan.shared_integer_divider == 0 ||
        !plan.average_tones_are_ordered)
    {
        return false;
    }

    for (const auto& tone : plan.tones)
    {
        if (!std::isfinite(tone.requested_frequency_hz) ||
            tone.requested_frequency_hz <= 0.0 ||
            tone.lower_divider_word == 0 || tone.upper_divider_word == 0)
        {
            return false;
        }
    }
    return true;
}
}

Rp1GpclkLifecycle::Rp1GpclkLifecycle(
    Rp1GpclkResourceAdapter& adapter) noexcept
    : adapter_(adapter)
{
}

Rp1GpclkLifecycle::~Rp1GpclkLifecycle()
{
    stop();
}

Rp1GpclkLifecycleResult Rp1GpclkLifecycle::start(
    const Rp1GpclkPlan& plan,
    CancellationCheck cancelled)
{
    if (clock_owned_ || pin_owned_ || output_may_be_enabled_ || running_)
        return {false, "RP1 GPCLK lifecycle is already active."};
    if (!validPlan(plan))
        return {false, "RP1 GPCLK lifecycle requires a valid numerical plan."};
    if (cancellationRequested(cancelled))
        return {false, "RP1 GPCLK startup cancelled before resource acquisition."};

    std::string error;
    if (!adapter_.acquireClock(error))
        return fail(error.empty() ? "Could not acquire RP1 GPCLK resource." : error);
    clock_owned_ = true;
    if (cancellationRequested(cancelled))
        return fail("RP1 GPCLK startup cancelled after clock acquisition.");

    if (!adapter_.acquirePin(error))
        return fail(error.empty() ? "Could not acquire RP1 GPCLK pin resource." : error);
    pin_owned_ = true;
    if (cancellationRequested(cancelled))
        return fail("RP1 GPCLK startup cancelled after pin acquisition.");

    if (!adapter_.configureClock(plan, error))
        return fail(error.empty() ? "Could not configure RP1 GPCLK resource." : error);
    if (cancellationRequested(cancelled))
        return fail("RP1 GPCLK startup cancelled after clock configuration.");

    if (!adapter_.configurePin(error))
        return fail(error.empty() ? "Could not configure RP1 GPCLK pin resource." : error);
    if (cancellationRequested(cancelled))
        return fail("RP1 GPCLK startup cancelled after pin configuration.");

    // Enabling can fail after partially changing an eventual hardware adapter,
    // so cleanup must attempt disable even when enableOutput reports failure.
    output_may_be_enabled_ = true;
    if (!adapter_.enableOutput(error))
        return fail(error.empty() ? "Could not enable RP1 GPCLK output." : error);
    if (cancellationRequested(cancelled))
        return fail("RP1 GPCLK startup cancelled after output enablement.");

    running_ = true;
    return {true, {}};
}

void Rp1GpclkLifecycle::cancel() noexcept
{
    stop();
}

void Rp1GpclkLifecycle::stop() noexcept
{
    running_ = false;
    if (output_may_be_enabled_)
    {
        adapter_.disableOutput();
        output_may_be_enabled_ = false;
    }
    if (pin_owned_)
    {
        adapter_.releasePin();
        pin_owned_ = false;
    }
    if (clock_owned_)
    {
        adapter_.releaseClock();
        clock_owned_ = false;
    }
}

bool Rp1GpclkLifecycle::ownsClock() const noexcept
{
    return clock_owned_;
}

bool Rp1GpclkLifecycle::ownsPin() const noexcept
{
    return pin_owned_;
}

bool Rp1GpclkLifecycle::outputEnabled() const noexcept
{
    return output_may_be_enabled_;
}

bool Rp1GpclkLifecycle::running() const noexcept
{
    return running_;
}

Rp1GpclkLifecycleResult Rp1GpclkLifecycle::fail(
    const std::string& error) noexcept
{
    stop();
    return {false, error};
}

bool Rp1GpclkLifecycle::cancellationRequested(
    const CancellationCheck& cancelled) const
{
    return cancelled && cancelled();
}

} // namespace wsprrypi
