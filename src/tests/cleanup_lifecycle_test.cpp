#include "WSPR-Transmitter/src/wspr_transmit.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

wsprrypi::TransmissionRequest qrss_request()
{
    wsprrypi::TransmissionRequest request;
    request.id.value = 400;
    request.mode = wsprrypi::TransmissionMode::QRSS;
    request.output.backend = wsprrypi::BackendKind::SIMULATED;
    request.output.output = wsprrypi::ClockSource::GPIO_CLK;

    wsprrypi::QrssPayload payload;
    payload.message = "E";
    payload.frequency_hz = 14097100.0;
    payload.timing.dot = std::chrono::milliseconds(1);
    payload.timing.dash = std::chrono::milliseconds(3);
    payload.timing.intra_element_gap = std::chrono::milliseconds(1);
    payload.timing.inter_character_gap = std::chrono::milliseconds(3);
    payload.timing.inter_word_gap = std::chrono::milliseconds(7);
    request.payload = payload;
    return request;
}

TransmissionRequest legacy_request()
{
    TransmissionRequest request;
    request.actual_rf_frequency_hz = 14097100.0;
    request.dial_frequency_hz = 14097100.0;
    return request;
}

WsprTransmitter::SimulatedRuntimeConfig failing_cleanup_config()
{
    WsprTransmitter::SimulatedRuntimeConfig config;
    config.trace_path = "/tmp/wsprrypi-cleanup-lifecycle-trace.json";
    config.fail_cleanup = true;
    return config;
}

void test_explicit_cleanup_and_stop_failure()
{
    WsprTransmitter transmitter;
    transmitter.selectBackend(
        wsprrypi::BackendKind::SIMULATED,
        {},
        failing_cleanup_config());
    transmitter.configureExecution(qrss_request(), legacy_request());

    transmitter.stopAndJoin();
    expect(transmitter.getState() == WsprTransmitter::State::FAILED,
           "cleanup failure must set FAILED state");
    expect(!transmitter.lastCleanupResult().ok,
           "last cleanup result must remain observable");
    transmitter.stopAndJoin();
    expect(!transmitter.lastCleanupResult().ok,
           "repeated cleanup must preserve the deterministic failure");
}

void test_backend_replacement_failure()
{
    WsprTransmitter transmitter;
    transmitter.selectBackend(
        wsprrypi::BackendKind::SIMULATED,
        {},
        failing_cleanup_config());
    transmitter.configureExecution(qrss_request(), legacy_request());

    bool threw = false;
    try
    {
        transmitter.selectBackend(wsprrypi::BackendKind::RPI_CLOCK_GPIO);
    }
    catch (const std::exception& e)
    {
        threw = std::string(e.what()).find("backend replacement cleanup failed") !=
                std::string::npos;
    }
    expect(threw, "backend replacement must stop on cleanup failure");
    expect(transmitter.getState() == WsprTransmitter::State::FAILED,
           "replacement cleanup failure must set FAILED state");
}

void test_configuration_and_cleanup_failure_preserve_both_errors()
{
    WsprTransmitter transmitter;
    auto config = failing_cleanup_config();
    config.fail_configure = true;
    transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED, {}, config);

    bool preserved = false;
    try
    {
        transmitter.configureExecution(qrss_request(), legacy_request());
    }
    catch (const std::exception& e)
    {
        const std::string error = e.what();
        preserved = error.find("Injected simulated configure failure") !=
                        std::string::npos &&
                    error.find("Injected simulated cleanup failure") !=
                        std::string::npos;
    }
    expect(preserved, "configuration and cleanup errors must both survive");
    expect(transmitter.getState() == WsprTransmitter::State::FAILED,
           "configuration cleanup failure must set FAILED state");
}

void test_execution_cleanup_failure_prevents_completion()
{
    WsprTransmitter transmitter;
    transmitter.selectBackend(
        wsprrypi::BackendKind::SIMULATED,
        {},
        failing_cleanup_config());
    transmitter.configureExecution(qrss_request(), legacy_request());
    transmitter.startAsync();
    for (int i = 0; i < 100 &&
         transmitter.getState() != WsprTransmitter::State::FAILED; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(transmitter.getState() == WsprTransmitter::State::FAILED,
           "execution cleanup failure must prevent COMPLETE state");
}

void test_cancellation_cleanup_failure_prevents_false_cancel_success()
{
    WsprTransmitter transmitter;
    auto config = failing_cleanup_config();
    config.cancel_event = 0;
    transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED, {}, config);
    transmitter.configureExecution(qrss_request(), legacy_request());
    transmitter.startAsync();
    for (int i = 0; i < 100 &&
         transmitter.getState() != WsprTransmitter::State::FAILED; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(transmitter.getState() == WsprTransmitter::State::FAILED,
           "cancellation cleanup failure must remain a failed lifecycle");
}

void test_async_execution_exception_is_reported_without_process_abort()
{
    WsprTransmitter transmitter;
    WsprTransmitter::SimulatedRuntimeConfig config;
    config.fail_event = 0;
    transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED, {}, config);
    transmitter.configureExecution(qrss_request(), legacy_request());
    transmitter.startAsync();
    for (int i = 0; i < 1000 &&
         transmitter.getState() != WsprTransmitter::State::FAILED; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(transmitter.getState() == WsprTransmitter::State::FAILED,
           "worker exception must become FAILED without terminating the process");
    transmitter.stopAndJoin();
    expect(transmitter.lastCleanupResult().ok,
           "worker exception must complete backend cleanup");
}

void test_startup_quiesce_failure_propagates_without_arming_cleanup()
{
    WsprTransmitter transmitter;
    WsprTransmitter::SimulatedRuntimeConfig config;
    config.trace_path = "/tmp/wsprrypi-startup-quiesce-lifecycle-trace.json";
    config.fail_startup_quiesce = true;
    config.fail_cleanup = true;
    transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED, {}, config);

    const auto first = transmitter.quiesceForStartup();
    const auto second = transmitter.quiesceForStartup();
    expect(!first.ok && first.error == "Injected simulated startup quiesce failure.",
           "startup quiesce failure must propagate through WsprTransmitter");
    expect(!second.ok && second.error == first.error,
           "repeated startup quiesce failure must remain deterministic");

    transmitter.stopAndJoin();
    expect(transmitter.lastCleanupResult().ok,
           "startup quiesce failure must not arm injected execution cleanup failure");
}
}

int main()
try
{
    test_async_execution_exception_is_reported_without_process_abort();
    test_explicit_cleanup_and_stop_failure();
    test_backend_replacement_failure();
    test_configuration_and_cleanup_failure_preserve_both_errors();
    test_execution_cleanup_failure_prevents_completion();
    test_cancellation_cleanup_failure_prevents_false_cancel_success();
    test_startup_quiesce_failure_propagates_without_arming_cleanup();
    std::cout << "cleanup lifecycle tests passed\n";
    return EXIT_SUCCESS;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
}
