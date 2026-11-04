// SPDX-License-Identifier: MIT
#include "rp1_pps_calibration.hpp"
#include "pps_input.hpp"
#include "Chipset-Offsets/src/chipset_offsets.hpp"
#include "PPM-Manager/src/ppm_manager.hpp"

#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <array>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <cstdlib>

#if defined(__linux__)
#define WSPRRYPI_HAVE_RP1_PPS_CAPTURE 1
#include "vendor/rp1_pio/rp1_pio_if.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
static_assert(sizeof(rp1_pio_sm_config) == 16);
static_assert(sizeof(rp1_pio_sm_init_args) == 20);
static_assert(sizeof(rp1_pio_sm_get_args) == 8);
#else
#define WSPRRYPI_HAVE_RP1_PPS_CAPTURE 0
#endif

namespace wsprrypi {

bool rp1_pps_reference_ready(const PPMProviderSnapshot& provider,
    std::chrono::steady_clock::time_point now) {
    const auto reference = provider.withAgeAt(now);
    return reference.synchronized && reference.selected_source && reference.leap_normal &&
        !reference.combined_sources &&
        reference.source_provenance == "GNSS/PPS or other local reference clock" &&
        std::isfinite(reference.age_seconds) && reference.age_seconds >= 0 &&
        reference.age_seconds <= kRp1PpsMaximumReferenceReportAgeSeconds &&
        !reference.source_signature.empty();
}

Rp1PpsResult evaluate_rp1_pps_measurement(const Rp1PpsMeasurement& m) {
    Rp1PpsResult r;
    const double elapsed = std::chrono::duration<double>(m.completed_at - m.started_at).count();
    if (!std::isfinite(m.edge_uncertainty_ns) || m.edge_uncertainty_ns <= 0 ||
        m.edge_uncertainty_ns > 1000 || elapsed < kRp1PpsPeriods - 0.01 ||
        elapsed > kRp1PpsPeriods + 2.0 || !m.iterations) {
        r.reason = "PPS capture interval, reference budget, or count is invalid.";
        return r;
    }
    // From the successful WAIT rising-edge sample to the final JMP PIN
    // sample: 2*iterations + 2*periods + 1 clk_sys cycles. The fixed
    // instructions include initialization and one boundary branch per phase.
    const std::uint64_t cycles = 2ULL * m.iterations + 2 * kRp1PpsPeriods + 1;
    r.parent_error_ppm = (double(cycles) / (kRp1PpsNominalHz * kRp1PpsPeriods) - 1) * 1e6;
    r.additional_ppm = r.parent_error_ppm - chipsetIntrinsicOffsetPpm(ClockChipset::Rp1);
    // Bound both electrical endpoints plus four clk_sys cycles of capture
    // quantization/synchronization. Reference accuracy remains conditional on
    // the operator's PPS source satisfying the documented 1 us edge budget.
    r.uncertainty_ppm = (2 * m.edge_uncertainty_ns * 1e-9 + 4 / kRp1PpsNominalHz) /
                        kRp1PpsPeriods * 1e6;
    if (!std::isfinite(r.parent_error_ppm) || std::abs(r.parent_error_ppm) > 200 ||
        !std::isfinite(r.additional_ppm) || std::abs(r.additional_ppm) > 200 ||
        r.uncertainty_ppm > 0.25) {
        r.reason = "PPS frequency measurement exceeds its correction or uncertainty limit.";
        return r;
    }
    r.valid = true;
    r.reason = "RF clock counted over ten PPS periods; conditional uncertainty <=0.202 ppm with a 1 us reference-edge budget.";
    return r;
}

struct Rp1PpsCalibration::Impl {
    std::mutex mutex;
    std::atomic<bool> stop{false};
    std::thread worker;
    SystemClockFrequencyEstimate current;
    std::chrono::steady_clock::time_point acquired{};
    std::function<std::unique_ptr<Rp1PpsCapture>(unsigned)> factory;
    unsigned input_pin = 17;

    void publish_error(const std::string& reason) {
        std::lock_guard lock(mutex);
        current = {};
        current.provider_name = "RP1 PPS";
        current.reason = reason;
        acquired = {};
    }

#if WSPRRYPI_HAVE_RP1_PPS_CAPTURE
    struct Capture : Rp1PpsCapture {
        int fd = -1;
        int sm = -1;
        unsigned input_pin;
        PpsInputBinding reference;
        explicit Capture(unsigned pin) : input_pin(pin) {
            if (const auto* disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
                disabled && std::string(disabled) == "1")
                throw std::runtime_error("Direct PPS hardware access is disabled.");
            // Refuse another board, another PPS route, or a clock-tree change.
            std::ifstream model("/proc/device-tree/model");
            std::string board; std::getline(model, board, '\0');
            if (board.find("Raspberry Pi 5") == std::string::npos)
                throw std::runtime_error("Direct RP1 PPS requires Raspberry Pi 5.");
            reference = resolve_pps_input(input_pin);
            verify_clock_tree();
            fd = ::open("/dev/pio0", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) throw std::runtime_error("Unable to open /dev/pio0 for direct PPS.");
            try {
                rp1_pio_sm_claim_args claim{};
                sm = call(PIO_IOC_SM_CLAIM, &claim);
                if (sm < 0 || sm >= 4) throw std::runtime_error("Invalid PIO state-machine allocation.");
                // Origin zero is intentional: fail busy rather than loading
                // unrelocated jumps or clearing another client's program.
                rp1_pio_add_program_args program{};
                program.origin = 0;
                program.num_instrs = 14;
                std::copy(kRp1PpsProgram.begin(), kRp1PpsProgram.end(), program.instrs);
                if (call(PIO_IOC_ADD_PROGRAM, &program) != 0)
                    throw std::runtime_error("Unexpected PIO program origin.");
                rp1_pio_sm_init_args init{};
                init.sm = sm;
                init.config.clkdiv = 1U << 16;
                init.config.execctrl = (input_pin << 24) | (13U << 12);
                init.config.shiftctrl = (1U << 18) | (1U << 19);
                init.config.pinctrl = input_pin << 15;
                call(PIO_IOC_SM_INIT, &init);
                rp1_pio_sm_set_enabled_args enable{};
                enable.mask = 1U << sm; enable.enable = 1;
                call(PIO_IOC_SM_SET_ENABLED, &enable);
            } catch (...) { ::close(fd); fd = -1; throw; }
        }
        ~Capture() {
            // Kernel close disables and releases only this file's state
            // machines and program references, including exception paths.
            if (fd >= 0) ::close(fd);
        }
        int call(unsigned long command, void* args) {
            int result = ::ioctl(fd, command, args);
            if (result < 0) throw std::runtime_error(
                "PPS PIO operation " + std::to_string(command & 255) + ": " + std::strerror(errno));
            return result;
        }
        static void verify_clock_tree() {
            std::ifstream clocks("/sys/kernel/debug/clk/clk_summary");
            std::string line;
            std::size_t parent_indent = 0;
            bool parent = false, child = false;
            while (std::getline(clocks, line)) {
                std::istringstream row(line);
                std::string name; unsigned long a, b, c; std::uint64_t rate;
                if (!(row >> name >> a >> b >> c >> rate)) continue;
                const auto indent = line.find_first_not_of(' ');
                if (name == "pll_sys") {
                    parent = rate == 200000000; parent_indent = indent;
                } else if (parent && indent <= parent_indent) parent = false;
                if (parent && name == "clk_sys" && indent == parent_indent + 3 && rate == 200000000)
                    child = true;
            }
            if (!child) throw std::runtime_error("Cannot verify RP1 clk_sys directly from 200 MHz pll_sys.");
        }
        void verify_reference() const {
            if (resolve_pps_input(input_pin).identity != reference.identity)
                throw std::runtime_error("Selected RP1 PPS input changed or disappeared.");
        }
        Rp1PpsMeasurement measure(const std::atomic<bool>& stop) override {
            verify_reference();
            verify_clock_tree();
            rp1_pio_sm_fifo_state_args fifo{}; fifo.sm = sm;
            call(PIO_IOC_SM_FIFO_STATE, &fifo);
            if (!fifo.empty) throw std::runtime_error("Unexpected retained PPS FIFO data.");
            Rp1PpsMeasurement m;
            m.started_at = std::chrono::steady_clock::now();
            rp1_pio_sm_put_args trigger{}; trigger.sm = sm;
            call(PIO_IOC_SM_PUT, &trigger);
            for (;;) {
                if (stop) throw std::runtime_error("PPS acquisition stopped.");
                const auto now = std::chrono::steady_clock::now();
                if (now - m.started_at > std::chrono::seconds(kRp1PpsPeriods + 2))
                    throw std::runtime_error("PPS capture timed out; reference missing or interrupted.");
                call(PIO_IOC_SM_FIFO_STATE, &fifo);
                if (!fifo.empty) {
                    rp1_pio_sm_get_args sample{}; sample.sm = sm;
                    call(PIO_IOC_SM_GET, &sample);
                    m.iterations = ~sample.data;
                    m.completed_at = std::chrono::steady_clock::now();
                    verify_clock_tree();
                    verify_reference();
                    return m;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    };
#endif

    void run() {
        while (!stop) {
          try {
            auto capture = factory(input_pin);
            if (!capture) throw std::runtime_error("PPS capture unavailable.");
            std::array<double, 3> history{};
            unsigned observations = 0;
            while (!stop) {
                const auto measurement = capture->measure(stop);
                const auto result = evaluate_rp1_pps_measurement(measurement);
                if (!result.valid) throw std::runtime_error(result.reason);
                history[observations++ % history.size()] = result.additional_ppm;
                const auto [low, high] = std::minmax_element(history.begin(), history.end());
                const bool stable = observations >= history.size() && *high - *low <= 0.1;
                SystemClockFrequencyEstimate next;
                next.provider_name = "RP1 PPS";
                next.source_provenance = "Direct PPS on GPIO" + std::to_string(input_pin) + " (operator-qualified reference)";
                next.source_signature = "rp1:pll_sys:200000000:pio0:gpio" + std::to_string(input_pin);
                next.qualification = stable ? FrequencyEstimateQualification::Qualified : FrequencyEstimateQualification::Converging;
                next.frequency_ppm = result.additional_ppm;
                next.reason = stable ? result.reason : "Collecting three stable, distinct ten-second PPS measurements.";
                next.synchronized = true;
                next.selected_source = true;
                next.leap_normal = true;
                next.retained_source_samples = observations;
                next.source_stability_span_seconds = kRp1PpsPeriods * std::min<unsigned>(observations, history.size());
                const auto measured_at = std::min(measurement.completed_at,
                    measurement.started_at + std::chrono::seconds(kRp1PpsPeriods));
                next.snapshot_time = std::chrono::system_clock::now() -
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(
                        std::chrono::steady_clock::now() - measured_at);
                std::lock_guard lock(mutex);
                current = std::move(next);
                // Conservative age: the first PPS edge can occur up to a
                // second after the trigger; never date delayed FIFO readback
                // as a newly captured electrical endpoint.
                acquired = measured_at;
            }
          } catch (const std::exception& error) {
            publish_error(error.what());
            for (unsigned i = 0; i < 50 && !stop; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
          }
        }
    }
};

Rp1PpsCalibration::Rp1PpsCalibration() : Rp1PpsCalibration([](unsigned pin) -> std::unique_ptr<Rp1PpsCapture> {
#if WSPRRYPI_HAVE_RP1_PPS_CAPTURE
    return std::make_unique<Impl::Capture>(pin);
#else
    (void)pin;
    throw std::runtime_error("Direct PPS requires Linux RP1 PIO support; using configured manual fallback.");
#endif
}) {}
Rp1PpsCalibration::Rp1PpsCalibration(std::function<std::unique_ptr<Rp1PpsCapture>()> factory)
    : Rp1PpsCalibration([f=std::move(factory)](unsigned) { return f(); }) {}
Rp1PpsCalibration::Rp1PpsCalibration(std::function<std::unique_ptr<Rp1PpsCapture>(unsigned)> factory)
    : impl_(std::make_unique<Impl>()) { impl_->factory = std::move(factory); }
Rp1PpsCalibration::~Rp1PpsCalibration() {
    impl_->stop = true;
    if (impl_->worker.joinable()) impl_->worker.join();
}
SystemClockFrequencyEstimate Rp1PpsCalibration::snapshot(bool enabled,
    std::chrono::steady_clock::time_point now, unsigned input_pin) {
    if (impl_->worker.joinable() && (!enabled || input_pin > 27 || impl_->input_pin != input_pin)) {
        impl_->stop = true;
        impl_->worker.join();
    }
    if (enabled && input_pin > 27) {
        impl_->publish_error("PPS input GPIO must be from 0 through 27.");
        std::lock_guard lock(impl_->mutex);
        return impl_->current;
    }
    // Called under the scheduler's estimate mutex. Joining is bounded by one
    // nonblocking RPC plus a 20 ms poll; no device call waits for a PPS edge.
    if (!enabled) {
        impl_->stop = true;
        if (impl_->worker.joinable()) impl_->worker.join();
        impl_->publish_error("Direct PPS calibration is disabled.");
        return {};
    }
    if (!impl_->worker.joinable()) {
        impl_->input_pin = input_pin;
        impl_->stop = false;
        impl_->publish_error("Waiting for direct PPS capture.");
        try {
            impl_->worker = std::thread([this] { impl_->run(); });
        } catch (const std::exception& error) {
            impl_->publish_error(std::string("Unable to start direct PPS acquisition: ") + error.what());
        }
    }
    std::lock_guard lock(impl_->mutex);
    auto sample = impl_->current;
    if (impl_->acquired != std::chrono::steady_clock::time_point{}) {
        sample.age_seconds = std::chrono::duration<double>(now - impl_->acquired).count();
        if (sample.age_seconds < 0 || sample.age_seconds > kRp1PpsMaximumAgeSeconds) {
            sample.frequency_ppm.reset();
            sample.qualification = FrequencyEstimateQualification::Unavailable;
            sample.reason = "Direct PPS measurement expired; using configured manual fallback.";
        }
    }
    return sample;
}
} // namespace wsprrypi
