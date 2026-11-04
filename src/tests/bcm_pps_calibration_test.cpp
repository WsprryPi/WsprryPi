// SPDX-License-Identifier: MIT
#include "bcm_pps_calibration.hpp"
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
using namespace wsprrypi;
using Clock = std::chrono::steady_clock;
void require(bool ok, const char* phase) { if (!ok) throw std::runtime_error(phase); }
BcmPpsMeasurement measurement(double ppm, LegacyGpioProcessorProfile p = LegacyGpioProcessorProfile::Bcm2711) {
    BcmPpsMeasurement m;
    m.processor = p; m.nominal_timer_hz = 1000000;
    m.clock_identity = "test-bcm-clock:" + std::to_string(static_cast<int>(p));
    m.first = {10000000,10000002};
    const auto span = std::uint64_t(std::llround(120000000 * (1 + ppm / 1e6)));
    m.last = {m.first.low_us + span, m.first.high_us + span};
    m.completed_at = Clock::now();
    return m;
}
std::string tree(LegacyGpioProcessorProfile p, std::uint64_t rate = 1000000) {
    const bool pi4 = p == LegacyGpioProcessorProfile::Bcm2711;
    return "osc 1 1 0 " + std::to_string(pi4 ? 54000000 : 19200000) +
        "\n   timer 1 1 0 " + std::to_string(rate) + "\n   plld 1 1 0 1500000000\n      plld_per 1 1 0 " +
        std::to_string(pi4 ? 750000000 : 500000000) + "\n";
}
std::string cells(std::initializer_list<std::uint32_t> words) {
    std::string result;
    for (auto word : words) for (int shift : {24,16,8,0}) result += char(word >> shift);
    return result;
}
std::string compatible(const char* soc) { return std::string("raspberrypi,test")+'\0'+soc+'\0'; }
bool valid_tree(std::string s, LegacyGpioProcessorProfile p, double timer = 1000000) {
    std::istringstream input(s); return bcm_pps_clock_tree_valid(input, p, timer);
}
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<BcmPpsMeasurement> samples;
    unsigned calls = 0;
    std::atomic<unsigned> destroyed{0};
    bool fail = false, closed = false;
    void close() { std::lock_guard lock(mutex); closed = true; changed.notify_all(); }
};
struct Capture : BcmPpsCapture {
    std::shared_ptr<Queue> queue;
    explicit Capture(std::shared_ptr<Queue> q) : queue(std::move(q)) {}
    ~Capture() override { ++queue->destroyed; }
    BcmPpsMeasurement measure(const std::atomic<bool>&) override {
        std::unique_lock lock(queue->mutex);
        ++queue->calls; queue->changed.notify_all();
        queue->changed.wait(lock, [&] { return queue->closed || queue->fail || !queue->samples.empty(); });
        if (queue->closed) throw std::runtime_error("closed");
        if (queue->fail) { queue->fail = false; throw std::runtime_error("reference lost"); }
        auto m = queue->samples.front(); queue->samples.pop_front(); return m;
    }
};
struct Session {
    std::shared_ptr<Queue> q = std::make_shared<Queue>();
    BcmPpsCalibration c{[v=q] { return std::make_unique<Capture>(v); }};
    std::uint64_t next_counter = 10000000;
    BcmPpsMeasurement last_measurement;
    Clock::time_point latest_time{};
    Session() {
        std::cerr << "BCM phase: start capture\n";
        c.snapshot(true);
        std::unique_lock lock(q->mutex); q->changed.wait(lock, [&] { return q->calls >= 1; });
    }
    ~Session() { q->close(); c.snapshot(false); }
    SystemClockFrequencyEstimate advance(const char* phase, std::initializer_list<double> ppms, bool fail = false, const std::string& identity = {}, bool duplicate = false) {
        std::cerr << "BCM phase: " << phase << '\n';
        std::unique_lock lock(q->mutex);
        const auto target = q->calls + ppms.size() + unsigned(fail);
        Clock::time_point now = Clock::now();
        for (double ppm : ppms) {
            auto m = measurement(ppm);
            const auto offset = next_counter - m.first.low_us;
            m.first.low_us += offset; m.first.high_us += offset;
            m.last.low_us += offset; m.last.high_us += offset;
            next_counter = m.last.high_us + 1000000;
            if (!identity.empty()) m.clock_identity = identity;
            if (duplicate) m = last_measurement;
            else last_measurement = m;
            now = m.completed_at; q->samples.push_back(m);
        }
        latest_time = now;
        q->fail = fail; q->changed.notify_all();
        q->changed.wait(lock, [&] { return q->calls >= target; });
        require(q->calls == target, "worker advanced past boundary");
        lock.unlock();
        auto s = c.snapshot(true, now);
        std::cerr << "  samples=" << s.retained_source_samples << " qualification=" << int(s.qualification) << " reason=" << s.reason << '\n';
        return s;
    }
};
int main() {
 try {
    for (auto p : {LegacyGpioProcessorProfile::Bcm2835, LegacyGpioProcessorProfile::Bcm2836Bcm2837, LegacyGpioProcessorProfile::Bcm2711}) {
        require(valid_tree(tree(p), p), "supported oscillator/timer/PLLD identity");
        auto bad = tree(p); bad.replace(0,3,"bad"); require(!valid_tree(bad,p), "wrong ancestry refused");
        bad = tree(p); bad.replace(bad.find("plld_per"),8,"bad_per_"); require(!valid_tree(bad,p), "missing RF parent refused");
        require(!valid_tree(tree(p)+tree(p),p), "duplicate clocks refused");
        bad = tree(p);
        const auto rf_branch = bad.find("   plld ");
        bad.insert(rf_branch, "xosc 1 1 0 " + std::to_string(p == LegacyGpioProcessorProfile::Bcm2711 ? 54000000 : 19200000) + "\n");
        require(!valid_tree(bad,p), "equal-rate independent oscillator roots cannot establish a common RF reference");
        require(!valid_tree(tree(p,999999),p,1000002.5), "retuned timer refused");
        for (double ppm : {-150.,-42.3,0.,42.3,150.}) {
            const auto r = evaluate_bcm_pps_measurement(measurement(ppm,p));
            const auto intrinsic = legacyGpioClockModel(p,LegacyGpioClockParent::PllD).intrinsic_system_to_rf_difference_ppm;
            require(r.valid && std::abs(r.additional_ppm + intrinsic - ppm) < 0.01, "full measured error replaces intrinsic exactly once");
            auto m = measurement(ppm,p); m.first.high_us = m.first.low_us + 20; m.last.high_us = m.last.low_us + 20;
            require(evaluate_bcm_pps_measurement(m).uncertainty_ppm <= 0.220, "endpoint/reference/quantization budget");
        }
    }
    for (auto [soc, base] : {std::pair{"brcm,bcm2835",0x20000000U}, {"brcm,bcm2836",0x3f000000U}, {"brcm,bcm2837",0x3f000000U}, {"brcm,bcm2711",0xfe000000U}}) {
        require(bcm_pps_board_layout(compatible(soc),1,1,1,cells({0x7e000000,base,0x1800000})).peripheral_base==base,"32-bit DT range verified");
        require(bcm_pps_board_layout(compatible(soc),1,2,2,cells({0x7e000000,0,base,0,0x1800000})).peripheral_base==base,"64-bit DT range verified");
        bool refused=false;
        try { bcm_pps_board_layout(compatible(soc),1,1,1,cells({0x7e000000,0xdeadbeef,0x1800000})); } catch (...) { refused=true; }
        require(refused,"DT disagreement never falls back to a hardcoded address");
    }
    for (const auto& bad : {compatible("brcm,bcm2835-fake"),compatible("brcm,bcm2712"),std::string("not-a-pi")}) {
        bool refused=false;try { bcm_pps_board_layout(bad,1,1,1,cells({0x7e000000,0x20000000,0x1800000})); } catch (...) { refused=true; }
        require(refused,"unsupported or malformed board rejected");
    }
    bool malformed=false;
    try { bcm_pps_board_layout(compatible("brcm,bcm2835"),1,1,1,"bad"); } catch (...) { malformed=true; }
    require(malformed,"truncated ranges rejected");
    auto rounded_tree=tree(LegacyGpioProcessorProfile::Bcm2835);
    rounded_tree.replace(rounded_tree.find("500000000"),9,"500000015");
    require(valid_tree(rounded_tree,LegacyGpioProcessorProfile::Bcm2835),"PLL programming/report rounding need not match an ideal integer rate");
    auto allowance=measurement(0);
    allowance.parent_nominal_allowance_ppm=0.032;
    require(evaluate_bcm_pps_measurement(allowance).valid,"small nominal deviation is budgeted");
    allowance.first.high_us=allowance.first.low_us+20; allowance.last.high_us=allowance.last.low_us+20;
    allowance.parent_nominal_allowance_ppm=0.1;
    require(!evaluate_bcm_pps_measurement(allowance).valid,"total uncertainty including nominal deviation cannot exceed policy");
    auto old=measurement(-42.3);
    old.first.low_us += (1ULL<<60); old.first.high_us += (1ULL<<60);
    old.last.low_us += (1ULL<<60); old.last.high_us += (1ULL<<60);
    require(std::abs(evaluate_bcm_pps_measurement(old).additional_ppm+42.3)<0.01,"long uptime must not lose counter precision");
    BcmPpsEdgeTracker tracker;
    require(!tracker.observe(10,true,11), "initial high is not an edge");
    require(!tracker.observe(12,false,13), "low arms edge");
    auto edge=tracker.observe(14,true,15);
    require(edge && edge->low_us==12 && edge->high_us==15, "edge lies in independent timer bracket");
    require(!tracker.observe(16,true,17), "held high cannot duplicate an edge");
    bool refused=false;
    try { tracker.observe(5,false,6); } catch (...) { refused=true; }
    require(refused,"counter reset refused by production tracker");
    BcmPpsEdgeTracker delayed;
    delayed.observe(1,false,2);
    refused=false;try { delayed.observe(100,true,101); } catch (...) { refused=true; }
    require(refused,"scheduler-delayed endpoint refused by production tracker");
    BcmPpsEdgeTracker intermediate(500);
    intermediate.observe(1,false,2);
    require(intermediate.observe(100,true,101).has_value(),"bounded intermediate delay does not spoil endpoint frequency precision");
    require(bcm_pps_inputs_valid(0,0x800), "input and locked PLL accepted");
    require(!bcm_pps_inputs_valid(1U<<21,0x800), "output mux refused");
    require(!bcm_pps_inputs_valid(0,0), "unlocked RF PLL refused");
    const double exact = bcm_pps_timer_rate(0x291,78643,19200000);
    require(std::abs(exact - 1000002.543138) < 0.001, "fractional timer divisor must not be rounded to 1 MHz");
    auto fractional=measurement(0,LegacyGpioProcessorProfile::Bcm2835);
    fractional.nominal_timer_hz=exact;
    auto span=std::uint64_t(std::llround(exact*120*(1-42.3/1e6)));
    fractional.last={fractional.first.low_us+span,fractional.first.high_us+span};
    require(std::abs(evaluate_bcm_pps_measurement(fractional).additional_ppm+39.8)<0.01,"exact fractional timer rate and Pi1 intrinsic compose correctly");
    require(bcm_pps_timer_rate(0x91,54*4096,54000000) == 1000000, "Pi4 integer divider");
    for (auto pair : {std::pair{0x91U,78643U}, {0x90U,78643U}, {0x11U,78643U}, {0x91U,0U}, {0x91U,0x100000U}}) {
        bool refused=false; try { (void)bcm_pps_timer_rate(pair.first,pair.second,19200000); } catch (...) { refused=true; }
        require(refused,"invalid clock register refused");
    }
    auto m = measurement(0); m.first.high_us += 21;
    require(!evaluate_bcm_pps_measurement(m).valid,"wide sampling bracket refused");
    m=measurement(0);m.last.low_us=0; require(!evaluate_bcm_pps_measurement(m).valid,"backward counter refused");
    m=measurement(0);m.nominal_timer_hz=std::numeric_limits<double>::quiet_NaN();require(!evaluate_bcm_pps_measurement(m).valid,"NaN refused");
    require(!evaluate_bcm_pps_measurement(measurement(201)).valid,"overrange refused");
    Session session;
    auto first=session.advance("first",{-42.3});
    require(first.retained_source_samples==1 && first.qualification==FrequencyEstimateQualification::Converging,"first cannot qualify");
    auto qualified=session.advance("qualified",{-42.3,-42.3});
    require(qualified.retained_source_samples==3 && qualified.qualification==FrequencyEstimateQualification::Qualified,"three distinct observations qualify");
    auto during_capture=session.c.snapshot(true,session.latest_time+std::chrono::seconds(120));
    require(during_capture.frequency_ppm && during_capture.qualification==FrequencyEstimateQualification::Qualified,"valid measurement survives the next long acquisition");
    auto correction=select_gpio_frequency_correction(true,0.1,-3,qualified);
    require(correction.valid && std::abs(correction.effective_ppm - (-42.2))<0.01,"Pi4 zero intrinsic and residual applied once");
    auto unstable=session.advance("instability",{-41.9});
    require(unstable.qualification==FrequencyEstimateQualification::Converging,"unstable sample retires qualification");
    auto requalified=session.advance("requalified",{-42.3,-42.3,-42.3});
    require(requalified.qualification==FrequencyEstimateQualification::Qualified,"stable history requalifies");
    auto expired=session.c.snapshot(true,Clock::now()+std::chrono::seconds(151));
    require(!expired.frequency_ppm && expired.qualification==FrequencyEstimateQualification::Unavailable,"old measurement expires");
    require(select_gpio_frequency_correction(true,0.1,-3,expired).effective_ppm==-3,"expiry uses manual without residual");
    require(!session.c.snapshot(true,Clock::time_point{}).frequency_ppm,"backward clock refused");
    auto lost=session.advance("reference loss",{},true);
    require(!lost.frequency_ppm && lost.retained_source_samples==0 && lost.reason=="reference lost","reference loss clears state");
    require(session.q->destroyed>=1,"error releases capture");
    auto recovered=session.advance("reacquisition",{-42.3});
    require(recovered.retained_source_samples==1 && recovered.qualification==FrequencyEstimateQualification::Converging,"recovery starts fresh");
    auto changed=session.advance("clock identity replacement",{-42.3},false,"replacement-clock");
    require(!changed.frequency_ppm && changed.retained_source_samples==0 && changed.reason.find("identity changed")!=std::string::npos,"new clock retires old qualification");
    auto replaced=session.advance("fresh replacement",{-42.3},false,"replacement-clock");
    require(replaced.retained_source_samples==1 && replaced.qualification==FrequencyEstimateQualification::Converging,"replacement starts fresh history");
    auto duplicate=session.advance("duplicate observation",{-42.3},false,"replacement-clock",true);
    require(!duplicate.frequency_ppm && duplicate.reason.find("Non-distinct")!=std::string::npos,"duplicate observations cannot qualify");
    session.q->close();session.c.snapshot(false);
    require(session.q->destroyed>=2,"disable releases capture");
    std::cout << "BCM PPS clock identity, exact divider, uncertainty, correction, qualification, loss, expiry and cleanup passed\n";
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
