#include "execution_plan_compiler.hpp"
#include "wspr_transmit.hpp"
#include "wspr_transmit_backend_si5351.hpp"
#include "wspr_reference_adapter.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

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

    class ProbeBridge : public IControllerBridge
    {
    public:
        explicit ProbeBridge(const wsprrypi::ExecutionPlan& plan)
            : plan_(plan)
        {
        }

        WsprTransmitState backendStateValue() const noexcept override
        {
            return state_.load(std::memory_order_acquire);
        }

        void backendSetStateValue(WsprTransmitState state) noexcept override
        {
            state_.store(state, std::memory_order_release);
        }

        bool backendShouldStop() const noexcept override
        {
            return stop_requested_.load(std::memory_order_acquire);
        }

        void backendSignalStopRequest() noexcept override
        {
            stop_requested_.store(true, std::memory_order_release);
            stop_cv_.notify_all();
        }

        void backendRequestStopTxNoJoin() noexcept override
        {
            backendSignalStopRequest();
        }

        bool backendWaitInterruptableFor(
            std::chrono::nanoseconds duration) override
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            const bool interrupted = stop_cv_.wait_for(
                lock,
                duration,
                [this]
                {
                    return stop_requested_.load(std::memory_order_acquire);
                });
            return !interrupted;
        }

        void backendThrowIfStopRequested(const char* context) override
        {
            if (backendShouldStop())
                throw std::runtime_error(context);
        }

        void backendReportExecutionProgress(std::size_t event_index) noexcept override
        {
            visited_event_indexes.push_back(event_index);
            if (event_index >= plan_.events.size())
                return;

            const int char_index = plan_.events[event_index].message_char_index;
            if (char_progression.empty() || char_progression.back() != char_index)
            {
                char_progression.push_back(char_index);
            }
        }

        void backendFireTransmitCallback(
            WsprTransmissionCallbackEvent,
            WsprTransmitLogLevel,
            const std::string&,
            double) override
        {
        }

        bool backendRestartCurrentConfiguration() override
        {
            return false;
        }

        const wsprrypi::ExecutionPlan& plan_;
        std::vector<std::size_t> visited_event_indexes{};
        std::vector<int> char_progression{};

    private:
        std::atomic<WsprTransmitState> state_{WsprTransmitState::ENABLED};
        std::atomic<bool> stop_requested_{false};
        std::condition_variable stop_cv_{};
        std::mutex stop_mutex_{};
    };

    wsprrypi::BackendCompileResult configure_si5351_dry_run(
        WsprSi5351Backend& backend,
        const wsprrypi::ExecutionPlan& plan)
    {
        wsprrypi::BackendExecutionInputs inputs;
        inputs.power_level = 1;
        inputs.tx_gpio = 4;
        return backend.configure(plan, inputs);
    }

    WsprSi5351Backend::Config make_dry_run_config()
    {
        WsprSi5351Backend::Config config;
        config.device.i2c_bus = 1;
        config.device.i2c_address = 0x60;
        config.device.reference_hz = 27000000;
        config.planner.reference_hz = 27000000;
        config.planner.tx_output = Si5351Device::Output::CLK0;
        config.power_level = 1;
        config.dry_run = true;
        return config;
    }

    wsprrypi::ExecutionPlan compile_qrss_plan(const std::string& message)
    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::QRSS;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        wsprrypi::QrssPayload payload;
        payload.message = message;
        payload.frequency_hz = 7038600.0;
        payload.timing.dot = std::chrono::milliseconds(5);
        payload.timing.dash = std::chrono::milliseconds(15);
        payload.timing.intra_element_gap = std::chrono::milliseconds(5);
        payload.timing.inter_character_gap = std::chrono::milliseconds(15);
        payload.timing.inter_word_gap = std::chrono::milliseconds(35);
        request.payload = payload;

        return wsprrypi::ExecutionPlanCompiler{}.compile(request);
    }

    wsprrypi::ExecutionPlan compile_fskcw_plan(const std::string& message)
    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::FSKCW;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        wsprrypi::FskcwPayload payload;
        payload.message = message;
        payload.mark_frequency_hz = 7038605.0;
        payload.space_frequency_hz = 7038600.0;
        payload.timing.dot = std::chrono::milliseconds(5);
        payload.timing.dash = std::chrono::milliseconds(15);
        payload.timing.intra_element_gap = std::chrono::milliseconds(5);
        payload.timing.inter_character_gap = std::chrono::milliseconds(15);
        payload.timing.inter_word_gap = std::chrono::milliseconds(35);
        request.payload = payload;

        return wsprrypi::ExecutionPlanCompiler{}.compile(request);
    }

    wsprrypi::ExecutionPlan compile_dfcw_plan(const std::string& message)
    {
        wsprrypi::TransmissionRequest request;
        request.mode = wsprrypi::TransmissionMode::DFCW;
        request.output.backend = wsprrypi::BackendKind::SI5351;
        request.output.output = wsprrypi::ClockSource::SI5351_CLK0;
        request.output.gpio = 4;

        wsprrypi::DfcwPayload payload;
        payload.message = message;
        payload.dot_frequency_hz = 7038600.0;
        payload.dash_frequency_hz = 7038605.0;
        payload.timing.dot = std::chrono::milliseconds(5);
        payload.timing.dash = std::chrono::milliseconds(17);
        payload.timing.intra_element_gap = std::chrono::milliseconds(2);
        payload.timing.inter_character_gap = std::chrono::milliseconds(7);
        payload.timing.inter_word_gap = std::chrono::milliseconds(11);
        request.payload = payload;

        return wsprrypi::ExecutionPlanCompiler{}.compile(request);
    }

    std::size_t first_event_for_char(
        const std::vector<std::size_t>& visited_event_indexes,
        const wsprrypi::ExecutionPlan& plan,
        int char_index)
    {
        for (const std::size_t event_index : visited_event_indexes)
        {
            if (event_index < plan.events.size() &&
                plan.events[event_index].message_char_index == char_index)
            {
                return event_index;
            }
        }

        return plan.events.size();
    }

    struct ExecutionProbeResult
    {
        wsprrypi::ExecutionResult result{};
        std::vector<std::size_t> visited_event_indexes{};
        std::vector<int> char_progression{};
        std::chrono::steady_clock::duration elapsed{};
    };

    ExecutionProbeResult execute_si5351_dry_run(
        const wsprrypi::ExecutionPlan& plan)
    {
        ProbeBridge bridge(plan);
        WsprSi5351Backend backend(bridge, make_dry_run_config());
        const wsprrypi::BackendCompileResult compile_result =
            configure_si5351_dry_run(backend, plan);
        require(
            compile_result.ok,
            "Si5351 dry-run backend must configure the execution plan");

        const auto started = std::chrono::steady_clock::now();
        const wsprrypi::ExecutionResult execute_result = backend.execute(plan);
        const auto finished = std::chrono::steady_clock::now();

        return ExecutionProbeResult{
            execute_result,
            bridge.visited_event_indexes,
            bridge.char_progression,
            finished - started};
    }

    void require_full_duration(
        const wsprrypi::ExecutionPlan& plan,
        std::chrono::steady_clock::duration elapsed,
        const std::string& context)
    {
        const auto planned =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                plan.summary.total_duration);
        const auto tolerance = std::chrono::milliseconds(3);
        require(
            elapsed + tolerance >= planned,
            context + " must remain active for the full planned duration");
    }
}

int main()
{
    {
        const wsprrypi::ExecutionPlan plan = compile_qrss_plan("AT");

        std::size_t inter_character_gap_index = plan.events.size();
        std::size_t second_character_index = plan.events.size();
        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            const auto& event = plan.events[i];
            if (event.message_char_index == 0)
            {
                if (!event.rf_on)
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

        const ExecutionProbeResult probe = execute_si5351_dry_run(plan);

        require(
            probe.result.ok && !probe.result.stopped && !probe.result.faulted,
            "QRSS dry-run execution must complete without an early stop");
        require(
            !probe.visited_event_indexes.empty() &&
                probe.visited_event_indexes.back() == (plan.events.size() - 1U),
            "QRSS dry-run execution must visit the full execution plan");
        require(
            first_event_for_char(probe.visited_event_indexes, plan, 1) ==
                second_character_index,
            "QRSS dry-run execution must progress to the second character after the inter-character gap");
        require(
            probe.char_progression.size() >= 2U &&
                probe.char_progression[0] == 0 &&
                probe.char_progression[1] == 1,
            "QRSS dry-run execution must report character progression from the first to the second character");
        require_full_duration(
            plan,
            probe.elapsed,
            "QRSS \"AT\" execution");
    }

    {
        const wsprrypi::ExecutionPlan plan = compile_qrss_plan("ATA");
        const ExecutionProbeResult probe = execute_si5351_dry_run(plan);

        require(
            probe.result.ok && !probe.result.stopped && !probe.result.faulted,
            "Longer QRSS dry-run execution must complete without truncation");
        require(
            !probe.visited_event_indexes.empty() &&
                probe.visited_event_indexes.back() == (plan.events.size() - 1U),
            "Longer QRSS dry-run execution must visit the full plan");
        require(
            probe.char_progression.size() >= 3U &&
                probe.char_progression[0] == 0 &&
                probe.char_progression[1] == 1 &&
                probe.char_progression[2] == 2,
            "Longer QRSS dry-run execution must reach the final character");
        require_full_duration(
            plan,
            probe.elapsed,
            "QRSS \"ATA\" execution");
    }

    {
        const wsprrypi::ExecutionPlan plan = compile_fskcw_plan("AT");
        const ExecutionProbeResult probe = execute_si5351_dry_run(plan);

        require(
            probe.result.ok && !probe.result.stopped && !probe.result.faulted,
            "FSKCW dry-run execution must complete without an early stop");
        require(
            !probe.visited_event_indexes.empty() &&
                probe.visited_event_indexes.back() == (plan.events.size() - 1U),
            "FSKCW dry-run execution must visit the full execution plan");
        require(
            probe.char_progression.size() >= 2U &&
                probe.char_progression[0] == 0 &&
                probe.char_progression[1] == 1,
            "FSKCW dry-run execution must reach the second character");
        require_full_duration(
            plan,
            probe.elapsed,
            "FSKCW \"AT\" execution");
    }

    {
        const wsprrypi::ExecutionPlan plan = compile_dfcw_plan("LE E");

        bool saw_dot = false;
        bool saw_dash = false;
        bool saw_intra_gap = false;
        bool saw_character_gap = false;
        bool saw_word_gap = false;

        for (const auto& event : plan.events)
        {
            if (event.rf_on)
            {
                require(
                    event.duration == std::chrono::milliseconds(5),
                    "DFCW dot and dash symbols must both use dot duration");
                if (event.frequency_hz == 7038600.0)
                    saw_dot = true;
                if (event.frequency_hz == 7038605.0)
                    saw_dash = true;
            }
            else
            {
                require(
                    event.frequency_hz == 0.0,
                    "DFCW spacing gaps must be RF-off events");
                if (event.duration == std::chrono::milliseconds(2))
                    saw_intra_gap = true;
                if (event.duration == std::chrono::milliseconds(7))
                    saw_character_gap = true;
                if (event.duration == std::chrono::milliseconds(11))
                    saw_word_gap = true;
            }
        }

        require(saw_dot, "DFCW plans must emit dot-frequency symbols");
        require(saw_dash, "DFCW plans must emit dash-frequency symbols");
        require(
            saw_intra_gap,
            "DFCW plans must use the DFCW intra-element gap duration");
        require(
            saw_character_gap,
            "DFCW plans must use the DFCW inter-character gap duration");
        require(
            saw_word_gap,
            "DFCW plans must use the DFCW inter-word gap duration");
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
