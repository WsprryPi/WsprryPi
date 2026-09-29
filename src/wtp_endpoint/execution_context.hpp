#pragma once
#include "WSPR-Transmitter/src/transmission_backend.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace wsprrypi {
class WtpPiSimulationContext final : public IExecutionContext {
public:
    bool stopRequested() const noexcept override { return stopped_; }
    bool waitInterruptibleFor(std::chrono::nanoseconds duration) override {
        std::unique_lock lock(mutex_);
        return !cv_.wait_for(lock, duration, [this] { return stopped_.load(); });
    }
    void reportExecutionProgress(std::size_t) noexcept override {}
    std::chrono::nanoseconds logicalNow() const noexcept override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }
    void reset() { stopped_ = false; }
    void stop() { stopped_ = true; cv_.notify_all(); }
private:
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};
} // namespace wsprrypi
