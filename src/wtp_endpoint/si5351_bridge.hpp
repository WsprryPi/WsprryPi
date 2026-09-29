#pragma once

#include "WSPR-Transmitter/src/wspr_transmit.hpp"
#include "WSPR-Transmitter/src/wspr_transmit_backend_si5351.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace wsprrypi {

// Narrow parent adapter for the existing Si5351 backend. WTP and local work
// must still enter through the shared authority before either backend runs.
class WtpPiSi5351Bridge final : public IControllerBridge {
public:
    using EventSink = std::function<void(WsprTransmissionCallbackEvent,
        WsprTransmitLogLevel, const std::string&, double)>;
    explicit WtpPiSi5351Bridge(EventSink sink = {}) : sink_(std::move(sink)) {}

    WsprTransmitState backendStateValue() const noexcept override { return state_.load(); }
    void backendSetStateValue(WsprTransmitState state) noexcept override { state_ = state; }
    bool backendShouldStop() const noexcept override { return stopped_.load(); }
    void backendSignalStopRequest() noexcept override { request_stop(); }
    void backendRequestStopTxNoJoin() noexcept override { request_stop(); }
    bool backendWaitInterruptableFor(std::chrono::nanoseconds duration) override;
    void backendThrowIfStopRequested(const char* context) override;
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
    void request_stop() noexcept;
    void reset_stop() noexcept { stopped_ = false; }

private:
    EventSink sink_;
    std::atomic<WsprTransmitState> state_{WsprTransmitState::DISABLED};
    std::atomic<bool> stopped_{false};
    std::atomic<std::size_t> progress_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

WsprSi5351Backend::Config wtp_pi_si5351_backend_config(
    int i2c_bus, int i2c_address, std::uint32_t reference_hz,
    bool crystal_reference, int crystal_load_pf, int tx_output,
    int power_level, bool dry_run = false);

} // namespace wsprrypi
