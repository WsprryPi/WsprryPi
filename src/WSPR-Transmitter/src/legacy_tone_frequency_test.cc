// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "gpio_frequency_plan.hpp"
#include "legacy_gpio_clock_model.hpp"
#include "execution_plan_compiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "legacy_tone_frequency_test: " << message << '\n';
        std::exit(1);
    }
}

void test_shared_mode_frequencies()
{
    using namespace std::chrono_literals;
    constexpr double carrier = 14097100.0;
    const wsprrypi::MorseTiming timing{1ms, 3ms, 1ms, 3ms, 7ms};
    for (const auto backend : {wsprrypi::BackendKind::RPI_CLOCK_GPIO,
             wsprrypi::BackendKind::RP1_GPCLK, wsprrypi::BackendKind::SI5351,
             wsprrypi::BackendKind::SIMULATED, wsprrypi::BackendKind::WTP})
    for (const auto mode : {wsprrypi::TransmissionMode::TONE,
             wsprrypi::TransmissionMode::QRSS, wsprrypi::TransmissionMode::FSKCW,
             wsprrypi::TransmissionMode::DFCW})
    {
        wsprrypi::TransmissionRequest request;
        request.output.backend = backend;
        request.mode = mode;
        double high = carrier;
        if (mode == wsprrypi::TransmissionMode::TONE)
            request.payload = wsprrypi::TonePayload{carrier, 1ms, {}};
        else if (mode == wsprrypi::TransmissionMode::QRSS)
            request.payload = wsprrypi::QrssPayload{"ET", carrier, timing, {}};
        else if (mode == wsprrypi::TransmissionMode::FSKCW)
        {
            high = carrier + 5.0;
            request.payload = wsprrypi::FskcwPayload{"ET", high, carrier, timing, {}};
        }
        else
        {
            high = carrier + 5.0;
            request.payload = wsprrypi::DfcwPayload{"ET", carrier, high, timing, {}};
        }
        const auto plan = wsprrypi::ExecutionPlanCompiler{}.compile(request);
        bool saw_low = false;
        bool saw_high = false;
        for (const auto& event : plan.events)
        {
            if (!event.rf_on) continue;
            expect(event.frequency_hz == carrier || event.frequency_hz == high,
                   "shared plans must preserve requested carriers across backends and CW modes");
            saw_low |= event.frequency_hz == carrier;
            saw_high |= event.frequency_hz == high;
        }
        expect(saw_low && saw_high, "shared plan must contain every expected RF frequency");
        if (mode != wsprrypi::TransmissionMode::TONE)
        {
            WsprTransmissionPlan compat;
            compat.frequency_hz = plan.reference_frequency_hz;
            compat.tone_spacing_hz = mode == wsprrypi::TransmissionMode::QRSS
                ? 1.46484375 : high - carrier;
            expect(compat.symbolFrequencyHz(0) == carrier &&
                       (high == carrier || compat.symbolFrequencyHz(1) == high),
                   "CW compatibility tables must retain their existing reference compensation");
            expect(gpioPlanDividerTable(500e6, compat).applied_frequency_hz ==
                       compat.frequency_hz,
                   "ordinary CW tables must retain their reference frequency");
        }
    }
}

void test_legacy_tone_frequency()
{
    TransmissionRequest request;
    request.mode = TransmissionMode::TONE;
    request.power_level = 3;
    request.tx_gpio = 20;
    for (const auto profile : {wsprrypi::LegacyGpioProcessorProfile::Bcm2835,
                              wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837,
                              wsprrypi::LegacyGpioProcessorProfile::Bcm2711})
    for (const double ppm : {-100.0, 0.0, 37.5})
    for (const double frequency : {137500.0, 475700.0, 1838100.0, 3570100.0,
             5288700.0, 7040100.0, 10140200.0, 14097100.0, 18106100.0,
             21096100.0, 24926100.0, 28126100.0, 50000000.0})
    {
        request.actual_rf_frequency_hz = frequency;
        request.ppm = ppm;
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            const auto plan = makeLegacyGpioTransmissionPlan(request);
            expect(plan.frequency_hz == frequency && plan.tone_spacing_hz == 0.0 &&
                       plan.symbolFrequencyHz(0) == frequency,
                   "legacy TONE must place the emitter's symbol zero at requested RF");
            expect(plan.ppm == ppm && plan.power_level == 3 && plan.tx_gpio == 20 &&
                       plan.symbolCount() == 0,
                   "TONE adaptation must preserve correction, power, GPIO and symbol count");
            const auto clock = wsprrypi::selectLegacyGpioClockForAdditionalCorrection(profile,
                plan.symbolFrequencyHz(0), plan.symbolFrequencyHz(3), plan.ppm);
            expect((profile == wsprrypi::LegacyGpioProcessorProfile::Bcm2711 &&
                        frequency == 137500.0)
                       ? clock.model.parent == wsprrypi::LegacyGpioClockParent::Oscillator
                       : clock.model.parent == wsprrypi::LegacyGpioClockParent::PllD,
                   "TONE must retain the correct representable RF parent");
            const auto floor_word = static_cast<std::uint32_t>(
                std::floor(clock.corrected_rate_hz / frequency * 4096.0));
            if ((floor_word >> 12) != ((floor_word + 1) >> 12))
            {
                bool rejected = false;
                try { (void)gpioPlanDividerTable(clock.corrected_rate_hz, plan); }
                catch (const std::out_of_range&) { rejected = true; }
                expect(rejected && request.actual_rf_frequency_hz == frequency,
                       "calibrated TONE at an unsafe divider boundary must reject without changing RF");
                continue;
            }
            const auto table = gpioPlanDividerTable(clock.corrected_rate_hz, plan);
            expect(table.applied_frequency_hz == frequency,
                   "TONE table must never substitute an internal center for requested RF");
            for (std::size_t symbol = 0; symbol < 4; ++symbol)
            {
                expect(table.words[2 * symbol] == table.words[0] &&
                           table.words[2 * symbol + 1] == table.words[1],
                       "every TONE table entry must use the same carrier divider pair");
            }
            const double lower_hz = clock.corrected_rate_hz * 4096.0 / table.words[0];
            const double upper_hz = clock.corrected_rate_hz * 4096.0 / table.words[1];
            expect(lower_hz <= frequency + 1e-8 && upper_hz >= frequency - 1e-8 &&
                       (table.words[0] >> 12) == (table.words[1] >> 12),
                   "actual TONE divider words must safely bracket the requested carrier");
            const double ratio = (upper_hz - plan.symbolFrequencyHz(0)) /
                (upper_hz - lower_hz);
            const auto lower_clocks = std::llround(std::clamp(ratio, 0.0, 1.0) * 1000000);
            const double mean_hz = (lower_hz * lower_clocks +
                upper_hz * (1000000 - lower_clocks)) / 1000000;
            expect(std::fabs(mean_hz - frequency) <=
                       (upper_hz - lower_hz) / 2000000 + 1e-8,
                   "dither arithmetic must target requested RF within its count resolution");
            request.actual_rf_frequency_hz = table.applied_frequency_hz;
        }
    }

    for (const double source : {54e6, 500e6, 750e6, 500e6 * 1.0000375})
    {
        // Immediately above an integer-divisor carrier the two words
        // straddle that integer, even though a generic range check passes.
        request.actual_rf_frequency_hz = source / (32.0 - 0.5 / 4096.0);
        const auto crossing = makeLegacyGpioTransmissionPlan(request);
        bool rejected = false;
        try { (void)gpioPlanDividerTable(source, crossing); }
        catch (const std::out_of_range&) { rejected = true; }
        expect(rejected, "exact TONE must reject an unsafe integer-crossing pair, not shift RF");

        for (const double divisor : {32.0, 32.0 + 0.5 / 4096.0,
                                      32.0 - 1.5 / 4096.0})
        {
            request.actual_rf_frequency_hz = source / divisor;
            const auto plan = makeLegacyGpioTransmissionPlan(request);
            expect(gpioPlanDividerTable(source, plan).applied_frequency_hz ==
                       request.actual_rf_frequency_hz,
                   "safe exact and adjacent divider-boundary tones must not shift");
        }
    }

    request.mode = TransmissionMode::WSPR;
    request.actual_rf_frequency_hz = 14097100.0;
    const auto wspr = makeLegacyGpioTransmissionPlan(request);
    const double expected[] = {14097097.802734375, 14097099.267578125,
                               14097100.732421875, 14097102.197265625};
    for (std::uint32_t symbol = 0; symbol < 4; ++symbol)
        expect(wspr.symbolFrequencyHz(symbol) == expected[symbol],
               "legacy WSPR must retain all four centered symbol frequencies");

    // Independently reproduce the old table calculation at ordinary and
    // integer-boundary centers; the extraction must not alter WSPR words.
    for (const double source : {54e6, 500e6, 750e6})
    for (const double center : {3570100.0, source / 32.0})
    {
        auto plan = wspr;
        plan.frequency_hz = center;
        constexpr double spacing = 1.46484375;
        const double div_lo = (std::floor(source / (center - 1.5 * spacing) * 4096) + 1) / 4096;
        const double div_hi = std::floor(source / (center + 1.5 * spacing) * 4096) / 4096;
        const double applied = std::floor(div_lo) == std::floor(div_hi)
            ? center : source / std::floor(div_lo) - 1.6 * spacing;
        const auto table = gpioPlanDividerTable(source, plan);
        expect(table.applied_frequency_hz == applied,
               "WSPR hardware-limit center handling must remain unchanged");
        for (std::size_t i = 0; i < 8; ++i)
        {
            const double hz = applied - 1.5 * spacing + (i / 2) * spacing;
            const auto word = static_cast<std::uint32_t>(
                std::floor(source / hz * 4096) + (i % 2 == 0 ? 1 : 0));
            expect(table.words[i] == word, "WSPR table words must match the original arithmetic");
        }
    }
}

}

int main()
{
    test_legacy_tone_frequency();
    test_shared_mode_frequencies();
    std::cout << "legacy_tone_frequency_test: passed\n";
}
