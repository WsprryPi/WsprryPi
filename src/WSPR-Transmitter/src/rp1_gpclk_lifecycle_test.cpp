#include "rp1_gpclk_lifecycle.hpp"
#include "rp1_gpclk_planner.hpp"

#include <algorithm>
#include <iostream>
#include <string>
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

enum class FailurePoint
{
    none,
    acquire_clock,
    acquire_pin,
    configure_clock,
    configure_pin,
    enable_output
};

struct FakeResources
{
    bool clock_owned{false};
    bool pin_owned{false};
    bool output_enabled{false};
};

class FakeAdapter final : public wsprrypi::Rp1GpclkResourceAdapter
{
public:
    explicit FakeAdapter(FakeResources& resources)
        : resources_(resources)
    {
    }

    bool acquireClock(std::string& error) override
    {
        trace.push_back("acquire-clock");
        if (failure == FailurePoint::acquire_clock || resources_.clock_owned)
            return reject(error, "fake clock acquisition failed");
        resources_.clock_owned = true;
        return true;
    }

    bool acquirePin(std::string& error) override
    {
        observeQuiescence();
        trace.push_back("acquire-pin");
        if (failure == FailurePoint::acquire_pin || resources_.pin_owned)
            return reject(error, "fake pin acquisition failed");
        resources_.pin_owned = true;
        return true;
    }

    bool configureClock(
        const wsprrypi::Rp1GpclkPlan&,
        std::string& error) override
    {
        observeQuiescence();
        trace.push_back("configure-clock");
        if (failure == FailurePoint::configure_clock)
            return reject(error, "fake clock configuration failed");
        return true;
    }

    bool configurePin(std::string& error) override
    {
        observeQuiescence();
        trace.push_back("configure-pin");
        if (failure == FailurePoint::configure_pin)
            return reject(error, "fake pin configuration failed");
        return true;
    }

    bool enableOutput(std::string& error) override
    {
        observeQuiescence();
        trace.push_back("enable-output");
        if (failure == FailurePoint::enable_output)
        {
            // Model an adapter that may have partially enabled before failing.
            resources_.output_enabled = true;
            return reject(error, "fake output enable failed");
        }
        resources_.output_enabled = true;
        return true;
    }

    void disableOutput() noexcept override
    {
        trace.push_back("disable-output");
        resources_.output_enabled = false;
    }

    void releasePin() noexcept override
    {
        trace.push_back("release-pin");
        resources_.pin_owned = false;
    }

    void releaseClock() noexcept override
    {
        trace.push_back("release-clock");
        resources_.clock_owned = false;
    }

    FailurePoint failure{FailurePoint::none};
    bool non_enable_step_observed_output{false};
    std::vector<std::string> trace;

private:
    bool reject(std::string& error, const char* message)
    {
        error = message;
        return false;
    }

    void observeQuiescence()
    {
        if (resources_.output_enabled)
            non_enable_step_observed_output = true;
    }

    FakeResources& resources_;
};

wsprrypi::Rp1GpclkPlan validPlan()
{
    wsprrypi::Rp1GpclkPlannerInput input;
    input.center_frequency_hz = 14097100.0;
    input.tone_spacing_hz = 1.46484375;
    input.parent_frequency_hz = 200000000.0;
    const auto result = wsprrypi::planRp1GpclkWspr(input);
    expect(result.ok, "test fixture planner must produce a valid plan");
    return result.plan;
}

bool resourcesReleased(const FakeResources& resources)
{
    return !resources.clock_owned && !resources.pin_owned &&
        !resources.output_enabled;
}

void expectSuffix(
    const std::vector<std::string>& trace,
    const std::vector<std::string>& suffix,
    const std::string& message)
{
    expect(trace.size() >= suffix.size() &&
               std::equal(suffix.rbegin(), suffix.rend(), trace.rbegin()),
           message);
}

void test_construction_and_invalid_start_are_quiescent()
{
    FakeResources resources;
    FakeAdapter adapter(resources);
    wsprrypi::Rp1GpclkLifecycle lifecycle(adapter);
    expect(adapter.trace.empty() && resourcesReleased(resources),
           "construction must not acquire or enable anything");

    wsprrypi::Rp1GpclkPlan invalid;
    const auto result = lifecycle.start(invalid);
    expect(!result.ok && adapter.trace.empty() && resourcesReleased(resources),
           "invalid plan must fail before resource acquisition");
}

void test_success_exclusion_and_normal_stop()
{
    FakeResources resources;
    FakeAdapter first_adapter(resources);
    FakeAdapter second_adapter(resources);
    wsprrypi::Rp1GpclkLifecycle first(first_adapter);
    wsprrypi::Rp1GpclkLifecycle second(second_adapter);
    const auto plan = validPlan();

    expect(first.start(plan).ok, "first fake owner must start");
    expect(first.running() && first.ownsClock() && first.ownsPin() &&
               first.outputEnabled(),
           "successful start must record the complete active state");
    expect(!first_adapter.non_enable_step_observed_output,
           "output must remain disabled through every prerequisite");
    expect(!second.start(plan).ok && !second.running(),
           "competing owner must fail closed");

    first.stop();
    expect(resourcesReleased(resources), "normal stop must release all resources");
    expectSuffix(first_adapter.trace,
                 {"disable-output", "release-pin", "release-clock"},
                 "normal stop must disable output before reverse-order release");
    const auto trace_size = first_adapter.trace.size();
    first.stop();
    expect(first_adapter.trace.size() == trace_size,
           "repeated stop must be idempotent");

    expect(second.start(plan).ok,
           "a clean session must acquire resources after the first owner stops");
    second.stop();
}

void test_cancellation_at_each_boundary()
{
    const auto plan = validPlan();
    for (int cancel_check = 1; cancel_check <= 6; ++cancel_check)
    {
        FakeResources resources;
        FakeAdapter adapter(resources);
        wsprrypi::Rp1GpclkLifecycle lifecycle(adapter);
        int checks = 0;
        const auto result = lifecycle.start(plan, [&]() {
            return ++checks == cancel_check;
        });
        expect(!result.ok && result.error.find("cancelled") != std::string::npos,
               "cancellation at each startup boundary must be reported");
        expect(resourcesReleased(resources) && !lifecycle.running(),
               "startup cancellation must leave all fake resources quiescent");
        if (cancel_check == 2)
            expectSuffix(adapter.trace, {"release-clock"},
                         "post-clock cancellation must release the clock");
        else if (cancel_check >= 3 && cancel_check <= 5)
            expectSuffix(adapter.trace, {"release-pin", "release-clock"},
                         "setup cancellation must reverse both acquisitions");
        else if (cancel_check == 6)
            expectSuffix(adapter.trace,
                         {"disable-output", "release-pin", "release-clock"},
                         "post-enable cancellation must disable before release");
    }
}

void test_running_cancellation_and_destruction()
{
    const auto plan = validPlan();
    FakeResources resources;
    FakeAdapter adapter(resources);
    {
        wsprrypi::Rp1GpclkLifecycle lifecycle(adapter);
        expect(lifecycle.start(plan).ok, "running-cancellation fixture must start");
        lifecycle.cancel();
        expect(resourcesReleased(resources),
               "running cancellation must disable and release all resources");
    }

    adapter.trace.clear();
    {
        wsprrypi::Rp1GpclkLifecycle lifecycle(adapter);
        expect(lifecycle.start(plan).ok, "destructor fixture must start");
    }
    expect(resourcesReleased(resources),
           "destruction must leave no owned resource or enabled output");
    expectSuffix(adapter.trace,
                 {"disable-output", "release-pin", "release-clock"},
                 "destructor cleanup must use fail-closed ordering");
}

void test_injected_failures_and_recovery()
{
    const auto plan = validPlan();
    for (const auto point : {FailurePoint::acquire_clock,
                             FailurePoint::acquire_pin,
                             FailurePoint::configure_clock,
                             FailurePoint::configure_pin,
                             FailurePoint::enable_output})
    {
        FakeResources resources;
        FakeAdapter adapter(resources);
        adapter.failure = point;
        wsprrypi::Rp1GpclkLifecycle lifecycle(adapter);
        const auto result = lifecycle.start(plan);
        expect(!result.ok && resourcesReleased(resources),
               "every injected setup failure must unwind completely");

        if (point == FailurePoint::enable_output)
            expectSuffix(adapter.trace,
                         {"disable-output", "release-pin", "release-clock"},
                         "uncertain enable failure must still disable before release");
        else if (point == FailurePoint::configure_clock ||
                 point == FailurePoint::configure_pin)
            expectSuffix(adapter.trace,
                         {"release-pin", "release-clock"},
                         "configuration failure must reverse both acquisitions");
        else if (point == FailurePoint::acquire_pin)
            expectSuffix(adapter.trace, {"release-clock"},
                         "pin acquisition failure must release the clock");

        adapter.failure = FailurePoint::none;
        expect(lifecycle.start(plan).ok,
               "a failed session must not prevent a later clean session");
        lifecycle.stop();
        expect(resourcesReleased(resources),
               "recovered session must also clean up completely");
    }
}
}

int main()
{
    test_construction_and_invalid_start_are_quiescent();
    test_success_exclusion_and_normal_stop();
    test_cancellation_at_each_boundary();
    test_running_cancellation_and_destruction();
    test_injected_failures_and_recovery();

    if (failures != 0)
    {
        std::cerr << failures << " RP1 GPCLK lifecycle test(s) failed\n";
        return 1;
    }
    std::cout << "RP1 GPCLK fake-driver lifecycle tests passed\n";
    return 0;
}
