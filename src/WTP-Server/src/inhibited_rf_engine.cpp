#include "wtp_server/inhibited_rf_engine.hpp"

namespace wsprrypico::wtp {

PrepareResult InhibitedRfEngine::prepare(const Job&) {
    return {true, {}};
}

bool InhibitedRfEngine::begin(const Job& job, std::uint64_t start_monotonic_ns) {
    duration_ns_ = job.total_duration_ns;
    start_monotonic_ns_ = start_monotonic_ns;
    running_ = true;
    return true;
}

EngineReport InhibitedRfEngine::poll(std::uint64_t monotonic_now_ns) {
    if (!running_ || !duration_ns_) {
        return {EngineState::Idle, false};
    }
    if (monotonic_now_ns < start_monotonic_ns_) {
        return {EngineState::Armed, false};
    }
    const auto elapsed = monotonic_now_ns - start_monotonic_ns_;
    if (elapsed >= *duration_ns_) {
        running_ = false;
        return {EngineState::Complete, false};
    }
    return {EngineState::Running, false};
}

bool InhibitedRfEngine::disable(std::uint64_t) {
    running_ = false;
    duration_ns_.reset();
    return true;
}

bool InhibitedRfEngine::output_active() const {
    return false;
}

} // namespace wsprrypico::wtp
