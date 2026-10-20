#include "gpio_startup_quiesce_qualification.hpp"

#include "wspr_transmit.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>

int production_platform_hook_calls = 0;

bool platform_supports_gpio_clock_transmission(std::string *)
{
    ++production_platform_hook_calls;
    return false;
}

void block_signals()
{
}

std::string WsprTransmitter::formatFrequencyMHz(double)
{
    return {};
}

namespace
{
[[noreturn]] void fail(const std::string &message)
{
    std::cerr << "gpio_startup_quiesce_qualification_test: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition)
        fail(message);
}

class FakeAccess final : public IRpiStartupQuiesceAccess
{
public:
    std::map<RpiStartupQuiesceRegister, std::uint32_t> registers{
        {RpiStartupQuiesceRegister::Dma0ControlStatus, 1},
        {RpiStartupQuiesceRegister::Dma0ControlBlockAddress, 0x11111111},
        {RpiStartupQuiesceRegister::Dma0TransferInformation, 0x22222222},
        {RpiStartupQuiesceRegister::Dma0SourceAddress, 0x33333333},
        {RpiStartupQuiesceRegister::Dma0DestinationAddress, 0x44444444},
        {RpiStartupQuiesceRegister::Dma0TransferLength, 0x55555555},
        {RpiStartupQuiesceRegister::Dma0Stride, 0x66666666},
        {RpiStartupQuiesceRegister::Dma0NextControlBlock, 0x77777777},
        {RpiStartupQuiesceRegister::Dma0Debug, 0},
        {RpiStartupQuiesceRegister::PwmControl, 0xffffffff},
        {RpiStartupQuiesceRegister::PwmDmaConfiguration, 0xffffffff},
        {RpiStartupQuiesceRegister::Gpclk0Control, 0x00000210},
        {RpiStartupQuiesceRegister::GpioFunctionSelect0, 0xa5a5f5a5},
        {RpiStartupQuiesceRegister::GpioFunctionSelect2, 0x5a5a5a5f}};
    int opens{0};
    int maps{0};
    int unmaps{0};
    int closes{0};
    int reads{0};
    int writes{0};
    int fail_read_at{0};
    int fail_write_at{0};
    int fail_map_at{0};
    bool fail_unmap{false};
    bool fail_close{false};
    bool retain_inactive_descriptor_state{false};

    bool supportedPlatform(std::string &) override { return true; }

    bool discoverPeripheralBase(std::uint32_t &base, std::string &error) override
    {
        if (opened_ || mapped_)
            return reject(error, "fake discovery lifecycle");
        base = 0x3f000000;
        return true;
    }

    bool open(std::string &error) override
    {
        ++opens;
        if (opened_ || mapped_)
            return reject(error, "fake open lifecycle");
        opened_ = true;
        return true;
    }

    bool map(std::uint32_t base, std::size_t size, std::string &error) override
    {
        ++maps;
        if (!opened_ || mapped_ || base != 0x3f000000 ||
            size != gpio_startup_quiesce_qualification::kMapSize)
            return reject(error, "fake map bounds or lifecycle");
        if (fail_map_at == maps)
            return reject(error, "injected map failure");
        mapped_ = true;
        return true;
    }

    bool read(
        RpiStartupQuiesceRegister reg,
        std::uint32_t &value,
        std::string &error) override
    {
        ++reads;
        if (!opened_ || !mapped_ || !registers.contains(reg))
            return reject(error, "fake read lifecycle or register");
        if (fail_read_at == reads)
            return reject(error, "injected read failure");
        value = registers[reg];
        return true;
    }

    bool write(
        RpiStartupQuiesceRegister reg,
        std::uint32_t value,
        std::string &error) override
    {
        ++writes;
        if (!opened_ || !mapped_ || !registers.contains(reg))
            return reject(error, "fake write lifecycle or register");
        if (fail_write_at == writes)
            return reject(error, "injected write failure");
        if (retain_inactive_descriptor_state && value == 0 &&
            (reg == RpiStartupQuiesceRegister::Dma0TransferInformation ||
             reg == RpiStartupQuiesceRegister::Dma0SourceAddress ||
             reg == RpiStartupQuiesceRegister::Dma0DestinationAddress ||
             reg == RpiStartupQuiesceRegister::Dma0TransferLength ||
             reg == RpiStartupQuiesceRegister::Dma0Stride))
            return true;
        registers[reg] = value;
        return true;
    }

    bool unmap(std::size_t size, std::string &error) override
    {
        ++unmaps;
        if (!opened_ || !mapped_ ||
            size != gpio_startup_quiesce_qualification::kMapSize)
            return reject(error, "fake unmap bounds or lifecycle");
        mapped_ = false;
        if (fail_unmap)
            return reject(error, "injected unmap failure");
        return true;
    }

    bool close(std::string &error) override
    {
        ++closes;
        if (!opened_ || mapped_)
            return reject(error, "fake close lifecycle");
        opened_ = false;
        if (fail_close)
            return reject(error, "injected close failure");
        return true;
    }

    bool balanced() const
    {
        return !opened_ && !mapped_ && opens == closes && maps == unmaps;
    }

private:
    static bool reject(std::string &error, const std::string &message)
    {
        if (error.empty())
            error = message;
        return false;
    }

    bool opened_{false};
    bool mapped_{false};
};

bool parse(std::initializer_list<const char *> args,
           gpio_startup_quiesce_qualification::Options &options)
{
    std::vector<std::string> storage;
    for (const char *arg : args)
        storage.emplace_back(arg);
    std::vector<char *> argv;
    for (std::string &arg : storage)
        argv.push_back(arg.data());
    std::string error;
    return gpio_startup_quiesce_qualification::parseOptions(
        static_cast<int>(argv.size()), argv.data(), options, error);
}

void testStrictCli()
{
    using gpio_startup_quiesce_qualification::Options;
    Options options;
    expect(parse({"qualification", "--gpio", "4", "--count", "2",
                  "--i-understand-this-accesses-live-rpi-clock-hardware"}, options) &&
               options.gpio == 4 && options.count == 2,
           "canonical GPIO 4 CLI must be accepted");
    expect(parse({"qualification", "--gpio", "20", "--count", "2",
                  "--i-understand-this-accesses-live-rpi-clock-hardware"}, options) &&
               options.gpio == 20,
           "canonical GPIO 20 CLI must be accepted");
    expect(!parse({"qualification"}, options), "missing arguments must be rejected");
    expect(!parse({"qualification", "--count", "2", "--gpio", "4",
                   "--i-understand-this-accesses-live-rpi-clock-hardware"}, options),
           "reordered arguments must be rejected");
    expect(!parse({"qualification", "--gpio", "5", "--count", "2",
                   "--i-understand-this-accesses-live-rpi-clock-hardware"}, options),
           "unsupported GPIO must be rejected");
    expect(!parse({"qualification", "--gpio", "4", "--count", "1",
                   "--i-understand-this-accesses-live-rpi-clock-hardware"}, options),
           "count other than two must be rejected");
    expect(!parse({"qualification", "--gpio", "4", "--count", "2"}, options),
           "missing acknowledgement must be rejected");
    expect(!parse({"qualification", "--gpio", "4", "--count", "2",
                   "--i-understand-this-accesses-live-rpi-clock-hardware", "extra"}, options),
           "extra arguments must be rejected");
}

void testSuccessfulQualification(int gpio)
{
    using namespace gpio_startup_quiesce_qualification;
    auto fake = std::make_shared<FakeAccess>();
    const auto selected = gpio == 4
        ? RpiStartupQuiesceRegister::GpioFunctionSelect0
        : RpiStartupQuiesceRegister::GpioFunctionSelect2;
    const std::uint32_t original_gpio = fake->registers[selected];
    const Result result = run(Options{gpio, 2}, fake);
    const unsigned shift = gpio == 4 ? 12u : 0u;
    const std::uint32_t mask = std::uint32_t{0x7} << shift;
    expect(result.ok && result.backend_calls == 2,
           "successful qualification must make exactly two backend calls");
    expect((result.after_first.dma_control_status & 1u) == 0 &&
               result.after_first.dma_control_block_address == 0 &&
               result.after_first.pwm_control == 0 &&
               result.after_first.pwm_dma_configuration == 0 &&
               (result.after_first.gpclk0_control & ((1u << 4) | (1u << 7))) == 0,
           "first observation must verify safe DMA, PWM, and GPCLK state");
    expect(result.after_first.gpio_function_select == (original_gpio & ~mask) &&
               result.after_second.gpio_function_select == (original_gpio & ~mask),
           "selected GPIO mux must be cleared while unrelated bits are preserved");
    expect(fake->opens == 5 && fake->maps == 5 && fake->unmaps == 5 &&
               fake->closes == 5 && fake->balanced(),
           "three observations plus two backend calls must balance five lifecycles");
    expect(result.audit.lifecycle_balanced && !result.audit.forbidden_operation,
           "successful qualification must satisfy the audit policy");
}

void testInactiveDescriptorStateMayRemainVisible()
{
    using namespace gpio_startup_quiesce_qualification;
    auto fake = std::make_shared<FakeAccess>();
    fake->retain_inactive_descriptor_state = true;
    const Result result = run(Options{4, 2}, fake);
    expect(result.ok && result.backend_calls == 2,
           "inactive DMA descriptor fields retained by hardware must not fail qualification");
    expect((result.after_second.dma_control_status & 1u) == 0 &&
               result.after_second.dma_control_block_address == 0 &&
               result.after_second.dma_next_control_block == 0 &&
               result.after_second.dma_transfer_information != 0 &&
               result.after_second.dma_source_address != 0 &&
               result.after_second.dma_destination_address != 0,
           "retained descriptor test must still prove inactive detached DMA state");
}

void testAuditRejections()
{
    using namespace gpio_startup_quiesce_qualification;
    auto fake = std::make_shared<FakeAccess>();
    AuditReport report;
    auto audit = makeAuditingAccess(fake, 4, report);
    std::string error;
    std::uint32_t base = 0;
    expect(audit->supportedPlatform(error) &&
               audit->discoverPeripheralBase(base, error) && audit->open(error) &&
               audit->map(base, kMapSize, error),
           "auditor setup must succeed with a fake delegate");
    expect(!audit->write(RpiStartupQuiesceRegister::Dma0ControlStatus, 1, error) &&
               report.forbidden_operation,
           "auditor must reject DMA start");
    expect(audit->unmap(kMapSize, error) && audit->close(error),
           "auditor must clean up after rejecting a write");

    auto clock_fake = std::make_shared<FakeAccess>();
    AuditReport clock_report;
    auto clock_audit = makeAuditingAccess(clock_fake, 4, clock_report);
    error.clear();
    expect(clock_audit->supportedPlatform(error) &&
               clock_audit->discoverPeripheralBase(base, error) &&
               clock_audit->open(error) && clock_audit->map(base, kMapSize, error),
           "clock auditor setup must succeed");
    expect(!clock_audit->write(
               RpiStartupQuiesceRegister::Gpclk0Control,
               0x5a000010,
               error) && clock_report.forbidden_operation,
           "auditor must reject GPCLK enable");
    expect(clock_audit->unmap(kMapSize, error) && clock_audit->close(error),
           "clock auditor must clean up after rejection");

    auto lifecycle_fake = std::make_shared<FakeAccess>();
    AuditReport lifecycle_report;
    auto lifecycle = makeAuditingAccess(lifecycle_fake, 4, lifecycle_report);
    std::uint32_t value = 0;
    error.clear();
    expect(!lifecycle->read(
               RpiStartupQuiesceRegister::Dma0ControlStatus, value, error) &&
               lifecycle_report.forbidden_operation,
           "auditor must reject register access outside a mapped lifecycle");
}

void testBuildOnlyTargetSourceContract()
{
    std::ifstream makefile("Makefile");
    expect(makefile.is_open(), "fake test must inspect the dependency Makefile");
    const std::string source(
        (std::istreambuf_iterator<char>(makefile)),
        std::istreambuf_iterator<char>());
    const std::string target =
        "gpio-startup-quiesce-qualification: build/bin/gpio_startup_quiesce_qualification\n"
        "\t$(Q)echo \"Built guarded live GPIO startup-quiesce qualification executable; it is not run by this target.\"";
    expect(source.find(target) != std::string::npos,
           "live qualification target must remain build-only");
    expect(source.find(
               "gpio-startup-quiesce-qualification: build/bin/gpio_startup_quiesce_qualification\n"
               "\t$(Q)./build/bin/gpio_startup_quiesce_qualification") ==
               std::string::npos,
           "ordinary target must never execute the live qualification binary");
}

void testFailureCleanup()
{
    using namespace gpio_startup_quiesce_qualification;
    auto observation_failure = std::make_shared<FakeAccess>();
    observation_failure->fail_read_at = 1;
    const Result observation = run(Options{4, 2}, observation_failure);
    expect(!observation.ok && observation.backend_calls == 0 &&
               observation_failure->unmaps == 1 &&
               observation_failure->closes == 1 && observation_failure->balanced(),
           "observation failure must clean up before any backend call");

    auto backend_failure = std::make_shared<FakeAccess>();
    backend_failure->fail_write_at = 1;
    const Result backend = run(Options{4, 2}, backend_failure);
    expect(!backend.ok && backend.backend_calls == 1 &&
               backend_failure->unmaps == 2 && backend_failure->closes == 2 &&
               backend_failure->balanced(),
           "backend failure must clean up observation and backend resources");
}
} // namespace

int main()
{
    testStrictCli();
    testSuccessfulQualification(4);
    testSuccessfulQualification(20);
    testInactiveDescriptorStateMayRemainVisible();
    testAuditRejections();
    testFailureCleanup();
    testBuildOnlyTargetSourceContract();
    expect(production_platform_hook_calls == 0,
           "fake qualification tests must never create or call production access");
    std::cout << "gpio_startup_quiesce_qualification_test passed\n";
    return 0;
}
