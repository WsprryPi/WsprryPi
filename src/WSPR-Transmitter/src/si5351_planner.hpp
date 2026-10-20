#ifndef SI5351_PLANNER_HPP
#define SI5351_PLANNER_HPP

#include "si5351_device.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @brief Builds Si5351 register plans for a transmission
 *
 * Converts requested RF frequencies into a backend-specific programming plan
 * suitable for efficient runtime execution.
 *
 * The planner should:
 *
 * - Choose the static PLL parking strategy
 * - Build initial register writes
 * - Precompute per-tone register write sets
 * - Minimize symbol-boundary work
 *
 * The planner should not perform I2C I/O or manage execution timing.
 */
class Si5351Planner
{
public:
    // Numerical planner envelope; individual tone sets still require planning.
    static constexpr double minimum_output_hz = 1'000'000.0 / 128.0;
    static constexpr double maximum_output_hz = 200'000'000.0;
    /**
     * @brief Transmission mode understood by the planner
     */
    enum class Mode
    {
        TONE,
        WSPR,
        QRSS,
        FSKCW,
        DFCW
    };

    /**
     * @brief Planner configuration
     */
    struct Config
    {
        std::uint32_t reference_hz = 27000000;
        double calibration_ppm = 0.0;
        std::uint64_t parked_pll_hz = 850000000;
        Si5351Device::Output tx_output = Si5351Device::Output::CLK0;
        bool park_unused_outputs = true;
        bool disable_tx_output_when_idle = true;
        // Append optional fields to preserve existing aggregate initialization.
        bool prefer_integer_multisynth = false;
        // Disable only for pure MultiSynth analysis; normal execution retains
        // the guarded PLL-retune path for every supported mode.
        bool allow_guarded_pll_retune = true;
    };

    /**
     * @brief Frequency entry for one tone
     */
    struct ToneEntry
    {
        double frequency_hz = 0.0;
    };

    /**
     * @brief Decoded Si5351 divider parameters
     */
    struct DividerPlan
    {
        bool valid = false;
        std::uint32_t integer = 0;
        std::uint32_t numerator = 0;
        std::uint32_t denominator = 1;
        std::uint32_t p1 = 0;
        std::uint32_t p2 = 0;
        std::uint32_t p3 = 1;
        double actual_ratio = 0.0;
    };

    /**
     * @brief Per-tone PLL-retune plan
     *
     * The plan records the complete divider and register data.  The containing
     * tone set marks that the backend must inhibit the output around these
     * writes.
     */
    struct PllRetuneCandidate
    {
        bool valid = false;
        double target_pll_hz = 0.0;
        double actual_pll_hz = 0.0;
        std::uint32_t r_divider = 1;
        DividerPlan pll;
        DividerPlan multisynth;
        std::vector<Si5351Device::RegisterWrite> pll_writes;
        std::vector<Si5351Device::RegisterWrite> multisynth_writes;
    };

    /**
     * @brief Precomputed register set for one tone
     *
     * This should contain only the writes needed to move the active output
     * into the requested tone state, not necessarily a full chip image.
     */
    struct ToneRegisterSet
    {
        double requested_hz = 0.0;
        double actual_hz = 0.0;
        std::uint32_t r_divider = 1;
        std::vector<Si5351Device::RegisterWrite> writes;
        bool requires_output_inhibit = false;
        PllRetuneCandidate pll_retune_candidate;
    };

    /**
     * @brief Full planning result for one transmission
     */
    struct Plan
    {
        Mode mode = Mode::TONE;
        double calibration_ppm = 0.0;
        double effective_reference_hz = 0.0;
        std::vector<Si5351Device::RegisterWrite> startup_writes;
        std::vector<Si5351Device::RegisterWrite> idle_writes;
        std::vector<ToneRegisterSet> tone_sets;
    };

    /**
     * @brief Construct a planner
     *
     * @param config Planner configuration
     */
    explicit Si5351Planner(const Config& config);

    /**
     * @brief Build a transmission plan
     *
     * @param mode Transmission mode
     * @param tones Unique tone frequencies to precompute
     * @return Planned startup and tone register data
     */
    Plan buildPlan(
        Mode mode,
        const std::vector<ToneEntry>& tones) const;

    /**
     * @brief Return planner configuration
     *
     * @return Active planner configuration
     */
    const Config& getConfig() const noexcept;

private:
    /**
     * @brief Build startup writes shared by all tones
     *
     * @return Register writes for initial device programming
     */
    std::vector<Si5351Device::RegisterWrite> buildStartupWrites() const;

    /**
     * @brief Build idle writes used when transmission is inactive
     *
     * @return Register writes for the idle state
     */
    std::vector<Si5351Device::RegisterWrite> buildIdleWrites() const;

    /**
     * @brief Build one tone register set
     *
     * @param frequency_hz Requested RF frequency
     * @param allow_pll_retune_candidate Whether a guarded divide-by-6 PLL
     * retune may be used when the parked-PLL plan is unavailable
     * @return Precomputed tone register set
     */
    ToneRegisterSet buildToneRegisterSet(
        double frequency_hz,
        std::uint32_t r_divider,
        bool allow_pll_retune_candidate,
        std::uint32_t integer_multisynth = 0) const;

    /**
     * @brief Quantize a requested frequency to the achievable output
     *
     * @param requested_hz Requested output frequency
     * @return Achievable output frequency
     */
    double quantizeFrequency(
        double requested_hz,
        std::uint32_t r_divider) const;

    /**
     * @brief Return the calibrated reference used for synthesis planning
     *
     * Uses the existing GPIO-compatible convention: a positive correction
     * lowers the effective reference by the requested parts per million.
     *
     * @return Finite positive effective reference, or zero when unusable
     */
    double effectiveReferenceHz() const noexcept;

    Config config_;
};

#endif
