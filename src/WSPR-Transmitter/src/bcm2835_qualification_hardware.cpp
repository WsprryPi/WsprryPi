// SPDX-License-Identifier: MIT
#include "bcm2835_qualification_hardware.hpp"
#include "bcm2835_qualification_lease.hpp"
#include "mailbox.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <thread>

namespace wsprrypi
{
namespace
{
using namespace std::chrono_literals;
constexpr unsigned password = 0x5a000000;
constexpr unsigned enable_bit = 1 << 4;
constexpr unsigned busy_bit = 1 << 7;
constexpr unsigned running = enable_bit | busy_bit;
constexpr unsigned kill_bit = 1 << 5;
constexpr unsigned plld = 6;
constexpr unsigned mash3 = 3 << 9;
constexpr unsigned gp_control = 0x101070;
constexpr unsigned gp_divider = 0x101074;
constexpr unsigned pwm_clock = 0x1010a0;
constexpr unsigned pwm_divider = 0x1010a4;
constexpr unsigned pwm_control = 0x20c000;
constexpr unsigned pwm_dmac = 0x20c008;
constexpr unsigned pwm_range = 0x20c010;
constexpr unsigned pads = 0x10002c;
constexpr unsigned dma_control = 0x7000;
constexpr unsigned dma_block = 0x7004;
constexpr unsigned dma_next = 0x701c;
constexpr unsigned dma_enable = 0x7ff0;
constexpr unsigned dma_reset = 1u << 31;
constexpr unsigned dma_active = 1;
constexpr unsigned dma_error = 1u << 8;
constexpr unsigned dma_in_flight = dma_reset | (1u << 30) | (1u << 6) | dma_active;
constexpr unsigned fields = 0x1f;
constexpr unsigned register_map_size = 0x210000;
constexpr unsigned ring_size = 3 * Mailbox::PAGE_SIZE;

struct alignas(32) DmaBlock
{
    unsigned info, source, destination, length, stride, next, reserved[2];
};
constexpr unsigned maximum_blocks = bcm2835_qualification_dither_blocks * 4;
constexpr unsigned data_offset = sizeof(DmaBlock) * maximum_blocks;

class NativeHardware final : public QualificationHardware
{
public:
    NativeHardware()
    {
        if (getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS") || !bcm2835QualificationBoard() ||
            Mailbox::discoverPeripheralBase() != 0x20000000)
            throw std::runtime_error("BCM2835 qualification MMIO unavailable.");
        registers_ = mailbox_.mapMem(0x20000000, register_map_size);
    }

    ~NativeHardware() override
    {
        try { mailbox_.unMapMem(registers_, register_map_size); } catch (...) {}
    }

    QualificationObservation observe() override
    {
        const unsigned clock = reg(gp_control);
        return {{reg(pads) & fields}, pin(4), pin(20), bool(clock & enable_bit),
            bool(clock & busy_bit), !(reg(dma_control) & dma_in_flight) &&
                !reg(dma_block) && !reg(dma_next),
            !(reg(pwm_clock) & running) && !reg(pwm_control) && !reg(pwm_dmac)};
    }

    unsigned prepare(QualificationBank original) override
    {
        const auto state = observe();
        if (!state.idle() || state.bank != original || pin(32) == 4 || pin(34) == 4 ||
            !(reg(dma_enable) & 1))
            throw std::runtime_error("BCM2835 stored bank or idle resources changed.");
        mailbox_.open();
        handle_ = mailbox_.memAlloc(ring_size, Mailbox::PAGE_SIZE);
        if (!handle_) throw std::runtime_error("BCM2835 DMA allocation failed.");
        bus_ = mailbox_.memLock(handle_);
        if (!bus_ || bus_ > std::numeric_limits<unsigned>::max() - ring_size ||
            bus_ % Mailbox::PAGE_SIZE)
            throw std::runtime_error("Invalid BCM2835 DMA allocation.");
        ring_ = mailbox_.mapMem(Mailbox::busToPhysical(bus_), ring_size);
        return bus_;
    }

    void enable(const Bcm2835QualificationPlan& plan, QualificationBank original,
        const std::function<bool()>& still_armed) override
    {
        const auto state = observe();
        if (!ring_ || !state.idle() || state.bank != original ||
            pin(32) == 4 || pin(34) == 4 || !still_armed())
            throw std::runtime_error("BCM2835 output enable is no longer admitted.");

        buildRing(plan);
        reg(gp_divider) = password | plan.dividers.words[0];
        reg(pwm_divider) = password | 0x2000;
        reg(pwm_clock) = password | plld | enable_bit;
        reg(pwm_range) = 32;
        reg(pwm_control) = 1 << 6; // clear FIFO
        reg(pwm_dmac) = (1u << 31) | 0x0707;
        reg(pwm_control) = 0x21; // channel 1, FIFO, serializer mode disabled
        reg(dma_block) = bus_;
        reg(dma_control) = dma_active | (8u << 16) | (8u << 20);
        __sync_synchronize();
        if (!still_armed() || (reg(pads) & fields) != original.bits ||
            pin(4) || pin(20) || pin(32) == 4 || pin(34) == 4 ||
            (reg(gp_control) & running))
            throw std::runtime_error("BCM2835 pin, bank, clock, or deadline changed before enable.");
        const auto applied = original.withDrive(plan.tone.power_level);
        reg(pads) = password | applied.bits;
        if ((reg(pads) & fields) != applied.bits)
            throw std::runtime_error("BCM2835 drive readback failed.");
        const int selected = plan.tone.tx_gpio;
        setPin(selected, selected == 4 ? 4 : 2);
        __sync_synchronize();
        if (!still_armed())
            throw std::runtime_error("BCM2835 deadline closed before clock enable.");
        reg(gp_control) = password | mash3 | plld | enable_bit;
    }

    bool active(const Bcm2835QualificationPlan& plan, QualificationBank original) override
    {
        const auto state = observe();
        const auto divider = reg(gp_divider) & 0xffffff;
        if (!state.clock_enabled || !state.clock_busy ||
            (reg(gp_control) & (mash3 | 15)) != (mash3 | plld) ||
            (divider != plan.dividers.words[0] && divider != plan.dividers.words[1]) ||
            (reg(dma_control) & (dma_active | dma_error)) != dma_active || !ownsDma(bus_) ||
            state.gpio4 != (plan.tone.tx_gpio == 4 ? 4u : 0u) ||
            state.gpio20 != (plan.tone.tx_gpio == 20 ? 2u : 0u) ||
            state.bank != original.withDrive(plan.tone.power_level))
            return false;

        // ACTIVE alone can remain set while DMA is stuck at one divider.
        // Require observable ring progress inside the sample-bracket budget.
        const unsigned block = reg(dma_block);
        for (unsigned attempt = 0; attempt < 6; ++attempt)
        {
            std::this_thread::sleep_for(150us);
            if (reg(dma_block) != block)
                return ownsDma(bus_) && (reg(dma_control) & (dma_active | dma_error)) == dma_active;
        }
        return false;
    }

    bool stop(const Bcm2835QualificationPlan& plan,
        QualificationBank original, unsigned worker_bus) override
    {
        const int selected = plan.tone.tx_gpio;
        const unsigned alternate = selected == 4 ? 4 : 2;
        const unsigned selected_mux = pin(selected);
        const unsigned clock = reg(gp_control);
        const auto applied = original.withDrive(plan.tone.power_level);
        const bool dma_owned = ownsDma(worker_bus);
        const bool route_known = selected_mux == 0 || selected_mux == alternate;
        const bool clock_owned = (clock & (mash3 | 15)) == (mash3 | plld) &&
            (selected_mux == alternate || dma_owned);

        // Stop writes are scoped to the owned route. A different pin function
        // or unexpected shared register is evidence for reconciliation.
        if (selected_mux == alternate && (clock_owned || dma_owned))
            setPin(selected, 0);
        __sync_synchronize();
        if ((clock & running) && clock_owned)
            stopClock(gp_control);
        if (dma_owned)
        {
            reg(dma_control) = dma_reset;
            if (!waitClear(dma_control, dma_in_flight))
                return false;
            reg(dma_block) = 0;
        }
        if (dma_owned && reg(pwm_control) == 0x21 &&
            reg(pwm_dmac) == ((1u << 31) | 0x0707) &&
            (reg(pwm_clock) & 15) == plld)
        {
            reg(pwm_control) = 0;
            reg(pwm_dmac) = 0;
            stopClock(pwm_clock);
        }
        __sync_synchronize();
        const auto state = observe();
        if (!route_known || !state.idle())
            return false;
        if (state.bank == applied && state.bank != original && dma_owned)
            reg(pads) = password | original.bits;
        return observe().idle() && observe().bank == original;
    }

    bool finish() override
    {
        if (ring_) mailbox_.unMapMem(ring_, ring_size);
        ring_ = nullptr;
        bool ok = true;
        if (bus_) ok = mailbox_.memUnlock(handle_) == 0;
        if (handle_) ok = (mailbox_.memFree(handle_) == 0) && ok;
        bus_ = handle_ = 0;
        mailbox_.close();
        return ok;
    }

private:
    volatile unsigned& reg(unsigned offset) { return *reinterpret_cast<volatile unsigned*>(registers_ + offset); }
    unsigned pin(int gpio) { return (reg(0x200000 + gpio / 10 * 4) >> (gpio % 10 * 3)) & 7; }
    void setPin(int gpio, unsigned mux)
    {
        auto& fsel = reg(0x200000 + gpio / 10 * 4);
        const auto shift = gpio % 10 * 3;
        fsel = (fsel & ~(7u << shift)) | (mux << shift);
    }

    bool ownsDma(unsigned base)
    {
        const auto address = reg(dma_block);
        return base && address >= base && address - base < data_offset &&
            (address - base) % sizeof(DmaBlock) == 0;
    }

    bool waitClear(unsigned offset, unsigned mask)
    {
        const auto deadline = std::chrono::steady_clock::now() + 20ms;
        while (reg(offset) & mask)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(100us);
        }
        return true;
    }

    void stopClock(unsigned offset)
    {
        const auto control = reg(offset) & 0xfff;
        reg(offset) = password | (control & ~enable_bit);
        if (!waitClear(offset, running))
        {
            reg(offset) = password | (control & ~enable_bit) | kill_bit;
            if (waitClear(offset, running))
                reg(offset) = password | (control & ~enable_bit);
        }
    }

    void buildRing(const Bcm2835QualificationPlan& plan)
    {
        auto* blocks = reinterpret_cast<volatile DmaBlock*>(ring_);
        auto* values = reinterpret_cast<volatile unsigned*>(ring_ + data_offset);
        values[0] = password | plan.dividers.words[0];
        values[1] = password | plan.dividers.words[1];
        values[2] = 0;
        unsigned count = 0;
        const auto add = [&](unsigned info, unsigned source, unsigned target, unsigned length)
        {
            auto& block = blocks[count++];
            block.info = info;
            block.source = bus_ + data_offset + source * 4;
            block.destination = target;
            block.length = length;
            block.stride = 0;
            block.next = bus_ + count * sizeof(DmaBlock);
            block.reserved[0] = 0;
            block.reserved[1] = 0;
        };
        constexpr auto clocks = bcm2835_qualification_dither_clocks;
        constexpr auto block_clocks = clocks / bcm2835_qualification_dither_blocks;
        std::int64_t lower_sent = 0;
        for (std::int64_t i = 0; i < clocks; i += block_clocks)
        {
            const auto lower = gpioDitherLowerClockCount(
                double(plan.lower_clocks) / clocks, block_clocks, i, lower_sent);
            const std::array<unsigned, 2> lengths{unsigned(lower), unsigned(block_clocks - lower)};
            for (unsigned symbol = 0; symbol < lengths.size(); ++symbol)
            {
                if (lengths[symbol] == 0) continue;
                add((1 << 3) | (1 << 26), symbol, 0x7e101074, 4);
                add((1 << 3) | (1 << 6) | (5 << 16) | (1 << 26), 2, 0x7e20c018,
                    lengths[symbol] * 4);
            }
            lower_sent += lower;
        }
        blocks[count - 1].next = bus_;
        __sync_synchronize();
    }

    Mailbox mailbox_;
    volatile std::uint8_t* registers_{};
    volatile std::uint8_t* ring_{};
    unsigned bus_{};
    unsigned handle_{};
};
}

std::unique_ptr<QualificationHardware> openBcm2835QualificationHardware()
{
    return std::make_unique<NativeHardware>();
}
}
