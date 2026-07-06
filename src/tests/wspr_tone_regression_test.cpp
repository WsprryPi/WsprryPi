#include "execution_plan_compiler.hpp"
#include "wspr_transmit_backend_rpi.hpp"
#include "wspr_reference_adapter.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace
{
    void require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    wsprrypi::TransmissionRequest request;
    request.mode = wsprrypi::TransmissionMode::WSPR;
    request.output.backend = wsprrypi::BackendKind::RPI_CLOCK_GPIO;
    request.output.output = wsprrypi::ClockSource::GPIO_CLK;
    request.output.gpio = 20;

    PreparedWsprTransmission prepared;
    prepared.plan_type = "Type1Single";
    prepared.frames.resize(1);
    prepared.total_frame_count = 1U;
    prepared.current_frame = 1U;
    for (std::size_t i = 0; i < WSPR_SYMBOL_COUNT; ++i)
    {
        prepared.frames[0].symbols[i] =
            static_cast<std::uint8_t>(i % 4U);
    }

    wsprrypi::WsprPayload payload;
    payload.prepared = prepared;
    payload.base_frequency_hz = 14097100.0;
    request.payload = payload;

    const wsprrypi::ExecutionPlan plan =
        wsprrypi::ExecutionPlanCompiler{}.compile(request);

    require(
        plan.events.size() == WSPR_SYMBOL_COUNT,
        "WSPR payload must compile into one RF event per WSPR symbol");

    std::set<long long> requested_microhertz;
    for (const auto& event : plan.events)
    {
        requested_microhertz.insert(
            static_cast<long long>(std::llround(event.frequency_hz * 1000000.0)));
    }

    require(
        requested_microhertz.size() == 4U,
        "WSPR symbol expansion must request four distinct RF tone frequencies");

    require(
        WsprRpiBackend::frequencyDitherBlockClocks() == 1000U,
        "Raspberry Pi GPIO backend must use fine dither blocks on both 32-bit and 64-bit builds");

    std::cout << "wspr_tone_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
