#pragma once

#include "WTP-Server/include/wtp_server/job_service.hpp"
#include "WSPR-Transmitter/src/transmission_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace wsprrypi {

// Finite WTP RF-event execution through the selected native backend.
// The historical class/file name remains for source compatibility.
class WtpPiToneEngine final : public wsprrypico::wtp::RfEngine {
public:
    using RealizeFrequency = std::function<std::optional<std::vector<std::uint64_t>>(
        const wsprrypico::wtp::Job&)>;
    WtpPiToneEngine(ITransmissionBackend& backend, BackendExecutionInputs inputs,
                    BackendKind kind, wsprrypico::wtp::Clock& clock,
                    RealizeFrequency realize, double calibration_ppm = 0.0,
                    std::function<void()> reset_execution_context = {},
                    bool backend_controls_enable = false,
                    std::function<void()> stop_execution_context = {},
                    bool allow_unqualified_frequency = false,
                    bool allow_non_amateur_frequency = false,
                    HardwareProfile hardware_profile = HardwareProfile::UNSPECIFIED,
                    std::function<bool(const wsprrypico::wtp::Job&, ExecutionPlan&, BackendExecutionInputs&)> prepare_inputs = {});
    ~WtpPiToneEngine() override;
    WtpPiToneEngine(const WtpPiToneEngine&) = delete;
    WtpPiToneEngine& operator=(const WtpPiToneEngine&) = delete;

    wsprrypico::wtp::PrepareResult prepare(const wsprrypico::wtp::Job&) override;
    bool schedules_locally() const override { return true; }
    bool owns_execution_plan() const override { return true; }
    std::uint64_t start_resolution_ns() const override { return 1'000'000; }
    std::uint64_t completion_acknowledgement_ns() const override { return 100'000'000; }
    bool schedule(const wsprrypico::wtp::Job&, std::uint64_t,
                  const wsprrypico::wtp::LocalStartConditions&) override;
    bool begin(const wsprrypico::wtp::Job&, std::uint64_t) override { return false; }
    wsprrypico::wtp::EngineReport poll(std::uint64_t) override;
    bool disable(std::uint64_t deadline_monotonic_ns) override;
    bool output_active() const override { return output_active_.load(); }
    bool startup_safe() const noexcept { return startup_safe_; }
    bool admit_output_enable();
    void observe_output_enable();
    struct LaunchObservation {
        std::uint64_t target_monotonic_ns;
        std::optional<std::uint64_t> observed_monotonic_ns;
    };
    LaunchObservation launch_observation() const;

private:
    void run(std::uint64_t start_monotonic_ns,
             wsprrypico::wtp::LocalStartConditions conditions) noexcept;
    void join_finished();

    ITransmissionBackend& backend_;
    BackendExecutionInputs inputs_;
    BackendKind kind_;
    wsprrypico::wtp::Clock& clock_;
    RealizeFrequency realize_;
    double calibration_ppm_ = 0.0;
    bool allow_unqualified_frequency_ = false;
    bool allow_non_amateur_frequency_ = false;
    HardwareProfile hardware_profile_;
    std::function<bool(const wsprrypico::wtp::Job&, ExecutionPlan&, BackendExecutionInputs&)> prepare_inputs_;
    std::function<void()> reset_execution_context_;
    std::function<void()> stop_execution_context_;
    bool backend_controls_enable_ = false;
    bool launch_missed_ = false;
    std::uint64_t start_monotonic_ns_ = 0;
    wsprrypico::wtp::LocalStartConditions start_conditions_{};
    std::optional<ExecutionPlan> plan_;
    std::string prepared_job_id_;
    std::uint64_t prepared_duration_ns_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> output_active_{false};
    bool startup_safe_ = false;
    bool stop_requested_ = false;
    bool worker_done_ = true;
    wsprrypico::wtp::EngineReport report_{};
};

} // namespace wsprrypi
