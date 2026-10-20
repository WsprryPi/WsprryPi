#include "simulated_transmit_backend.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using namespace std::chrono_literals;

class RealtimeContext final : public wsprrypi::IExecutionContext
{
public:
    bool stopRequested() const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopped_;
    }

    bool waitInterruptibleFor(std::chrono::nanoseconds duration) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        wait_entered_ = true;
        condition_.notify_all();
        return !condition_.wait_for(lock, duration, [this] { return stopped_; });
    }

    void reportExecutionProgress(std::size_t) noexcept override {}

    std::chrono::nanoseconds logicalNow() const noexcept override
    {
        return {};
    }

    void cancel()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        condition_.notify_all();
    }

    bool waitUntilEntered(std::chrono::nanoseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return wait_entered_; });
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool stopped_{false};
    bool wait_entered_{false};
};

void expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

wsprrypi::ExecutionPlan make_plan(
    std::chrono::nanoseconds event_duration,
    std::size_t event_count)
{
    wsprrypi::ExecutionPlan plan;
    plan.id.value = 40;
    plan.request_id.value = 400;
    plan.backend = wsprrypi::BackendKind::SIMULATED;
    plan.mode = wsprrypi::TransmissionMode::QRSS;
    plan.reference_frequency_hz = 14097100.0;

    for (std::size_t index = 0; index < event_count; ++index)
    {
        plan.events.push_back({
            event_duration * index,
            event_duration,
            wsprrypi::RfEventType::SET_FREQUENCY,
            plan.reference_frequency_hz + static_cast<double>(index),
            true});
    }
    plan.summary.total_duration = event_duration * event_count;
    return plan;
}

void verify_successful_realtime_execution()
{
    RealtimeContext context;
    wsprrypi::SimulatedBackendConfig config;
    config.virtual_time = false;
    wsprrypi::SimulatedTransmitBackend backend(context, config);
    const auto plan = make_plan(40ms, 2);

    expect(backend.configure(plan, {}).ok, "real-time configure failed");
    const auto start = std::chrono::steady_clock::now();
    const auto execution = backend.execute(plan);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    expect(execution.ok, "real-time execution failed");
    expect(elapsed >= 60ms, "real-time execution completed like virtual time");
    expect(elapsed < 4s, "real-time execution exceeded its local bound");

    const std::string first_trace = backend.traceJson();
    const auto first_event = first_trace.find(
        "\"kind\":\"event\",\"event_index\":0,\"logical_ns\":0");
    const auto second_event = first_trace.find(
        "\"kind\":\"event\",\"event_index\":1,\"logical_ns\":40000000");
    const auto complete = first_trace.find(
        "\"kind\":\"complete\",\"event_index\":-1,\"logical_ns\":80000000");
    expect(first_event != std::string::npos, "first logical event is missing");
    expect(second_event != std::string::npos, "second logical event is missing");
    expect(complete != std::string::npos, "completion marker is missing");
    expect(second_event > first_event, "second logical event is out of order");
    expect(complete > second_event, "completion is out of order");
    expect(backend.cleanup().ok, "real-time cleanup failed");

    expect(backend.configure(plan, {}).ok, "repeat configure failed");
    expect(backend.execute(plan).ok, "repeat execution failed");
    expect(
        backend.traceJson() == first_trace,
        "real-time repeat did not reset deterministic trace state");
    expect(backend.cleanup().ok, "repeat cleanup failed");
}

void verify_interruptible_wait_cancellation()
{
    RealtimeContext context;
    wsprrypi::SimulatedBackendConfig config;
    config.virtual_time = false;
    wsprrypi::SimulatedTransmitBackend backend(context, config);
    const auto plan = make_plan(2s, 1);
    expect(backend.configure(plan, {}).ok, "cancellation configure failed");

    wsprrypi::ExecutionResult execution;
    const auto start = std::chrono::steady_clock::now();
    std::thread worker([&] { execution = backend.execute(plan); });
    const bool entered_wait = context.waitUntilEntered(1s);
    context.cancel();
    worker.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    expect(entered_wait, "execution did not enter its interruptible wait");
    expect(execution.stopped, "interrupted wait did not report cancellation");
    expect(!execution.ok, "interrupted wait reported success");
    expect(elapsed < 1500ms, "interrupted wait did not return promptly");
    expect(
        backend.traceJson().find("\"kind\":\"cancelled\"") != std::string::npos,
        "interrupted wait did not trace cancellation");
    expect(backend.cleanup().ok, "cancellation cleanup failed");
}
} // namespace

int main()
try
{
    verify_successful_realtime_execution();
    verify_interruptible_wait_cancellation();
    std::cout << "real-time simulated backend tests passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
