/**
 * @file si5351_backend_harness.cpp
 * @brief Temporary direct Si5351 backend test harness.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "execution_plan.hpp"
#include "wspr_transmit.hpp"
#include "wspr_transmit_backend_si5351.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <cstdlib>
#include <cmath>
#include <iomanip>

namespace
{
    struct HarnessOptions
    {
        std::string mode = "tone";
        double freq_a_hz = 14097100.0;
        double freq_b_hz = 14097102.0;
        double freq_c_hz = 14097104.0;
        double freq_d_hz = 14097106.0;
        int duration_ms = 2000;
        int power_level = 1;
        int i2c_bus = 1;
        std::uint8_t i2c_address = 0x60;
        bool dry_run = false;
        double ppm = 0.0;
        bool pll_only = false;
        bool integer_ms = false;
        bool burst = false;
        std::string scenario;
        std::string fade = "none";
    };

    static const char *log_level_name(WsprTransmitLogLevel level) noexcept
    {
        switch (level)
        {
            case WsprTransmitLogLevel::DEBUG:
                return "debug";
            case WsprTransmitLogLevel::INFO:
                return "info";
            case WsprTransmitLogLevel::WARN:
                return "warn";
            case WsprTransmitLogLevel::ERROR:
                return "error";
            case WsprTransmitLogLevel::FATAL:
                return "fatal";
        }

        return "unknown";
    }

    static void print_usage(const char *program)
    {
        std::cerr
            << "Usage: " << program << " [options]\n"
            << "  --mode tone|qrss|fskcw|dfcw|wspr\n"
            << "  --freq <hz>\n"
            << "  --freq-a <hz>\n"
            << "  --freq-b <hz>\n"
            << "  --freq-c <hz>\n"
            << "  --freq-d <hz>\n"
            << "  --duration-ms <ms>\n"
            << "  --power-level <1..4>\n"
            << "  --i2c-bus <n>\n"
            << "  --i2c-address <addr>\n"
            << "  --dry-run\n"
            << "  --pll-only (experimental compatible PLL transitions)\n"
            << "  --integer-ms (experimental common integer output divider)\n"
            << "  --burst (experimental parameter bursts and control cache)\n"
            << "  --ppm <ppm>\n"
            << "  --scenario carrier|transitions|keyed (bounded comparison)\n"
            << "  --fade none|linear|raised_cosine (keyed scenario only)\n";
    }

    static double parse_double_arg(
        const std::string& option,
        const char *value)
    {
        try
        {
            return std::stod(value);
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("Invalid value for " + option + ".");
        }
    }

    static int parse_int_arg(
        const std::string& option,
        const char *value)
    {
        try
        {
            return std::stoi(value);
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("Invalid value for " + option + ".");
        }
    }

    static unsigned parse_unsigned_arg(
        const std::string& option,
        const char *value)
    {
        try
        {
            return static_cast<unsigned>(std::stoul(value, nullptr, 0));
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("Invalid value for " + option + ".");
        }
    }

    static HarnessOptions parse_options(int argc, char **argv)
    {
        HarnessOptions options;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto require_value = [&]() -> const char * {
                if (i + 1 >= argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return argv[++i];
            };

            if (arg == "--help" || arg == "-h")
            {
                print_usage(argv[0]);
                std::exit(0);
            }
            else if (arg == "--mode")
            {
                options.mode = require_value();
            }
            else if (arg == "--freq")
            {
                options.freq_a_hz = parse_double_arg(arg, require_value());
            }
            else if (arg == "--freq-a")
            {
                options.freq_a_hz = parse_double_arg(arg, require_value());
            }
            else if (arg == "--freq-b")
            {
                options.freq_b_hz = parse_double_arg(arg, require_value());
            }
            else if (arg == "--freq-c")
            {
                options.freq_c_hz = parse_double_arg(arg, require_value());
            }
            else if (arg == "--freq-d")
            {
                options.freq_d_hz = parse_double_arg(arg, require_value());
            }
            else if (arg == "--duration-ms")
            {
                options.duration_ms = parse_int_arg(arg, require_value());
            }
            else if (arg == "--power-level")
            {
                options.power_level = parse_int_arg(arg, require_value());
            }
            else if (arg == "--i2c-bus")
            {
                options.i2c_bus = parse_int_arg(arg, require_value());
            }
            else if (arg == "--i2c-address")
            {
                const unsigned address =
                    parse_unsigned_arg(arg, require_value());
                if (address > 0x7f)
                    throw std::invalid_argument("I2C address out of range.");
                options.i2c_address = static_cast<std::uint8_t>(address);
            }
            else if (arg == "--burst") options.burst = true;
            else if (arg == "--integer-ms") options.integer_ms = true;
            else if (arg == "--pll-only") options.pll_only = true;
            else if (arg == "--ppm") options.ppm = parse_double_arg(arg, require_value());
            else if (arg == "--scenario") options.scenario = require_value();
            else if (arg == "--fade") options.fade = require_value();
            else if (arg == "--dry-run")
            {
                options.dry_run = true;
            }
            else
            {
                throw std::invalid_argument("Unknown option: " + arg);
            }
        }

        if (options.duration_ms <= 0)
            throw std::invalid_argument("Duration must be positive.");
        if (options.power_level < 1 || options.power_level > 4)
            throw std::invalid_argument("Power level must be 1..4.");

        if (!std::isfinite(options.ppm) || std::abs(options.ppm)>200)
            throw std::invalid_argument("PPM must be finite and within +/-200.");
        if (!options.scenario.empty() && options.scenario != "carrier" &&
            options.scenario != "transitions" && options.scenario != "keyed")
            throw std::invalid_argument("Unknown comparison scenario.");
        if (options.fade != "none" && options.fade != "linear" && options.fade != "raised_cosine")
            throw std::invalid_argument("Unknown fade.");
        if (options.fade != "none" && options.scenario != "keyed")
            throw std::invalid_argument("Fade requires keyed scenario.");
        return options;
    }

    class HarnessBridge : public IControllerBridge
    {
    public:
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
                    return stop_requested_.load(
                        std::memory_order_acquire);
                });
            return !interrupted;
        }

        void backendThrowIfStopRequested(const char *context) override
        {
            if (backendShouldStop())
                throw std::runtime_error(context);
        }

        void backendReportExecutionProgress(std::size_t) noexcept override
        {
        }

        void backendFireTransmitCallback(
            WsprTransmissionCallbackEvent event,
            WsprTransmitLogLevel level,
            const std::string& msg,
            double value) override
        {
            (void)event;
            (void)value;
            std::cout << "[si5351][" << log_level_name(level) << "] "
                      << msg << std::endl;
        }

        bool backendRestartCurrentConfiguration() override
        {
            return false;
        }

    private:
        std::atomic<WsprTransmitState> state_{WsprTransmitState::ENABLED};
        std::atomic<bool> stop_requested_{false};
        std::condition_variable stop_cv_{};
        std::mutex stop_mutex_{};
    };

    static wsprrypi::TransmissionMode parse_mode(const std::string& mode)
    {
        if (mode == "tone")
            return wsprrypi::TransmissionMode::TONE;
        if (mode == "qrss")
            return wsprrypi::TransmissionMode::QRSS;
        if (mode == "fskcw")
            return wsprrypi::TransmissionMode::FSKCW;
        if (mode == "dfcw")
            return wsprrypi::TransmissionMode::DFCW;
        if (mode == "wspr")
            return wsprrypi::TransmissionMode::WSPR;

        throw std::invalid_argument("Unsupported mode: " + mode);
    }

    static std::vector<double> mode_frequencies(
        const HarnessOptions& options)
    {
        if (options.mode == "tone" || options.mode == "qrss")
            return {options.freq_a_hz};
        if (options.mode == "fskcw" || options.mode == "dfcw")
            return {options.freq_a_hz, options.freq_b_hz};
        if (options.mode == "wspr")
        {
            return {
                options.freq_a_hz,
                options.freq_b_hz,
                options.freq_c_hz,
                options.freq_d_hz};
        }

        throw std::invalid_argument("Unsupported mode: " + options.mode);
    }

    static wsprrypi::ExecutionPlan build_plan(
        const HarnessOptions& options)
    {
        wsprrypi::ExecutionPlan plan;
        plan.id.value = 1;
        plan.request_id.value = 1;
        plan.mode = parse_mode(options.mode);
        plan.backend = wsprrypi::BackendKind::SI5351;
        plan.reference_frequency_hz = 27000000.0;
        plan.calibration.ppm = options.ppm;
        plan.duration_was_explicit = true;
        if (!options.scenario.empty())
        {
            if (!std::isfinite(options.freq_a_hz) ||
                (options.freq_a_hz != 7040100.0 && options.freq_a_hz != 144490500.0))
                throw std::invalid_argument("Comparison scenario requires the reviewed 40m or 2m carrier.");
            const bool transitions = options.scenario == "transitions";
            const bool keyed = options.scenario == "keyed";
            plan.mode = transitions ? wsprrypi::TransmissionMode::WSPR : wsprrypi::TransmissionMode::TONE;
            auto add = [&](int start_ms, int duration_ms, bool on, double frequency) {
                wsprrypi::RfEvent event;
                event.offset_from_start = std::chrono::milliseconds(start_ms);
                event.duration = std::chrono::milliseconds(duration_ms);
                event.type = on ? wsprrypi::RfEventType::SET_FREQUENCY : wsprrypi::RfEventType::RF_OFF;
                event.frequency_hz = on ? frequency : 0;
                event.rf_on = on;
                if (on && keyed && options.fade != "none") {
                    event.envelope.fade_shape = options.fade == "linear" ? wsprrypi::FadeShape::LINEAR : wsprrypi::FadeShape::RAISED_COSINE;
                    event.envelope.fade_in = std::chrono::milliseconds(20);
                    event.envelope.fade_out = std::chrono::milliseconds(20);
                    event.envelope.fade_slice = std::chrono::milliseconds(2);
                }
                plan.events.push_back(event);
            };
            add(0,2000,false,0);
            if (transitions) {
                for (int i=0;i<16;++i)
                    add(2000+i*500,500,true,options.freq_a_hz+(i%4)*1.46484375);
                add(10000,2000,false,0);
            } else if (keyed) {
                for(int i=0;i<8;++i) {
                    add(2000+i*1000,500,true,options.freq_a_hz);
                    add(2500+i*1000,500,false,0);
                }
                add(10000,2000,false,0);
            } else {
                for(int i=0;i<3;++i) {
                    add(2000+i*4000,2000,true,options.freq_a_hz);
                    add(4000+i*4000,2000,false,0);
                }
            }
            plan.summary.total_duration = std::chrono::milliseconds(transitions||keyed?12000:14000);
            plan.summary.event_count = plan.events.size();
            plan.summary.min_frequency_hz = options.freq_a_hz;
            plan.summary.max_frequency_hz = options.freq_a_hz+(transitions?4.39453125:0);
            return plan;
        }

        const std::vector<double> frequencies = mode_frequencies(options);
        const auto total_duration =
            std::chrono::milliseconds(options.duration_ms);
        const auto segment_duration =
            total_duration / static_cast<int>(frequencies.size());

        for (std::size_t i = 0; i < frequencies.size(); ++i)
        {
            wsprrypi::RfEvent event;
            event.offset_from_start = segment_duration *
                static_cast<int>(i);
            event.duration = segment_duration;
            event.type = wsprrypi::RfEventType::SET_FREQUENCY;
            event.frequency_hz = frequencies[i];
            event.rf_on = true;
            plan.events.push_back(event);
        }

        wsprrypi::RfEvent off_event;
        off_event.offset_from_start = total_duration;
        off_event.duration = std::chrono::nanoseconds{0};
        off_event.type = wsprrypi::RfEventType::RF_OFF;
        off_event.frequency_hz = 0.0;
        off_event.rf_on = false;
        plan.events.push_back(off_event);

        plan.summary.total_duration = total_duration;
        plan.summary.event_count = plan.events.size();
        plan.summary.min_frequency_hz = frequencies.front();
        plan.summary.max_frequency_hz = frequencies.front();
        for (const double frequency_hz : frequencies)
        {
            if (frequency_hz < plan.summary.min_frequency_hz)
                plan.summary.min_frequency_hz = frequency_hz;
            if (frequency_hz > plan.summary.max_frequency_hz)
                plan.summary.max_frequency_hz = frequency_hz;
        }

        return plan;
    }

    static WsprSi5351Backend::Config build_backend_config(
        const HarnessOptions& options)
    {
        WsprSi5351Backend::Config config;
        config.device.i2c_bus = options.i2c_bus;
        config.device.i2c_address = options.i2c_address;
        config.device.reference_hz = 27000000;
        config.device.reference_source =
            Si5351Device::ReferenceSource::EXTERNAL_TCXO;
        config.planner.reference_hz = 27000000;
        config.planner.tx_output = Si5351Device::Output::CLK0;
        config.power_level = options.power_level;
        config.dry_run = options.dry_run;
        config.pll_only_updates = options.pll_only;
        config.planner.prefer_integer_multisynth = options.integer_ms;
        config.device.optimize_register_writes = options.burst;
        return config;
    }

    static void print_summary(const HarnessOptions& options, const wsprrypi::ExecutionPlan& plan)
    {
        std::vector<double> frequencies;
        for (const auto& event : plan.events)
            if (event.frequency_hz > 0 &&
                std::find(frequencies.begin(), frequencies.end(), event.frequency_hz) == frequencies.end())
                frequencies.push_back(event.frequency_hz);
        const auto duration = plan.events.empty() ? std::chrono::nanoseconds::zero()
            : plan.events.back().offset_from_start + plan.events.back().duration;
        std::cout << std::setprecision(17) << "Si5351 backend harness\n";
        std::cout << "  Scenario: " << options.scenario << " fade=" << options.fade << " ppm=" << options.ppm << "\n";
        std::cout << "  Mode:        " << (options.scenario.empty() ? options.mode : (options.scenario == "transitions" ? "wspr" : "tone")) << "\n";
        std::cout << "  Frequencies:";
        for (const double frequency_hz : frequencies)
            std::cout << " " << frequency_hz;
        std::cout << " Hz\n";
        std::cout << "  Duration:    " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << " ms\n";
        std::cout << "  Power level: " << options.power_level << "\n";
        std::cout << "  Dry run:     "
                  << (options.dry_run ? "yes" : "no") << "\n";
        std::cout << "  I2C:         /dev/i2c-" << options.i2c_bus
                  << " addr 0x" << std::hex
                  << static_cast<unsigned>(options.i2c_address)
                  << std::dec << "\n";
        if (options.dry_run)
        {
            std::cout << "  Hardware:    I2C and RF output disabled\n";
        }
        std::cout << std::flush;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const HarnessOptions options = parse_options(argc, argv);
        const wsprrypi::ExecutionPlan plan = build_plan(options);
        print_summary(options, plan);
        if (options.dry_run)
        {
            std::cout << "[si5351][info] Harness dry-run mode enabled; "
                      << "hardware access will be skipped." << std::endl;
        }

        HarnessBridge bridge;
        WsprSi5351Backend backend(
            bridge,
            build_backend_config(options));
        const wsprrypi::BackendCompileResult configure_result =
            backend.configure(
                plan,
                wsprrypi::BackendExecutionInputs{
                    options.power_level,
                    0});
        std::cout << "Configure: "
                  << (configure_result.ok ? "ok" : "failed") << "\n";
        if (!configure_result.error.empty())
            std::cout << "  Error: " << configure_result.error << "\n";
        if (!configure_result.ok)
            return 2;
        std::cout << std::flush;

        const wsprrypi::ExecutionResult execute_result =
            backend.execute(plan);
        std::cout << "Execute:   "
                  << (execute_result.ok ? "ok" : "failed") << "\n";
        std::cout << "  Stopped: " << (execute_result.stopped ? "yes" : "no")
                  << "\n";
        std::cout << "  Faulted: " << (execute_result.faulted ? "yes" : "no")
                  << "\n";
        if (!execute_result.error.empty())
            std::cout << "  Error: " << execute_result.error << "\n";

        return execute_result.ok ? 0 : 3;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        print_usage(argv[0]);
        return 1;
    }
}
