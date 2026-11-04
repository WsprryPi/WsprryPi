// SPDX-License-Identifier: MIT
#if defined(__linux__) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include "bcm_pps_calibration.hpp"
#include "pps_input.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace wsprrypi {
namespace {
using Clock = std::chrono::steady_clock;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
double oscillator(LegacyGpioProcessorProfile p) {
    return p == LegacyGpioProcessorProfile::Bcm2711 ? 54000000. : 19200000.;
}
bool edge_valid(BcmPpsEdge e) { return e.high_us >= e.low_us && e.high_us - e.low_us <= 20; }
}
std::uint32_t cell(const std::string& s, std::size_t pos) {
    check(pos <= s.size() && s.size() - pos >= 4, "Malformed device-tree cells.");
    std::uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i) n = (n << 8) | static_cast<unsigned char>(s[pos+i]);
    return n;
}
BcmPpsBoardLayout bcm_pps_board_layout(const std::string& compatible,
    unsigned child, unsigned parent, unsigned size, const std::string& ranges) {
    const auto has_compatible = [&](const std::string& value) {
        for (std::size_t at = 0; at < compatible.size();) {
            const auto end = compatible.find('\0', at);
            if (end == std::string::npos) return false;
            if (compatible.substr(at, end-at) == value) return true;
            at = end + 1;
        }
        return false;
    };
    check(compatible.find("raspberrypi,") == 0 && !compatible.empty() && compatible.back() == '\0', "Direct BCM PPS requires a Raspberry Pi 1–4.");
    LegacyGpioProcessorProfile processor{};
    std::uint64_t physical = 0;
    if (has_compatible("brcm,bcm2711")) { processor = LegacyGpioProcessorProfile::Bcm2711; physical = 0xfe000000; }
    else if (has_compatible("brcm,bcm2837") || has_compatible("brcm,bcm2836")) { processor = LegacyGpioProcessorProfile::Bcm2836Bcm2837; physical = 0x3f000000; }
    else if (has_compatible("brcm,bcm2835")) { processor = LegacyGpioProcessorProfile::Bcm2835; physical = 0x20000000; }
    else throw std::runtime_error("Unsupported BCM PPS processor.");
    check(child == 1 && (parent == 1 || parent == 2) && (size == 1 || size == 2), "Unsupported BCM peripheral address cells.");
    const auto stride = 4 * (child + parent + size);
    check(ranges.size() % stride == 0, "Malformed BCM peripheral ranges.");
    bool found = false;
    for (std::size_t i = 0; i < ranges.size(); i += stride) {
        std::uint64_t actual = cell(ranges, i + 4);
        if (parent == 2) actual = (actual << 32) | cell(ranges, i + 8);
        std::uint64_t length = cell(ranges, i + 4 * (child + parent));
        if (size == 2) length = (length << 32) | cell(ranges, i + 4 * (child + parent + 1));
        if (cell(ranges, i) == 0x7e000000 && actual == physical && length >= 0x201000) found = true;
    }
    check(found, "Cannot verify BCM peripheral mapping from device tree.");
    return {processor, physical};
}
std::optional<BcmPpsEdge> BcmPpsEdgeTracker::observe(std::uint64_t before, bool high, std::uint64_t after) {
    check(maximum_bracket_us_ > 0 && maximum_bracket_us_ <= 500, "Invalid PPS bracket policy.");
    check((!previous_ || before >= *previous_) && after >= before, "BCM timer moved backwards.");
    previous_ = after;
    if (!high) low_ = before;
    else if (low_) {
        BcmPpsEdge edge{*low_, after};
        low_.reset();
        check(edge.high_us >= edge.low_us && edge.high_us - edge.low_us <= maximum_bracket_us_,
              "PPS edge sampling uncertainty exceeds its permitted bracket.");
        return edge;
    }
    return std::nullopt;
}
bool bcm_pps_inputs_valid(std::uint32_t gpio_fsel, std::uint32_t clock_locks, unsigned input_pin) {
    return input_pin <= 27 && ((gpio_fsel >> (3 * (input_pin % 10))) & 7) == 0 && (clock_locks & (1U << 11));
}
double bcm_pps_timer_rate(std::uint32_t control, std::uint32_t divider, double hz) {
    // TIMER is an oscillator-only mux with a 6.12 divider. Bit 9 enables
    // fractional division (CM_FRAC in the BCM kernel driver). A nonzero
    // fraction without that mode must never be treated as a fractional rate.
    check((control & ~0x200U) == 0x91 && (!(divider & 0xfffU) || (control & 0x200U)) && (divider & ~0x3ffffU) == 0 && divider >= 4096 &&
          (hz == 19200000 || hz == 54000000), "Unsupported BCM system-timer clock configuration.");
    const double rate = hz * 4096 / divider;
    check(rate >= 999900 && rate <= 1000100, "BCM timer is not configured near 1 MHz.");
    return rate;
}
bool bcm_pps_clock_tree_valid(std::istream& input, LegacyGpioProcessorProfile p, double timer_hz,
    std::uint64_t* reported_rf_hz) {
    std::vector<std::pair<std::size_t, std::string>> parents;
    bool timer = false, plld = false;
    std::string line;
    unsigned timer_count = 0, plld_count = 0, oscillator_count = 0;
    std::uint64_t rf_hz = 0;
    const double nominal_rf = legacyGpioClockModel(p, LegacyGpioClockParent::PllD).nominal_rate_hz;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string name; unsigned long enable, prepare, protect; std::uint64_t rate;
        if (!(row >> name >> enable >> prepare >> protect >> rate)) continue;
        const auto indent = line.find_first_not_of(' ');
        while (!parents.empty() && parents.back().first >= indent) parents.pop_back();
        const bool from_osc = std::any_of(parents.begin(), parents.end(), [&](const auto& parent) {
            return parent.second == "osc" || parent.second == "xosc";
        });
        if (name == "osc" || name == "xosc") {
            ++oscillator_count;
            if (rate != oscillator(p)) return false;
        }
        if (name == "timer") {
            ++timer_count;
            timer = from_osc && !parents.empty() &&
                (parents.back().second == "osc" || parents.back().second == "xosc") &&
                std::abs(double(rate) - timer_hz) <= 1;
        }
        if (name == "plld_per") {
            ++plld_count;
            plld = from_osc && !parents.empty() && parents.back().second == "plld" &&
                std::abs(double(rate) - nominal_rf) <= nominal_rf * 0.2e-6;
            rf_hz = rate;
        }
        parents.emplace_back(indent, name);
    }
    const bool valid = timer && plld && timer_count == 1 && plld_count == 1 && oscillator_count == 1;
    if (valid && reported_rf_hz) *reported_rf_hz = rf_hz;
    return valid;
}
BcmPpsResult evaluate_bcm_pps_measurement(const BcmPpsMeasurement& m) {
    BcmPpsResult r;
    if (m.clock_identity.empty() || m.clock_identity.size() > 128 || m.completed_at == Clock::time_point{} ||
        !std::isfinite(m.parent_nominal_allowance_ppm) || m.parent_nominal_allowance_ppm < 0.001 ||
        m.parent_nominal_allowance_ppm > 0.203 || !edge_valid(m.first) || !edge_valid(m.last) || m.last.low_us <= m.first.high_us ||
        !std::isfinite(m.nominal_timer_hz) || m.nominal_timer_hz < 999900 || m.nominal_timer_hz > 1000100) {
        r.reason = "BCM PPS edge brackets, counter order, or timer rate are invalid."; return r;
    }
    // Subtract unsigned counters before converting, retaining precision at long uptime.
    const double ticks = double(m.last.low_us - m.first.low_us) +
        (double(m.last.high_us - m.last.low_us) - double(m.first.high_us - m.first.low_us)) / 2;
    const double parent_ppm = (ticks / (m.nominal_timer_hz * kBcmPpsPeriods) - 1) * 1e6;
    // Half of each bracket + 2 timer ticks per endpoint (counter quantization
    // and an input-synchronization allowance) + 1 us external edge.
    // Charge the observed RF-parent nominal deviation and report quantization
    // as uncertainty; the same correction must also serve Pi4 oscillator RF.
    const double endpoint_ticks = double(m.first.high_us - m.first.low_us + m.last.high_us - m.last.low_us) / 2 + 4;
    r.uncertainty_ppm = (endpoint_ticks / m.nominal_timer_hz + 2e-6) / kBcmPpsPeriods * 1e6 + m.parent_nominal_allowance_ppm;
    try { r.additional_ppm = parent_ppm - legacyGpioClockModel(m.processor, LegacyGpioClockParent::PllD).intrinsic_system_to_rf_difference_ppm; }
    catch (const std::exception&) { r.reason = "Unsupported BCM processor."; return r; }
    if (!std::isfinite(parent_ppm) || std::abs(parent_ppm) > 200 || std::abs(r.additional_ppm) > 200 ||
        r.uncertainty_ppm > 0.25) {
        r.reason = "BCM PPS measurement exceeds correction or uncertainty limits."; return r;
    }
    r.valid = true;
    r.reason = "BCM oscillator measured over 120 PPS periods; RF parents share its verified clock ancestry.";
    return r;
}
namespace {
#if defined(__linux__)
std::string read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Cannot read BCM PPS board, reference, or clock identity.");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
// BCM peripheral reads can be reordered across different peripheral blocks.
// Order the timer/GPIO observations in hardware, not only in the compiler.
void peripheral_read_barrier() {
#if defined(__aarch64__) || (defined(__arm__) && __ARM_ARCH >= 7)
    asm volatile("dmb sy" ::: "memory");
#elif defined(__arm__)
    const unsigned zero = 0;
    asm volatile("mcr p15, 0, %0, c7, c10, 5" :: "r"(zero) : "memory");
#else
    std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}
struct Mapping {
    void* address = MAP_FAILED;
    std::size_t mapped_size = 0, offset = 0;
    Mapping() = default;
    Mapping(const Mapping&) = delete;
    ~Mapping() { if (address != MAP_FAILED) ::munmap(address, mapped_size); }
    void open(int fd, std::uint64_t base) {
        const auto page = ::sysconf(_SC_PAGESIZE);
        check(page >= 4096, "Unsupported BCM register mapping page size.");
        mapped_size = static_cast<std::size_t>(page);
        offset = base % mapped_size;
        check(offset + 4096 <= mapped_size, "BCM register page crosses its mapping.");
        address = ::mmap(nullptr, mapped_size, PROT_READ, MAP_SHARED, fd, static_cast<off_t>(base - offset));
        check(address != MAP_FAILED, "Cannot map read-only BCM PPS registers.");
    }
    std::uint32_t read(unsigned word) const {
        peripheral_read_barrier();
        const auto* registers = reinterpret_cast<volatile std::uint32_t*>(static_cast<char*>(address) + offset);
        const auto value = registers[word];
        peripheral_read_barrier();
        return value;
    }
};
struct BcmCapture final : BcmPpsCapture {
    Mapping timer, gpio, clocks;
    LegacyGpioProcessorProfile processor{};
    std::string compatible;
    std::uint32_t timer_control = 0, timer_divider = 0;
    double nominal = 0;
    std::optional<std::uint64_t> reported_rf_hz;
    unsigned input_pin;
    PpsInputBinding reference;
    explicit BcmCapture(unsigned pin) : input_pin(pin) {
        const auto* disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
        check(!disabled || std::string(disabled) != "1", "Direct PPS hardware access is disabled.");
        compatible = read_file("/proc/device-tree/compatible");
        const auto layout = bcm_pps_board_layout(compatible,
            cell(read_file("/proc/device-tree/soc/#address-cells"), 0),
            cell(read_file("/proc/device-tree/#address-cells"), 0),
            cell(read_file("/proc/device-tree/soc/#size-cells"), 0),
            read_file("/proc/device-tree/soc/ranges"));
        processor = layout.processor;
        const auto physical = layout.peripheral_base;
        static_assert(sizeof(off_t) >= 8, "BCM physical mappings require 64-bit file offsets.");
        const int fd = ::open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC | O_NOFOLLOW);
        check(fd >= 0, "Cannot open /dev/mem for read-only BCM PPS capture.");
        try { timer.open(fd, physical + 0x3000); gpio.open(fd, physical + 0x200000); clocks.open(fd, physical + 0x101000); }
        catch (...) { ::close(fd); throw; }
        ::close(fd);
        timer_control = clocks.read(0xe8/4) & 0xffffff;
        timer_divider = clocks.read(0xec/4) & 0xffffff;
        nominal = bcm_pps_timer_rate(timer_control, timer_divider, oscillator(processor));
        reference = resolve_pps_input(input_pin);
        verify();
    }
    void verify() {
        check(read_file("/proc/device-tree/compatible") == compatible, "BCM board identity changed.");
        check(resolve_pps_input(input_pin).identity == reference.identity, "Selected BCM PPS input changed or disappeared.");
        check(bcm_pps_inputs_valid(gpio.read(input_pin / 10), clocks.read(0x114/4), input_pin),
              "Selected PPS GPIO must remain an input and BCM PLLD must remain locked.");
        check((clocks.read(0xe8/4) & 0xffffff) == timer_control &&
              (clocks.read(0xec/4) & 0xffffff) == timer_divider, "BCM timer clock changed.");
        std::istringstream tree(read_file("/sys/kernel/debug/clk/clk_summary"));
        std::uint64_t rate = 0;
        check(bcm_pps_clock_tree_valid(tree, processor, nominal, &rate), "Cannot verify BCM timer and RF-parent ancestry/rates.");
        check(!reported_rf_hz || *reported_rf_hz == rate, "BCM RF-parent nominal rate changed.");
        reported_rf_hz = rate;
    }
    std::uint64_t counter() const {
        // Retry only a high-word rollover; no register is ever written.
        for (unsigned i = 0; i < 3; ++i) {
            const auto high = timer.read(2), low = timer.read(1);
            if (timer.read(2) == high) return (std::uint64_t(high) << 32) | low;
        }
        throw std::runtime_error("Incoherent BCM timer read.");
    }
    BcmPpsEdge edge(const std::atomic<bool>& stop, unsigned maximum_bracket_us = 20) {
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        BcmPpsEdgeTracker tracker(maximum_bracket_us);
        while (!stop) {
            const auto before = counter();
            const bool high = gpio.read(0x34/4) & (1U << input_pin);
            const auto after = counter();
            if (const auto result = tracker.observe(before, high, after)) return *result;
            check(Clock::now() < deadline, "BCM PPS capture timed out; reference missing or interrupted.");
        }
        throw std::runtime_error("BCM PPS acquisition stopped.");
    }
    BcmPpsMeasurement measure(const std::atomic<bool>& stop) override {
        verify();
        BcmPpsMeasurement m;
        m.processor = processor; m.nominal_timer_hz = nominal;
        m.clock_identity = "bcm:" + std::to_string(static_cast<int>(processor)) +
            ":timer-div:" + std::to_string(timer_divider) + ":rf:" + std::to_string(*reported_rf_hz) + ":gpio" + std::to_string(input_pin);
        const double nominal_rf = legacyGpioClockModel(processor, LegacyGpioClockParent::PllD).nominal_rate_hz;
        m.parent_nominal_allowance_ppm = (std::abs(double(*reported_rf_hz) - nominal_rf) + 1) / nominal_rf * 1e6 * 1.0002;
        m.first = edge(stop);
        auto last = m.first;
        for (unsigned n = 0; n < kBcmPpsPeriods; ++n) {
            for (unsigned i = 0; i < 97 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // Only the first/last edges enter the frequency equation. Wider
            // intermediate brackets still identify one consecutive PPS period.
            const auto next = edge(stop, n + 1 == kBcmPpsPeriods ? 20 : 500);
            check(next.low_us > last.high_us && next.low_us - last.high_us > 999000 &&
                  next.high_us - last.low_us < 1001000, "PPS period is missing, duplicated, or implausible.");
            last = next;
            m.completed_at = Clock::now();
            verify();
        }
        m.last = last;
        return m;
    }
};
#endif
}
struct BcmPpsCalibration::Impl {
    std::mutex mutex;
    std::atomic<bool> stop{false};
    std::thread worker;
    std::function<std::unique_ptr<BcmPpsCapture>(unsigned)> factory;
    unsigned input_pin = 17;
    SystemClockFrequencyEstimate current;
    Clock::time_point acquired{};
    void error(const std::string& reason) {
        std::lock_guard lock(mutex);
        current = {}; current.provider_name = "BCM PPS"; current.reason = reason; acquired = {};
    }
    void run() {
        while (!stop) {
            try {
                auto capture = factory(input_pin);
                check(bool(capture), "BCM PPS capture unavailable.");
                std::array<double, 3> history{};
                unsigned count = 0;
                std::string clock_identity;
                std::optional<std::uint64_t> last_counter;
                Clock::time_point last_completed{};
                while (!stop) {
                    const auto m = capture->measure(stop);
                    const auto r = evaluate_bcm_pps_measurement(m);
                    check(r.valid, r.reason.c_str());
                    check(clock_identity.empty() || clock_identity == m.clock_identity,
                          "BCM PPS clock identity changed; restarting qualification.");
                    check(!last_counter || (m.first.low_us > *last_counter && m.completed_at > last_completed),
                          "Non-distinct BCM PPS observation; restarting qualification.");
                    clock_identity = m.clock_identity;
                    last_counter = m.last.high_us;
                    last_completed = m.completed_at;
                    history[count++ % history.size()] = r.additional_ppm;
                    const auto [lo, hi] = std::minmax_element(history.begin(), history.end());
                    SystemClockFrequencyEstimate next;
                    next.provider_name = "BCM PPS";
                    next.source_signature = m.clock_identity;
                    next.source_provenance = "Direct PPS on GPIO" + std::to_string(input_pin) + "; shared BCM RF oscillator";
                    next.qualification = count >= 3 && *hi - *lo <= 0.1 ? FrequencyEstimateQualification::Qualified : FrequencyEstimateQualification::Converging;
                    next.reason = next.qualification == FrequencyEstimateQualification::Qualified ? r.reason : "Collecting three stable, distinct 120-second PPS measurements.";
                    next.frequency_ppm = r.additional_ppm;
                    next.synchronized = next.selected_source = next.leap_normal = true;
                    next.retained_source_samples = count;
                    next.source_stability_span_seconds = kBcmPpsPeriods * std::min<unsigned>(count, 3);
                    next.snapshot_time = std::chrono::system_clock::now() -
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(Clock::now() - m.completed_at);
                    std::lock_guard lock(mutex);
                    current = std::move(next); acquired = m.completed_at;
                }
            } catch (const std::exception& e) {
                error(e.what());
                for (unsigned i = 0; i < 50 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }
};
BcmPpsCalibration::BcmPpsCalibration() : BcmPpsCalibration([](unsigned pin) -> std::unique_ptr<BcmPpsCapture> {
#if defined(__linux__)
    return std::make_unique<BcmCapture>(pin);
#else
    (void)pin;
    throw std::runtime_error("Direct BCM PPS capture requires Linux on Raspberry Pi 1–4.");
#endif
}) {}
BcmPpsCalibration::BcmPpsCalibration(std::function<std::unique_ptr<BcmPpsCapture>()> factory) : BcmPpsCalibration([f=std::move(factory)](unsigned) { return f(); }) {}
BcmPpsCalibration::BcmPpsCalibration(std::function<std::unique_ptr<BcmPpsCapture>(unsigned)> factory)
    : impl_(std::make_unique<Impl>()) { impl_->factory = std::move(factory); }
BcmPpsCalibration::~BcmPpsCalibration() { impl_->stop = true; if (impl_->worker.joinable()) impl_->worker.join(); }
SystemClockFrequencyEstimate BcmPpsCalibration::snapshot(bool enabled, Clock::time_point now, unsigned input_pin) {
    if (impl_->worker.joinable() && (!enabled || input_pin > 27 || impl_->input_pin != input_pin)) {
        impl_->stop = true; impl_->worker.join();
    }
    if (enabled && input_pin > 27) {
        impl_->error("PPS input GPIO must be from 0 through 27.");
        std::lock_guard lock(impl_->mutex); return impl_->current;
    }
    if (!enabled) {
        impl_->stop = true;
        if (impl_->worker.joinable()) impl_->worker.join();
        impl_->error("Direct PPS calibration is disabled."); return {};
    }
    if (!impl_->worker.joinable()) {
        impl_->input_pin = input_pin;
        impl_->stop = false; impl_->error("Waiting for direct BCM PPS capture.");
        try { impl_->worker = std::thread([this] { impl_->run(); }); }
        catch (const std::exception& e) { impl_->error(std::string("Cannot start BCM PPS worker: ") + e.what()); }
    }
    std::lock_guard lock(impl_->mutex);
    auto result = impl_->current;
    if (impl_->acquired != Clock::time_point{}) {
        result.age_seconds = std::chrono::duration<double>(now - impl_->acquired).count();
        if (result.age_seconds < 0 || result.age_seconds > kBcmPpsMaximumAgeSeconds) {
            result.frequency_ppm.reset(); result.qualification = FrequencyEstimateQualification::Unavailable;
            result.reason = "Direct BCM PPS measurement expired; using manual fallback.";
        }
    }
    return result;
}
}
