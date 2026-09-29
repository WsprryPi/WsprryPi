#ifndef WSPR_TRANSMIT_BACKEND_SI5351_HPP
#define WSPR_TRANSMIT_BACKEND_SI5351_HPP

#include "si5351_device.hpp"
#include "si5351_planner.hpp"
#include "transmission_backend.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <time.h>
#include <vector>

class IControllerBridge;

/**
 * @brief Si5351 transmission backend
 *
 * Implements the generic transmission backend interface using an Si5351A
 * clock generator driven from an external reference.
 *
 * This backend should:
 *
 * - Translate the generic execution plan into precomputed Si5351 tone sets
 * - Program static device state before transmission
 * - Apply minimal register changes at symbol boundaries
 * - Enable or disable CLK0 as needed
 *
 * This backend should not own scheduler policy or high-level planning.
 */
class WsprSi5351Backend : public wsprrypi::ITransmissionBackend
{
public:
    /**
     * @brief Backend configuration
     */
    struct Config
    {
        Si5351Device::Config device;
        Si5351Planner::Config planner;
        std::shared_ptr<Si5351Device::I2CAdapter> device_adapter{};

        /**
         * @brief Si5351 drive-strength power level.
         *
         * This level maps directly to Si5351 output drive strength, not
         * calibrated RF output power. Level 1 is the safest default.
         */
        int power_level = 1;

        /**
         * @brief Skip hardware access for harness and CI validation.
         *
         * Dry-run mode still executes backend planning, timing, and callback
         * flow, but it does not open I2C or enable RF output.
         */
        bool dry_run = false;

        // Maintainer opt-in: adjacent compatible integer-MS plans may retune
        // PLLA without reset/inhibition. First/incompatible tones remain guarded.
        bool pll_only_updates = false;

        // Typed integration hooks for a finite scheduled tone. Admission runs
        // after the output-enable register read, immediately before its write.
        // The observer runs after the first successful enable transaction.
        std::function<bool()> output_enable_admission;
        std::function<void()> first_output_enabled;
        bool anchor_finite_tone_to_enable = false;
        bool confirm_output_disable = false;
    };

    /**
     * @brief Construct the backend
     *
     * @param owner Controller bridge used for callbacks and stop requests.
     * @param config Backend configuration
     */
    WsprSi5351Backend(
        IControllerBridge& owner,
        const Config& config);

    /**
     * @brief Destroy the backend
     */
    ~WsprSi5351Backend() override;

    WsprSi5351Backend(const WsprSi5351Backend&) = delete;
    WsprSi5351Backend& operator=(const WsprSi5351Backend&) = delete;

    WsprSi5351Backend(WsprSi5351Backend&&) = delete;
    WsprSi5351Backend& operator=(WsprSi5351Backend&&) = delete;

    /**
     * @brief Return backend identity information.
     *
     * @return Backend identity details.
     */
    wsprrypi::BackendInfo info() const override;

    /**
     * @brief Return backend capability information.
     *
     * @return Conservative Si5351 backend capability details.
     */
    wsprrypi::BackendCapabilities capabilities() const override;

    /**
     * @brief Configure the backend for a compiled execution plan
     *
     * @param plan Generic execution plan
     * @param inputs Backend execution inputs
     * @return Backend compile result
     */
    wsprrypi::BackendCompileResult configure(
        const wsprrypi::ExecutionPlan& plan,
        const wsprrypi::BackendExecutionInputs& inputs) override;

    /**
     * @brief Execute the supplied execution plan.
     *
     * @param plan Generic execution plan
     * @return Execution result
     */
    wsprrypi::ExecutionResult execute(
        const wsprrypi::ExecutionPlan& plan) override;

    /** Disable every Si5351 output without configuring transmission state. */
    wsprrypi::StartupQuiesceResult quiesceForStartup() override;

    /**
     * @brief Request transmission stop.
     */
    void stop() noexcept override;

    /**
     * @brief Perform best-effort backend cleanup.
     */
    wsprrypi::CleanupResult cleanup() noexcept override;

    /**
     * @brief Return the backend configuration
     *
     * @return Active configuration
     */
    const Config& getConfig() const noexcept;

private:
    bool outputIsDisabled();
    std::optional<timespec> first_enable_time_;
    /**
     * @brief Map a generic transmission mode to the Si5351 planner mode.
     *
     * @param mode Generic transmission mode.
     * @param planner_mode Filled with the planner mode on success.
     * @return True if the mode is supported by this backend.
     */
    bool mapPlannerMode(
        wsprrypi::TransmissionMode mode,
        Si5351Planner::Mode& planner_mode) const;

    /**
     * @brief Extract unique tone frequencies from an execution plan.
     *
     * @param plan Generic execution plan.
     * @param frequencies Filled with unique frequencies in encounter order.
     * @param event_tone_indexes Filled with tone indexes per event.
     * @param error Filled with an error on failure.
     * @return True if tone extraction succeeded.
     */
    bool extractToneFrequencies(
        const wsprrypi::ExecutionPlan& plan,
        std::vector<double>& frequencies,
        std::vector<std::size_t>& event_tone_indexes,
        std::string& error) const;

    /**
     * @brief Validate a generated Si5351 planner result.
     *
     * @param plan Planner output to validate.
     * @param expected_tones Required tone count for the mode.
     * @param error Filled with an error on failure.
     * @return True if the planner output is usable.
     */
    bool validatePlannerOutput(
        const Si5351Planner::Plan& plan,
        std::size_t expected_tones,
        std::string& error) const;

    /**
     * @brief Return whether an execution plan matches the cached plan.
     *
     * @param plan Candidate plan supplied to execute.
     * @return True if the plan matches the configured plan.
     */
    bool planMatchesConfigured(
        const wsprrypi::ExecutionPlan& plan) const noexcept;

    /**
     * @brief Map a Si5351 power level to device drive strength.
     *
     * @param power_level Si5351 drive-strength level from 1 through 4.
     * @param drive_strength Filled with the mapped drive strength.
     * @return True if the power level is valid.
     */
    bool mapPowerLevelToDriveStrength(
        int power_level,
        Si5351Device::DriveStrength& drive_strength) const noexcept;

    /**
     * @brief Reset active drive strength from configured power level.
     */
    void resetActiveDriveStrengthFromConfig() noexcept;

    /**
     * @brief Return the expected unique tone count for a planner mode.
     *
     * @param mode Planner mode.
     * @return Expected unique tone count.
     */
    std::size_t expectedToneCount(Si5351Planner::Mode mode) const noexcept;

    /**
     * @brief Reset backend transient state
     */
    void resetState();

    /**
     * @brief Apply startup programming before execution begins
     *
     * @return True on success
     */
    bool applyStartupProgramming();

    /**
     * @brief Apply idle programming after execution ends
     *
     * @return True on success
     */
    bool applyIdleProgramming();

    /**
     * @brief Switch the active output to the specified tone index
     *
     * @param tone_index Tone index in the planned tone-set array
     * @param rf_enabled Whether the output is currently enabled
     * @return True on success
     */
    bool applyTone(std::size_t tone_index, bool rf_enabled);

    /**
     * @brief Enable the transmit output
     *
     * @return True on success
     */
    bool waitForPllReady();
    bool enableTransmitOutput();

    /**
     * @brief Disable the transmit output
     *
     * @return True on success
     */
    bool disableTransmitOutput();

    bool runEnvelopeEvent(
        const wsprrypi::RfEvent& event,
        const timespec& event_start,
        bool& rf_enabled,
        std::string& error);

    IControllerBridge& owner_;
    Config config_;
    Si5351Device device_;
    wsprrypi::ExecutionPlan current_plan_;
    std::vector<double> unique_tone_frequencies_;
    std::vector<std::size_t> event_tone_indexes_;
    Si5351Planner::Plan si5351_plan_;
    bool configured_;
    bool execution_cleanup_completed_;
    wsprrypi::CleanupResult execution_cleanup_result_;
    bool stop_requested_;
    int active_power_level_;
    Si5351Device::DriveStrength active_drive_strength_;
    std::size_t current_tone_index_;
};

#endif
