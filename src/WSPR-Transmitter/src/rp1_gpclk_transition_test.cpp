#include "rp1_gpclk_planner.hpp"
#include "rp1_gpclk_transition.hpp"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Applied
{
    std::uint64_t lower_word;
    std::uint64_t upper_word;
    std::uint32_t lower_count;
    std::uint32_t upper_count;
    std::size_t tone;
    std::uint64_t generation;
};

class FakeAdapter final : public wsprrypi::Rp1GpclkTransitionAdapter
{
public:
    bool applyToneProgram(
        const wsprrypi::Rp1GpclkTonePlan& program,
        std::size_t tone,
        std::uint64_t generation,
        std::string& error) override
    {
        if (fail_at >= 0 && static_cast<int>(applied.size()) == fail_at)
        {
            error = "injected divider transition failure";
            return false;
        }
        applied.push_back({program.lower_divider_word,
                           program.upper_divider_word,
                           program.lower_word_count,
                           program.upper_word_count,
                           tone,
                           generation});
        return true;
    }

    void failClosed() noexcept override
    {
        ++fail_closed;
    }

    int fail_at{-1};
    std::atomic<int> fail_closed{0};
    std::vector<Applied> applied;
};

wsprrypi::Rp1GpclkPlan validPlan()
{
    wsprrypi::Rp1GpclkPlannerInput input;
    input.center_frequency_hz = 14097100.0;
    input.tone_spacing_hz = 1.46484375;
    input.parent_frequency_hz = 200000000.0;
    const auto result = wsprrypi::planRp1GpclkWspr(input);
    expect(result.ok, "transition fixture planner must produce a valid plan");
    return result.plan;
}

std::vector<wsprrypi::Rp1GpclkTransitionEvent> fourToneSchedule()
{
    return {{0, 0}, {1'000'000, 1}, {2'000'000, 2}, {3'000'000, 3}};
}

void test_all_four_tones_and_scheduling()
{
    const auto plan = validPlan();
    FakeAdapter adapter;
    wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);
    const auto started = sequence.start(plan, fourToneSchedule());
    expect(started.ok, "valid four-tone sequence must start");
    expect(sequence.advance(started.generation, 0).applied == 1,
           "first due tone must apply at time zero");
    expect(sequence.advance(started.generation, 999'999).applied == 0,
           "future transition must not apply early");
    expect(sequence.advance(started.generation, 2'000'000).applied == 2,
           "advance must apply every newly due transition in order");
    const auto final = sequence.advance(started.generation, 3'000'000);
    expect(final.ok && final.complete && final.applied == 1,
           "final due transition must complete the schedule");
    expect(adapter.applied.size() == 4,
           "all four tone transitions must reach the adapter");
    for (std::size_t i = 0; i < adapter.applied.size(); ++i)
    {
        expect(adapter.applied[i].tone == i &&
                   adapter.applied[i].lower_word == plan.tones[i].lower_divider_word &&
                   adapter.applied[i].upper_word == plan.tones[i].upper_divider_word &&
                   adapter.applied[i].lower_count == plan.tones[i].lower_word_count &&
                   adapter.applied[i].upper_count == plan.tones[i].upper_word_count,
               "adapter must receive each exact planner-supplied dither program");
        if (i > 0)
            expect(plan.tones[i].average_frequency_hz >
                       plan.tones[i - 1].average_frequency_hz,
                   "planned dither-average tone frequencies must remain ordered");
    }
    sequence.stop();
    expect(adapter.fail_closed == 1,
           "completed schedule must still fail closed exactly once on stop");
}

void test_repeated_tone_selection()
{
    const auto plan = validPlan();
    FakeAdapter adapter;
    wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);
    const auto started = sequence.start(plan, {{0, 2}, {10, 2}, {20, 2}});
    const auto result = sequence.advance(started.generation, 20);
    expect(result.ok && result.complete && result.applied == 3,
           "repeated tone events must remain explicit deterministic transitions");
    expect(adapter.applied.size() == 3 &&
               adapter.applied[0].lower_count == adapter.applied[1].lower_count &&
               adapter.applied[1].lower_count == adapter.applied[2].lower_count,
           "repeated tone events must reuse the same planner program");
    sequence.stop();
}

void test_generation_rejection_and_cancellation_boundaries()
{
    const auto plan = validPlan();
    FakeAdapter adapter;
    wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);
    const auto first = sequence.start(plan, fourToneSchedule());
    sequence.cancel(first.generation);
    expect(adapter.fail_closed == 1 && adapter.applied.empty(),
           "cancellation before the first callback must fail closed without a transition");

    const auto second = sequence.start(plan, fourToneSchedule());
    const auto stale = sequence.advance(first.generation, 10'000'000);
    expect(!stale.ok && stale.stale && adapter.applied.empty(),
           "stale generation must not reach the adapter");
    expect(sequence.advance(second.generation, 1'000'000).applied == 2,
           "current generation must continue normally after stale rejection");
    sequence.cancel(second.generation);
    const auto after = sequence.advance(second.generation, 10'000'000);
    expect(!after.ok && after.stale && adapter.applied.size() == 2,
           "no transition may occur after cancellation");
    sequence.cancel(second.generation);
    sequence.cutoff(second.generation);
    sequence.stop();
    expect(adapter.fail_closed == 2,
           "repeated cleanup requests must preserve exactly-once finalization");
}

void test_transition_failure_is_fail_closed()
{
    const auto plan = validPlan();
    FakeAdapter adapter;
    adapter.fail_at = 1;
    wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);
    const auto started = sequence.start(plan, fourToneSchedule());
    const auto result = sequence.advance(started.generation, 3'000'000);
    expect(!result.ok && result.error.find("injected") != std::string::npos,
           "injected transition failure must be reported");
    expect(!sequence.active() && adapter.applied.size() == 1 &&
               adapter.fail_closed == 1,
           "transition failure must immediately invalidate and fail closed");
    expect(sequence.advance(started.generation, 3'000'000).stale &&
               adapter.applied.size() == 1,
           "failed generation must reject every later callback");
}

void test_destruction_is_exactly_once()
{
    const auto plan = validPlan();
    FakeAdapter adapter;
    {
        wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);
        const auto started = sequence.start(plan, fourToneSchedule());
        expect(started.ok && sequence.advance(started.generation, 0).applied == 1,
               "destruction fixture must begin an active sequence");
    }
    expect(adapter.fail_closed == 1 && adapter.applied.size() == 1,
           "destruction must finalize an active generation exactly once");
}

void test_cutoff_cancel_race()
{
    const auto plan = validPlan();
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        FakeAdapter adapter;
        wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);
        const auto started = sequence.start(plan, fourToneSchedule());
        std::atomic<bool> go{false};
        std::thread cancel_thread([&]() {
            while (!go.load(std::memory_order_acquire)) {}
            sequence.cancel(started.generation);
        });
        std::thread cutoff_thread([&]() {
            while (!go.load(std::memory_order_acquire)) {}
            sequence.cutoff(started.generation);
        });
        go.store(true, std::memory_order_release);
        cancel_thread.join();
        cutoff_thread.join();
        expect(adapter.fail_closed == 1 && !sequence.active(),
               "cutoff/cancellation race must finalize exactly once");
        expect(sequence.advance(started.generation, 10'000'000).stale,
               "race loser must not leave a callback-capable generation");
    }
}

void test_invalid_plan_and_schedule_rejection()
{
    const auto plan = validPlan();
    FakeAdapter adapter;
    wsprrypi::Rp1GpclkTransitionSequence sequence(adapter);

    auto invalid = plan;
    invalid.tones[0].lower_divider_word = 0;
    expect(!sequence.start(invalid, fourToneSchedule()).ok,
           "invalid divider plan must fail before activation");
    expect(!sequence.start(plan, {}).ok,
           "empty transition schedule must be rejected");
    expect(!sequence.start(plan, {{10, 0}, {9, 1}}).ok,
           "non-monotonic transition schedule must be rejected");
    expect(!sequence.start(plan, {{0, 4}}).ok,
           "out-of-range tone index must be rejected");
    expect(adapter.applied.empty() && adapter.fail_closed == 0,
           "invalid input must not touch the adapter");
}
}

int main()
{
    test_all_four_tones_and_scheduling();
    test_repeated_tone_selection();
    test_generation_rejection_and_cancellation_boundaries();
    test_transition_failure_is_fail_closed();
    test_destruction_is_exactly_once();
    test_cutoff_cancel_race();
    test_invalid_plan_and_schedule_rejection();

    if (failures != 0)
    {
        std::cerr << failures << " RP1 GPCLK transition test(s) failed\n";
        return 1;
    }
    std::cout << "RP1 GPCLK transition sequencing tests passed\n";
    return 0;
}
