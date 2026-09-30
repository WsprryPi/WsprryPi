#pragma once

#include "WSPR-Transmitter/src/wspr_transmit.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace wsprrypi {
// Narrow parent adapter for the existing native backends. WTP and local work
// must still enter through the shared authority before either backend runs.
class WtpPiNativeBridge final : public IControllerBridge {
public:
    using EventSink = std::function<void(WsprTransmissionCallbackEvent,
        WsprTransmitLogLevel, const std::string&, double)>;
    explicit WtpPiNativeBridge(EventSink sink = {}) : sink_(std::move(sink)) {}

    WsprTransmitState backendStateValue() const noexcept override { return state_.load(); }
    void backendSetStateValue(WsprTransmitState state) noexcept override { state_ = state; }
    bool backendShouldStop() const noexcept override { return stopped_.load(); }
    void backendSignalStopRequest() noexcept override { request_stop(); }
    void backendRequestStopTxNoJoin() noexcept override { request_stop(); }
    bool backendWaitInterruptableFor(std::chrono::nanoseconds duration) override {
        std::unique_lock lock(mutex_);
        return !cv_.wait_for(lock, duration, [this] { return stopped_.load(); });
    }
    void backendThrowIfStopRequested(const char* context) override {
        if (stopped_) throw std::runtime_error(context ? context : "WTP execution stopped");
    }
    void backendReportExecutionProgress(std::size_t index) noexcept override {
        progress_ = index;
    }
    void backendFireTransmitCallback(WsprTransmissionCallbackEvent event,
                                     WsprTransmitLogLevel level,
                                     const std::string& message,
                                     double value) override {
        if (sink_) sink_(event, level, message, value);
    }
    bool backendRestartCurrentConfiguration() override { return false; }
    void request_stop() noexcept { stopped_ = true; cv_.notify_all(); }
    void reset_stop() noexcept { stopped_ = false; }

private:
    EventSink sink_;
    std::atomic<WsprTransmitState> state_{WsprTransmitState::DISABLED};
    std::atomic<bool> stopped_{false};
    std::atomic<std::size_t> progress_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace wsprrypi
