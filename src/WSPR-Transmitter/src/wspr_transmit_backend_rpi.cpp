/**
 * @file wspr_transmit_backend_rpi.cpp
 * @brief Raspberry Pi hardware realization for committed transmission plans.
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

#include "wspr_transmit_backend_rpi.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/mman.h>
#include <unistd.h>

#include "bcm_model.hpp"
#include "mailbox.hpp"
#include "signal_handler.hpp"
#include "version.hpp"

namespace
{
static inline timespec add_ns(timespec t, int64_t ns);
static bool wait_until_interruptible(
    IControllerBridge& owner,
    clockid_t clock_id,
    const timespec& target);
}

class WsprRpiBackend::StandardFeldExecutionAdapter final
    : public wsprrypi::IRpiStandardFeldExecutionAdapter
{
public:
    StandardFeldExecutionAdapter(
        WsprRpiBackend& backend,
        const WsprTransmissionPlan& compatibility_plan,
        wsprrypi::StandardFeldExecutionGate::Generation generation)
        : backend_(backend),
          compatibility_plan_(compatibility_plan), generation_(generation)
    {
    }

    bool cancellation_requested() const override
    {
        return backend_.standard_feld_gate_.stop_requested(generation_) ||
            backend_.owner_.backendShouldStop();
    }

    bool watchdog_faulted() const override
    {
        return backend_.standard_feld_gate_.watchdog_faulted(generation_) ||
            backend_.watchdog_faulted_.load(std::memory_order_acquire);
    }

    std::string watchdog_diagnostic() const override
    {
        return "Standard Feld DMA watchdog fault latched.";
    }

    wsprrypi::RpiStandardFeldAdapterResult establish_initial_safe_state() override
    {
        return shutdown_and_verify("initial safe-state establishment");
    }

    wsprrypi::RpiStandardFeldAdapterResult wait_until(
        std::chrono::nanoseconds absolute_deadline) override
    {
        if (!origin_.has_value())
        {
            struct timespec now{};
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            {
                return wsprrypi::RpiStandardFeldAdapterResult::failure(
                    "CLOCK_MONOTONIC origin acquisition failed.");
            }
            origin_ = now;
        }
        const timespec target = add_ns(
            *origin_,
            absolute_deadline.count());
        if (!wait_until_interruptible(
                backend_.owner_, CLOCK_MONOTONIC, target))
            return wsprrypi::RpiStandardFeldAdapterResult::cancelled();
        return wsprrypi::RpiStandardFeldAdapterResult::success();
    }

    wsprrypi::RpiStandardFeldAdapterResult apply_carrier(
        double frequency_hz,
        std::size_t) override
    {
        const double configured_carrier_hz =
            compatibility_plan_.frequency_hz -
            1.5 * compatibility_plan_.tone_spacing_hz;
        if (frequency_hz != configured_carrier_hz)
        {
            return wsprrypi::RpiStandardFeldAdapterResult::failure(
                "Compiled carrier does not match the configured GPIO clock carrier.");
        }
        return wsprrypi::RpiStandardFeldAdapterResult::success();
    }

    wsprrypi::RpiStandardFeldAdapterResult apply_rf_checked(
        const wsprrypi::RfEvent& event,
        std::size_t event_index) override
    {
        if (!event.rf_on)
        {
            // RF-off is intrinsically safe. Release before joining the
            // watchdog so a concurrently faulting watchdog can publish its
            // stop under the same transition mutex and exit.
            if (rf_enabled_)
            {
                backend_.stop_watchdog();
                backend_.disable_clock();
                rf_enabled_ = false;
            }
            return wsprrypi::RpiStandardFeldAdapterResult::success();
        }

        if (!rf_enabled_)
        {
            const auto authorization = backend_.standard_feld_gate_.authorize_rf_on(
                generation_, [this]() { backend_.transmit_on(compatibility_plan_); });
            if (authorization != wsprrypi::StandardFeldExecutionGate::RfAuthorization::GRANTED)
                return wsprrypi::RpiStandardFeldAdapterResult::cancelled();
            rf_enabled_ = true;
        }
        backend_.start_watchdog();
        backend_.transmit_symbol(
            compatibility_plan_,
            0U,
            std::chrono::duration<double>(event.duration).count(),
            static_cast<int>(event_index));
        return wsprrypi::RpiStandardFeldAdapterResult::success();
    }

    wsprrypi::RpiStandardFeldAdapterResult report_progress(
        std::size_t event_index,
        const wsprrypi::RfEvent::RasterProgress& progress) override
    {
        if (progress.absolute_position != event_index)
        {
            return wsprrypi::RpiStandardFeldAdapterResult::failure(
                "Raster progress identity disagrees with its event index.");
        }
        if (!backend_.owner_.backendReportRasterProgress(
                generation_.value, event_index, progress))
            return wsprrypi::RpiStandardFeldAdapterResult::failure(
                "Internal Standard Feld progress storage rejected an update.");
        return wsprrypi::RpiStandardFeldAdapterResult::success();
    }

    wsprrypi::RpiStandardFeldAdapterResult complete_terminal_shutdown() override
    {
        return shutdown_and_verify("complete terminal shutdown");
    }

private:
    wsprrypi::RpiStandardFeldAdapterResult shutdown_and_verify(
        const char* context)
    {
        backend_.stop_watchdog();
        backend_.disable_hardware_sequence();
        rf_enabled_ = false;

        if (!backend_.watchdog_stop_.load(std::memory_order_acquire))
            return wsprrypi::RpiStandardFeldAdapterResult::failure(
                std::string(context) + ": watchdog did not stop.");
        if (backend_.dma_config_.peripheral_base_virtual == nullptr)
            return wsprrypi::RpiStandardFeldAdapterResult::success();

        const std::uint32_t dma_cs = static_cast<std::uint32_t>(
            backend_.access_bus_address(WsprRpiBackend::DMA_BUS_BASE + 0x00));
        const std::uint32_t pwm_ctl = static_cast<std::uint32_t>(
            backend_.access_bus_address(WsprRpiBackend::PWM_BUS_BASE + 0x00));
        const std::uint32_t pwm_dmac = static_cast<std::uint32_t>(
            backend_.access_bus_address(WsprRpiBackend::PWM_BUS_BASE + 0x08));
        const std::uint32_t gp0ctl = static_cast<std::uint32_t>(
            backend_.access_bus_address(WsprRpiBackend::CM_GP0CTL_BUS));
        std::string failures;
        const auto add = [&failures](const char* component) {
            if (!failures.empty()) failures += ", ";
            failures += component;
        };
        if ((dma_cs & 1U) != 0U) add("DMA remains active");
        if (pwm_ctl != 0U) add("PWM control remains enabled");
        if (pwm_dmac != 0U) add("PWM DMA remains enabled");
        if ((gp0ctl & (1U << 7U)) != 0U) add("GPCLK busy remains asserted");
        if (!failures.empty())
            return wsprrypi::RpiStandardFeldAdapterResult::failure(
                std::string(context) + ": " + failures + ".");
        return wsprrypi::RpiStandardFeldAdapterResult::success();
    }

    WsprRpiBackend& backend_;
    const WsprTransmissionPlan& compatibility_plan_;
    wsprrypi::StandardFeldExecutionGate::Generation generation_{};
    std::optional<timespec> origin_{};
    bool rf_enabled_{false};
};

namespace
{
    static constexpr size_t NUM_PAGES = 4096;
    static constexpr double kWsprSymbolPeriodSeconds = 8192.0 / 12000.0;
    static constexpr double kWsprToneSpacingHz = 1.0 / kWsprSymbolPeriodSeconds;
    static constexpr double kPwmSampleTableSize = 1024.0;
    static constexpr double kMaxRandomizedIterMultiplier = 1.5;

    static std::chrono::nanoseconds max_dma_block_duration(
        double pwm_clock_init_hz,
        std::uint32_t pwm_clocks_per_iter_nominal) noexcept
    {
        if (!std::isfinite(pwm_clock_init_hz) || pwm_clock_init_hz <= 0.0)
        {
            return std::chrono::nanoseconds::zero();
        }

        const double max_samples =
            static_cast<double>(pwm_clocks_per_iter_nominal) *
            kMaxRandomizedIterMultiplier;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(
                (max_samples * kPwmSampleTableSize) / pwm_clock_init_hz));
    }

    static std::chrono::nanoseconds dma_block_timeout(
        double pwm_clock_init_hz,
        std::uint32_t pwm_clocks_per_iter_nominal,
        std::chrono::nanoseconds minimum_timeout,
        std::chrono::nanoseconds slack) noexcept
    {
        const auto block_duration =
            max_dma_block_duration(pwm_clock_init_hz, pwm_clocks_per_iter_nominal);
        const auto derived_timeout =
            block_duration + block_duration + slack;
        return derived_timeout > minimum_timeout
                   ? derived_timeout
                   : minimum_timeout;
    }

    static inline int cpu_count() noexcept
    {
        long n = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1)
            return 1;
        if (n > INT32_MAX)
            return INT32_MAX;
        return static_cast<int>(n);
    }

    static inline int clamp_cpu(int cpu, int ncpu) noexcept
    {
        if (ncpu <= 1)
            return 0;
        if (cpu < 0)
            return 0;
        if (cpu >= ncpu)
            return ncpu - 1;
        return cpu;
    }

    class MailboxMemoryPool
    {
        size_t total_size_;
        uint32_t mem_ref_;
        std::uintptr_t bus_addr_;
        volatile uint8_t *virt_addr_;

    public:
        explicit MailboxMemoryPool(unsigned numpages)
            : total_size_(numpages * Mailbox::PAGE_SIZE),
              mem_ref_(0), bus_addr_(0), virt_addr_(nullptr)
        {
            try
            {
                mem_ref_ = mailbox.memAlloc(total_size_, Mailbox::BLOCK_SIZE);

                bus_addr_ = mailbox.memLock(mem_ref_);
                if (bus_addr_ == 0)
                    throw std::runtime_error("MailboxMemoryPool: memLock failed");

                auto phys = static_cast<off_t>(Mailbox::busToPhysical(bus_addr_));
                virt_addr_ = mailbox.mapMem(phys, total_size_);
                if (virt_addr_ == nullptr)
                    throw std::runtime_error("MailboxMemoryPool: mapMem failed");
            }
            catch (const std::runtime_error &e)
            {
                if (std::string(e.what()).find("timed out") != std::string::npos)
                {
                    throw;
                }

                if (virt_addr_)
                {
                    mailbox.unMapMem(virt_addr_, total_size_);
                    virt_addr_ = nullptr;
                }
                if (bus_addr_)
                {
                    mailbox.memUnlock(mem_ref_);
                    bus_addr_ = 0;
                }
                if (mem_ref_)
                {
                    mailbox.memFree(mem_ref_);
                    mem_ref_ = 0;
                }
                throw;
            }
        }

        ~MailboxMemoryPool()
        {
            if (virt_addr_)
            {
                mailbox.unMapMem(virt_addr_, total_size_);
            }
            if (bus_addr_)
            {
                mailbox.memUnlock(mem_ref_);
                mailbox.memFree(mem_ref_);
            }
        }
    };

    static inline bool gpclk0_wait_not_busy(
        volatile uint32_t &gp0ctl_reg,
        int max_us)
    {
        const int polls = (max_us <= 0) ? 0 : (max_us / 100);
        for (int i = 0; i < polls; ++i)
        {
            if ((gp0ctl_reg & (1 << 7)) == 0)
                return true;

            struct timespec ts{};
            ts.tv_sec = 0;
            ts.tv_nsec = 100000;
            nanosleep(&ts, nullptr);
        }
        return ((gp0ctl_reg & (1 << 7)) == 0);
    }

    static inline void gpclk0_disable_wait(volatile uint32_t &gp0ctl_reg)
    {
        uint32_t ctl = gp0ctl_reg;
        ctl = (ctl & 0x7EFu) | 0x5A000000u;
        gp0ctl_reg = ctl;

        if (gpclk0_wait_not_busy(gp0ctl_reg, 200000))
            return;

        ctl = static_cast<uint32_t>(gp0ctl_reg);
        ctl |= (1u << 5);
        ctl |= 0x5A000000u;
        gp0ctl_reg = static_cast<int>(ctl);

        (void)gpclk0_wait_not_busy(gp0ctl_reg, 200000);
    }

    static inline timespec add_ns(timespec t, int64_t ns)
    {
        int64_t sec = ns / 1000000000LL;
        int64_t rem = ns % 1000000000LL;
        t.tv_sec += sec;
        t.tv_nsec += rem;
        if (t.tv_nsec >= 1000000000L)
        {
            t.tv_sec += 1;
            t.tv_nsec -= 1000000000L;
        }
        else if (t.tv_nsec < 0)
        {
            t.tv_sec -= 1;
            t.tv_nsec += 1000000000L;
        }
        return t;
    }

    static inline int64_t diff_ns(const timespec &future, const timespec &past)
    {
        return (static_cast<int64_t>(future.tv_sec) -
                static_cast<int64_t>(past.tv_sec)) *
                   1000000000LL +
               (static_cast<int64_t>(future.tv_nsec) -
                static_cast<int64_t>(past.tv_nsec));
    }

    static bool wait_until_interruptible(
        IControllerBridge &owner,
        clockid_t clk_id,
        const timespec &target)
    {
        while (!owner.backendShouldStop())
        {
            timespec now{};
            if (::clock_gettime(clk_id, &now) != 0)
            {
                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "clock_gettime");
            }

            const int64_t remaining_ns = diff_ns(target, now);
            if (remaining_ns <= 0)
            {
                return true;
            }

            if (!owner.backendWaitInterruptableFor(
                    std::chrono::nanoseconds{remaining_ns}))
            {
                return false;
            }
        }

        return false;
    }

    static bool trace_wspr_tones_enabled() noexcept
    {
        const char *env = std::getenv("WSPRYPI_TRACE_WSPR_TONES");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }

    class ProductionRpiStartupQuiesceAccess final
        : public IRpiStartupQuiesceAccess
    {
    public:
        bool supportedPlatform(std::string &error) override
        {
            return platform_supports_gpio_clock_transmission(&error);
        }

        bool discoverPeripheralBase(
            std::uint32_t &base,
            std::string &error) override
        {
            if (opened_ || mapped_ != nullptr)
                return fail(error, "Peripheral discovery requested after startup-quiesce access opened.");
            try
            {
                base = Mailbox::discoverPeripheralBase();
                return true;
            }
            catch (const std::exception &e)
            {
                return fail(error, std::string("Could not discover peripheral base: ") + e.what());
            }
            catch (...)
            {
                return fail(error, "Could not discover peripheral base.");
            }
        }

        bool open(std::string &error) override
        {
            if (opened_ || mapped_ != nullptr)
                return fail(error, "Startup-quiesce access is already open.");
            try
            {
                mailbox.open();
                opened_ = true;
                return true;
            }
            catch (const std::exception &e)
            {
                return fail(error, std::string("Could not open startup-quiesce access: ") + e.what());
            }
            catch (...)
            {
                return fail(error, "Could not open startup-quiesce access.");
            }
        }

        bool map(
            std::uint32_t peripheral_base,
            std::size_t size,
            std::string &error) override
        {
            if (!opened_ || mapped_ != nullptr)
                return fail(error, "Startup-quiesce map requested in an invalid lifecycle state.");
            if (size != kMapSize)
                return fail(error, "Startup-quiesce map size was not exactly 0x210000 bytes.");
            try
            {
                mapped_ = mailbox.mapMem(peripheral_base, size);
                if (mapped_ == nullptr)
                    return fail(error, "Startup-quiesce peripheral mapping returned null.");
                return true;
            }
            catch (const std::exception &e)
            {
                return fail(error, std::string("Could not map startup-quiesce peripherals: ") + e.what());
            }
            catch (...)
            {
                return fail(error, "Could not map startup-quiesce peripherals.");
            }
        }

        bool read(
            RpiStartupQuiesceRegister reg,
            std::uint32_t &value,
            std::string &error) override
        {
            volatile std::uint32_t *address = registerAddress(reg, error);
            if (address == nullptr)
                return false;
            value = *address;
            return true;
        }

        bool write(
            RpiStartupQuiesceRegister reg,
            std::uint32_t value,
            std::string &error) override
        {
            volatile std::uint32_t *address = registerAddress(reg, error);
            if (address == nullptr)
                return false;
            *address = value;
            return true;
        }

        bool unmap(std::size_t size, std::string &error) override
        {
            if (!opened_ || mapped_ == nullptr || size != kMapSize)
                return fail(error, "Startup-quiesce unmap requested in an invalid lifecycle state.");
            try
            {
                mailbox.unMapMem(mapped_, size);
                mapped_ = nullptr;
                return true;
            }
            catch (const std::exception &e)
            {
                return fail(error, std::string("Could not unmap startup-quiesce peripherals: ") + e.what());
            }
            catch (...)
            {
                return fail(error, "Could not unmap startup-quiesce peripherals.");
            }
        }

        bool close(std::string &error) override
        {
            if (!opened_ || mapped_ != nullptr)
                return fail(error, "Startup-quiesce close requested in an invalid lifecycle state.");
            mailbox.close();
            opened_ = false;
            return true;
        }

    private:
        static constexpr std::size_t kMapSize = 0x210000;

        static bool fail(std::string &error, const std::string &message)
        {
            if (error.empty())
                error = message;
            return false;
        }

        volatile std::uint32_t *registerAddress(
            RpiStartupQuiesceRegister reg,
            std::string &error)
        {
            if (!opened_ || mapped_ == nullptr)
            {
                fail(error, "Startup-quiesce register access requested while unmapped.");
                return nullptr;
            }

            std::size_t offset = 0;
            switch (reg)
            {
            case RpiStartupQuiesceRegister::Dma0ControlStatus: offset = 0x7000; break;
            case RpiStartupQuiesceRegister::Dma0ControlBlockAddress: offset = 0x7004; break;
            case RpiStartupQuiesceRegister::Dma0TransferInformation: offset = 0x7008; break;
            case RpiStartupQuiesceRegister::Dma0SourceAddress: offset = 0x700c; break;
            case RpiStartupQuiesceRegister::Dma0DestinationAddress: offset = 0x7010; break;
            case RpiStartupQuiesceRegister::Dma0TransferLength: offset = 0x7014; break;
            case RpiStartupQuiesceRegister::Dma0Stride: offset = 0x7018; break;
            case RpiStartupQuiesceRegister::Dma0NextControlBlock: offset = 0x701c; break;
            case RpiStartupQuiesceRegister::Dma0Debug: offset = 0x7020; break;
            case RpiStartupQuiesceRegister::PwmControl: offset = 0x20c000; break;
            case RpiStartupQuiesceRegister::PwmDmaConfiguration: offset = 0x20c008; break;
            case RpiStartupQuiesceRegister::Gpclk0Control: offset = 0x101070; break;
            case RpiStartupQuiesceRegister::GpioFunctionSelect0: offset = 0x200000; break;
            case RpiStartupQuiesceRegister::GpioFunctionSelect2: offset = 0x200008; break;
            default:
                fail(error, "Unknown startup-quiesce register identifier.");
                return nullptr;
            }
            return reinterpret_cast<volatile std::uint32_t *>(mapped_ + offset);
        }

        bool opened_{false};
        volatile std::uint8_t *mapped_{nullptr};
    };
}

std::shared_ptr<IRpiStartupQuiesceAccess>
makeProductionRpiStartupQuiesceAccess()
{
    return std::make_shared<ProductionRpiStartupQuiesceAccess>();
}

double gpioCorrectedPlldFrequency(double nominal_hz, double source_rate_ppm)
{
    static constexpr double kMaxSourceRatePpm = 200.0;

    if (!std::isfinite(nominal_hz) || nominal_hz <= 0.0)
    {
        throw std::invalid_argument(
            "GPIO nominal PLLD frequency must be finite and positive.");
    }
    if (!std::isfinite(source_rate_ppm) ||
        std::fabs(source_rate_ppm) > kMaxSourceRatePpm)
    {
        throw std::invalid_argument(
            "GPIO source-rate PPM must be finite and within +/-200.");
    }

    // A positive estimate means the physical source runs fast. Increasing
    // the assumed PLLD rate increases the programmed divisor and moves the
    // physical GPIO RF output lower toward the requested frequency.
    return nominal_hz * (1.0 + source_rate_ppm * 1.0e-6);
}

namespace
{
    constexpr std::uint32_t kGpclkDividerMask = 0x00FFFFFFu;
    constexpr double kGpclkDividerScale = 4096.0;
    constexpr double kGpclkMash3MinimumDivisor = 5.0;

    bool gpioClockCanRepresent(
        double source_hz,
        double minimum_tone_hz,
        double maximum_tone_hz)
    {
        if (!std::isfinite(source_hz) || source_hz <= 0.0 ||
            !std::isfinite(minimum_tone_hz) || minimum_tone_hz <= 0.0 ||
            !std::isfinite(maximum_tone_hz) ||
            maximum_tone_hz < minimum_tone_hz)
        {
            return false;
        }

        for (const double tone_hz : {minimum_tone_hz, maximum_tone_hz})
        {
            const double scaled = source_hz / tone_hz * kGpclkDividerScale;
            const double lower = std::floor(scaled);
            const double upper = lower + 1.0;
            if (!std::isfinite(scaled) ||
                lower < kGpclkMash3MinimumDivisor * kGpclkDividerScale ||
                upper > static_cast<double>(kGpclkDividerMask))
            {
                return false;
            }
        }
        return true;
    }
}

GpioRfClockPlan gpioPlanRfClock(
    GpioProcessorClockProfile profile,
    double minimum_tone_hz,
    double maximum_tone_hz,
    double source_rate_ppm)
{
    const double plld_nominal_hz =
        profile == GpioProcessorClockProfile::Bcm2711 ? 750e6 : 500e6;
    const double corrected_plld_hz =
        gpioCorrectedPlldFrequency(plld_nominal_hz, source_rate_ppm);
    if (gpioClockCanRepresent(
            corrected_plld_hz,
            minimum_tone_hz,
            maximum_tone_hz))
    {
        return {
            GpioRfClockSource::PllD,
            plld_nominal_hz,
            corrected_plld_hz};
    }

    if (profile == GpioProcessorClockProfile::Bcm2711)
    {
        constexpr double oscillator_nominal_hz = 54e6;
        const double corrected_oscillator_hz =
            gpioCorrectedPlldFrequency(
                oscillator_nominal_hz,
                source_rate_ppm);
        if (gpioClockCanRepresent(
                corrected_oscillator_hz,
                minimum_tone_hz,
                maximum_tone_hz))
        {
            return {
                GpioRfClockSource::Oscillator,
                oscillator_nominal_hz,
                corrected_oscillator_hz};
        }
    }

    throw std::out_of_range(
        "GPIO RF frequency cannot be represented by an available GPCLK source.");
}

std::uint32_t gpioBuildDividerWord(
    double source_hz,
    double tone_hz,
    bool round_up_one_lsb)
{
    if (!gpioClockCanRepresent(source_hz, tone_hz, tone_hz))
    {
        throw std::out_of_range(
            "GPIO RF tuning word is outside the GPCLK 12.12 divisor range.");
    }
    const double scaled = source_hz / tone_hz * kGpclkDividerScale;
    const double word = std::floor(scaled) + (round_up_one_lsb ? 1.0 : 0.0);
    if (word < 0.0 || word > static_cast<double>(kGpclkDividerMask))
    {
        throw std::out_of_range(
            "GPIO RF tuning word exceeds the GPCLK divider field.");
    }
    return static_cast<std::uint32_t>(word);
}

std::int64_t gpioDitherLowerClockCount(
    double lower_ratio,
    std::int64_t block_clocks,
    std::int64_t clocks_scheduled,
    std::int64_t lower_clocks_scheduled)
{
    if (!std::isfinite(lower_ratio) || lower_ratio < 0.0 || lower_ratio > 1.0 ||
        block_clocks < 0 || clocks_scheduled < 0 || lower_clocks_scheduled < 0 ||
        lower_clocks_scheduled > clocks_scheduled)
    {
        throw std::invalid_argument("Invalid GPIO divider-dither state.");
    }

    std::int64_t lower_clocks =
        static_cast<std::int64_t>(std::llround(
            lower_ratio * static_cast<double>(clocks_scheduled + block_clocks))) -
        lower_clocks_scheduled;

    return std::clamp(lower_clocks, std::int64_t{0}, block_clocks);
}

WsprRpiBackend::DMAConfig::DMAConfig()
    : plld_nominal_freq(500000000.0 * (1 - 2.500e-6)),
      plld_clock_frequency(plld_nominal_freq),
      gpclk_nominal_freq(plld_nominal_freq),
      gpclk_clock_frequency(gpclk_nominal_freq),
      processor_profile(GpioProcessorClockProfile::Legacy500Mhz),
      gpclk_source(GpioRfClockSource::PllD),
      peripheral_base_virtual(nullptr),
      orig_gp0ctl(0),
      orig_gp0div(0),
      orig_gpfsel0(0),
      orig_gpfsel1(0),
      orig_gpfsel2(0),
      orig_pwm_ctl(0),
      orig_pwm_sta(0),
      orig_pwm_rng1(0),
      orig_pwm_rng2(0),
      orig_pwm_fifocfg(0)
{
}

WsprRpiBackend::WsprRpiBackend(IControllerBridge &owner)
    : WsprRpiBackend(owner, nullptr, 4)
{
}

WsprRpiBackend::WsprRpiBackend(
    IControllerBridge &owner,
    std::shared_ptr<IRpiStartupQuiesceAccess> startup_quiesce_access,
    int startup_quiesce_gpio)
    : owner_(owner),
      startup_quiesce_access_(
          startup_quiesce_access
              ? std::move(startup_quiesce_access)
              : makeProductionRpiStartupQuiesceAccess()),
      configured_tx_gpio_(startup_quiesce_gpio)
{
    const int ncpu = cpu_count();
    watchdog_cpu_ = clamp_cpu(watchdog_cpu_, ncpu);

    recovery_stop_.store(false, std::memory_order_release);
    recovery_pending_.store(false, std::memory_order_release);
    recovery_thread_ = std::thread(&WsprRpiBackend::recovery_worker, this);
}

WsprRpiBackend::~WsprRpiBackend()
{
    recovery_stop_.store(true, std::memory_order_release);
    recovery_cv_.notify_all();
    if (recovery_thread_.joinable() &&
        recovery_thread_.get_id() != std::this_thread::get_id())
    {
        recovery_thread_.join();
    }

    stop_watchdog();
}

wsprrypi::BackendInfo WsprRpiBackend::info() const
{
    return wsprrypi::BackendInfo{
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        "rpi-clock-gpio",
        "Raspberry Pi GPCLK0/PWM DMA backend"};
}

wsprrypi::BackendCapabilities WsprRpiBackend::capabilities() const
{
    wsprrypi::BackendCapabilities caps;
    caps.output_class = wsprrypi::BackendOutputClass::PHYSICAL_GPIO_RF;
    caps.supported_modes =
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::WSPR) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::QRSS) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::FSKCW) |
        wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::DFCW);
    caps.supports_frequency_switching = true;
    caps.supports_rf_gating = true;
    caps.supports_fade_shape = true;
    caps.supports_precomputed_execution = true;
    caps.nominal_frequency_resolution_hz = std::pow(2.0, -12);
    return caps;
}

wsprrypi::BackendCompileResult WsprRpiBackend::configure(
    const wsprrypi::ExecutionPlan &plan,
    const wsprrypi::BackendExecutionInputs &inputs)
{
    wsprrypi::BackendCompileResult result;
    if (plan.backend != wsprrypi::BackendKind::RPI_CLOCK_GPIO)
    {
        result.error =
            "Execution plan is not targeted for the Raspberry Pi GPIO backend.";
        return result;
    }

    if (plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD)
    {
        const auto validation =
            wsprrypi::RpiStandardFeldExecution::validate(plan);
        if (!validation.ok)
        {
            result.error = validation.error;
            return result;
        }
    }

    auto compat = build_execution_plan_config(plan, inputs, &result);
    if (!compat.has_value())
    {
        if (result.error.empty())
            result.error = "Unsupported execution plan.";
        return result;
    }

    if (!platform_supports_gpio_clock_transmission(&result.error))
        return result;

    const WsprTransmissionConfigureResult applied =
        setup_dma_freq_table(compat->compatibility_plan);

    configured_plan_ = compat;
    result.ok = true;

    if (applied.applied_frequency_hz !=
        compat->compatibility_plan.frequency_hz)
    {
        const double requested_frequency_hz =
            plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD
                ? plan.reference_frequency_hz
                : compat->compatibility_plan.frequency_hz;
        const double actual_frequency_hz =
            plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD
                ? applied.applied_frequency_hz -
                    1.5 * compat->compatibility_plan.tone_spacing_hz
                : applied.applied_frequency_hz;
        result.adjustments.push_back(wsprrypi::BackendAdjustment{
            0,
            requested_frequency_hz,
            actual_frequency_hz,
            plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD
                ? "Standard Feld carrier quantized by Raspberry Pi clock divisor."
                : "Center frequency quantized by Raspberry Pi clock divisor."});
        configured_plan_->compatibility_plan.frequency_hz =
            applied.applied_frequency_hz;
    }

    return result;
}

wsprrypi::ExecutionResult WsprRpiBackend::execute(
    const wsprrypi::ExecutionPlan &plan)
{
    wsprrypi::ExecutionResult result;
    if (!configured_plan_.has_value())
    {
        result.error = "No configured execution plan.";
        return result;
    }
    const auto& compat = configured_plan_;

    if (plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD)
    {
        dma_buf_ptr_ = 0;
        const auto generation = standard_feld_gate_.activate(plan.id.value);
        struct GateDeactivation final
        {
            wsprrypi::StandardFeldExecutionGate& gate;
            wsprrypi::StandardFeldExecutionGate::Generation generation;
            ~GateDeactivation() { gate.deactivate(generation); }
        } deactivate{standard_feld_gate_, generation};
        if (!owner_.backendActivateRasterProgress(plan, generation.value))
        {
            result.error =
                "Standard Feld internal progress storage could not be prepared.";
            result.faulted = true;
            return result;
        }
        StandardFeldExecutionAdapter adapter{
            *this,
            compat->compatibility_plan,
            generation};
        const auto execution =
            wsprrypi::RpiStandardFeldExecution::execute(plan, adapter);
        if (!owner_.backendFinalizeRasterProgress(
                generation.value, execution.terminal, execution.watchdog_faulted))
        {
            result.error = execution.error.empty()
                ? "Standard Feld internal progress storage rejected terminal state."
                : execution.error + " Standard Feld internal progress storage rejected terminal state.";
            result.faulted = true;
            return result;
        }
        result.ok = execution.terminal ==
            wsprrypi::RpiStandardFeldExecutionTerminal::COMPLETED;
        result.stopped = execution.terminal ==
            wsprrypi::RpiStandardFeldExecutionTerminal::CANCELLED;
        result.faulted =
            execution.terminal ==
                wsprrypi::RpiStandardFeldExecutionTerminal::FAILED ||
            execution.terminal ==
                wsprrypi::RpiStandardFeldExecutionTerminal::REJECTED;
        result.error = execution.error;
        return result;
    }

    try
    {
        dma_buf_ptr_ = 0;

        struct TxOffGuard
        {
            WsprRpiBackend *self;
            ~TxOffGuard()
            {
                if (self)
                    self->transmit_off();
            }
        } tx_guard{this};

        struct timespec t0_ts{};
        clock_gettime(CLOCK_MONOTONIC, &t0_ts);
        bool rf_enabled = false;

        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            if (owner_.backendShouldStop())
                break;

            const auto &event = plan.events[i];
            owner_.backendReportExecutionProgress(i);

            if (i > 0)
            {
                const timespec target =
                    add_ns(t0_ts, event.offset_from_start.count());
                if (!wait_until_interruptible(owner_, CLOCK_MONOTONIC, target))
                    break;
            }

            if (plan.mode == wsprrypi::TransmissionMode::QRSS)
            {
                execute_qrss_event(
                    event,
                    compat->compatibility_plan,
                    rf_enabled,
                    static_cast<int>(i));
            }
            else if (plan.mode == wsprrypi::TransmissionMode::FSKCW)
            {
                execute_fskcw_event(
                    event,
                    compat->compatibility_plan,
                    rf_enabled,
                    static_cast<int>(i));
            }
            else if (plan.mode == wsprrypi::TransmissionMode::DFCW)
            {
                execute_dfcw_event(
                    event,
                    compat->compatibility_plan,
                    rf_enabled,
                    static_cast<int>(i));
            }
            else
            {
                const auto symbol = reconstruct_wspr_symbol(
                    event,
                    compat->compatibility_plan);

                if (!rf_enabled)
                {
                    transmit_on(compat->compatibility_plan);
                    start_watchdog();
                    rf_enabled = true;
                }

                transmit_symbol(
                    compat->compatibility_plan,
                    symbol,
                    std::chrono::duration<double>(event.duration).count(),
                    static_cast<int>(i));
            }
        }

        if (!owner_.backendShouldStop() && !plan.events.empty())
        {
            const auto &last = plan.events.back();
            const timespec end_target =
                add_ns(
                    t0_ts,
                    (last.offset_from_start + last.duration).count());
            (void)wait_until_interruptible(
                owner_,
                CLOCK_MONOTONIC,
                end_target);
        }

        result.ok = true;
        result.stopped = owner_.backendShouldStop();
        return result;
    }
    catch (const std::exception &e)
    {
        result.faulted = true;
        result.error = e.what();
        return result;
    }
}

wsprrypi::StartupQuiesceResult WsprRpiBackend::quiesceForStartup()
{
    std::string error;

    try
    {
        stop_watchdog();
    }
    catch (const std::exception& e)
    {
        error = std::string("Could not stop GPIO backend watchdog: ") +
            e.what();
    }
    catch (...)
    {
        error = "Could not stop GPIO backend watchdog.";
    }

    if (dma_config_.peripheral_base_virtual == nullptr)
    {
        if (!error.empty())
            return {false, error};
        return quiesce_fresh_process();
    }

    if (!platform_supports_gpio_clock_transmission(&error))
    {
        if (error.empty())
            error = "GPIO startup quiesce is unavailable on this platform.";
        return {false, error};
    }

    if (!force_dma_reset_sequence() && error.empty())
    {
        error = "Could not reset GPIO backend DMA and clock state.";
    }

    if (!set_mapped_transmit_gpio_safe(error) && error.empty())
        error = "Could not return the GPIO backend transmit pin to input mode.";

    return wsprrypi::StartupQuiesceResult{error.empty(), error};
}

wsprrypi::StartupQuiesceResult WsprRpiBackend::quiesce_fresh_process()
{
    constexpr std::size_t map_size = 0x210000;
    constexpr std::uint32_t dma_reset_abort = (1u << 30) | (1u << 31);
    constexpr std::uint32_t dma_abort = 1u << 31;
    constexpr std::uint32_t clock_password = 0x5a000000u;
    constexpr std::uint32_t clock_busy = 1u << 7;
    constexpr std::uint32_t clock_kill = 1u << 5;

    std::string error;
    std::uint32_t peripheral_base = 0;
    bool opened = false;
    bool mapped = false;

    auto retain_error = [&error](const std::string &fallback)
    {
        if (error.empty())
            error = fallback;
    };
    auto write = [&](RpiStartupQuiesceRegister reg, std::uint32_t value,
                     const char *fallback)
    {
        if (!startup_quiesce_access_->write(reg, value, error))
        {
            retain_error(fallback);
            return false;
        }
        return true;
    };
    auto read = [&](RpiStartupQuiesceRegister reg, std::uint32_t &value,
                    const char *fallback)
    {
        if (!startup_quiesce_access_->read(reg, value, error))
        {
            retain_error(fallback);
            return false;
        }
        return true;
    };

    if (!startup_quiesce_access_->supportedPlatform(error))
    {
        retain_error("GPIO startup quiesce is unavailable on this platform.");
        return {false, error};
    }
    if (configured_tx_gpio_ != 4 && configured_tx_gpio_ != 20)
    {
        retain_error("Unsupported GPIO backend transmit pin for startup quiesce.");
        return {false, error};
    }
    if (!startup_quiesce_access_->discoverPeripheralBase(peripheral_base, error))
    {
        retain_error("Could not discover GPIO startup-quiesce peripheral base.");
        return {false, error};
    }
    if (!startup_quiesce_access_->open(error))
    {
        retain_error("Could not open GPIO startup-quiesce peripheral access.");
        return {false, error};
    }
    opened = true;
    if (!startup_quiesce_access_->map(peripheral_base, map_size, error))
    {
        retain_error("Could not map GPIO startup-quiesce peripherals.");
    }
    else
    {
        mapped = true;
    }

    bool sequence_ok = mapped;
    if (sequence_ok)
    {
        sequence_ok =
            write(RpiStartupQuiesceRegister::Dma0ControlStatus,
                  dma_reset_abort, "Could not reset DMA channel 0.") &&
            (::usleep(10), true) &&
            write(RpiStartupQuiesceRegister::Dma0ControlStatus,
                  dma_abort, "Could not stop DMA channel 0.") &&
            write(RpiStartupQuiesceRegister::Dma0ControlBlockAddress,
                  0, "Could not clear DMA channel 0 control block.") &&
            write(RpiStartupQuiesceRegister::Dma0TransferInformation,
                  0, "Could not clear DMA channel 0 transfer information.") &&
            write(RpiStartupQuiesceRegister::Dma0SourceAddress,
                  0, "Could not clear DMA channel 0 source address.") &&
            write(RpiStartupQuiesceRegister::Dma0DestinationAddress,
                  0, "Could not clear DMA channel 0 destination address.") &&
            write(RpiStartupQuiesceRegister::Dma0TransferLength,
                  0, "Could not clear DMA channel 0 transfer length.") &&
            write(RpiStartupQuiesceRegister::Dma0Stride,
                  0, "Could not clear DMA channel 0 stride.") &&
            write(RpiStartupQuiesceRegister::Dma0NextControlBlock,
                  0, "Could not clear DMA channel 0 next control block.") &&
            write(RpiStartupQuiesceRegister::Dma0Debug,
                  7, "Could not clear DMA channel 0 debug state.");
    }

    if (sequence_ok)
    {
        sequence_ok =
            write(RpiStartupQuiesceRegister::PwmControl,
                  0, "Could not disable PWM during startup quiesce.") &&
            write(RpiStartupQuiesceRegister::PwmDmaConfiguration,
                  0, "Could not disable PWM DMA during startup quiesce.");
    }

    if (sequence_ok)
    {
        std::uint32_t control = 0;
        sequence_ok = read(
            RpiStartupQuiesceRegister::Gpclk0Control,
            control,
            "Could not read GPCLK0 control during startup quiesce.");
        if (sequence_ok)
        {
            control = (control & 0x7efu) | clock_password;
            sequence_ok = write(
                RpiStartupQuiesceRegister::Gpclk0Control,
                control,
                "Could not disable GPCLK0 during startup quiesce.");
        }

        bool not_busy = false;
        for (int poll = 0; sequence_ok && poll < 2000; ++poll)
        {
            sequence_ok = read(
                RpiStartupQuiesceRegister::Gpclk0Control,
                control,
                "Could not confirm GPCLK0 shutdown during startup quiesce.");
            if (sequence_ok && (control & clock_busy) == 0)
            {
                not_busy = true;
                break;
            }
            if (sequence_ok)
                ::usleep(100);
        }
        if (sequence_ok && !not_busy)
        {
            control |= clock_kill | clock_password;
            sequence_ok = write(
                RpiStartupQuiesceRegister::Gpclk0Control,
                control,
                "Could not kill GPCLK0 during startup quiesce.");
            if (sequence_ok)
            {
                sequence_ok = read(
                    RpiStartupQuiesceRegister::Gpclk0Control,
                    control,
                    "Could not confirm killed GPCLK0 state.");
            }
        }
    }

    if (sequence_ok)
    {
        RpiStartupQuiesceRegister gpio_register;
        unsigned shift = 0;
        if (configured_tx_gpio_ == 4)
        {
            gpio_register = RpiStartupQuiesceRegister::GpioFunctionSelect0;
            shift = 12;
        }
        else if (configured_tx_gpio_ == 20)
        {
            gpio_register = RpiStartupQuiesceRegister::GpioFunctionSelect2;
            shift = 0;
        }
        else
        {
            retain_error("Unsupported GPIO backend transmit pin for startup quiesce.");
            sequence_ok = false;
        }

        std::uint32_t function_select = 0;
        if (sequence_ok)
            sequence_ok = read(
                gpio_register,
                function_select,
                "Could not read transmit GPIO function select.");
        if (sequence_ok)
            sequence_ok = write(
                gpio_register,
                function_select & ~(std::uint32_t{0x7} << shift),
                "Could not return transmit GPIO to input mode.");
    }

    if (mapped && !startup_quiesce_access_->unmap(map_size, error))
        retain_error("Could not unmap GPIO startup-quiesce peripherals.");
    if (opened && !startup_quiesce_access_->close(error))
        retain_error("Could not close GPIO startup-quiesce peripheral access.");

    return {sequence_ok && error.empty(), error};
}

bool WsprRpiBackend::set_mapped_transmit_gpio_safe(std::string &error) noexcept
{
    try
    {
        std::uintptr_t offset = 0; unsigned shift = 0;
        if (configured_tx_gpio_ == 4) { offset = 0; shift = 12; }
        else if (configured_tx_gpio_ == 20) { offset = 8; shift = 0; }
        else { error = "Unsupported GPIO backend transmit pin for startup quiesce."; return false; }
        volatile uint32_t &gpfsel = access_bus_address(GPIO_BUS_BASE + offset);
        gpfsel = static_cast<uint32_t>(gpfsel) & ~(uint32_t{0x7} << shift);
        return true;
    }
    catch (...) { error = "Could not set GPIO backend transmit pin inactive."; return false; }
}

void WsprRpiBackend::stop() noexcept
{
    if (!publishStandardFeldStop())
        owner_.backendRequestStopTxNoJoin();
}

bool WsprRpiBackend::publishStandardFeldStop(bool watchdog_fault) noexcept
{
    const bool published = standard_feld_gate_.publish_stop(watchdog_fault);
    if (published && watchdog_fault)
        watchdog_faulted_.store(true, std::memory_order_release);
    if (published)
        owner_.backendSignalStopRequest();
    return published;
}

wsprrypi::CleanupResult WsprRpiBackend::cleanup() noexcept
{
    cleanupTransmission();
    return {true, {}};
}

std::optional<WsprRpiBackend::ExecutionPlanConfig>
WsprRpiBackend::build_execution_plan_config(
    const wsprrypi::ExecutionPlan &plan,
    const wsprrypi::BackendExecutionInputs &inputs,
    wsprrypi::BackendCompileResult *result) const
{
    if (plan.mode != wsprrypi::TransmissionMode::WSPR &&
        plan.mode != wsprrypi::TransmissionMode::QRSS &&
        plan.mode != wsprrypi::TransmissionMode::FSKCW &&
        plan.mode != wsprrypi::TransmissionMode::DFCW &&
        plan.mode != wsprrypi::TransmissionMode::STANDARD_FELD)
    {
        if (result)
            result->error = "Only WSPR, QRSS, FSKCW, DFCW, and Standard Feld execution plans are currently supported.";
        return std::nullopt;
    }

    if (plan.events.empty())
    {
        if (result)
            result->error = "Execution plan has no events.";
        return std::nullopt;
    }

    ExecutionPlanConfig config;
    config.standard_feld =
        plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD;
    config.compatibility_plan.frequency_hz = plan.reference_frequency_hz;
    config.compatibility_plan.tone_spacing_hz = kWsprToneSpacingHz;
    if (plan.mode == wsprrypi::TransmissionMode::STANDARD_FELD)
    {
        // The compatibility table places symbol zero 1.5 tone spacings below
        // its center. Standard Feld holds that stable symbol at its compiled
        // carrier without regenerating any raster decisions here.
        config.compatibility_plan.frequency_hz =
            plan.reference_frequency_hz + 1.5 * kWsprToneSpacingHz;
    }
    if (plan.mode == wsprrypi::TransmissionMode::FSKCW ||
        plan.mode == wsprrypi::TransmissionMode::DFCW)
    {
        if (plan.summary.max_frequency_hz <= 0.0 ||
            plan.summary.min_frequency_hz <= 0.0 ||
            plan.summary.max_frequency_hz <= plan.summary.min_frequency_hz)
        {
            if (result)
                result->error =
                    plan.mode == wsprrypi::TransmissionMode::FSKCW
                        ? "FSKCW execution plan has invalid tone frequencies."
                        : "DFCW execution plan has invalid tone frequencies.";
            return std::nullopt;
        }

        config.compatibility_plan.tone_spacing_hz =
            plan.summary.max_frequency_hz - plan.summary.min_frequency_hz;
        config.compatibility_plan.frequency_hz =
            plan.summary.min_frequency_hz + 1.5 * config.compatibility_plan.tone_spacing_hz;
    }
    config.compatibility_plan.power_level = inputs.power_level;
    config.compatibility_plan.ppm = plan.calibration.ppm;
    config.compatibility_plan.tx_gpio = inputs.tx_gpio;
    config.compatibility_plan.total_symbol_count = 0;
    for (const auto& event : plan.events)
    {
        if (event.rf_on)
            ++config.compatibility_plan.total_symbol_count;
    }
    return config;
}

std::uint32_t WsprRpiBackend::reconstruct_compatibility_symbol(
    const wsprrypi::RfEvent &event,
    const WsprTransmissionPlan &plan,
    long min_symbol,
    long max_symbol) const
{
    if (plan.tone_spacing_hz <= 0.0)
    {
        throw std::runtime_error(
            "Execution-plan compatibility plan has invalid tone spacing.");
    }

    const double tone0_frequency_hz =
        plan.frequency_hz - 1.5 * plan.tone_spacing_hz;
    const double symbol_position =
        (event.frequency_hz - tone0_frequency_hz) / plan.tone_spacing_hz;
    const long symbol = std::lround(symbol_position);

    if (symbol < min_symbol || symbol > max_symbol)
    {
        throw std::runtime_error(
            "Execution-plan event frequency does not map to a valid compatibility symbol.");
    }

    return static_cast<std::uint32_t>(symbol);
}

std::uint32_t WsprRpiBackend::reconstruct_wspr_symbol(
    const wsprrypi::RfEvent &event,
    const WsprTransmissionPlan &plan) const
{
    // The current DMA emitter still expects compatibility-table symbols.
    return reconstruct_compatibility_symbol(event, plan, 0L, 3L);
}

void WsprRpiBackend::execute_qrss_event(
    const wsprrypi::RfEvent& event,
    const WsprTransmissionPlan& plan,
    bool& rf_enabled,
    int symbol_index)
{
    if (!event.rf_on)
    {
        if (rf_enabled)
        {
            // QRSS gaps blank RF inside one committed CW stream. Tearing the
            // DMA/PWM path down here would abort the remaining characters.
            stop_watchdog();
            disable_clock();
            rf_enabled = false;
        }
        return;
    }

    // QRSS currently reuses the legacy WSPR 4-tone DMA table by placing the
    // desired fixed QRSS frequency at symbol 0 in the compatibility plan.
    transmit_symbol_with_envelope(
        plan,
        0U,
        event,
        rf_enabled,
        symbol_index);
}

void WsprRpiBackend::execute_fskcw_event(
    const wsprrypi::RfEvent& event,
    const WsprTransmissionPlan& plan,
    bool& rf_enabled,
    int symbol_index)
{
    if (!event.rf_on)
    {
        throw std::runtime_error(
            "FSKCW execution-plan event unexpectedly disables RF.");
    }

    const auto symbol =
        reconstruct_compatibility_symbol(event, plan, 0L, 1L);
    transmit_symbol_with_envelope(
        plan,
        symbol,
        event,
        rf_enabled,
        symbol_index);
}

void WsprRpiBackend::execute_dfcw_event(
    const wsprrypi::RfEvent& event,
    const WsprTransmissionPlan& plan,
    bool& rf_enabled,
    int symbol_index)
{
    if (!event.rf_on)
    {
        if (rf_enabled)
        {
            stop_watchdog();
            disable_clock();
            rf_enabled = false;
        }
        return;
    }

    const auto symbol =
        reconstruct_compatibility_symbol(event, plan, 0L, 1L);
    transmit_symbol_with_envelope(
        plan,
        symbol,
        event,
        rf_enabled,
        symbol_index);
}

void WsprRpiBackend::startFaultMonitoring()
{
    start_watchdog();
}

void WsprRpiBackend::stopFaultMonitoring()
{
    stop_watchdog();
}

void WsprRpiBackend::prepareTransmission()
{
    // Hardware resource allocation only. Planning and request selection
    // remain in higher layers.
    setup_dma();
}

WsprTransmissionConfigureResult WsprRpiBackend::configureTransmission(
    const WsprTransmissionPlan &plan)
{
    return setup_dma_freq_table(plan);
}

void WsprRpiBackend::cleanupTransmission()
{
    configured_plan_.reset();
    dma_cleanup();
}

int WsprRpiBackend::getOutputPowerMilliwatts(int level)
{
    return get_gpio_power_mw(level);
}

void WsprRpiBackend::beginTransmissionOutput(const WsprTransmissionPlan &plan)
{
    dma_buf_ptr_ = 0;
    transmit_on(plan);
}

void WsprRpiBackend::endTransmissionOutput()
{
    transmit_off();
}

void WsprRpiBackend::emitSymbol(
    const WsprTransmissionPlan &plan,
    const std::uint32_t &sym_num,
    const double &tsym,
    int symbol_index)
{
    transmit_symbol(plan, sym_num, tsym, symbol_index);
}

void WsprRpiBackend::resetTransmissionOutput() noexcept
{
    dma_buf_ptr_ = 0;
    (void)force_dma_reset_sequence();
}

bool WsprRpiBackend::faulted() const noexcept
{
    return watchdog_faulted_.load(std::memory_order_acquire);
}

void WsprRpiBackend::clearFault() noexcept
{
    watchdog_faulted_.store(false, std::memory_order_release);
}

void WsprRpiBackend::setAutoRecover(bool enable) noexcept
{
    watchdog_auto_recover_.store(enable, std::memory_order_release);
}

bool WsprRpiBackend::autoRecoverEnabled() const noexcept
{
    return watchdog_auto_recover_.load(std::memory_order_acquire);
}

bool WsprRpiBackend::recoverFromFault()
{
    std::lock_guard<std::mutex> lk(recovery_mtx_);
    return recover_from_watchdog_fault_locked();
}

bool WsprRpiBackend::recoveryInProgress() const noexcept
{
    return recovery_in_progress_.load(std::memory_order_acquire);
}

void WsprRpiBackend::request_watchdog_recovery() noexcept
{
    if (recovery_stop_.load(std::memory_order_acquire))
    {
        return;
    }

    recovery_pending_.store(true, std::memory_order_release);
    recovery_cv_.notify_all();
}

void WsprRpiBackend::recovery_worker()
{
    block_signals();

    for (;;)
    {
        std::unique_lock<std::mutex> lk(recovery_wait_mtx_);

        const auto wake_pred =
            [&]()
        {
            if (recovery_stop_.load(std::memory_order_acquire))
            {
                return true;
            }

            if (recovery_pending_.load(std::memory_order_acquire))
            {
                return true;
            }

            if (watchdog_auto_recover_.load(std::memory_order_acquire) &&
                watchdog_faulted_.load(std::memory_order_acquire) &&
                (owner_.backendStateValue() == WsprTransmitState::HUNG))
            {
                std::lock_guard<std::mutex> rlk(recovery_rate_mtx_);
                const auto now_tp = std::chrono::steady_clock::now();
                return (recovery_defer_until_ == std::chrono::steady_clock::time_point{}) ||
                       (now_tp >= recovery_defer_until_);
            }

            return false;
        };

        if (!recovery_stop_.load(std::memory_order_acquire) &&
            !recovery_pending_.load(std::memory_order_acquire))
        {
            std::chrono::steady_clock::time_point defer_tp{};
            {
                std::lock_guard<std::mutex> rlk(recovery_rate_mtx_);
                defer_tp = recovery_defer_until_;
            }

            if (defer_tp != std::chrono::steady_clock::time_point{})
            {
                recovery_cv_.wait_until(lk, defer_tp, wake_pred);
            }
            else
            {
                recovery_cv_.wait(lk, wake_pred);
            }
        }
        else
        {
            recovery_cv_.wait(lk, wake_pred);
        }

        if (recovery_stop_.load(std::memory_order_acquire))
        {
            break;
        }

        recovery_pending_.store(false, std::memory_order_release);
        lk.unlock();

        std::lock_guard<std::mutex> rlk(recovery_mtx_);
        (void)recover_from_watchdog_fault_locked();
    }
}

bool WsprRpiBackend::recover_from_watchdog_fault_locked()
{
    if (!watchdog_faulted_.load(std::memory_order_acquire))
    {
        return false;
    }

    const auto now_tp = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> rlk(recovery_rate_mtx_);

        while (!recovery_attempts_.empty() &&
               (now_tp - recovery_attempts_.front()) > kRecoveryWindow)
        {
            recovery_attempts_.pop_front();
        }

        std::chrono::steady_clock::time_point defer_tp{};

        if (!recovery_attempts_.empty() &&
            (now_tp - recovery_attempts_.back()) < kMinRecoveryInterval)
        {
            defer_tp = recovery_attempts_.back() + kMinRecoveryInterval;
        }
        else if (recovery_attempts_.size() >= kMaxRecoveriesInWindow)
        {
            defer_tp = recovery_attempts_.front() + kRecoveryWindow;
        }

        if (defer_tp != std::chrono::steady_clock::time_point{})
        {
            recovery_defer_until_ = defer_tp;
            const auto defer_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    defer_tp - now_tp)
                    .count();

            {
                std::ostringstream oss;
                oss
                    << "Watchdog recovery deferred (rate limited), "
                    << "retry in " << defer_ms << " ms.";
                owner_.backendFireTransmitCallback(
                    WsprTransmissionCallbackEvent::LOGGING,
                    WsprTransmitLogLevel::ERROR,
                    oss.str(),
                    0.0);
            }

            owner_.backendSetStateValue(WsprTransmitState::HUNG);
            return false;
        }

        recovery_defer_until_ = std::chrono::steady_clock::time_point{};
        recovery_attempts_.push_back(now_tp);
    }

    const WsprTransmitState prior_state = owner_.backendStateValue();
    if (prior_state == WsprTransmitState::DISABLED)
    {
        post_recovery_state_ = WsprTransmitState::DISABLED;
    }
    else if (prior_state == WsprTransmitState::COMPLETE ||
             prior_state == WsprTransmitState::CANCELLED)
    {
        post_recovery_state_ = prior_state;
    }
    else
    {
        post_recovery_state_ = WsprTransmitState::ENABLED;
    }

    recovery_in_progress_.store(true, std::memory_order_release);
    owner_.backendSetStateValue(WsprTransmitState::RECOVERING);

    {
        std::ostringstream oss;
        oss << "Attempting watchdog recovery.";
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    force_dma_reset_sequence();

    try
    {
        owner_.backendRestartCurrentConfiguration();
        clearFault();
        owner_.backendSetStateValue(post_recovery_state_);
        recovery_in_progress_.store(false, std::memory_order_release);
    }
    catch (const std::exception &e)
    {
        recovery_in_progress_.store(false, std::memory_order_release);
        owner_.backendSetStateValue(WsprTransmitState::HUNG);
        {
            std::ostringstream oss;
            oss << "Watchdog recovery failed: "
                << e.what();
            owner_.backendFireTransmitCallback(
                WsprTransmissionCallbackEvent::LOGGING,
                WsprTransmitLogLevel::ERROR,
                oss.str(),
                0.0);
        }
        return false;
    }

    {
        std::ostringstream oss;
        oss << "Watchdog recovery complete.";
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }
    return true;
}

void WsprRpiBackend::start_watchdog()
{
    const int ncpu = cpu_count();

    if (ncpu <= 1)
    {
        {
            std::ostringstream oss;
            oss << "Watchdog disabled (single CPU system).";
            owner_.backendFireTransmitCallback(
                WsprTransmissionCallbackEvent::LOGGING,
                WsprTransmitLogLevel::DEBUG,
                oss.str(),
                0.0);
        }
        return;
    }

    if (watchdog_faulted_.load(std::memory_order_acquire))
    {
        return;
    }

    const bool was_stopped = watchdog_stop_.exchange(false, std::memory_order_acq_rel);
    if (!was_stopped)
    {
        return;
    }

    if (watchdog_thread_.joinable() &&
        watchdog_thread_.get_id() != std::this_thread::get_id())
    {
        watchdog_thread_.join();
    }

    constexpr auto kPollPeriod = std::chrono::milliseconds(20);
    constexpr auto kHeartbeatPeriod = std::chrono::seconds(2);
    const auto kStallTimeout = dma_block_timeout(
        pwm_clock_init_,
        PWM_CLOCKS_PER_ITER_NOMINAL,
        std::chrono::milliseconds(250),
        std::chrono::milliseconds(100));

    std::optional<std::chrono::nanoseconds> inject_stall_after;
    if (const char *env = std::getenv("WSPR_TX_INJECT_WD_STALL"))
    {
        const std::string_view v(env);
        if (!v.empty() && v != "0")
        {
            const bool is_ms = (v.size() >= 2 && v.substr(v.size() - 2) == "ms");
            std::string tmp(v);
            if (is_ms)
                tmp.resize(tmp.size() - 2);

            char *endp = nullptr;
            const long n = std::strtol(tmp.c_str(), &endp, 10);
            if (endp != nullptr && *endp == '\0' && n > 0)
            {
                if (is_ms)
                    inject_stall_after = std::chrono::milliseconds(n);
                else
                    inject_stall_after = std::chrono::seconds(n);
            }
            else if (v == "1")
            {
                inject_stall_after = std::chrono::seconds(0);
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kStartupGrace = std::chrono::milliseconds(750);
    watchdog_last_change_ns_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            (now + kStartupGrace).time_since_epoch())
            .count(),
        std::memory_order_release);
    watchdog_last_conblk_.store(0, std::memory_order_release);
    watchdog_last_txfr_len_.store(0, std::memory_order_release);

    {
        std::ostringstream oss;
        oss << "DMA watchdog started."
            << " stall_timeout_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   kStallTimeout)
                   .count();
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    watchdog_thread_ = std::thread(
        [this, kPollPeriod, kStallTimeout, kHeartbeatPeriod, inject_stall_after]
        {
            cpu_set_t cpus;
            CPU_ZERO(&cpus);
            CPU_SET(watchdog_cpu_, &cpus);
            pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);

            sched_param sch{};
            sch.sched_priority = 0;
            pthread_setschedparam(pthread_self(), SCHED_OTHER, &sch);

            auto read_conblk = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(DMA_BUS_BASE + 0x04));
            };

            auto read_txfr_len = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(DMA_BUS_BASE + 0x14));
            };

            auto read_dma_debug = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(DMA_BUS_BASE + 0x20));
            };

            auto read_dma_nextconbk = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(DMA_BUS_BASE + 0x1C));
            };

            auto read_pwm_ctl = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(PWM_BUS_BASE + 0x00));
            };

            auto read_pwm_sta = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(PWM_BUS_BASE + 0x04));
            };

            auto read_pwm_dmac = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(PWM_BUS_BASE + 0x08));
            };

            auto read_gp0ctl = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(CM_GP0CTL_BUS));
            };

            auto read_gp0div = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(CM_GP0DIV_BUS));
            };

            auto read_cs = [this]() -> std::uint32_t
            {
                return static_cast<std::uint32_t>(
                    access_bus_address(DMA_BUS_BASE + 0x00));
            };

            auto last_heartbeat = std::chrono::steady_clock::now();
            std::uint32_t last_heartbeat_conblk = 0;
            std::uint32_t last_heartbeat_txfr_len = 0;

            bool injected = false;
            std::optional<std::chrono::steady_clock::time_point> tx_start;
            std::uint32_t injected_conblk = 0;
            std::uint32_t injected_txfr_len = 0;

            while (!watchdog_stop_.load(std::memory_order_acquire))
            {
                if (owner_.backendStateValue() != WsprTransmitState::TRANSMITTING)
                {
                    std::this_thread::sleep_for(kPollPeriod);
                    continue;
                }

                const std::uint32_t cs = read_cs();
                const bool active = (cs & 0x1u) != 0u;

                if (!active)
                {
                    const auto ts = std::chrono::steady_clock::now();
                    watchdog_last_change_ns_.store(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            ts.time_since_epoch())
                            .count(),
                        std::memory_order_release);
                    watchdog_last_conblk_.store(read_conblk(), std::memory_order_release);
                    watchdog_last_txfr_len_.store(read_txfr_len(), std::memory_order_release);
                    std::this_thread::sleep_for(kPollPeriod);
                    continue;
                }

                const auto now_tp = std::chrono::steady_clock::now();
                if (!tx_start.has_value())
                    tx_start = now_tp;

                if (!injected && inject_stall_after.has_value())
                {
                    const auto elapsed = now_tp - *tx_start;
                    const auto after =
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            *inject_stall_after);
                    if (elapsed >= after)
                    {
                        injected = true;
                        injected_conblk = read_conblk();
                        injected_txfr_len = read_txfr_len();

                        std::ostringstream oss;
                        oss << "DMA watchdog: injecting stall."
                            << " after="
                            << std::chrono::duration_cast<std::chrono::milliseconds>(
                                   *inject_stall_after)
                                   .count()
                            << " ms";
                        owner_.backendFireTransmitCallback(
                            WsprTransmissionCallbackEvent::LOGGING,
                            WsprTransmitLogLevel::DEBUG,
                            oss.str(),
                            0.0);
                    }
                }

                const std::uint32_t conblk = injected ? injected_conblk : read_conblk();
                const std::uint32_t txfr_len = injected ? injected_txfr_len : read_txfr_len();

                const std::uint32_t last_conblk =
                    watchdog_last_conblk_.load(std::memory_order_acquire);
                const std::uint32_t last_txfr_len =
                    watchdog_last_txfr_len_.load(std::memory_order_acquire);

                if (conblk != last_conblk || txfr_len != last_txfr_len)
                {
                    const auto ts = std::chrono::steady_clock::now();
                    watchdog_last_conblk_.store(conblk, std::memory_order_release);
                    watchdog_last_txfr_len_.store(txfr_len, std::memory_order_release);
                    watchdog_last_change_ns_.store(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            ts.time_since_epoch())
                            .count(),
                        std::memory_order_release);
                    std::this_thread::sleep_for(kPollPeriod);
                    continue;
                }

                const auto now_ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now_tp.time_since_epoch())
                        .count();
                const auto last_ns = watchdog_last_change_ns_.load(std::memory_order_acquire);
                const auto stalled_for = std::chrono::nanoseconds(now_ns - last_ns);

                if ((now_tp - last_heartbeat) >= kHeartbeatPeriod)
                {
                    const bool advancing =
                        (conblk != last_heartbeat_conblk) ||
                        (txfr_len != last_heartbeat_txfr_len);
                    std::ostringstream oss;
                    oss
                        << "DMA watchdog: CS=0x" << std::hex << cs
                        << " CONBLK_AD=0x" << conblk
                        << " TXFR_LEN=0x" << txfr_len
                        << std::dec
                        << (advancing ? " advancing" : " not-advancing")
                        << " stalled_for="
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               stalled_for)
                               .count()
                        << " ms";
                    owner_.backendFireTransmitCallback(
                        WsprTransmissionCallbackEvent::LOGGING,
                        WsprTransmitLogLevel::DEBUG,
                        oss.str(),
                        0.0);
                    last_heartbeat = now_tp;
                    last_heartbeat_conblk = conblk;
                    last_heartbeat_txfr_len = txfr_len;
                }

                if (stalled_for >= kStallTimeout)
                {
                    // Serialize fault latching/stop publication with the
                    // Standard Feld checked RF-enable edge.  Cleanup and the
                    // watchdog join occur after this short critical section.
                    const bool standard_feld = publishStandardFeldStop(true);
                    if (!standard_feld)
                    {
                        watchdog_faulted_.store(true, std::memory_order_release);
                    }
                    owner_.backendSetStateValue(WsprTransmitState::HUNG);

                    {
                        std::ostringstream oss;
                        oss << "DMA watchdog detected a stall."
                            << " CS=0x" << std::hex << cs
                            << " CONBLK_AD=0x" << conblk
                            << std::dec;
                        owner_.backendFireTransmitCallback(
                            WsprTransmissionCallbackEvent::LOGGING,
                            WsprTransmitLogLevel::ERROR,
                            oss.str(),
                            0.0);
                    }

                    try
                    {
                        const std::uint32_t debug = read_dma_debug();
                        const std::uint32_t nextconbk = read_dma_nextconbk();
                        const std::uint32_t pwm_ctl = read_pwm_ctl();
                        const std::uint32_t pwm_sta = read_pwm_sta();
                        const std::uint32_t pwm_dmac = read_pwm_dmac();
                        const std::uint32_t gp0ctl = read_gp0ctl();
                        const std::uint32_t gp0div = read_gp0div();

                        std::ostringstream flags;
                        flags
                            << ((cs & (1u << 0)) ? " ACTIVE" : "")
                            << ((cs & (1u << 1)) ? " END" : "")
                            << ((cs & (1u << 2)) ? " INT" : "")
                            << ((cs & (1u << 3)) ? " DREQ" : "")
                            << ((cs & (1u << 4)) ? " PAUSED" : "")
                            << ((cs & (1u << 5)) ? " DREQ_STOPS" : "")
                            << ((cs & (1u << 6)) ? " WAIT_OUTSTANDING" : "")
                            << ((cs & (1u << 8)) ? " ERROR" : "")
                            << ((cs & (1u << 9)) ? " WAIT_RESP" : "")
                            << ((cs & (1u << 28)) ? " WIDE_BURSTS" : "")
                            << ((cs & (1u << 29)) ? " DISDEBUG" : "")
                            << ((cs & (1u << 30)) ? " ABORT" : "")
                            << ((cs & (1u << 31)) ? " RESET" : "");

                        std::ostringstream oss;
                        oss
                            << "DMA watchdog diagnostics: "
                            << " CS=0x" << std::hex << cs
                            << " (" << flags.str() << " )"
                            << " CONBLK_AD=0x" << conblk
                            << " NEXTCONBK=0x" << nextconbk
                            << " TXFR_LEN=0x" << txfr_len
                            << " DEBUG=0x" << debug
                            << " PWM_CTL=0x" << pwm_ctl
                            << " PWM_STA=0x" << pwm_sta
                            << " PWM_DMAC=0x" << pwm_dmac
                            << " GP0CTL=0x" << gp0ctl
                            << " GP0DIV=0x" << gp0div
                            << std::dec;
                        owner_.backendFireTransmitCallback(
                            WsprTransmissionCallbackEvent::LOGGING,
                            WsprTransmitLogLevel::DEBUG,
                            oss.str(),
                            0.0);
                    }
                    catch (...)
                    {
                    }

                    if (!standard_feld)
                        owner_.backendRequestStopTxNoJoin();
                    force_dma_reset_sequence();
                    request_watchdog_recovery();
                    return;
                }

                std::this_thread::sleep_for(kPollPeriod);
            }
        });
}

void WsprRpiBackend::stop_watchdog()
{
    const bool was_running = !watchdog_stop_.exchange(true, std::memory_order_acq_rel);
    if (!was_running)
    {
        return;
    }

    if (watchdog_thread_.joinable() &&
        watchdog_thread_.get_id() != std::this_thread::get_id())
    {
        watchdog_thread_.join();
    }

    {
        std::ostringstream oss;
        oss << "DMA watchdog stopped.";
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }
}

bool WsprRpiBackend::force_dma_reset_sequence() noexcept
{
    if (dma_config_.peripheral_base_virtual == nullptr)
    {
        return true;
    }

    try
    {
        volatile DMAregs *DMA0 =
            reinterpret_cast<volatile DMAregs *>(&(access_bus_address(DMA_BUS_BASE)));

        DMA0->CS = (1u << 30) | (1u << 31);
        usleep(10);
        DMA0->CS = 1u << 31;
        DMA0->CONBLK_AD = 0;
        DMA0->TI = 0;
        DMA0->SOURCE_AD = 0;
        DMA0->DEST_AD = 0;
        DMA0->TXFR_LEN = 0;
        DMA0->STRIDE = 0;
        DMA0->NEXTCONBK = 0;
        DMA0->DEBUG = 7u;

        access_bus_address(PWM_BUS_BASE + 0x00) = 0;
        access_bus_address(PWM_BUS_BASE + 0x08) = 0;

        disable_clock();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void WsprRpiBackend::dma_cleanup()
{
    if (!dma_setup_done_)
    {
        return;
    }
    dma_setup_done_ = false;

    if (!dma_config_.peripheral_base_virtual)
    {
        return;
    }

    disable_hardware_sequence();

    access_bus_address(CM_GP0DIV_BUS) = dma_config_.orig_gp0div;
    access_bus_address(CM_GP0CTL_BUS) = dma_config_.orig_gp0ctl;
    access_bus_address(GPIO_BUS_BASE + 0) = dma_config_.orig_gpfsel0;
    access_bus_address(GPIO_BUS_BASE + 4) = dma_config_.orig_gpfsel1;
    access_bus_address(GPIO_BUS_BASE + 8) = dma_config_.orig_gpfsel2;
    access_bus_address(PWM_BUS_BASE + 0x00) = dma_config_.orig_pwm_ctl;
    access_bus_address(PWM_BUS_BASE + 0x04) = dma_config_.orig_pwm_sta;
    access_bus_address(PWM_BUS_BASE + 0x10) = dma_config_.orig_pwm_rng1;
    access_bus_address(PWM_BUS_BASE + 0x20) = dma_config_.orig_pwm_rng2;
    access_bus_address(PWM_BUS_BASE + 0x08) = dma_config_.orig_pwm_fifocfg;

    if (dma_config_.peripheral_base_virtual)
    {
        ::mailbox.unMapMem(
            dma_config_.peripheral_base_virtual,
            Mailbox::PAGE_SIZE * NUM_PAGES);
        dma_config_.peripheral_base_virtual = nullptr;
    }

    deallocate_memory_pool();
    mailbox.close();

    dma_config_ = DMAConfig();
    mailbox_struct_ = MailboxStruct();
    dma_buf_ptr_ = 0;
}

int WsprRpiBackend::get_gpio_power_mw(int level)
{
    if (level < 0 || level >= static_cast<int>(DRIVE_STRENGTH_TABLE.size()))
    {
        throw std::out_of_range(
            "WsprTransmitter::get_gpio_power_mw: Drive strength level "
            "must be between 0 and 7");
    }
    return DRIVE_STRENGTH_TABLE[level];
}

inline volatile uint32_t &WsprRpiBackend::access_bus_address(std::uintptr_t bus_addr)
{
    std::uintptr_t offset = Mailbox::offsetFromBase(bus_addr);
    return *reinterpret_cast<volatile uint32_t *>(
        dma_config_.peripheral_base_virtual + offset);
}

inline void WsprRpiBackend::set_bit_bus_address(std::uintptr_t base, unsigned int bit)
{
    volatile uint32_t &reg = access_bus_address(base);
    const uint32_t mask = uint32_t{1} << bit;
    const uint32_t value = reg;
    reg = value | mask;
}

inline void WsprRpiBackend::clear_bit_bus_address(std::uintptr_t base, unsigned int bit)
{
    volatile uint32_t &reg = access_bus_address(base);
    const uint32_t mask = uint32_t{1} << bit;
    const uint32_t value = reg;
    reg = value & ~mask;
}

void WsprRpiBackend::configure_transmit_gpio(int gpio)
{
    std::uintptr_t register_offset = 0;
    unsigned shift = 0;
    std::uint32_t function_bits = 0;

    switch (gpio)
    {
    case 4:
        register_offset = 0;
        shift = 12;
        function_bits = 0b100u; // ALT0 -> GPCLK0 on GPIO4
        break;
    case 20:
        register_offset = 8;
        shift = 0;
        function_bits = 0b010u; // ALT5 -> GPCLK0 on GPIO20
        break;
    default:
        throw std::invalid_argument(
            "Unsupported transmit GPIO. GPCLK0 output is supported on BCM GPIO 4 or 20.");
    }

    volatile uint32_t &gpfsel =
        access_bus_address(GPIO_BUS_BASE + register_offset);
    const std::uint32_t mask = static_cast<std::uint32_t>(0b111u << shift);
    const std::uint32_t current_value = static_cast<std::uint32_t>(gpfsel);
    const std::uint32_t updated_value =
        (current_value & ~mask) | (function_bits << shift);
    gpfsel = static_cast<int>(updated_value);
    configured_tx_gpio_ = gpio;
}

void WsprRpiBackend::get_plld()
{
    static std::optional<unsigned> cached_revision;
    if (!cached_revision)
    {
        std::ifstream file("/proc/cpuinfo");
        if (file)
        {
            std::string line;
            unsigned value = 0;
            const std::string pattern = "Revision\t: %x";
            while (std::getline(file, line))
            {
                if (sscanf(line.c_str(), pattern.c_str(), &value) == 1)
                {
                    cached_revision = value;
                    break;
                }
            }
        }
        if (!cached_revision)
        {
            cached_revision = 0;
        }
    }

    unsigned rev = *cached_revision;
    BCMChip proc_id;

    if (rev & 0x800000)
    {
        auto raw = (rev & 0xF000) >> 12;
        proc_id = static_cast<BCMChip>(raw);
    }
    else
    {
        proc_id = BCMChip::BCM_HOST_PROCESSOR_BCM2835;
    }

    double base_freq_hz = 500e6;
    switch (proc_id)
    {
    case BCMChip::BCM_HOST_PROCESSOR_BCM2835:
    case BCMChip::BCM_HOST_PROCESSOR_BCM2836:
    case BCMChip::BCM_HOST_PROCESSOR_BCM2837:
        base_freq_hz = 500e6;
        dma_config_.processor_profile =
            GpioProcessorClockProfile::Legacy500Mhz;
        break;

    case BCMChip::BCM_HOST_PROCESSOR_BCM2711:
        base_freq_hz = 750e6;
        dma_config_.processor_profile =
            GpioProcessorClockProfile::Bcm2711;
        break;

    default:
        throw std::runtime_error(
            std::string("Error: Unknown chipset (") +
            std::string(to_string(proc_id)) + ")");
    }

    dma_config_.plld_nominal_freq = base_freq_hz;
    dma_config_.plld_clock_frequency = base_freq_hz;
    dma_config_.gpclk_nominal_freq = base_freq_hz;
    dma_config_.gpclk_clock_frequency = base_freq_hz;
    dma_config_.gpclk_source = GpioRfClockSource::PllD;

    if (dma_config_.plld_clock_frequency <= 0)
    {
        std::ostringstream oss;
        oss << "Error: Invalid PLLD frequency; defaulting to 500 MHz";
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::ERROR,
            oss.str(),
            0.0);
        dma_config_.plld_nominal_freq = 500e6;
        dma_config_.plld_clock_frequency = 500e6;
        dma_config_.gpclk_nominal_freq = 500e6;
        dma_config_.gpclk_clock_frequency = 500e6;
        dma_config_.processor_profile =
            GpioProcessorClockProfile::Legacy500Mhz;
        dma_config_.gpclk_source = GpioRfClockSource::PllD;
    }
}

void WsprRpiBackend::allocate_memory_pool(unsigned numpages)
{
    mailbox_struct_.mem_ref = mailbox.memAlloc(
        Mailbox::PAGE_SIZE * numpages,
        Mailbox::BLOCK_SIZE);
    if (mailbox_struct_.mem_ref == 0)
    {
        throw std::runtime_error("Error: memAlloc failed.");
    }

    mailbox_struct_.bus_addr = mailbox.memLock(mailbox_struct_.mem_ref);
    if (mailbox_struct_.bus_addr == 0)
    {
        mailbox.memFree(mailbox_struct_.mem_ref);
        throw std::runtime_error("Error: memLock failed.");
    }

    mailbox_struct_.virt_addr = mailbox.mapMem(
        mailbox.busToPhysical(mailbox_struct_.bus_addr),
        Mailbox::PAGE_SIZE * numpages);
    if (mailbox_struct_.virt_addr == nullptr)
    {
        mailbox.memUnlock(mailbox_struct_.mem_ref);
        mailbox.memFree(mailbox_struct_.mem_ref);
        throw std::runtime_error("Error: mapMem failed.");
    }

    mailbox_struct_.pool_size = numpages;
    mailbox_struct_.pool_cnt = 0;
}

void WsprRpiBackend::get_real_mem_page_from_pool(void **vAddr, void **bAddr)
{
    if (mailbox_struct_.pool_cnt >= mailbox_struct_.pool_size)
    {
        throw std::runtime_error("Error: unable to allocate more pages.");
    }

    unsigned offset = mailbox_struct_.pool_cnt * Mailbox::PAGE_SIZE;

    *vAddr = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(mailbox_struct_.virt_addr) + offset);
    *bAddr = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(mailbox_struct_.bus_addr) + offset);

    mailbox_struct_.pool_cnt++;
}

void WsprRpiBackend::deallocate_memory_pool()
{
    if (mailbox_struct_.virt_addr != nullptr)
    {
        mailbox.unMapMem(
            mailbox_struct_.virt_addr,
            mailbox_struct_.pool_size * Mailbox::PAGE_SIZE);
        mailbox_struct_.virt_addr = nullptr;
    }

    if (mailbox_struct_.mem_ref != 0)
    {
        mailbox.memUnlock(mailbox_struct_.mem_ref);
        mailbox.memFree(mailbox_struct_.mem_ref);
        mailbox_struct_.mem_ref = 0;
    }

    mailbox_struct_.pool_size = 0;
    mailbox_struct_.pool_cnt = 0;
}

void WsprRpiBackend::disable_hardware_sequence()
{
    const bool was_on =
        (owner_.backendStateValue() == WsprTransmitState::TRANSMITTING);

    if (was_on && dma_config_.peripheral_base_virtual != nullptr)
    {
        const std::uint32_t conblk =
            static_cast<std::uint32_t>(access_bus_address(DMA_BUS_BASE + 0x04));
        const std::uint32_t cs =
            static_cast<std::uint32_t>(access_bus_address(DMA_BUS_BASE + 0x00));

        std::ostringstream oss;
        oss << "DMA before off: CS=0x"
            << std::hex << cs
            << " CONBLK_AD=0x" << conblk
            << std::dec;
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    if (dma_config_.peripheral_base_virtual == nullptr)
    {
        if (!recovery_in_progress_.load(std::memory_order_acquire))
        {
            owner_.backendSetStateValue(WsprTransmitState::ENABLED);
        }
        return;
    }

    volatile DMAregs *DMA0 =
        reinterpret_cast<volatile DMAregs *>(&(access_bus_address(DMA_BUS_BASE)));
    DMA0->CS = 1u << 31;

    access_bus_address(PWM_BUS_BASE + 0x00) = 0;
    access_bus_address(PWM_BUS_BASE + 0x08) = 0;

    disable_clock();
}

void WsprRpiBackend::disable_clock()
{
    if (!recovery_in_progress_.load(std::memory_order_acquire))
    {
        owner_.backendSetStateValue(WsprTransmitState::ENABLED);
    }

    if (dma_config_.peripheral_base_virtual == nullptr)
        return;

    gpclk0_disable_wait(access_bus_address(CM_GP0CTL_BUS));
}

void WsprRpiBackend::transmit_on(const WsprTransmissionPlan &plan)
{
    configure_transmit_gpio(plan.tx_gpio);

    access_bus_address(PADS_GPIO_0_27_BUS) = 0x5a000018 + plan.power_level;

    const auto source = static_cast<std::uint32_t>(dma_config_.gpclk_source);
    struct GPCTL setupword = {source, 0, 0, 0, 0, 3, 0x5A};
    setupword = {source, 1, 0, 0, 0, 3, 0x5A};
    int temp;
    std::memcpy(&temp, &setupword, sizeof(int));

    access_bus_address(CM_GP0CTL_BUS) = temp;

    bool verified = false;
    std::uint32_t observed_control = 0;
    std::uint32_t observed_divider = 0;
    for (int attempt = 0; attempt < 20 && !verified; ++attempt)
    {
        observed_control = access_bus_address(CM_GP0CTL_BUS);
        observed_divider =
            access_bus_address(CM_GP0DIV_BUS) & kGpclkDividerMask;
        verified = (observed_control & 0xFu) == source &&
            (observed_control & (1u << 4)) != 0 &&
            std::find(
                active_gpclk_words_.begin(),
                active_gpclk_words_.end(),
                observed_divider) != active_gpclk_words_.end();
        if (!verified)
        {
            (void)owner_.backendWaitInterruptableFor(
                std::chrono::milliseconds(1));
        }
    }
    if (!verified)
    {
        gpclk0_disable_wait(access_bus_address(CM_GP0CTL_BUS));
        std::ostringstream oss;
        oss << "GPCLK source/divider readback did not match the validated RF plan:"
            << " control=0x" << std::hex << observed_control
            << " divider=0x" << observed_divider
            << " expected_source=0x" << source
            << " expected_divider=0x" << active_gpclk_words_[0]
            << std::dec << ".";
        throw std::runtime_error(oss.str());
    }
    owner_.backendSetStateValue(WsprTransmitState::TRANSMITTING);
}

void WsprRpiBackend::transmit_off()
{
    stop_watchdog();
    disable_hardware_sequence();
}

void WsprRpiBackend::transmit_symbol(
    const WsprTransmissionPlan &plan,
    const std::uint32_t &sym_num,
    const double &tsym,
    int symbol_index)
{
    if (owner_.backendShouldStop())
    {
        return;
    }

    constexpr std::uint32_t kMask = 0x3FFu;
    constexpr std::uint32_t kLead = 64u;
    constexpr int kPollSleepUs = 50;
    const auto max_wait =
        dma_block_timeout(
            pwm_clock_init_,
            PWM_CLOCKS_PER_ITER_NOMINAL,
            std::chrono::milliseconds(200),
            std::chrono::milliseconds(50));
    const int kMaxWaitUs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(max_wait).count());

    auto dma_conblk_ad = [&]() -> std::uint32_t
    {
        return static_cast<std::uint32_t>(
            access_bus_address(DMA_BUS_BASE + 0x04));
    };

    auto wait_cb_not_active = [&](std::uint32_t idx) -> bool
    {
        const std::uint32_t target =
            static_cast<std::uint32_t>(instructions_[idx].b);

        int waited_us = 0;
        while (!owner_.backendShouldStop())
        {
            const std::uint32_t cur = dma_conblk_ad();
            if (cur != target)
                return true;

            if (waited_us >= kMaxWaitUs)
            {
                std::ostringstream oss;
                oss << "DMA appears stuck at CONBLK_AD=0x"
                    << std::hex << cur << std::dec
                    << ", forcing stop to avoid deadlock."
                    << " waited_ms=" << (waited_us / 1000.0)
                    << " max_wait_ms="
                    << std::chrono::duration_cast<std::chrono::milliseconds>(
                           max_wait)
                           .count()
                    << " pwm_clock_init_hz=" << std::fixed << std::setprecision(3)
                    << pwm_clock_init_;
                owner_.backendFireTransmitCallback(
                    WsprTransmissionCallbackEvent::LOGGING,
                    WsprTransmitLogLevel::DEBUG,
                    oss.str(),
                    0.0);

                if (!publishStandardFeldStop())
                    owner_.backendSignalStopRequest();
                return false;
            }

            usleep(kPollSleepUs);
            waited_us += kPollSleepUs;
        }
        return false;
    };

    auto advance_with_lead = [&]() -> void
    {
        dma_buf_ptr_ = (dma_buf_ptr_ + kLead) & kMask;
    };

    const bool is_tone = (tsym == 0.0);
    const int f0_idx = static_cast<int>(sym_num) * 2;
    const int f1_idx = f0_idx + 1;

    const std::int64_t pwm_clocks_per_iter =
        static_cast<std::int64_t>(PWM_CLOCKS_PER_ITER_NOMINAL);

    const double f0_freq =
        dma_config_.gpclk_clock_frequency /
        (static_cast<double>(
             reinterpret_cast<std::uint32_t *>(const_page_.v)[f0_idx] & 0x00FFFFFFu) /
         std::pow(2.0, 12));
    const double f1_freq =
        dma_config_.gpclk_clock_frequency /
        (static_cast<double>(
             reinterpret_cast<std::uint32_t *>(const_page_.v)[f1_idx] & 0x00FFFFFFu) /
         std::pow(2.0, 12));
    const double tone_freq =
        plan.frequency_hz - 1.5 * plan.tone_spacing_hz +
        static_cast<double>(sym_num) * plan.tone_spacing_hz;
    const double f0_ratio = std::clamp(
        1.0 - (tone_freq - f0_freq) / (f1_freq - f0_freq),
        0.0,
        1.0);

    advance_with_lead();

    if (is_tone)
    {
        std::int64_t clocks_scheduled = 0;
        std::int64_t lower_clocks_scheduled = 0;

        while (!owner_.backendShouldStop())
        {
            const std::int64_t lower_clocks = gpioDitherLowerClockCount(
                f0_ratio,
                pwm_clocks_per_iter,
                clocks_scheduled,
                lower_clocks_scheduled);
            const std::int64_t upper_clocks =
                pwm_clocks_per_iter - lower_clocks;

            dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
            if (!wait_cb_not_active(dma_buf_ptr_))
                return;

            reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->SOURCE_AD =
                static_cast<std::uint32_t>(
                    static_cast<std::uintptr_t>(const_page_.b) +
                    static_cast<std::uintptr_t>(f0_idx * 4));

            dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
            if (!wait_cb_not_active(dma_buf_ptr_))
                return;

            reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->TXFR_LEN =
                static_cast<std::uint32_t>(lower_clocks);

            dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
            if (!wait_cb_not_active(dma_buf_ptr_))
                return;

            reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->SOURCE_AD =
                static_cast<std::uint32_t>(
                    static_cast<std::uintptr_t>(const_page_.b) +
                    static_cast<std::uintptr_t>(f1_idx * 4));

            dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
            if (!wait_cb_not_active(dma_buf_ptr_))
                return;

            reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->TXFR_LEN =
                static_cast<std::uint32_t>(upper_clocks);

            clocks_scheduled += pwm_clocks_per_iter;
            lower_clocks_scheduled += lower_clocks;
        }

        return;
    }

    const std::uint32_t table_size = 1024;
    const double pwm_sample_hz =
        pwm_clock_init_ /
        static_cast<double>(table_size);
    const std::int64_t n_pwmclk_per_sym =
        static_cast<std::int64_t>(std::llround(pwm_sample_hz * tsym));

    {
        const int total_symbols =
            static_cast<int>(plan.symbolCount());

        std::ostringstream oss;
        oss
            << "sym=" << sym_num
            << " idx=";

        if (symbol_index >= 0)
        {
            oss
                << std::setw(3) << std::setfill('0') << (symbol_index + 1)
                << "/" << total_symbols;
        }
        else
        {
            oss << "-";
        }

        oss
            << " tsym=" << std::fixed << std::setprecision(6) << tsym
            << " pwm_clock_init_=" << std::fixed << std::setprecision(3)
            << pwm_clock_init_ << std::defaultfloat
            << " n_pwmclk_per_sym=" << n_pwmclk_per_sym
            << " pwm_clocks_per_iter=" << pwm_clocks_per_iter;

#ifdef DEBUG_TRANS_SYMBOL
        owner_.backendFireTransmitCallback(
                    WsprTransmissionCallbackEvent::LOGGING,
                    WsprTransmitLogLevel::DEBUG,
                    oss.str(),
                    0.0);
#endif
    }

    if (n_pwmclk_per_sym <= 0 || n_pwmclk_per_sym > 5'000'000'000LL)
        throw std::runtime_error(
            "transmit_symbol(): invalid n_pwmclk_per_sym (bad PWM clock).");

    std::int64_t n_pwmclk_transmitted = 0;
    std::int64_t n_f0_transmitted = 0;

    if (trace_wspr_tones_enabled())
    {
        std::ostringstream oss;
        oss
            << "WSPR tone request:"
            << " idx=" << (symbol_index >= 0 ? symbol_index + 1 : 0)
            << "/" << plan.symbolCount()
            << " symbol=" << sym_num
            << " requested_hz=" << std::fixed << std::setprecision(6)
            << tone_freq
            << " lower_word=0x" << std::hex
            << (reinterpret_cast<std::uint32_t *>(const_page_.v)[f0_idx] & 0x00FFFFFFu)
            << " upper_word=0x"
            << (reinterpret_cast<std::uint32_t *>(const_page_.v)[f1_idx] & 0x00FFFFFFu)
            << std::dec
            << " lower_hz=" << std::fixed << std::setprecision(6) << f0_freq
            << " upper_hz=" << std::fixed << std::setprecision(6) << f1_freq
            << " f0_ratio=" << std::fixed << std::setprecision(9) << f0_ratio
            << " dither_block_clocks=" << pwm_clocks_per_iter
            << " symbol_pwm_clocks=" << n_pwmclk_per_sym;
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    while (n_pwmclk_transmitted < n_pwmclk_per_sym &&
           !owner_.backendShouldStop())
    {
        std::int64_t n_pwmclk = pwm_clocks_per_iter;

        n_pwmclk += static_cast<std::int64_t>(std::llround(
            (std::rand() / (RAND_MAX + 1.0) - 0.5) *
            static_cast<double>(n_pwmclk)));

        if (n_pwmclk <= 0)
            n_pwmclk = 1;

        if (n_pwmclk_transmitted + n_pwmclk > n_pwmclk_per_sym)
            n_pwmclk = n_pwmclk_per_sym - n_pwmclk_transmitted;

        const std::int64_t n_f0 = gpioDitherLowerClockCount(
            f0_ratio,
            n_pwmclk,
            n_pwmclk_transmitted,
            n_f0_transmitted);

        const std::int64_t n_f1 = n_pwmclk - n_f0;

        dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
        if (!wait_cb_not_active(dma_buf_ptr_))
            return;

        reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->SOURCE_AD =
            static_cast<std::uint32_t>(
                static_cast<std::uintptr_t>(const_page_.b) +
                static_cast<std::uintptr_t>(f0_idx * 4));

        dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
        if (!wait_cb_not_active(dma_buf_ptr_))
            return;

        reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->TXFR_LEN =
            static_cast<std::uint32_t>(n_f0);

        dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
        if (!wait_cb_not_active(dma_buf_ptr_))
            return;

        reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->SOURCE_AD =
            static_cast<std::uint32_t>(
                static_cast<std::uintptr_t>(const_page_.b) +
                static_cast<std::uintptr_t>(f1_idx * 4));

        dma_buf_ptr_ = (dma_buf_ptr_ + 1) & kMask;
        if (!wait_cb_not_active(dma_buf_ptr_))
            return;

        reinterpret_cast<CB *>(instructions_[dma_buf_ptr_].v)->TXFR_LEN =
            static_cast<std::uint32_t>(n_f1);

        n_pwmclk_transmitted += n_pwmclk;
        n_f0_transmitted += n_f0;
    }
}

void WsprRpiBackend::transmit_symbol_with_envelope(
    const WsprTransmissionPlan &plan,
    const std::uint32_t &sym_num,
    const wsprrypi::RfEvent &event,
    bool &rf_enabled,
    int symbol_index)
{
    // GPIO DMA emission expects a continuous symbol run. Re-slicing a CW event
    // into multiple transmit_symbol() calls and disabling the clock between
    // slices restarts the symbol path, which produces audible/visible startup
    // transients instead of a stable full-length tone. Keep GPIO CW emission
    // continuous even when fade shaping is requested.
    if (!rf_enabled)
    {
        transmit_on(plan);
        start_watchdog();
        rf_enabled = true;
    }

    transmit_symbol(
        plan,
        sym_num,
        std::chrono::duration<double>(event.duration).count(),
        symbol_index);
}

double WsprRpiBackend::bit_trunc(const double &d, const int &lsb)
{
    const double factor = std::pow(2.0, lsb);
    return std::floor(d / factor) * factor;
}

void WsprRpiBackend::create_dma_pages(
    PageInfo &const_page,
    PageInfo &instr_page,
    PageInfo instructions[])
{
    allocate_memory_pool(1025);

    {
        void *tmp_v, *tmp_b;
        get_real_mem_page_from_pool(&tmp_v, &tmp_b);
        const_page.v = tmp_v;
        const_page.b = reinterpret_cast<std::uintptr_t>(tmp_b);
    }

    int instrCnt = 0;

    while (instrCnt < 1024)
    {
        {
            void *tmp_v, *tmp_b;
            get_real_mem_page_from_pool(&tmp_v, &tmp_b);
            instr_page.v = tmp_v;
            instr_page.b = reinterpret_cast<std::uintptr_t>(tmp_b);
        }

        struct CB *instr0 = reinterpret_cast<struct CB *>(instr_page.v);

        for (int i = 0; i < static_cast<int>(Mailbox::PAGE_SIZE / sizeof(struct CB)); i++)
        {
            instructions[instrCnt].v = static_cast<void *>(
                static_cast<char *>(instr_page.v) + sizeof(struct CB) * i);
            instructions[instrCnt].b = instr_page.b + static_cast<std::uintptr_t>(
                                                          sizeof(struct CB) * i);

            instr0->SOURCE_AD = static_cast<uint32_t>(const_page.b + 2048);
            instr0->DEST_AD = PWM_BUS_BASE + 0x18;
            instr0->TXFR_LEN = 4;
            instr0->STRIDE = 0;
            instr0->TI = (1 << 6) | (5 << 16) | (1 << 26);
            instr0->RES1 = 0;
            instr0->RES2 = 0;

            if (i % 2)
            {
                instr0->DEST_AD = CM_GP0DIV_BUS;
                instr0->STRIDE = 4;
                instr0->TI = (1 << 26);
            }

            if (instrCnt != 0)
            {
                reinterpret_cast<volatile CB *>(instructions[instrCnt - 1].v)
                    ->NEXTCONBK = static_cast<uint32_t>(instructions[instrCnt].b);
            }

            instr0++;
            instrCnt++;
        }
    }

    reinterpret_cast<volatile CB *>(instructions[1023].v)
        ->NEXTCONBK = static_cast<uint32_t>(instructions[0].b);
    reinterpret_cast<volatile CB *>(instructions[1023].v)
        ->NEXTCONBK = static_cast<uint32_t>(instructions[0].b);

    access_bus_address(CLK_BUS_BASE + 40 * 4) = 0x5A000026;
    owner_.backendThrowIfStopRequested("waiting for hardware");
    (void)owner_.backendWaitInterruptableFor(std::chrono::milliseconds(1));
    owner_.backendThrowIfStopRequested("waiting for hardware");
    access_bus_address(CLK_BUS_BASE + 41 * 4) = 0x5A002000;
    access_bus_address(CLK_BUS_BASE + 40 * 4) = 0x5A000016;
    owner_.backendThrowIfStopRequested("waiting for hardware");
    (void)owner_.backendWaitInterruptableFor(std::chrono::milliseconds(1));
    owner_.backendThrowIfStopRequested("waiting for hardware");

    access_bus_address(PWM_BUS_BASE + 0x0) = 0;
    owner_.backendThrowIfStopRequested("waiting for hardware");
    (void)owner_.backendWaitInterruptableFor(std::chrono::milliseconds(1));
    owner_.backendThrowIfStopRequested("waiting for hardware");
    access_bus_address(PWM_BUS_BASE + 0x4) = -1;
    owner_.backendThrowIfStopRequested("waiting for hardware");
    (void)owner_.backendWaitInterruptableFor(std::chrono::milliseconds(1));
    owner_.backendThrowIfStopRequested("waiting for hardware");
    access_bus_address(PWM_BUS_BASE + 0x10) = 32;
    access_bus_address(PWM_BUS_BASE + 0x20) = 32;
    access_bus_address(PWM_BUS_BASE + 0x0) = -1;
    owner_.backendThrowIfStopRequested("waiting for hardware");
    (void)owner_.backendWaitInterruptableFor(std::chrono::milliseconds(1));
    owner_.backendThrowIfStopRequested("waiting for hardware");
    access_bus_address(PWM_BUS_BASE + 0x8) = (1 << 31) | 0x0707;

    std::uintptr_t delta = DMA_BUS_BASE - Mailbox::PERIPH_BUS_BASE;
    volatile uint8_t *dma_base = dma_config_.peripheral_base_virtual + delta;
    volatile struct DMAregs *DMA0 = reinterpret_cast<volatile struct DMAregs *>(dma_base);
    DMA0->CS = 1 << 31;
    DMA0->CONBLK_AD = 0;
    DMA0->TI = 0;
    DMA0->CONBLK_AD = static_cast<uint32_t>(instr_page.b);
    DMA0->CS = (1 << 0) | (255 << 16);
}

void WsprRpiBackend::setup_dma()
{
    mailbox.open();
    get_plld();

    uint32_t base = Mailbox::discoverPeripheralBase();
    dma_config_.peripheral_base_virtual = ::mailbox.mapMem(
        base,
        Mailbox::PAGE_SIZE * NUM_PAGES);

    dma_config_.orig_gp0ctl = access_bus_address(CM_GP0CTL_BUS);
    dma_config_.orig_gp0div = access_bus_address(CM_GP0DIV_BUS);
    dma_config_.orig_gpfsel0 = access_bus_address(GPIO_BUS_BASE + 0);
    dma_config_.orig_gpfsel1 = access_bus_address(GPIO_BUS_BASE + 4);
    dma_config_.orig_gpfsel2 = access_bus_address(GPIO_BUS_BASE + 8);
    dma_config_.orig_pwm_ctl = access_bus_address(PWM_BUS_BASE + 0x00);
    dma_config_.orig_pwm_sta = access_bus_address(PWM_BUS_BASE + 0x04);
    dma_config_.orig_pwm_rng1 = access_bus_address(PWM_BUS_BASE + 0x10);
    dma_config_.orig_pwm_rng2 = access_bus_address(PWM_BUS_BASE + 0x20);
    dma_config_.orig_pwm_fifocfg = access_bus_address(PWM_BUS_BASE + 0x08);

    constexpr int kMaxAttempts = 3;
    int attempts = 0;
    while (true)
    {
        try
        {
            MailboxMemoryPool pool(1025);
            break;
        }
        catch (const std::system_error &e)
        {
            if (e.code().value() == ETIMEDOUT)
            {
                std::ostringstream oss;
                oss << attempts
                    << ") allocating memory pool, retrying.";
                owner_.backendFireTransmitCallback(
                    WsprTransmissionCallbackEvent::LOGGING,
                    WsprTransmitLogLevel::DEBUG,
                    oss.str(),
                    0.0);

                if (++attempts >= kMaxAttempts)
                    throw std::runtime_error(
                        "Mailbox::setup_dma() Too many mailbox timeouts, "
                        "giving up");

                try
                {
                    ::mailbox.close();
                }
                catch (...)
                {
                }
                owner_.backendThrowIfStopRequested("waiting to reopen mailbox");
                (void)owner_.backendWaitInterruptableFor(std::chrono::milliseconds(50));
                owner_.backendThrowIfStopRequested("waiting to reopen mailbox");
                ::mailbox.open();
            }
        }
        catch (...)
        {
            throw;
        }
    }

    create_dma_pages(const_page_, instr_page_, instructions_);
    dma_setup_done_ = true;

    uint32_t div_reg = static_cast<uint32_t>(
        access_bus_address(CLK_BUS_BASE + 41 * 4));
    uint32_t divisor = (div_reg >> 12) & 0xFFF;

    if (divisor == 0)
    {
        throw std::runtime_error(
            "setup_dma(): PWM clock divisor read back as 0 (bad register read/mapping).");
    }

    pwm_clock_init_ = dma_config_.plld_clock_frequency / double(divisor);

    if (!std::isfinite(pwm_clock_init_) || pwm_clock_init_ < 1e6 || pwm_clock_init_ > 2e9)
    {
        throw std::runtime_error(
            "setup_dma(): PWM clock computed out of range (bad divisor/readback).");
    }

    {
        std::ostringstream oss;
        oss << "PWM div reg=0x" << std::hex << div_reg << std::dec
            << " divisor=" << divisor
            << " pwm_clock_init_=" << std::fixed << std::setprecision(3)
            << pwm_clock_init_;
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    {
        std::ostringstream oss;
        oss << "Actual PWM clock = "
            << std::fixed << std::setprecision(0)
            << pwm_clock_init_
            << " Hz";
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }
}

WsprTransmissionConfigureResult WsprRpiBackend::setup_dma_freq_table(
    const WsprTransmissionPlan &plan)
{
    WsprTransmissionConfigureResult result{};
    result.applied_frequency_hz = plan.frequency_hz;

    configure_transmit_gpio(plan.tx_gpio);

    dma_config_.plld_clock_frequency = gpioCorrectedPlldFrequency(
        dma_config_.plld_nominal_freq,
        plan.ppm);

    const double minimum_tone_hz =
        plan.frequency_hz - 1.5 * plan.tone_spacing_hz;
    const double maximum_tone_hz =
        plan.frequency_hz + 1.5 * plan.tone_spacing_hz;
    const GpioRfClockPlan rf_clock = gpioPlanRfClock(
        dma_config_.processor_profile,
        minimum_tone_hz,
        maximum_tone_hz,
        plan.ppm);
    dma_config_.gpclk_nominal_freq = rf_clock.nominal_hz;
    dma_config_.gpclk_clock_frequency = rf_clock.corrected_hz;
    dma_config_.gpclk_source = rf_clock.source;

    {
        std::ostringstream oss;
        oss << "GPIO RF clock source="
            << (rf_clock.source == GpioRfClockSource::Oscillator
                    ? "oscillator"
                    : "PLLD")
            << " nominal_hz=" << std::fixed << std::setprecision(0)
            << rf_clock.nominal_hz
            << " corrected_hz=" << std::fixed << std::setprecision(3)
            << rf_clock.corrected_hz;
        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    if (!std::isfinite(dma_config_.plld_clock_frequency) ||
        dma_config_.plld_clock_frequency <= 0.0 ||
        !std::isfinite(dma_config_.gpclk_clock_frequency) ||
        dma_config_.gpclk_clock_frequency <= 0.0)
    {
        throw std::runtime_error(
            "configureTransmission(): invalid PLLD or GPCLK frequency after PPM correction.");
    }

    uint32_t div_reg = static_cast<uint32_t>(
        access_bus_address(CLK_BUS_BASE + 41 * 4));
    uint32_t divisor = (div_reg >> 12) & 0xFFF;

    if (divisor == 0)
    {
        throw std::runtime_error(
            "configureTransmission(): PWM clock divisor read back as 0.");
    }

    pwm_clock_init_ = dma_config_.plld_clock_frequency / double(divisor);

    double div_lo = bit_trunc(
                        dma_config_.gpclk_clock_frequency /
                            (plan.frequency_hz - 1.5 * plan.tone_spacing_hz),
                        -12) +
                    std::pow(2.0, -12);
    double div_hi = bit_trunc(
        dma_config_.gpclk_clock_frequency /
            (plan.frequency_hz + 1.5 * plan.tone_spacing_hz),
        -12);

    if (std::floor(div_lo) != std::floor(div_hi))
    {
        result.applied_frequency_hz =
            dma_config_.gpclk_clock_frequency / std::floor(div_lo) -
            1.6 * plan.tone_spacing_hz;
        if (plan.frequency_hz != 0.0)
        {
            std::stringstream temp;
            temp << "Center frequency has been changed to "
                 << WsprTransmitter::formatFrequencyMHz(result.applied_frequency_hz)
                 << " MHz";
            std::ostringstream oss;
            oss << temp.str()
                << " because of hardware limitations.";
            owner_.backendFireTransmitCallback(
                WsprTransmissionCallbackEvent::LOGGING,
                WsprTransmitLogLevel::DEBUG,
                oss.str(),
                0.0);
        }
    }

    double tone0_freq = result.applied_frequency_hz - 1.5 * plan.tone_spacing_hz;
    std::vector<std::uint32_t> tuning_word(1024);

    for (int i = 0; i < 8; i++)
    {
        double tone_freq = tone0_freq + (i >> 1) * plan.tone_spacing_hz;
        tuning_word[i] = gpioBuildDividerWord(
            dma_config_.gpclk_clock_frequency,
            tone_freq,
            i % 2 == 0);
        active_gpclk_words_[i] = tuning_word[i];
    }

    if (trace_wspr_tones_enabled())
    {
        std::ostringstream oss;
        oss
            << "WSPR tone table:"
            << " center_hz=" << std::fixed << std::setprecision(6)
            << result.applied_frequency_hz
            << " spacing_hz=" << std::fixed << std::setprecision(9)
            << plan.tone_spacing_hz
            << " ppm=" << std::fixed << std::setprecision(6)
            << plan.ppm
            << " plld_hz=" << std::fixed << std::setprecision(3)
            << dma_config_.plld_clock_frequency
            << " gpclk_source="
            << static_cast<std::uint32_t>(dma_config_.gpclk_source)
            << " gpclk_hz=" << std::fixed << std::setprecision(3)
            << dma_config_.gpclk_clock_frequency
            << " pwm_clock_hz=" << std::fixed << std::setprecision(3)
            << pwm_clock_init_
            << " dither_block_clocks="
            << WsprRpiBackend::frequencyDitherBlockClocks();

        for (int symbol = 0; symbol < 4; ++symbol)
        {
            const int lower_index = symbol * 2;
            const int upper_index = lower_index + 1;
            const double requested_hz =
                tone0_freq + static_cast<double>(symbol) * plan.tone_spacing_hz;
            const double lower_hz =
                dma_config_.gpclk_clock_frequency /
                (static_cast<double>(tuning_word[lower_index]) / std::pow(2.0, 12));
            const double upper_hz =
                dma_config_.gpclk_clock_frequency /
                (static_cast<double>(tuning_word[upper_index]) / std::pow(2.0, 12));
            oss
                << " tone" << symbol
                << "_requested_hz=" << std::fixed << std::setprecision(6)
                << requested_hz
                << " tone" << symbol
                << "_lower_word=0x" << std::hex << tuning_word[lower_index]
                << " tone" << symbol
                << "_upper_word=0x" << tuning_word[upper_index]
                << std::dec
                << " tone" << symbol
                << "_lower_hz=" << std::fixed << std::setprecision(6)
                << lower_hz
                << " tone" << symbol
                << "_upper_hz=" << std::fixed << std::setprecision(6)
                << upper_hz;
        }

        owner_.backendFireTransmitCallback(
            WsprTransmissionCallbackEvent::LOGGING,
            WsprTransmitLogLevel::DEBUG,
            oss.str(),
            0.0);
    }

    for (int i = 8; i < 1024; i++)
    {
        // DMA control blocks initially reference an otherwise unused table
        // entry before transmit_symbol() assigns the active tone pair.  Keep
        // every placeholder on a validated in-plan divider so enabling GPCLK
        // can never expose an arbitrary stale frequency.
        tuning_word[i] = tuning_word[0];
    }

    for (int i = 0; i < 1024; i++)
    {
        reinterpret_cast<std::uint32_t *>(const_page_.v)[i] =
            (0x5Au << 24) | (tuning_word[i] & kGpclkDividerMask);

        if ((i % 2 == 0) && (i < 8))
        {
            assert((tuning_word[i] & (~0xFFFu)) == (tuning_word[i + 1] & (~0xFFFu)));
        }
    }

    return result;
}
