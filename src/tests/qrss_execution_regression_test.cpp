#include "execution_plan_compiler.hpp"
// This test injects a compiled plan directly so it can drive the public
// backendReportExecutionProgress() seam without touching real hardware setup.
#define private public
#include "wspr_transmit.hpp"
#undef private
#include "wspr_reference_adapter.hpp"

#include <cstdlib>
#include <iostream>
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
    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::QRSS;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        wsprrypi::QrssPayload payload;
        payload.message = "AT";
        payload.frequency_hz = 7038600.0;
        payload.timing.dot = std::chrono::milliseconds(100);
        payload.timing.dash = std::chrono::milliseconds(300);
        payload.timing.intra_element_gap = std::chrono::milliseconds(100);
        payload.timing.inter_character_gap = std::chrono::milliseconds(300);
        payload.timing.inter_word_gap = std::chrono::milliseconds(700);
        request.payload = payload;

        const wsprrypi::ExecutionPlan plan =
            wsprrypi::ExecutionPlanCompiler{}.compile(request);

        std::size_t inter_character_gap_index = plan.events.size();
        std::size_t second_character_index = plan.events.size();
        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            const auto& event = plan.events[i];
            if (event.message_char_index == 0)
            {
                if (!event.rf_on &&
                    event.duration == payload.timing.inter_character_gap)
                {
                    inter_character_gap_index = i;
                }
            }
            else if (event.message_char_index == 1 &&
                     second_character_index == plan.events.size())
            {
                second_character_index = i;
            }
        }

        require(
            inter_character_gap_index < plan.events.size(),
            "QRSS execution plans must contain an inter-character gap after the first character");
        require(
            second_character_index < plan.events.size() &&
                second_character_index > inter_character_gap_index,
            "QRSS execution plans must continue to the second character after the inter-character gap");

        WsprTransmitter transmitter;
        // The public progress-reporting seam is the behavior under test.
        // Reaching it without hardware/backend configuration requires seeding
        // the already-compiled execution state directly.
        transmitter.current_execution_mode_ = wsprrypi::TransmissionMode::QRSS;
        transmitter.current_execution_plan_ = plan;
        transmitter.current_cw_message_ = payload.message;
        transmitter.current_cw_active_char_index_.store(
            -1,
            std::memory_order_release);
        transmitter.backendSetStateValue(WsprTransmitter::State::TRANSMITTING);

        for (std::size_t i = 0; i <= inter_character_gap_index; ++i)
        {
            transmitter.backendReportExecutionProgress(i);
        }

        require(
            transmitter.runtimeExecutionStatusSnapshot().cw_active_char_index == 0,
            "QRSS runtime status must still report the first character at the inter-character gap");
        require(
            transmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "QRSS progression simulation must remain active through the first inter-character gap");

        for (std::size_t i = inter_character_gap_index + 1U;
             i <= second_character_index;
             ++i)
        {
            transmitter.backendReportExecutionProgress(i);
        }

        require(
            transmitter.runtimeExecutionStatusSnapshot().cw_active_char_index == 1,
            "QRSS runtime status must report the second character once progression reaches it");
        require(
            transmitter.getState() == WsprTransmitter::State::TRANSMITTING,
            "QRSS progression simulation must remain active after advancing to the second character");
    }

    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::WSPR;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        PreparedWsprTransmission prepared;
        prepared.plan_type = "Type2Type3Paired";
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
            "slot-local WSPR payloads must still compile into exactly one frame");
    }

    std::cout << "qrss_execution_regression_test passed" << std::endl;
    return EXIT_SUCCESS;
}
