// SPDX-License-Identifier: MIT
#pragma once

#include "system_clock_frequency_estimate.hpp"
#include <cstdint>
#include <memory>
#include <atomic>
#include <functional>
#include <array>

namespace wsprrypi {

// The program counts clk_sys/2 iterations over ten complete PPS periods.
// It emits one word, then stops on PULL. No host operation occurs between
// interval endpoints, and its single output cannot fill an empty RX FIFO.
inline constexpr unsigned kRp1PpsPeriods = 10;
inline constexpr double kRp1PpsNominalHz = 200000000.0;
inline constexpr double kRp1PpsMaximumAgeSeconds = 30.0;
// Chrony's existing adapter refreshes every 120 seconds. This gate concerns
// cached reference-selection health, not the independently acquired RF sample.
inline constexpr double kRp1PpsMaximumReferenceReportAgeSeconds = 300.0;
inline constexpr std::array<std::uint16_t, 14> kRp1PpsProgram{
    0x80a0, // pull block: one trigger, then ten complete periods
    0x2020, // wait 0 pin 0
    0x20a0, // wait 1 pin 0
    0xa02b, // mov x, ~null
    0xe049, // set y, 9
    0x0046, // high: jmp x--, high_test
    0x00c5, // high_test: jmp pin, high
    0x00ca, // low: jmp pin, boundary
    0x0047, // jmp x--, low
    0x0007, // overflow: rejected by duration/count limits
    0x0085, // boundary: jmp y--, high
    0xa0c1, // mov isr, x
    0x8020, // push block: one word into an empty FIFO
    0x0000  // stop on pull; host never gates a measurement endpoint
};

struct Rp1PpsMeasurement {
    std::uint32_t iterations = 0;
    double edge_uncertainty_ns = 1000.0;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point completed_at{};
};

struct Rp1PpsResult {
    bool valid = false;
    double parent_error_ppm = 0;
    double additional_ppm = 0;
    double uncertainty_ppm = 0;
    std::string reason;
};

Rp1PpsResult evaluate_rp1_pps_measurement(const Rp1PpsMeasurement& measurement);
bool rp1_pps_reference_ready(const PPMProviderSnapshot& provider,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

class Rp1PpsCapture {
public:
    virtual ~Rp1PpsCapture() = default;
    virtual Rp1PpsMeasurement measure(const std::atomic<bool>& stop) = 0;
};

class Rp1PpsCalibration {
public:
    Rp1PpsCalibration();
    // Typed developer/test seam. Never exposed through CLI, INI, or the UI.
    explicit Rp1PpsCalibration(std::function<std::unique_ptr<Rp1PpsCapture>()> factory);
    ~Rp1PpsCalibration();
    Rp1PpsCalibration(const Rp1PpsCalibration&) = delete;
    Rp1PpsCalibration& operator=(const Rp1PpsCalibration&) = delete;
    // Explicit opt-in only. GPIO17 is the current supported input; it must
    // already be configured as a PPS input. This adapter never configures GPIO.
    SystemClockFrequencyEstimate snapshot(bool enabled,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wsprrypi
