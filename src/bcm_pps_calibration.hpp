// SPDX-License-Identifier: MIT
#pragma once
#include "system_clock_frequency_estimate.hpp"
#include "WSPR-Transmitter/src/legacy_gpio_clock_model.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>

namespace wsprrypi {
inline constexpr unsigned kBcmPpsPeriods = 120;
inline constexpr double kBcmPpsMaximumAgeSeconds = 150;
struct BcmPpsEdge { std::uint64_t low_us = 0, high_us = 0; };
class BcmPpsEdgeTracker {
public:
    explicit BcmPpsEdgeTracker(std::uint64_t maximum_bracket_us = 20)
        : maximum_bracket_us_(maximum_bracket_us) {}
    std::optional<BcmPpsEdge> observe(std::uint64_t before, bool high, std::uint64_t after);
private:
    std::optional<std::uint64_t> previous_, low_;
    std::uint64_t maximum_bracket_us_;
};
struct BcmPpsMeasurement {
    BcmPpsEdge first, last;
    std::string clock_identity;
    double nominal_timer_hz = 0;
    double parent_nominal_allowance_ppm = 0.002;
    LegacyGpioProcessorProfile processor = LegacyGpioProcessorProfile::Bcm2835;
    std::chrono::steady_clock::time_point completed_at{};
};
struct BcmPpsResult {
    bool valid = false;
    double additional_ppm = 0, uncertainty_ppm = 0;
    std::string reason;
};
struct BcmPpsBoardLayout {
    LegacyGpioProcessorProfile processor;
    std::uint64_t peripheral_base;
};
BcmPpsBoardLayout bcm_pps_board_layout(const std::string& compatible,
    unsigned child_cells, unsigned parent_cells, unsigned size_cells, const std::string& ranges);
// Pure validation seams used by the input adapter and hardware-free tests.
bool bcm_pps_inputs_valid(std::uint32_t gpio_fsel1, std::uint32_t clock_locks, unsigned input_pin = 17);
double bcm_pps_timer_rate(std::uint32_t control, std::uint32_t divider, double oscillator_hz);
bool bcm_pps_clock_tree_valid(std::istream&, LegacyGpioProcessorProfile, double timer_hz,
    std::uint64_t* reported_rf_hz = nullptr);
BcmPpsResult evaluate_bcm_pps_measurement(const BcmPpsMeasurement&);
class BcmPpsCapture {
public:
    virtual ~BcmPpsCapture() = default;
    virtual BcmPpsMeasurement measure(const std::atomic<bool>& stop) = 0;
};
class BcmPpsCalibration {
public:
    BcmPpsCalibration();
    explicit BcmPpsCalibration(std::function<std::unique_ptr<BcmPpsCapture>()> factory);
    explicit BcmPpsCalibration(std::function<std::unique_ptr<BcmPpsCapture>(unsigned)> factory);
    ~BcmPpsCalibration();
    BcmPpsCalibration(const BcmPpsCalibration&) = delete;
    BcmPpsCalibration& operator=(const BcmPpsCalibration&) = delete;
    SystemClockFrequencyEstimate snapshot(bool enabled,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(), unsigned input_pin = 17);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
