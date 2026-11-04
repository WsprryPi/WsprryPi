// SPDX-License-Identifier: MIT
#include "rp1_pps_calibration.hpp"
#include "PPM-Manager/src/ppm_manager.hpp"
#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace wsprrypi;
using Clock = std::chrono::steady_clock;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
Rp1PpsMeasurement measurement(double ppm) {
    Rp1PpsMeasurement m;
    m.iterations = std::uint32_t(std::llround((2000000000.L * (1 + ppm / 1e6L) - 21) / 2));
    m.completed_at = Clock::now();
    m.started_at = m.completed_at - std::chrono::seconds(10);
    return m;
}

// Execute the actual shipped words against a much shorter synthetic PPS
// period. Compare independently known electrical edge times with the decoded
// counter result, across input phase, duty cycle, and odd/even clock periods.
void instruction_model() {
    for (unsigned period : {99U, 100U, 101U, 1000U})
    for (unsigned width : {4U, 5U, 17U, 30U})
    for (unsigned phase : {4U, 5U, 19U}) {
        std::uint32_t x = 0, y = 0, isr = 0;
        unsigned pc = 0, edges = 0, start = 0, finish = 0, pushes = 0;
        bool trigger = true, counting = false;
        for (unsigned cycle = 0; cycle < period * 12; ++cycle) {
            const bool pin = cycle >= phase && (cycle - phase) % period < width;
            const auto instruction = kRp1PpsProgram[pc];
            const unsigned opcode = instruction >> 13, arg = instruction & 31;
            unsigned next = pc + 1;
            if (opcode == 0) {
                const unsigned condition = (instruction >> 5) & 7;
                bool take = condition == 0;
                if (condition == 2) take = x-- != 0;
                if (condition == 4) take = y-- != 0;
                if (condition == 6) {
                    take = pin;
                    if (pc == 7 && take) { ++edges; finish = cycle; }
                }
                if (take) next = arg;
            } else if (opcode == 1) {
                const bool polarity = instruction & 128;
                if (pin != polarity) next = pc;
                else if (polarity) { start = cycle; counting = true; }
            } else if (opcode == 4) {
                if (instruction & 128) {
                    if (!trigger) next = pc;
                    trigger = false;
                } else { ++pushes; require(counting && edges == 10, "exactly ten periods before push"); }
            } else if (opcode == 5) {
                if (pc == 3) x = UINT32_MAX;
                else isr = x;
            } else if (opcode == 7) y = arg;
            else throw std::runtime_error("Unexpected PIO instruction");
            pc = next;
            if (pushes && pc == 0) break;
        }
        const auto cycles = 2ULL * std::uint32_t(~isr) + 21;
        require(pushes == 1, "one FIFO word and no host interaction within interval");
        require(cycles == finish - start, "instruction-cycle formula must match actual sampling endpoints");
        require(std::abs(double(cycles) - 10 * period) <= 2, "capture quantization is bounded across duty and phase");
    }
}

struct Queue {
    std::mutex mutex;
    std::deque<Rp1PpsMeasurement> samples;
    bool fail = false;
    std::atomic<unsigned> destroyed{0};
};
struct FakeCapture : Rp1PpsCapture {
    std::shared_ptr<Queue> queue;
    explicit FakeCapture(std::shared_ptr<Queue> value) : queue(std::move(value)) {}
    ~FakeCapture() override { ++queue->destroyed; }
    Rp1PpsMeasurement measure(const std::atomic<bool>& stop) override {
        for (;;) {
            { std::lock_guard lock(queue->mutex);
              if (queue->fail) throw std::runtime_error("reference lost");
              if (!queue->samples.empty()) {
                auto m = queue->samples.front(); queue->samples.pop_front(); return m;
              }
            }
            if (stop) throw std::runtime_error("stopped");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};
template<class Predicate> SystemClockFrequencyEstimate await_sample(Rp1PpsCalibration& c, Predicate p) {
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline) {
        auto s = c.snapshot(true);
        if (p(s)) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("capture worker deadline");
}

void worker_and_selection() {
    auto queue = std::make_shared<Queue>();
    Rp1PpsCalibration c([queue] { return std::make_unique<FakeCapture>(queue); });
    { std::lock_guard lock(queue->mutex); queue->samples.push_back(measurement(-42.3)); }
    auto first = await_sample(c, [](const auto& s) { return s.retained_source_samples == 1; });
    require(first.qualification == FrequencyEstimateQualification::Converging, "one measurement cannot qualify");
    auto fallback = select_gpio_frequency_correction(true, 7, -3, first);
    require(fallback.effective_ppm == -3 && fallback.mode == GpioCorrectionMode::FixedManual, "acquisition uses manual without residual");
    { std::lock_guard lock(queue->mutex);
      queue->samples.push_back(measurement(-42.31)); queue->samples.push_back(measurement(-42.29)); }
    auto s = await_sample(c, [](const auto& s) { return s.qualification == FrequencyEstimateQualification::Qualified; });
    const auto selected = select_gpio_frequency_correction(true, 0.1, -3, s);
    const auto total = compose_gpio_frequency_correction(-46.245, selected);
    require(total.valid && std::abs(total.effective_ppm - (-42.19)) < 0.002, "intrinsic applies once; direct replaces host; residual once");
    { std::lock_guard lock(queue->mutex); queue->samples.push_back(measurement(-41.9)); }
    auto unsettled = await_sample(c, [](const auto& value) { return value.retained_source_samples == 4; });
    require(unsettled.qualification == FrequencyEstimateQualification::Converging,
            "new rate instability immediately retires qualification");
    require(select_gpio_frequency_correction(true, 0.1, -3, unsettled).mode == GpioCorrectionMode::FixedManual,
            "unstable PPS does not retain a formerly qualified value");
    { std::lock_guard lock(queue->mutex);
      for (unsigned i = 0; i < 3; ++i) queue->samples.push_back(measurement(-42.3)); }
    await_sample(c, [](const auto& value) { return value.qualification == FrequencyEstimateQualification::Qualified; });
    auto stale = c.snapshot(true, Clock::now() + std::chrono::seconds(31));
    require(!stale.frequency_ppm && stale.qualification == FrequencyEstimateQualification::Unavailable, "monotonic expiry never reuses old estimates");
    require(select_gpio_frequency_correction(true, 0.1, -3, stale).effective_ppm == -3, "expiry selects manual");
    auto backward = c.snapshot(true, Clock::time_point{});
    require(!backward.frequency_ppm, "backward clock cannot grant freshness");
    { std::lock_guard lock(queue->mutex); queue->fail = true; }
    await_sample(c, [](const auto& s) { return s.reason == "reference lost"; });
    require(queue->destroyed >= 1, "capture errors release resources");
    { std::lock_guard lock(queue->mutex); queue->fail = false; queue->samples.push_back(measurement(-42.3)); }
    auto recovered = await_sample(c, [](const auto& s) { return s.retained_source_samples == 1; });
    require(recovered.qualification == FrequencyEstimateQualification::Converging, "reacquisition starts a new qualification history");
    c.snapshot(false);
    require(queue->destroyed >= 2, "disable closes the owned input capture");
}

int main() {
    try {
        instruction_model();
        PPMProviderSnapshot reference;
        const auto now = Clock::now();
        reference.age_reference_time = now;
        reference.age_seconds = 16;
        reference.synchronized = reference.selected_source = reference.leap_normal = true;
        reference.source_provenance = "GNSS/PPS or other local reference clock";
        reference.source_signature = "*:PPS";
        require(rp1_pps_reference_ready(reference, now + std::chrono::seconds(120)),
                "normal 120-second provider polling must not retire a healthy PPS capture");
        require(!rp1_pps_reference_ready(reference, now + std::chrono::seconds(285)), "aged reference reports expire");
        require(!rp1_pps_reference_ready(reference, now - std::chrono::seconds(1)), "backward acquisition age refused");
        reference.combined_sources = true;
        require(!rp1_pps_reference_ready(reference, now), "combined reference cannot qualify direct PPS");
        reference.combined_sources = false; reference.source_provenance = "Network NTP";
        require(!rp1_pps_reference_ready(reference, now), "network source label cannot qualify PPS");
        reference.source_provenance = "GNSS/PPS or other local reference clock"; reference.leap_normal = false;
        require(!rp1_pps_reference_ready(reference, now), "abnormal leap state retires capture");
        for (double ppm : {-199.0, -46.245, -42.3, 0.0, 10.0, 150.0}) {
            const auto r = evaluate_rp1_pps_measurement(measurement(ppm));
            require(r.valid && std::abs(r.parent_error_ppm - ppm) < 0.002, "known positive/negative/zero rate");
            require(std::abs(r.additional_ppm - (ppm + 46.245)) < 0.002, "full-parent error reconciles intrinsic");
            require(r.uncertainty_ppm > 0.2 && r.uncertainty_ppm < 0.203, "both reference endpoints and quantization budgeted");
        }
        for (double ppm : {-250.0, 155.0, 201.0})
            require(!evaluate_rp1_pps_measurement(measurement(ppm)).valid, "parent/additional limits fail closed");
        auto invalid = measurement(0);
        invalid.edge_uncertainty_ns = std::numeric_limits<double>::quiet_NaN();
        require(!evaluate_rp1_pps_measurement(invalid).valid, "malformed uncertainty rejected");
        invalid = measurement(0); invalid.completed_at += std::chrono::seconds(4);
        require(!evaluate_rp1_pps_measurement(invalid).valid, "delayed readback rejected");
        invalid = measurement(0); invalid.completed_at = invalid.started_at;
        require(!evaluate_rp1_pps_measurement(invalid).valid, "short/duplicate edge interval rejected");
        worker_and_selection();
        std::cout << "RP1 PPS instruction timing, bounds, qualification, source loss, expiry, reacquisition, selection, and cleanup passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
