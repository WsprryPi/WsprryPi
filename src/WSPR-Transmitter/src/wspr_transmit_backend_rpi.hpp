/**
 * @file wspr_transmit_backend_rpi.hpp
 * @brief Raspberry Pi backend that realizes committed execution in hardware.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef WSPR_TRANSMIT_BACKEND_RPI_HPP
#define WSPR_TRANSMIT_BACKEND_RPI_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "transmission_backend.hpp"
#include "rpi_standard_feld_execution.hpp"
#include "wspr_transmit_backend.hpp"
#include "standard_feld_execution_gate.hpp"
#include "wspr_transmit.hpp"

/**
 * @brief Narrow register set used only to quiesce inherited GPIO transmitter
 *        state before the backend has created its normal runtime mapping.
 */
enum class RpiStartupQuiesceRegister
{
    Dma0ControlStatus,
    Dma0ControlBlockAddress,
    Dma0TransferInformation,
    Dma0SourceAddress,
    Dma0DestinationAddress,
    Dma0TransferLength,
    Dma0Stride,
    Dma0NextControlBlock,
    Dma0Debug,
    PwmControl,
    PwmDmaConfiguration,
    Gpclk0Control,
    GpioFunctionSelect0,
    GpioFunctionSelect2
};

/**
 * @brief Typed access seam for fresh-process startup quiescence.
 *
 * @details This deliberately cannot expose arbitrary addresses, raw mappings,
 * waveform memory, DMA allocation, or clock-frequency programming.
 */
class IRpiStartupQuiesceAccess
{
public:
    virtual ~IRpiStartupQuiesceAccess() = default;

    virtual bool supportedPlatform(std::string &error) = 0;
    virtual bool discoverPeripheralBase(
        std::uint32_t &base,
        std::string &error) = 0;
    virtual bool open(std::string &error) = 0;
    virtual bool map(
        std::uint32_t peripheral_base,
        std::size_t size,
        std::string &error) = 0;
    virtual bool read(
        RpiStartupQuiesceRegister reg,
        std::uint32_t &value,
        std::string &error) = 0;
    virtual bool write(
        RpiStartupQuiesceRegister reg,
        std::uint32_t value,
        std::string &error) = 0;
    virtual bool unmap(std::size_t size, std::string &error) = 0;
    virtual bool close(std::string &error) = 0;
};

/**
 * @brief Create the production fresh-process startup-quiesce access adapter.
 *
 * @details The concrete adapter and all raw MMIO details remain private to
 * wspr_transmit_backend_rpi.cpp.  This factory exists for the separately
 * built, guarded qualification executable.
 */
std::shared_ptr<IRpiStartupQuiesceAccess>
makeProductionRpiStartupQuiesceAccess();

/**
 * @brief Calculate the GPIO backend's effective PLLD rate.
 *
 * @details GPIO PPM is a source-clock rate estimate: positive means the
 * source runs fast and negative means it runs slow. The value is bounded to
 * the configuration contract so invalid execution plans fail closed.
 */
double gpioCorrectedPlldFrequency(double nominal_hz, double source_rate_ppm);

enum class GpioProcessorClockProfile
{
    Legacy500Mhz,
    Bcm2711
};

enum class GpioRfClockSource : std::uint32_t
{
    Oscillator = 1,
    PllD = 6
};

struct GpioRfClockPlan
{
    GpioRfClockSource source{GpioRfClockSource::PllD};
    double nominal_hz{0.0};
    double corrected_hz{0.0};
};

GpioRfClockPlan gpioPlanRfClock(
    GpioProcessorClockProfile profile,
    double minimum_tone_hz,
    double maximum_tone_hz,
    double source_rate_ppm);

std::uint32_t gpioBuildDividerWord(
    double source_hz,
    double tone_hz,
    bool round_up_one_lsb);

std::int64_t gpioDitherLowerClockCount(
    double lower_ratio,
    std::int64_t block_clocks,
    std::int64_t clocks_scheduled,
    std::int64_t lower_clocks_scheduled);

/**
 * @class WsprRpiBackend
 * @brief Raspberry Pi implementation of the generic transmission backend.
 *
 * @details
 * This backend owns all Raspberry Pi-specific transmission details,
 * including:
 * - Broadcom mailbox allocation and peripheral mapping
 * - DMA control-block construction and ring sequencing
 * - PWM and GPCLK programming
 * - GPIO drive-strength based output power control
 * - DMA watchdog monitoring and backend-private recovery
 *
 * The transmitter provides the backend-neutral execution plan and timing.
 * This backend translates that committed execution into Raspberry Pi
 * hardware actions while keeping DMA, mailbox, watchdog, and recovery state
 * private. It does not own WSPR planning or orchestration policy.
 */
class WsprRpiBackend : public WsprTransmitBackend,
                       public wsprrypi::ITransmissionBackend
{
public:
    /**
     * @brief Construct a Raspberry Pi backend bound to the controller bridge.
     *
     * @param owner Controller bridge used for state access, stop requests, and
     *              callback forwarding.
     */
    explicit WsprRpiBackend(IControllerBridge &owner);

    WsprRpiBackend(
        IControllerBridge &owner,
        std::shared_ptr<IRpiStartupQuiesceAccess> startup_quiesce_access,
        int startup_quiesce_gpio);

    /**
     * @brief Destroy the backend and release backend-owned resources.
     */
    ~WsprRpiBackend() override;

    wsprrypi::BackendInfo info() const override;
    wsprrypi::BackendCapabilities capabilities() const override;
    wsprrypi::BackendCompileResult configure(
        const wsprrypi::ExecutionPlan &plan,
        const wsprrypi::BackendExecutionInputs &inputs) override;
    wsprrypi::ExecutionResult execute(
        const wsprrypi::ExecutionPlan &plan) override;
    wsprrypi::StartupQuiesceResult quiesceForStartup() override;
    void stop() noexcept override;
    wsprrypi::CleanupResult cleanup() noexcept override;

    // The sole Standard Feld stop publisher.  It shares the short gate used
    // by the checked RF-on edge; it never joins, allocates, calls callbacks,
    // or performs hardware cleanup while that gate is held.
    bool publishStandardFeldStop(bool watchdog_fault = false) noexcept;

    /**
     * @brief Start the Raspberry Pi DMA watchdog.
     */
    void startFaultMonitoring() override;

    /**
     * @brief Stop the Raspberry Pi DMA watchdog.
     */
    void stopFaultMonitoring() override;

    /**
     * @brief Prepare DMA, mailbox, clock, and peripheral resources.
     */
    void prepareTransmission() override;

    /**
     * @brief Apply the committed transmission plan to Raspberry Pi hardware.
     *
     * @param plan Backend-neutral transmission snapshot.
     * @return Applied configuration result including the actual RF center
     *         frequency in hertz (Hz).
     */
    WsprTransmissionConfigureResult configureTransmission(
        const WsprTransmissionPlan &plan) override;

    /**
     * @brief Tear down DMA, mailbox, PWM, and clock resources.
     */
    void cleanupTransmission() override;

    /**
     * @brief Convert a drive-strength level into an estimated power value.
     *
     * @param level Drive-strength index.
     * @return Estimated output power in milliwatts (mW).
     */
    int getOutputPowerMilliwatts(int level) override;

    /**
     * @brief Enable Raspberry Pi RF output for the configured transmission.
     *
     * @param plan Backend-neutral transmission snapshot.
     */
    void beginTransmissionOutput(const WsprTransmissionPlan &plan) override;

    /**
     * @brief Disable Raspberry Pi RF output.
     */
    void endTransmissionOutput() override;

    /**
     * @brief Emit one symbol using the backend-private DMA ring.
     *
     * @param plan Backend-neutral transmission snapshot.
     * @param sym_num Symbol value to emit.
     * @param tsym Symbol duration in seconds.
     * @param symbol_index Zero-based symbol index, or `-1` when not
     *                     applicable.
     */
    void emitSymbol(
        const WsprTransmissionPlan &plan,
        const std::uint32_t &sym_num,
        const double &tsym,
        int symbol_index) override;

    /**
     * @brief Perform a best-effort hardware reset of active transmission
     *        output.
     */
    void resetTransmissionOutput() noexcept override;

    /**
     * @brief Return whether the DMA watchdog has latched a fault.
     */
    bool faulted() const noexcept override;

    /**
     * @brief Clear the latched DMA watchdog fault.
     */
    void clearFault() noexcept override;

    /**
     * @brief Enable or disable automatic watchdog recovery.
     *
     * @param enable True to enable automatic recovery.
     */
    void setAutoRecover(bool enable) noexcept override;

    /**
     * @brief Return whether automatic watchdog recovery is enabled.
     */
    bool autoRecoverEnabled() const noexcept override;

    /**
     * @brief Attempt synchronous recovery from a latched watchdog fault.
     *
     * @return True if recovery succeeded, false otherwise.
     */
    bool recoverFromFault() override;

    /**
     * @brief Return whether watchdog recovery is currently running.
     */
    bool recoveryInProgress() const noexcept override;

    static constexpr std::uint32_t frequencyDitherBlockClocks() noexcept
    {
        return PWM_CLOCKS_PER_ITER_NOMINAL;
    }

private:
    class StandardFeldExecutionAdapter;

    struct ExecutionPlanConfig
    {
        // Temporary bridge for the legacy DMA/tuning pipeline. The public
        // backend path is ExecutionPlan-based, but the low-level emitter still
        // consumes this reduced WSPR-specific shape.
        WsprTransmissionPlan compatibility_plan{};
        bool standard_feld{false};
    };

    struct PageInfo
    {
        std::uintptr_t b = 0;
        void *v = nullptr;
    };

    struct DMAConfig
    {
        double plld_nominal_freq;
        double plld_clock_frequency;
        double gpclk_nominal_freq;
        double gpclk_clock_frequency;
        GpioProcessorClockProfile processor_profile;
        GpioRfClockSource gpclk_source;
        volatile uint8_t *peripheral_base_virtual;
        uint32_t orig_gp0ctl;
        uint32_t orig_gp0div;
        uint32_t orig_gpfsel0;
        uint32_t orig_gpfsel1;
        uint32_t orig_gpfsel2;
        uint32_t orig_pwm_ctl;
        uint32_t orig_pwm_sta;
        uint32_t orig_pwm_rng1;
        uint32_t orig_pwm_rng2;
        uint32_t orig_pwm_fifocfg;

        DMAConfig();
    };

    struct MailboxStruct
    {
        uint32_t mem_ref = 0;
        std::uintptr_t bus_addr = 0;
        volatile uint8_t *virt_addr = nullptr;
        unsigned pool_size = 0;
        unsigned pool_cnt = 0;
    };

    struct CB
    {
        volatile unsigned int TI;
        volatile unsigned int SOURCE_AD;
        volatile unsigned int DEST_AD;
        volatile unsigned int TXFR_LEN;
        volatile unsigned int STRIDE;
        volatile unsigned int NEXTCONBK;
        volatile unsigned int RES1;
        volatile unsigned int RES2;
    };

    struct GPCTL
    {
        uint32_t SRC : 4;
        uint32_t ENAB : 1;
        uint32_t KILL : 1;
        uint32_t : 1;
        uint32_t BUSY : 1;
        uint32_t FLIP : 1;
        uint32_t MASH : 2;
        uint32_t : 13;
        uint32_t PASSWD : 8;
    };

    struct DMAregs
    {
        volatile unsigned int CS;
        volatile unsigned int CONBLK_AD;
        volatile unsigned int TI;
        volatile unsigned int SOURCE_AD;
        volatile unsigned int DEST_AD;
        volatile unsigned int TXFR_LEN;
        volatile unsigned int STRIDE;
        volatile unsigned int NEXTCONBK;
        volatile unsigned int DEBUG;
    };

    inline volatile uint32_t &access_bus_address(std::uintptr_t bus_addr);
    inline void set_bit_bus_address(std::uintptr_t base, unsigned int bit);
    inline void clear_bit_bus_address(std::uintptr_t base, unsigned int bit);
    void configure_transmit_gpio(int gpio);
    void start_watchdog();
    void stop_watchdog();
    void setup_dma();
    WsprTransmissionConfigureResult setup_dma_freq_table(
        const WsprTransmissionPlan &plan);
    void dma_cleanup();
    int get_gpio_power_mw(int level);
    void transmit_on(const WsprTransmissionPlan &plan);
    void transmit_off();
    void transmit_symbol(
        const WsprTransmissionPlan &plan,
        const std::uint32_t &sym_num,
        const double &tsym,
        int symbol_index);
    void transmit_symbol_with_envelope(
        const WsprTransmissionPlan &plan,
        const std::uint32_t &sym_num,
        const wsprrypi::RfEvent &event,
        bool &rf_enabled,
        int symbol_index);
    bool force_dma_reset_sequence() noexcept;
    wsprrypi::StartupQuiesceResult quiesce_fresh_process();
    bool set_mapped_transmit_gpio_safe(std::string &error) noexcept;
    void get_plld();
    void allocate_memory_pool(unsigned numpages);
    void get_real_mem_page_from_pool(void **vAddr, void **bAddr);
    void deallocate_memory_pool();
    void disable_hardware_sequence();
    void disable_clock();
    double bit_trunc(const double &d, const int &lsb);
    void create_dma_pages(PageInfo &const_page, PageInfo &instr_page, PageInfo instructions[]);
    void request_watchdog_recovery() noexcept;
    void recovery_worker();
    bool recover_from_watchdog_fault_locked();
    std::optional<ExecutionPlanConfig> build_execution_plan_config(
        const wsprrypi::ExecutionPlan &plan,
        const wsprrypi::BackendExecutionInputs &inputs,
        wsprrypi::BackendCompileResult *result = nullptr) const;
    std::uint32_t reconstruct_wspr_symbol(
        const wsprrypi::RfEvent &event,
        const WsprTransmissionPlan &plan) const;
    std::uint32_t reconstruct_compatibility_symbol(
        const wsprrypi::RfEvent &event,
        const WsprTransmissionPlan &plan,
        long min_symbol,
        long max_symbol) const;
    void execute_qrss_event(
        const wsprrypi::RfEvent& event,
        const WsprTransmissionPlan& plan,
        bool& rf_enabled,
        int symbol_index);
    void execute_fskcw_event(
        const wsprrypi::RfEvent& event,
        const WsprTransmissionPlan& plan,
        bool& rf_enabled,
        int symbol_index);
    void execute_dfcw_event(
        const wsprrypi::RfEvent& event,
        const WsprTransmissionPlan& plan,
        bool& rf_enabled,
        int symbol_index);

    IControllerBridge &owner_;
    std::shared_ptr<IRpiStartupQuiesceAccess> startup_quiesce_access_;

    std::thread watchdog_thread_{};
    std::atomic<bool> watchdog_stop_{true};
    std::atomic<bool> watchdog_faulted_{false};
    std::atomic<bool> watchdog_auto_recover_{true};
    // Sole Standard Feld lifecycle and RF-enable synchronization. It is never
    // held across joins, callbacks, allocation, progress, or cleanup.
    wsprrypi::StandardFeldExecutionGate standard_feld_gate_{};
    std::atomic<bool> recovery_stop_{false};
    std::atomic<bool> recovery_pending_{false};
    std::atomic<bool> recovery_in_progress_{false};

    mutable std::mutex recovery_rate_mtx_{};
    std::deque<std::chrono::steady_clock::time_point> recovery_attempts_{};
    std::chrono::steady_clock::time_point recovery_defer_until_{};
    WsprTransmitState post_recovery_state_{WsprTransmitState::ENABLED};

    std::thread recovery_thread_{};
    std::mutex recovery_wait_mtx_;
    std::condition_variable recovery_cv_;
    std::mutex recovery_mtx_;

    std::atomic<std::uint32_t> watchdog_last_conblk_{0};
    std::atomic<std::uint32_t> watchdog_last_txfr_len_{0};
    std::atomic<std::chrono::steady_clock::time_point::rep> watchdog_last_change_ns_{0};

    bool dma_setup_done_{false};
    std::uint32_t dma_buf_ptr_{0};
    double pwm_clock_init_{0};
    std::array<std::uint32_t, 8> active_gpclk_words_{};
    int watchdog_cpu_{1};
    int configured_tx_gpio_{4};
    std::optional<ExecutionPlanConfig> configured_plan_{};
    PageInfo const_page_{};
    PageInfo instr_page_{};
    PageInfo instructions_[1024]{};
    DMAConfig dma_config_{};
    MailboxStruct mailbox_struct_{};

    static constexpr auto kRecoveryWindow = std::chrono::minutes(10);
    static constexpr std::size_t kMaxRecoveriesInWindow = 3;
    static constexpr auto kMinRecoveryInterval = std::chrono::seconds(30);
    static constexpr uint32_t GPIO_BUS_BASE = 0x7E200000;
    static constexpr uint32_t CM_GP0CTL_BUS = 0x7E101070;
    static constexpr uint32_t CM_GP0DIV_BUS = 0x7E101074;
    static constexpr uint32_t PADS_GPIO_0_27_BUS = 0x7E10002C;
    static constexpr uint32_t CLK_BUS_BASE = 0x7E101000;
    static constexpr uint32_t DMA_BUS_BASE = 0x7E007000;
    static constexpr uint32_t PWM_BUS_BASE = 0x7E20C000;
    // Covers GPIO (0x200000), PWM (0x20c000), and all lower owned blocks.
    static constexpr std::size_t STARTUP_QUIESCE_MAP_SIZE = 0x210000;

    static constexpr std::uint32_t PWM_CLOCKS_PER_ITER_NOMINAL = 1000;

    static inline constexpr std::array<int, 8> DRIVE_STRENGTH_TABLE = {
        2, 4, 6, 8, 10, 12, 14, 16};
};

#endif
