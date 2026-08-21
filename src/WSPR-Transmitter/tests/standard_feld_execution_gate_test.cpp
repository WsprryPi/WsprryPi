#include "standard_feld_execution_gate.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace
{
using namespace std::chrono_literals;

void require(bool value, const std::string& message)
{
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void require_ready(std::future<void>& future, const char* message)
{
    require(future.wait_for(2s) == std::future_status::ready, message);
    future.get();
}

void test_rf_first_then_stop()
{
    wsprrypi::StandardFeldExecutionGate gate;
    const auto generation = gate.activate(1);
    std::mutex mutex;
    std::condition_variable cv;
    bool edge_entered = false;
    bool release_edge = false;
    int rf_edges = 0;
    auto rf = std::async(std::launch::async, [&] {
        require(gate.authorize_rf_on(generation, [&] {
                    std::unique_lock<std::mutex> lock{mutex};
                    ++rf_edges;
                    edge_entered = true;
                    cv.notify_all();
                    cv.wait(lock, [&] { return release_edge; });
                }) == wsprrypi::StandardFeldExecutionGate::RfAuthorization::GRANTED,
                "RF edge must own authorization before stop publication");
    });
    {
        std::unique_lock<std::mutex> lock{mutex};
        cv.wait(lock, [&] { return edge_entered; });
    }
    auto stop = std::async(std::launch::async, [&] {
        require(gate.publish_stop(generation), "active generation must receive stop");
    });
    require(stop.wait_for(0ms) == std::future_status::timeout,
            "stop publication must wait for an already-owned RF edge");
    {
        std::lock_guard<std::mutex> lock{mutex};
        release_edge = true;
    }
    cv.notify_all();
    require_ready(rf, "RF authorization deadlocked");
    require_ready(stop, "stop publication deadlocked");
    require(rf_edges == 1 && gate.stop_requested(generation),
            "published stop must be visible after the protected RF edge");
    require(gate.authorize_rf_on(generation, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::STOPPED,
            "no later RF edge may occur after stop publication returns");
}

void test_stop_first_watchdog_and_generation()
{
    wsprrypi::StandardFeldExecutionGate gate;
    const auto old_generation = gate.activate(7);
    require(gate.publish_stop(old_generation, true), "watchdog stop must publish");
    require(gate.publish_stop(old_generation), "repeated stop must be idempotent");
    require(gate.watchdog_faulted(old_generation), "watchdog latch must survive repeat stop");
    require(gate.authorize_rf_on(old_generation, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::STOPPED,
            "watchdog stop must deny RF-on");
    gate.deactivate(old_generation);
    require(!gate.publish_stop(old_generation), "deactivated generation must reject publication");
    const auto replacement = gate.activate(7);
    require(!gate.publish_stop(old_generation), "stale generation must not affect replacement");
    require(gate.authorize_rf_on(replacement, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::GRANTED,
            "replacement must not inherit stale stop state");
    gate.deactivate(replacement);
}

void test_stop_cleanup_and_deactivation()
{
    wsprrypi::StandardFeldExecutionGate gate;
    const auto generation = gate.activate(9);
    require(gate.publish_stop(generation), "stop before cleanup must publish");
    // Cleanup is intentionally outside the gate: deactivation represents its
    // completion and must not block a stale/late publisher indefinitely.
    gate.deactivate(generation);
    require(!gate.publish_stop(generation), "post-cleanup publication is stale");
    require(gate.authorize_rf_on(generation, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE,
            "deactivation must deny RF-on");
}

void test_post_carrier_stop_and_progress_cutoff()
{
    wsprrypi::StandardFeldExecutionGate gate;
    const auto generation = gate.activate(10);
    int carrier_calls = 1; // The production execution body has completed carrier.
    int rf_on_calls = 0;
    int progress_calls = 0;
    require(gate.publish_stop(generation), "post-carrier stop must publish");
    const auto authorization = gate.authorize_rf_on(generation, [&] { ++rf_on_calls; });
    require(carrier_calls == 1 &&
                authorization == wsprrypi::StandardFeldExecutionGate::RfAuthorization::STOPPED &&
                rf_on_calls == 0 && progress_calls == 0,
            "stop after carrier must suppress pending RF-on and progress");
    gate.deactivate(generation);
}

void test_activation_deactivation_and_reconfiguration()
{
    wsprrypi::StandardFeldExecutionGate gate;
    const wsprrypi::StandardFeldExecutionGate::Generation inactive{};
    require(!gate.publish_stop(inactive), "inactive generation must reject stop");
    require(gate.authorize_rf_on(inactive, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE,
            "inactive generation must reject RF authorization");

    const auto first = gate.activate(11);
    bool replacement_rejected = false;
    try { (void)gate.activate(12); }
    catch (const std::logic_error&) { replacement_rejected = true; }
    require(replacement_rejected, "activation must not replace a live generation");
    gate.deactivate({first.value + 1U});
    require(gate.authorize_rf_on(first, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::GRANTED,
            "wrong-generation deactivation must not affect active execution");
    gate.deactivate(first);
    gate.deactivate(first);
    require(!gate.publish_stop(first), "repeated deactivation must leave generation inactive");

    const auto replacement = gate.activate(12);
    require(!gate.publish_stop(first), "stale reconfiguration stop must not affect replacement");
    require(gate.publish_stop(replacement), "replacement stop must target replacement generation");
    require(gate.authorize_rf_on(first, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE &&
                gate.authorize_rf_on(replacement, [] {}) ==
                wsprrypi::StandardFeldExecutionGate::RfAuthorization::STOPPED,
            "RF authorization must not cross generation boundaries");
    gate.deactivate(replacement);
}

void test_repeated_watchdog_and_rf_off_race()
{
    wsprrypi::StandardFeldExecutionGate gate;
    const auto generation = gate.activate(13);
    require(gate.publish_stop(generation), "first user stop must publish");
    require(gate.publish_stop(generation, true), "watchdog after user stop must publish");
    require(gate.publish_stop(generation), "repeated user stop must be idempotent");
    require(gate.watchdog_faulted(generation), "watchdog must retain precedence after user stop");
    // RF-off/cleanup has no gate-held work: publication and deactivation remain
    // independent, so neither operation waits for a join or cleanup callback.
    auto deactivation = std::async(std::launch::async, [&] { gate.deactivate(generation); });
    require_ready(deactivation, "RF-off/cleanup representation deadlocked");
    require(!gate.publish_stop(generation, true),
            "post-cleanup watchdog publication must be harmless");
}

struct LockedPhase
{
    std::mutex mutex;
    std::condition_variable cv;
    bool entered{false};
    bool release{false};

    void hold()
    {
        std::unique_lock<std::mutex> lock{mutex};
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
    }
    void wait_until_entered()
    {
        std::unique_lock<std::mutex> lock{mutex};
        cv.wait(lock, [&] { return entered; });
    }
    void unblock()
    {
        { std::lock_guard<std::mutex> lock{mutex}; release = true; }
        cv.notify_all();
    }
};

void test_stop_racing_activation()
{
    { // Activation wins: stop waits, then targets the activated generation.
        wsprrypi::StandardFeldExecutionGate gate;
        LockedPhase phase;
        auto activation = std::async(std::launch::async, [&] {
            return gate.test_activate_while_locked(20, [&] { phase.hold(); });
        });
        phase.wait_until_entered();
        auto stop = std::async(std::launch::async, [&] { return gate.publish_stop(); });
        require(stop.wait_for(0ms) == std::future_status::timeout,
                "activation-wins stop must wait for lifecycle ownership");
        phase.unblock();
        require(activation.wait_for(2s) == std::future_status::ready,
                "activation-wins activation deadlocked");
        const auto generation = activation.get();
        require(stop.wait_for(2s) == std::future_status::ready && stop.get(),
                "activation-wins stop must publish to new generation");
        require(gate.stop_requested(generation) && !gate.watchdog_faulted(generation) &&
                    gate.authorize_rf_on(generation, [] {}) ==
                    wsprrypi::StandardFeldExecutionGate::RfAuthorization::STOPPED,
                "activation-wins stop must be visible to RF authorization");
        gate.deactivate(generation);
    }
    { // Inactive stop wins: it must not poison the later activation.
        wsprrypi::StandardFeldExecutionGate gate;
        LockedPhase phase;
        auto stop = std::async(std::launch::async, [&] {
            return gate.test_publish_stop_while_locked({}, false, [&] { phase.hold(); });
        });
        phase.wait_until_entered();
        auto activation = std::async(std::launch::async, [&] { return gate.activate(21); });
        require(activation.wait_for(0ms) == std::future_status::timeout,
                "inactive-stop activation must wait for lifecycle ownership");
        phase.unblock();
        require(stop.wait_for(2s) == std::future_status::ready && !stop.get(),
                "inactive stop must report no active generation");
        require(activation.wait_for(2s) == std::future_status::ready,
                "inactive-stop activation deadlocked");
        const auto generation = activation.get();
        require(!gate.stop_requested(generation) && !gate.watchdog_faulted(generation) &&
                    gate.authorize_rf_on(generation, [] {}) ==
                    wsprrypi::StandardFeldExecutionGate::RfAuthorization::GRANTED,
                "earlier inactive stop must not cancel later generation");
        require(gate.publish_stop(generation), "new generation-targeted stop must publish");
        gate.deactivate(generation);
    }
}

void test_stop_racing_deactivation()
{
    { // Stop wins then deactivation makes the stopped generation inactive.
        wsprrypi::StandardFeldExecutionGate gate;
        const auto generation = gate.activate(22);
        LockedPhase phase;
        auto stop = std::async(std::launch::async, [&] {
            return gate.test_publish_stop_while_locked(generation, false, [&] { phase.hold(); });
        });
        phase.wait_until_entered();
        auto deactivation = std::async(std::launch::async, [&] { gate.deactivate(generation); });
        require(deactivation.wait_for(0ms) == std::future_status::timeout,
                "stop-wins deactivation must wait for publication");
        phase.unblock();
        require(stop.wait_for(2s) == std::future_status::ready && stop.get(),
                "stop-wins publication deadlocked");
        require_ready(deactivation, "stop-wins deactivation deadlocked");
        require(gate.authorize_rf_on(generation, [] {}) ==
                    wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE,
                "deactivated stopped generation must deny RF authorization");
    }
    { // Deactivation wins: the later stop must not attach to inactive state.
        wsprrypi::StandardFeldExecutionGate gate;
        const auto generation = gate.activate(23);
        LockedPhase phase;
        auto deactivation = std::async(std::launch::async, [&] {
            gate.test_deactivate_while_locked(generation, [&] { phase.hold(); });
        });
        phase.wait_until_entered();
        auto stop = std::async(std::launch::async, [&] { return gate.publish_stop(generation); });
        require(stop.wait_for(0ms) == std::future_status::timeout,
                "deactivation-wins stop must wait for lifecycle ownership");
        phase.unblock();
        require_ready(deactivation, "deactivation-wins deactivation deadlocked");
        require(stop.wait_for(2s) == std::future_status::ready && !stop.get(),
                "deactivation-wins stop must reject inactive generation");
        require(gate.authorize_rf_on(generation, [] {}) ==
                    wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE,
                "deactivation-wins generation must remain inactive");
    }
}

void test_rf_authorization_racing_deactivation()
{
    { // RF edge wins: deactivation waits for exactly one in-gate callback.
        wsprrypi::StandardFeldExecutionGate gate;
        const auto generation = gate.activate(24);
        LockedPhase phase;
        int callbacks = 0;
        auto rf = std::async(std::launch::async, [&] {
            return gate.authorize_rf_on(generation, [&] { ++callbacks; phase.hold(); });
        });
        phase.wait_until_entered();
        auto deactivation = std::async(std::launch::async, [&] { gate.deactivate(generation); });
        require(deactivation.wait_for(0ms) == std::future_status::timeout,
                "RF-wins deactivation must wait for authorized callback");
        phase.unblock();
        require(rf.wait_for(2s) == std::future_status::ready &&
                    rf.get() == wsprrypi::StandardFeldExecutionGate::RfAuthorization::GRANTED,
                "RF-wins authorization must complete");
        require_ready(deactivation, "RF-wins deactivation deadlocked");
        require(callbacks == 1 && gate.authorize_rf_on(generation, [] {}) ==
                    wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE,
                "RF-wins callback must execute once and later authorization must fail");
    }
    { // Deactivation wins: queued authorization receives no callback.
        wsprrypi::StandardFeldExecutionGate gate;
        const auto generation = gate.activate(25);
        LockedPhase phase;
        int callbacks = 0;
        auto deactivation = std::async(std::launch::async, [&] {
            gate.test_deactivate_while_locked(generation, [&] { phase.hold(); });
        });
        phase.wait_until_entered();
        auto rf = std::async(std::launch::async, [&] {
            return gate.authorize_rf_on(generation, [&] { ++callbacks; });
        });
        require(rf.wait_for(0ms) == std::future_status::timeout,
                "deactivation-wins RF authorization must wait for lifecycle ownership");
        phase.unblock();
        require_ready(deactivation, "deactivation-wins lifecycle deadlocked");
        require(rf.wait_for(2s) == std::future_status::ready &&
                    rf.get() == wsprrypi::StandardFeldExecutionGate::RfAuthorization::INACTIVE &&
                    callbacks == 0,
                "deactivation-wins RF callback must not execute");
    }
}
} // namespace

int main()
{
    test_rf_first_then_stop();
    test_stop_first_watchdog_and_generation();
    test_stop_cleanup_and_deactivation();
    test_post_carrier_stop_and_progress_cutoff();
    test_activation_deactivation_and_reconfiguration();
    test_repeated_watchdog_and_rf_off_race();
    test_stop_racing_activation();
    test_stop_racing_deactivation();
    test_rf_authorization_racing_deactivation();
    std::cout << "PASS: Standard Feld production execution gate concurrency\n";
}
