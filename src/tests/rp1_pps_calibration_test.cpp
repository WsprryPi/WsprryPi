// SPDX-License-Identifier: MIT
#include "rp1_pps_calibration.hpp"
#include "PPM-Manager/src/ppm_manager.hpp"
#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <sstream>

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

// A call to measure() acknowledges publication of the preceding result:
// the production worker publishes before requesting another measurement.
// Tests release one batch at a time and inspect state only at that boundary.
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<double> samples;
    unsigned calls = 0;
    bool fail = false;
    bool closed = false;
    Clock::time_point completed_at{};
    std::atomic<unsigned> destroyed{0};

    void close() {
        std::lock_guard lock(mutex);
        closed = true;
        changed.notify_all();
    }
};
struct FakeCapture : Rp1PpsCapture {
    std::shared_ptr<Queue> queue;
    explicit FakeCapture(std::shared_ptr<Queue> value) : queue(std::move(value)) {}
    ~FakeCapture() override { ++queue->destroyed; }
    Rp1PpsMeasurement measure(const std::atomic<bool>&) override {
        std::unique_lock lock(queue->mutex);
        ++queue->calls;
        queue->changed.notify_all();
        queue->changed.wait(lock, [&] {
            return queue->closed || queue->fail || !queue->samples.empty();
        });
        if (queue->closed) throw std::runtime_error("test capture closed");
        if (queue->fail) {
            queue->fail = false;
            throw std::runtime_error("reference lost");
        }
        const auto m = measurement(queue->samples.front());
        queue->samples.pop_front();
        queue->completed_at = m.completed_at;
        return m;
    }
};
struct CaptureSession {
    std::shared_ptr<Queue> queue = std::make_shared<Queue>();
    Rp1PpsCalibration calibration{[q = queue] { return std::make_unique<FakeCapture>(q); }};
    CaptureSession() {
        std::cerr << "PPS worker phase: initial capture request\n";
        calibration.snapshot(true);
        std::unique_lock lock(queue->mutex);
        queue->changed.wait(lock, [&] { return queue->calls >= 1; });
    }
    ~CaptureSession() {
        // Also unblock measure() during assertion-failure stack unwinding.
        queue->close();
        calibration.snapshot(false);
    }
    SystemClockFrequencyEstimate advance(const char* phase,
                                         std::initializer_list<double> samples,
                                         bool fail = false) {
        // CI's job timeout remains the deadlock watchdog, not a state assertion.
        // Emit the pending phase before blocking so a stalled handshake is named.
        std::cerr << "PPS worker phase: " << phase << '\n';
        std::unique_lock lock(queue->mutex);
        const auto target = queue->calls + samples.size() + unsigned(fail);
        queue->samples.insert(queue->samples.end(), samples);
        queue->fail = fail;
        queue->changed.notify_all();
        queue->changed.wait(lock, [&] { return queue->calls >= target; });
        require(queue->calls == target, "capture advanced past the requested boundary");
        // Explicit sample time keeps freshness assertions independent of host
        // scheduling. Expiry itself is exercised with explicit time below.
        const auto now = queue->completed_at;
        lock.unlock();
        return calibration.snapshot(true, now);
    }
};
template<class Predicate>
void expect_phase(const char* phase, const SystemClockFrequencyEstimate& s, Predicate p) {
    if (p(s)) return;
    std::ostringstream message;
    message << phase << ": samples=" << s.retained_source_samples
            << ", qualification=" << static_cast<int>(s.qualification)
            << ", reason=" << s.reason;
    throw std::runtime_error(message.str());
}

void worker_and_selection() {
    CaptureSession session;
    auto& c = session.calibration;
    auto queue = session.queue;
    auto first = session.advance("first measurement", {-42.3});
    expect_phase("first measurement", first, [](const auto& s) {
        return s.retained_source_samples == 1 &&
               s.qualification == FrequencyEstimateQualification::Converging;
    });
    auto fallback = select_gpio_frequency_correction(true, 7, -3, first);
    require(fallback.effective_ppm == -3 && fallback.mode == GpioCorrectionMode::FixedManual, "acquisition uses manual without residual");
    auto s = session.advance("initial qualification", {-42.31, -42.29});
    expect_phase("initial qualification", s, [](const auto& value) {
        return value.retained_source_samples == 3 &&
               value.qualification == FrequencyEstimateQualification::Qualified;
    });
    const auto selected = select_gpio_frequency_correction(true, 0.1, -3, s);
    const auto total = compose_gpio_frequency_correction(-46.245, selected);
    require(total.valid && std::abs(total.effective_ppm - (-42.19)) < 0.002, "intrinsic applies once; direct replaces host; residual once");
    auto unsettled = session.advance("unstable measurement", {-41.9});
    expect_phase("unstable measurement", unsettled, [](const auto& value) {
        return value.retained_source_samples == 4 &&
               value.qualification == FrequencyEstimateQualification::Converging;
    });
    require(select_gpio_frequency_correction(true, 0.1, -3, unsettled).mode == GpioCorrectionMode::FixedManual,
            "unstable PPS does not retain a formerly qualified value");
    auto requalified = session.advance("qualification after instability", {-42.3, -42.3, -42.3});
    expect_phase("qualification after instability", requalified, [](const auto& value) {
        return value.retained_source_samples == 7 &&
               value.qualification == FrequencyEstimateQualification::Qualified;
    });
    auto stale = c.snapshot(true, queue->completed_at + std::chrono::seconds(31));
    require(!stale.frequency_ppm && stale.qualification == FrequencyEstimateQualification::Unavailable, "monotonic expiry never reuses old estimates");
    require(select_gpio_frequency_correction(true, 0.1, -3, stale).effective_ppm == -3, "expiry selects manual");
    auto backward = c.snapshot(true, Clock::time_point{});
    require(!backward.frequency_ppm, "backward clock cannot grant freshness");
    auto lost = session.advance("reference loss", {}, true);
    expect_phase("reference loss", lost, [](const auto& value) {
        return value.reason == "reference lost" && !value.frequency_ppm &&
               value.retained_source_samples == 0 &&
               value.qualification == FrequencyEstimateQualification::Unavailable;
    });
    require(queue->destroyed >= 1, "capture errors release resources");
    auto recovered = session.advance("reacquisition", {-42.3});
    expect_phase("reacquisition", recovered, [](const auto& value) {
        return value.retained_source_samples == 1 &&
               value.qualification == FrequencyEstimateQualification::Converging;
    });
    std::cerr << "PPS worker phase: disable and release capture\n";
    queue->close();
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
