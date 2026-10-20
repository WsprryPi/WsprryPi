#include "gpio_startup_quiesce_qualification.hpp"

#include "wspr_transmit.hpp"

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
using gpio_startup_quiesce_qualification::AuditReport;
using gpio_startup_quiesce_qualification::RegisterSnapshot;

constexpr const char *kAcknowledgement =
    "--i-understand-this-accesses-live-rpi-clock-hardware";

std::string hex32(std::uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

const char *registerName(RpiStartupQuiesceRegister reg)
{
    switch (reg)
    {
    case RpiStartupQuiesceRegister::Dma0ControlStatus: return "DMA0_CS";
    case RpiStartupQuiesceRegister::Dma0ControlBlockAddress: return "DMA0_CONBLK_AD";
    case RpiStartupQuiesceRegister::Dma0TransferInformation: return "DMA0_TI";
    case RpiStartupQuiesceRegister::Dma0SourceAddress: return "DMA0_SOURCE_AD";
    case RpiStartupQuiesceRegister::Dma0DestinationAddress: return "DMA0_DEST_AD";
    case RpiStartupQuiesceRegister::Dma0TransferLength: return "DMA0_TXFR_LEN";
    case RpiStartupQuiesceRegister::Dma0Stride: return "DMA0_STRIDE";
    case RpiStartupQuiesceRegister::Dma0NextControlBlock: return "DMA0_NEXTCONBK";
    case RpiStartupQuiesceRegister::Dma0Debug: return "DMA0_DEBUG";
    case RpiStartupQuiesceRegister::PwmControl: return "PWM_CTL";
    case RpiStartupQuiesceRegister::PwmDmaConfiguration: return "PWM_DMAC";
    case RpiStartupQuiesceRegister::Gpclk0Control: return "GPCLK0_CTL";
    case RpiStartupQuiesceRegister::GpioFunctionSelect0: return "GPFSEL0";
    case RpiStartupQuiesceRegister::GpioFunctionSelect2: return "GPFSEL2";
    }
    return "UNKNOWN";
}

class AuditingAccess final : public IRpiStartupQuiesceAccess
{
public:
    AuditingAccess(
        std::shared_ptr<IRpiStartupQuiesceAccess> delegate,
        int gpio,
        AuditReport &report)
        : delegate_(std::move(delegate)), gpio_(gpio), report_(report)
    {
    }

    bool supportedPlatform(std::string &error) override
    {
        report_.trace.emplace_back("support");
        if (gpio_ != 4 && gpio_ != 20)
            return reject(error, "Auditor rejected unsupported GPIO.");
        return delegateCall(
            delegate_->supportedPlatform(error), error,
            "Production adapter rejected the platform.");
    }

    bool discoverPeripheralBase(
        std::uint32_t &base,
        std::string &error) override
    {
        report_.trace.emplace_back("discover");
        if (opened_ || mapped_)
            return reject(error, "Auditor rejected discovery in an invalid lifecycle state.");
        return delegateCall(
            delegate_->discoverPeripheralBase(base, error), error,
            "Production adapter could not discover the peripheral base.");
    }

    bool open(std::string &error) override
    {
        report_.trace.emplace_back("open");
        ++report_.opens;
        if (opened_ || mapped_)
            return reject(error, "Auditor rejected duplicate open.");
        if (!delegateCall(delegate_->open(error), error,
                          "Production adapter open failed."))
            return false;
        opened_ = true;
        return true;
    }

    bool map(
        std::uint32_t peripheral_base,
        std::size_t size,
        std::string &error) override
    {
        report_.trace.emplace_back("map:" + hex32(static_cast<std::uint32_t>(size)));
        ++report_.maps;
        if (!opened_ || mapped_ || size != gpio_startup_quiesce_qualification::kMapSize)
            return reject(error, "Auditor rejected map lifecycle or size.");
        if (!delegateCall(delegate_->map(peripheral_base, size, error), error,
                          "Production adapter map failed."))
            return false;
        mapped_ = true;
        return true;
    }

    bool read(
        RpiStartupQuiesceRegister reg,
        std::uint32_t &value,
        std::string &error) override
    {
        if (!opened_ || !mapped_)
            return reject(error, "Auditor rejected read while unmapped.");
        if (!approvedRegister(reg))
            return reject(error, "Auditor rejected an unapproved register read.");
        if (!delegateCall(delegate_->read(reg, value, error), error,
                          "Production adapter register read failed."))
            return false;
        report_.trace.emplace_back(
            std::string("read:") + registerName(reg) + "=" + hex32(value));
        if (reg == selectedGpioRegister())
            selected_gpio_last_read_ = value;
        return true;
    }

    bool write(
        RpiStartupQuiesceRegister reg,
        std::uint32_t value,
        std::string &error) override
    {
        if (!opened_ || !mapped_)
            return reject(error, "Auditor rejected write while unmapped.");
        if (!safeWrite(reg, value))
            return reject(error, "Auditor rejected an unsafe or unexpected register write.");
        report_.trace.emplace_back(
            std::string("write:") + registerName(reg) + "=" + hex32(value));
        return delegateCall(delegate_->write(reg, value, error), error,
                            "Production adapter register write failed.");
    }

    bool unmap(std::size_t size, std::string &error) override
    {
        report_.trace.emplace_back("unmap:" + hex32(static_cast<std::uint32_t>(size)));
        ++report_.unmaps;
        if (!opened_ || !mapped_ || size != gpio_startup_quiesce_qualification::kMapSize)
            return reject(error, "Auditor rejected unmap lifecycle or size.");
        mapped_ = false;
        selected_gpio_last_read_.reset();
        return delegateCall(delegate_->unmap(size, error), error,
                            "Production adapter unmap failed.");
    }

    bool close(std::string &error) override
    {
        report_.trace.emplace_back("close");
        ++report_.closes;
        if (!opened_ || mapped_)
            return reject(error, "Auditor rejected close in an invalid lifecycle state.");
        opened_ = false;
        const bool ok = delegateCall(delegate_->close(error), error,
                                     "Production adapter close failed.");
        updateBalance();
        return ok;
    }

private:
    bool approvedRegister(RpiStartupQuiesceRegister reg) const
    {
        if (reg == RpiStartupQuiesceRegister::GpioFunctionSelect0 ||
            reg == RpiStartupQuiesceRegister::GpioFunctionSelect2)
            return reg == selectedGpioRegister();
        return std::string(registerName(reg)) != "UNKNOWN";
    }

    RpiStartupQuiesceRegister selectedGpioRegister() const
    {
        return gpio_ == 4
            ? RpiStartupQuiesceRegister::GpioFunctionSelect0
            : RpiStartupQuiesceRegister::GpioFunctionSelect2;
    }

    bool safeWrite(RpiStartupQuiesceRegister reg, std::uint32_t value) const
    {
        switch (reg)
        {
        case RpiStartupQuiesceRegister::Dma0ControlStatus:
            return value == 0xc0000000u || value == 0x80000000u;
        case RpiStartupQuiesceRegister::Dma0ControlBlockAddress:
        case RpiStartupQuiesceRegister::Dma0TransferInformation:
        case RpiStartupQuiesceRegister::Dma0SourceAddress:
        case RpiStartupQuiesceRegister::Dma0DestinationAddress:
        case RpiStartupQuiesceRegister::Dma0TransferLength:
        case RpiStartupQuiesceRegister::Dma0Stride:
        case RpiStartupQuiesceRegister::Dma0NextControlBlock:
        case RpiStartupQuiesceRegister::PwmControl:
        case RpiStartupQuiesceRegister::PwmDmaConfiguration:
            return value == 0;
        case RpiStartupQuiesceRegister::Dma0Debug:
            return value == 7;
        case RpiStartupQuiesceRegister::Gpclk0Control:
            return (value & (1u << 4)) == 0 &&
                   (value & 0xff000000u) == 0x5a000000u;
        case RpiStartupQuiesceRegister::GpioFunctionSelect0:
        case RpiStartupQuiesceRegister::GpioFunctionSelect2:
            if (reg != selectedGpioRegister() || !selected_gpio_last_read_.has_value())
                return false;
            {
                const unsigned shift = gpio_ == 4 ? 12u : 0u;
                return value ==
                    (*selected_gpio_last_read_ & ~(std::uint32_t{0x7} << shift));
            }
        }
        return false;
    }

    bool delegateCall(bool ok, std::string &error, const char *fallback)
    {
        if (ok)
            return true;
        if (error.empty())
            error = fallback;
        retainReportError(error);
        return false;
    }

    bool reject(std::string &error, const std::string &message)
    {
        report_.forbidden_operation = true;
        if (error.empty())
            error = message;
        retainReportError(error);
        updateBalance();
        return false;
    }

    void retainReportError(const std::string &error)
    {
        if (report_.error.empty())
            report_.error = error;
    }

    void updateBalance()
    {
        report_.lifecycle_balanced =
            !opened_ && !mapped_ && report_.opens == report_.closes &&
            report_.maps == report_.unmaps;
    }

    std::shared_ptr<IRpiStartupQuiesceAccess> delegate_;
    int gpio_;
    AuditReport &report_;
    bool opened_{false};
    bool mapped_{false};
    std::optional<std::uint32_t> selected_gpio_last_read_;
};

class QualificationBridge final : public IControllerBridge
{
public:
    WsprTransmitState backendStateValue() const noexcept override
    {
        return WsprTransmitState::DISABLED;
    }
    void backendSetStateValue(WsprTransmitState) noexcept override {}
    bool backendShouldStop() const noexcept override { return false; }
    void backendSignalStopRequest() noexcept override {}
    void backendRequestStopTxNoJoin() noexcept override {}
    bool backendWaitInterruptableFor(std::chrono::nanoseconds) override { return true; }
    void backendThrowIfStopRequested(const char *) override {}
    void backendReportExecutionProgress(std::size_t) noexcept override {}
    void backendFireTransmitCallback(
        WsprTransmissionCallbackEvent,
        WsprTransmitLogLevel,
        const std::string &,
        double) override {}
    bool backendRestartCurrentConfiguration() override { return false; }
};

bool readRegister(
    IRpiStartupQuiesceAccess &access,
    RpiStartupQuiesceRegister reg,
    std::uint32_t &value,
    std::string &error)
{
    return access.read(reg, value, error);
}

bool observe(
    IRpiStartupQuiesceAccess &access,
    int gpio,
    RegisterSnapshot &snapshot,
    std::string &error)
{
    std::uint32_t base = 0;
    bool opened = false;
    bool mapped = false;
    bool ok = access.supportedPlatform(error) &&
              access.discoverPeripheralBase(base, error) &&
              access.open(error);
    opened = ok;
    if (ok)
    {
        ok = access.map(base, gpio_startup_quiesce_qualification::kMapSize, error);
        mapped = ok;
    }
    if (ok)
    {
        ok =
            readRegister(access, RpiStartupQuiesceRegister::Dma0ControlStatus, snapshot.dma_control_status, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0ControlBlockAddress, snapshot.dma_control_block_address, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0TransferInformation, snapshot.dma_transfer_information, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0SourceAddress, snapshot.dma_source_address, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0DestinationAddress, snapshot.dma_destination_address, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0TransferLength, snapshot.dma_transfer_length, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0Stride, snapshot.dma_stride, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0NextControlBlock, snapshot.dma_next_control_block, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Dma0Debug, snapshot.dma_debug, error) &&
            readRegister(access, RpiStartupQuiesceRegister::PwmControl, snapshot.pwm_control, error) &&
            readRegister(access, RpiStartupQuiesceRegister::PwmDmaConfiguration, snapshot.pwm_dma_configuration, error) &&
            readRegister(access, RpiStartupQuiesceRegister::Gpclk0Control, snapshot.gpclk0_control, error) &&
            readRegister(
                access,
                gpio == 4
                    ? RpiStartupQuiesceRegister::GpioFunctionSelect0
                    : RpiStartupQuiesceRegister::GpioFunctionSelect2,
                snapshot.gpio_function_select,
                error);
    }

    if (mapped && !access.unmap(gpio_startup_quiesce_qualification::kMapSize, error))
        ok = false;
    if (opened && !access.close(error))
        ok = false;
    return ok;
}

bool safeState(
    const RegisterSnapshot &before,
    const RegisterSnapshot &after,
    int gpio,
    std::string &error)
{
    const bool dma_safe =
        (after.dma_control_status & 1u) == 0 &&
        after.dma_control_block_address == 0 &&
        after.dma_next_control_block == 0;
    if (!dma_safe)
    {
        error = "DMA channel 0 did not reach the required inactive detached state.";
        return false;
    }
    if (after.pwm_control != 0 || after.pwm_dma_configuration != 0)
    {
        error = "PWM control or DMA configuration remained enabled.";
        return false;
    }
    if ((after.gpclk0_control & ((1u << 4) | (1u << 7))) != 0)
    {
        error = "GPCLK0 remained enabled or busy.";
        return false;
    }
    const unsigned shift = gpio == 4 ? 12u : 0u;
    const std::uint32_t mask = std::uint32_t{0x7} << shift;
    if ((after.gpio_function_select & mask) != 0 ||
        (after.gpio_function_select & ~mask) !=
            (before.gpio_function_select & ~mask))
    {
        error = "Selected GPIO mux was not cleared without altering unrelated mux bits.";
        return false;
    }
    return true;
}
} // namespace

namespace gpio_startup_quiesce_qualification
{
bool parseOptions(int argc, char **argv, Options &options, std::string &error)
{
    if (argc != 6 || std::string(argv[1]) != "--gpio" ||
        std::string(argv[3]) != "--count" ||
        std::string(argv[5]) != kAcknowledgement)
    {
        error = "Refusing hardware access: require exactly --gpio 4|20 --count 2 " +
            std::string(kAcknowledgement) + ".";
        return false;
    }
    try
    {
        std::size_t used = 0;
        options.gpio = std::stoi(argv[2], &used, 10);
        if (used != std::string(argv[2]).size() ||
            (options.gpio != 4 && options.gpio != 20))
            throw std::invalid_argument("gpio");
        options.count = std::stoi(argv[4], &used, 10);
        if (used != std::string(argv[4]).size() || options.count != 2)
            throw std::invalid_argument("count");
    }
    catch (const std::exception &)
    {
        error = "Refusing hardware access: GPIO must be 4 or 20 and count must be exactly 2.";
        return false;
    }
    return true;
}

std::shared_ptr<IRpiStartupQuiesceAccess> makeAuditingAccess(
    std::shared_ptr<IRpiStartupQuiesceAccess> delegate,
    int gpio,
    AuditReport &report)
{
    return std::make_shared<AuditingAccess>(std::move(delegate), gpio, report);
}

Result run(
    const Options &options,
    std::shared_ptr<IRpiStartupQuiesceAccess> delegate)
{
    Result result;
    if ((options.gpio != 4 && options.gpio != 20) || options.count != 2 || !delegate)
    {
        result.error = "Qualification options or delegate are invalid.";
        return result;
    }

    auto access = makeAuditingAccess(std::move(delegate), options.gpio, result.audit);
    QualificationBridge bridge;
    WsprRpiBackend backend(bridge, access, options.gpio);

    if (!observe(*access, options.gpio, result.before, result.error))
        return result;

    const auto first = backend.quiesceForStartup();
    ++result.backend_calls;
    if (!first.ok)
    {
        result.error = first.error.empty() ? "First backend quiesce failed." : first.error;
        return result;
    }
    if (!observe(*access, options.gpio, result.after_first, result.error) ||
        !safeState(result.before, result.after_first, options.gpio, result.error))
        return result;

    const auto second = backend.quiesceForStartup();
    ++result.backend_calls;
    if (!second.ok)
    {
        result.error = second.error.empty() ? "Second backend quiesce failed." : second.error;
        return result;
    }
    if (!observe(*access, options.gpio, result.after_second, result.error) ||
        !safeState(result.after_first, result.after_second, options.gpio, result.error))
        return result;

    if (result.backend_calls != 2 || !result.audit.lifecycle_balanced ||
        result.audit.forbidden_operation)
    {
        result.error = "Qualification call count, lifecycle, or audit policy failed.";
        return result;
    }
    result.ok = true;
    return result;
}
} // namespace gpio_startup_quiesce_qualification
