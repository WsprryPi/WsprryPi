#include "si5351_device.hpp"
#include "transmission_controller.hpp"
#include "wspr_transmit.hpp"
#include "wspr_transmit_backend_rpi.hpp"
#include "wspr_transmit_backend_si5351.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

int production_platform_support_checks = 0;

bool platform_supports_gpio_clock_transmission(std::string*)
{
    ++production_platform_support_checks;
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
    [[noreturn]] void fail(const std::string& message)
    {
        std::cerr << "startup_quiesce_test: " << message << '\n';
        std::exit(1);
    }

    void expect(bool condition, const std::string& message)
    {
        if (!condition)
            fail(message);
    }

    class TestBridge final : public IControllerBridge
    {
    public:
        WsprTransmitState backendStateValue() const noexcept override
        {
            return WsprTransmitState::ENABLED;
        }
        void backendSetStateValue(WsprTransmitState) noexcept override {}
        bool backendShouldStop() const noexcept override { return false; }
        void backendSignalStopRequest() noexcept override {}
        void backendRequestStopTxNoJoin() noexcept override {}
        bool backendWaitInterruptableFor(std::chrono::nanoseconds) override
        {
            return true;
        }
        void backendThrowIfStopRequested(const char*) override {}
        void backendReportExecutionProgress(std::size_t) noexcept override {}
        void backendFireTransmitCallback(
            WsprTransmissionCallbackEvent,
            WsprTransmitLogLevel,
            const std::string&,
            double) override {}
        bool backendRestartCurrentConfiguration() override { return false; }
    };

    class FakeI2CAdapter final : public Si5351Device::I2CAdapter
    {
    public:
        bool fail_open{false};
        bool fail_select{false};
        bool fail_write{false};
        int open_calls{0};
        int select_calls{0};
        int close_calls{0};
        int read_calls{0};
        std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;

        int openDevice(const std::string&, int) override
        {
            ++open_calls;
            if (fail_open)
            {
                errno = ENOENT;
                return -1;
            }
            return 42;
        }
        int selectSlave(int, std::uint8_t) override
        {
            ++select_calls;
            if (fail_select)
            {
                errno = EIO;
                return -1;
            }
            return 0;
        }
        ssize_t writeData(int, const void* data, std::size_t size) override
        {
            if (fail_write)
            {
                errno = EIO;
                return -1;
            }
            expect(size == 2, "quiesce must use a complete register write");
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            writes.emplace_back(bytes[0], bytes[1]);
            return static_cast<ssize_t>(size);
        }
        ssize_t readData(int, void*, std::size_t) override
        {
            ++read_calls;
            errno = EIO;
            return -1;
        }
        int closeDevice(int) override
        {
            ++close_calls;
            return 0;
        }
    };

    enum class FailurePoint
    {
        Support,
        Discovery,
        Open,
        Map,
        Dma,
        Pwm,
        Gpclk,
        Gpio,
        Unmap,
        Close
    };

    const char* register_name(RpiStartupQuiesceRegister reg)
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
        fail("unknown typed startup-quiesce register");
    }

    std::string hex32(std::uint32_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
        return stream.str();
    }

    FailurePoint register_group(RpiStartupQuiesceRegister reg)
    {
        switch (reg)
        {
        case RpiStartupQuiesceRegister::Dma0ControlStatus:
        case RpiStartupQuiesceRegister::Dma0ControlBlockAddress:
        case RpiStartupQuiesceRegister::Dma0TransferInformation:
        case RpiStartupQuiesceRegister::Dma0SourceAddress:
        case RpiStartupQuiesceRegister::Dma0DestinationAddress:
        case RpiStartupQuiesceRegister::Dma0TransferLength:
        case RpiStartupQuiesceRegister::Dma0Stride:
        case RpiStartupQuiesceRegister::Dma0NextControlBlock:
        case RpiStartupQuiesceRegister::Dma0Debug:
            return FailurePoint::Dma;
        case RpiStartupQuiesceRegister::PwmControl:
        case RpiStartupQuiesceRegister::PwmDmaConfiguration:
            return FailurePoint::Pwm;
        case RpiStartupQuiesceRegister::Gpclk0Control:
            return FailurePoint::Gpclk;
        case RpiStartupQuiesceRegister::GpioFunctionSelect0:
        case RpiStartupQuiesceRegister::GpioFunctionSelect2:
            return FailurePoint::Gpio;
        }
        fail("unknown typed startup-quiesce register group");
    }

    const char* failure_name(FailurePoint point)
    {
        switch (point)
        {
        case FailurePoint::Support: return "support";
        case FailurePoint::Discovery: return "discovery";
        case FailurePoint::Open: return "open";
        case FailurePoint::Map: return "map";
        case FailurePoint::Dma: return "DMA";
        case FailurePoint::Pwm: return "PWM";
        case FailurePoint::Gpclk: return "GPCLK";
        case FailurePoint::Gpio: return "GPIO";
        case FailurePoint::Unmap: return "unmap";
        case FailurePoint::Close: return "close";
        }
        fail("unknown startup-quiesce failure point");
    }

    class FakeStartupQuiesceAccess final : public IRpiStartupQuiesceAccess
    {
    public:
        static constexpr std::uint32_t peripheral_base = 0x3f000000;
        static constexpr std::size_t required_map_size = 0x210000;

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
        std::set<FailurePoint> failures;
        std::vector<std::string> trace;
        int open_calls{0};
        int successful_opens{0};
        int close_calls{0};
        int map_calls{0};
        int successful_maps{0};
        int unmap_calls{0};
        int forbidden_dma_starts{0};
        int forbidden_gpclk_enables{0};
        int forbidden_divisor_or_frequency_writes{0};
        int forbidden_allocations{0};
        int forbidden_waveforms{0};
        int forbidden_plan_operations{0};

        bool supportedPlatform(std::string &error) override
        {
            trace.emplace_back("support");
            return succeed_or_fail(FailurePoint::Support, error);
        }

        bool discoverPeripheralBase(
            std::uint32_t &base,
            std::string &error) override
        {
            trace.emplace_back("discover:" + hex32(peripheral_base));
            if (!valid(!opened_ && !mapped_, error, "discovery lifecycle"))
                return false;
            if (!succeed_or_fail(FailurePoint::Discovery, error))
                return false;
            base = peripheral_base;
            return true;
        }

        bool open(std::string &error) override
        {
            trace.emplace_back("open");
            ++open_calls;
            if (!valid(!opened_ && !mapped_, error, "open lifecycle"))
                return false;
            if (!succeed_or_fail(FailurePoint::Open, error))
                return false;
            opened_ = true;
            ++successful_opens;
            return true;
        }

        bool map(
            std::uint32_t base,
            std::size_t size,
            std::string &error) override
        {
            trace.emplace_back("map:" + hex32(static_cast<std::uint32_t>(size)));
            ++map_calls;
            if (!valid(opened_ && !mapped_ && base == peripheral_base &&
                           size == required_map_size,
                       error, "map lifecycle or bounds"))
                return false;
            if (!succeed_or_fail(FailurePoint::Map, error))
                return false;
            mapped_ = true;
            ++successful_maps;
            return true;
        }

        bool read(
            RpiStartupQuiesceRegister reg,
            std::uint32_t &value,
            std::string &error) override
        {
            if (!valid(opened_ && mapped_, error, "read lifecycle"))
                return false;
            const auto found = registers.find(reg);
            if (!valid(found != registers.end(), error, "unknown read register"))
                return false;
            trace.emplace_back(
                std::string("read:") + register_name(reg) + "=" + hex32(found->second));
            if (!succeed_or_fail(register_group(reg), error))
                return false;
            value = found->second;
            return true;
        }

        bool write(
            RpiStartupQuiesceRegister reg,
            std::uint32_t value,
            std::string &error) override
        {
            if (!valid(opened_ && mapped_, error, "write lifecycle"))
                return false;
            if (!valid(registers.contains(reg), error, "unknown write register"))
                return false;
            trace.emplace_back(
                std::string("write:") + register_name(reg) + "=" + hex32(value));
            if (reg == RpiStartupQuiesceRegister::Dma0ControlStatus &&
                (value & 1u) != 0)
            {
                ++forbidden_dma_starts;
                return invalid(error, "forbidden DMA start");
            }
            if (reg == RpiStartupQuiesceRegister::Gpclk0Control &&
                (value & (1u << 4)) != 0)
            {
                ++forbidden_gpclk_enables;
                return invalid(error, "forbidden GPCLK enable");
            }
            if (!succeed_or_fail(register_group(reg), error))
                return false;
            registers[reg] = value;
            return true;
        }

        bool unmap(std::size_t size, std::string &error) override
        {
            trace.emplace_back("unmap:" + hex32(static_cast<std::uint32_t>(size)));
            ++unmap_calls;
            if (!valid(opened_ && mapped_ && size == required_map_size,
                       error, "unmap lifecycle or bounds"))
                return false;
            mapped_ = false;
            return succeed_or_fail(FailurePoint::Unmap, error);
        }

        bool close(std::string &error) override
        {
            trace.emplace_back("close");
            ++close_calls;
            if (!valid(opened_ && !mapped_, error, "close lifecycle"))
                return false;
            opened_ = false;
            return succeed_or_fail(FailurePoint::Close, error);
        }

        bool lifecycle_balanced() const
        {
            return !opened_ && !mapped_ && successful_opens == close_calls &&
                   successful_maps == unmap_calls;
        }

        bool no_forbidden_operations() const
        {
            return forbidden_dma_starts == 0 && forbidden_gpclk_enables == 0 &&
                   forbidden_divisor_or_frequency_writes == 0 &&
                   forbidden_allocations == 0 && forbidden_waveforms == 0 &&
                   forbidden_plan_operations == 0;
        }

    private:
        bool succeed_or_fail(FailurePoint point, std::string &error)
        {
            if (!failures.contains(point))
                return true;
            return invalid(error, std::string("injected ") + failure_name(point) + " failure");
        }

        static bool valid(bool condition, std::string &error, const char *message)
        {
            return condition ? true : invalid(error, message);
        }

        static bool invalid(std::string &error, const std::string &message)
        {
            if (error.empty())
                error = message;
            return false;
        }

        bool opened_{false};
        bool mapped_{false};
    };

    class NoopCompiler final : public wsprrypi::IExecutionPlanCompiler
    {
    public:
        wsprrypi::ExecutionPlan compile(
            const wsprrypi::TransmissionRequest&) const override
        {
            fail("startup quiesce must not compile an execution plan");
        }
    };

    class DispatchBackend final : public wsprrypi::ITransmissionBackend
    {
    public:
        int quiesce_calls{0};
        wsprrypi::BackendInfo info() const override { return {}; }
        wsprrypi::BackendCapabilities capabilities() const override { return {}; }
        wsprrypi::BackendCompileResult configure(
            const wsprrypi::ExecutionPlan&,
            const wsprrypi::BackendExecutionInputs&) override { return {}; }
        wsprrypi::ExecutionResult execute(
            const wsprrypi::ExecutionPlan&) override { return {}; }
        wsprrypi::StartupQuiesceResult quiesceForStartup() override
        {
            ++quiesce_calls;
            return {true, {}};
        }
        void stop() noexcept override {}
        wsprrypi::CleanupResult cleanup() noexcept override { return {true, {}}; }
    };

    WsprSi5351Backend::Config make_config(
        const std::shared_ptr<FakeI2CAdapter>& adapter,
        bool dry_run)
    {
        WsprSi5351Backend::Config config;
        config.device.i2c_bus = 7;
        config.device.i2c_address = 0x60;
        config.device_adapter = adapter;
        config.dry_run = dry_run;
        return config;
    }

    void test_controller_dispatch()
    {
        NoopCompiler compiler;
        DispatchBackend backend;
        wsprrypi::TransmissionController controller(compiler, backend);
        const auto result = controller.quiesceForStartup();
        expect(result.ok, "controller quiesce dispatch must succeed");
        expect(backend.quiesce_calls == 1,
               "controller must dispatch startup quiesce to the backend");
    }

    void test_si5351_quiesce_success_and_repeat()
    {
        TestBridge bridge;
        auto adapter = std::make_shared<FakeI2CAdapter>();
        WsprSi5351Backend backend(bridge, make_config(adapter, false));

        expect(backend.quiesceForStartup().ok,
               "first Si5351 startup quiesce must succeed");
        expect(backend.quiesceForStartup().ok,
               "repeated Si5351 startup quiesce must succeed");
        expect(adapter->open_calls == 2 && adapter->select_calls == 2,
               "each startup quiesce must open and select the configured I2C device");
        expect(adapter->close_calls == 2,
               "each successful startup quiesce must close the I2C device");
        expect(adapter->read_calls == 0,
               "startup quiesce must not read or program transmission state");
        expect(adapter->writes.size() == 2,
               "startup quiesce must perform exactly one write per call");
        for (const auto& write : adapter->writes)
        {
            expect(write.first == 3 && write.second == 0xff,
                   "startup quiesce must write register 3 = 0xFF only");
        }
    }

    void test_si5351_reference_source_initialization()
    {
        for (const auto& expected :
             {std::pair<int, std::uint8_t>{6, 0x52},
              std::pair<int, std::uint8_t>{8, 0x92},
              std::pair<int, std::uint8_t>{10, 0xD2}})
        {
            auto adapter = std::make_shared<FakeI2CAdapter>();
            Si5351Device::Config config;
            config.reference_source = Si5351Device::ReferenceSource::CRYSTAL;
            config.crystal_load_capacitance_pf = expected.first;
            Si5351Device device(config, adapter);
            expect(device.open(), "crystal-mode fake I2C open must succeed");
            expect(device.initialize(), "valid crystal capacitance must initialize");
            expect(adapter->writes.size() == 2,
                   "crystal initialization must disable outputs then write XTAL_CL");
            expect(adapter->writes[0] == std::make_pair<std::uint8_t, std::uint8_t>(3, 0xff),
                   "crystal initialization must disable all outputs first");
            expect(adapter->writes[1] ==
                       std::pair<std::uint8_t, std::uint8_t>{183, expected.second},
                   "crystal initialization must preserve Register 183 reserved bits");
        }

        auto external_adapter = std::make_shared<FakeI2CAdapter>();
        Si5351Device external_device(Si5351Device::Config{}, external_adapter);
        expect(external_device.open(), "external-TCXO fake I2C open must succeed");
        expect(external_device.initialize(), "external-TCXO initialization must succeed");
        expect(external_adapter->writes.size() == 1 &&
                   external_adapter->writes.front() ==
                       std::make_pair<std::uint8_t, std::uint8_t>(3, 0xff),
               "external-TCXO initialization must not write Register 183");

        for (const int invalid : {0, 7, 9, 12})
        {
            auto adapter = std::make_shared<FakeI2CAdapter>();
            Si5351Device::Config config;
            config.reference_source = Si5351Device::ReferenceSource::CRYSTAL;
            config.crystal_load_capacitance_pf = invalid;
            Si5351Device device(config, adapter);
            expect(device.open(), "invalid crystal test fake I2C open must succeed");
            expect(!device.initialize(), "invalid crystal capacitance must be rejected");
            expect(adapter->writes.empty(),
                   "invalid crystal capacitance must be rejected before any register write");
        }
    }

    void test_si5351_quiesce_failures_close_handles()
    {
        TestBridge bridge;
        auto write_failure = std::make_shared<FakeI2CAdapter>();
        write_failure->fail_write = true;
        WsprSi5351Backend write_backend(
            bridge, make_config(write_failure, false));
        const auto write_result = write_backend.quiesceForStartup();
        expect(!write_result.ok && !write_result.error.empty(),
               "a register-write failure must report failed quiescence");
        expect(write_failure->close_calls == 1,
               "a register-write failure must close the I2C device");

        auto select_failure = std::make_shared<FakeI2CAdapter>();
        select_failure->fail_select = true;
        WsprSi5351Backend select_backend(
            bridge, make_config(select_failure, false));
        const auto select_result = select_backend.quiesceForStartup();
        expect(!select_result.ok && !select_result.error.empty(),
               "an I2C setup failure must report failed quiescence");
        expect(select_failure->close_calls == 1,
               "an I2C setup failure must close the opened device handle");

        auto open_failure = std::make_shared<FakeI2CAdapter>();
        open_failure->fail_open = true;
        WsprSi5351Backend open_backend(
            bridge, make_config(open_failure, false));
        const auto open_result = open_backend.quiesceForStartup();
        expect(!open_result.ok && !open_result.error.empty(),
               "an I2C open failure must report failed quiescence");
        expect(open_failure->close_calls == 0,
               "an I2C open failure has no device handle to close");
    }

    void test_si5351_dry_run_avoids_i2c()
    {
        TestBridge bridge;
        auto adapter = std::make_shared<FakeI2CAdapter>();
        WsprSi5351Backend backend(bridge, make_config(adapter, true));

        expect(backend.quiesceForStartup().ok,
               "Si5351 dry-run startup quiesce must succeed");
        expect(adapter->open_calls == 0 && adapter->select_calls == 0 &&
                   adapter->close_calls == 0 && adapter->read_calls == 0 &&
                   adapter->writes.empty(),
               "Si5351 dry-run startup quiesce must not access I2C");
    }

    std::vector<std::string> successful_gpio_trace(
        const char *gpio_register,
        std::uint32_t original_gpio,
        std::uint32_t safe_gpio)
    {
        return {
            "support",
            "discover:0x3f000000",
            "open",
            "map:0x00210000",
            "write:DMA0_CS=0xc0000000",
            "write:DMA0_CS=0x80000000",
            "write:DMA0_CONBLK_AD=0x00000000",
            "write:DMA0_TI=0x00000000",
            "write:DMA0_SOURCE_AD=0x00000000",
            "write:DMA0_DEST_AD=0x00000000",
            "write:DMA0_TXFR_LEN=0x00000000",
            "write:DMA0_STRIDE=0x00000000",
            "write:DMA0_NEXTCONBK=0x00000000",
            "write:DMA0_DEBUG=0x00000007",
            "write:PWM_CTL=0x00000000",
            "write:PWM_DMAC=0x00000000",
            "read:GPCLK0_CTL=0x00000210",
            "write:GPCLK0_CTL=0x5a000200",
            "read:GPCLK0_CTL=0x5a000200",
            std::string("read:") + gpio_register + "=" + hex32(original_gpio),
            std::string("write:") + gpio_register + "=" + hex32(safe_gpio),
            "unmap:0x00210000",
            "close"};
    }

    void expect_safe_register_state(
        const std::shared_ptr<FakeStartupQuiesceAccess>& access,
        RpiStartupQuiesceRegister gpio_register,
        std::uint32_t expected_gpio,
        const std::string& context)
    {
        expect((access->registers[RpiStartupQuiesceRegister::Dma0ControlStatus] & 1u) == 0,
               context + ": DMA channel 0 must not be active");
        for (const auto reg : {
                 RpiStartupQuiesceRegister::Dma0ControlBlockAddress,
                 RpiStartupQuiesceRegister::Dma0TransferInformation,
                 RpiStartupQuiesceRegister::Dma0SourceAddress,
                 RpiStartupQuiesceRegister::Dma0DestinationAddress,
                 RpiStartupQuiesceRegister::Dma0TransferLength,
                 RpiStartupQuiesceRegister::Dma0Stride,
                 RpiStartupQuiesceRegister::Dma0NextControlBlock})
        {
            expect(access->registers[reg] == 0,
                   context + ": DMA channel 0 state must be cleared");
        }
        expect(access->registers[RpiStartupQuiesceRegister::Dma0Debug] == 7,
               context + ": DMA channel 0 debug errors must be cleared");
        expect(access->registers[RpiStartupQuiesceRegister::PwmControl] == 0 &&
                   access->registers[RpiStartupQuiesceRegister::PwmDmaConfiguration] == 0,
               context + ": PWM and PWM DMA must be disabled");
        expect((access->registers[RpiStartupQuiesceRegister::Gpclk0Control] &
                    ((1u << 4) | (1u << 7))) == 0,
               context + ": GPCLK0 must be disabled and not busy");
        expect(access->registers[gpio_register] == expected_gpio,
               context + ": only the selected GPIO mux field may be cleared");
        expect(access->lifecycle_balanced(),
               context + ": map/open lifecycle must be balanced");
        expect(access->no_forbidden_operations(),
               context + ": no forbidden startup operation may occur");
    }

    void test_gpio_ppm_sign_and_bounds()
    {
        constexpr double nominal_hz = 500000000.0;
        constexpr double requested_rf_hz = 28126100.0;
        constexpr double ppm = 100.0;
        constexpr double physical_fast_plld_hz =
            nominal_hz * (1.0 + ppm * 1.0e-6);

        const double zero_hz = gpioCorrectedPlldFrequency(nominal_hz, 0.0);
        const double positive_hz = gpioCorrectedPlldFrequency(nominal_hz, ppm);
        const double negative_hz = gpioCorrectedPlldFrequency(nominal_hz, -ppm);
        expect(zero_hz == nominal_hz,
               "zero GPIO source-rate PPM must leave PLLD unchanged");
        expect(positive_hz > zero_hz && negative_hz < zero_hz,
               "positive GPIO source-rate PPM must raise assumed PLLD and negative must lower it");

        const double zero_divisor = zero_hz / requested_rf_hz;
        const double positive_divisor = positive_hz / requested_rf_hz;
        const double negative_divisor = negative_hz / requested_rf_hz;
        const double zero_actual_rf_hz = physical_fast_plld_hz / zero_divisor;
        const double positive_actual_rf_hz = physical_fast_plld_hz / positive_divisor;
        const double negative_actual_rf_hz = physical_fast_plld_hz / negative_divisor;
        expect(positive_actual_rf_hz < zero_actual_rf_hz &&
                   negative_actual_rf_hz > zero_actual_rf_hz,
               "positive GPIO source-rate PPM must move fast-clock RF lower and negative must move it higher");
        expect(std::fabs(positive_actual_rf_hz - requested_rf_hz) < 1.0e-6,
               "matching positive GPIO source-rate PPM must correct the modeled 10 m RF frequency");

        expect(gpioCorrectedPlldFrequency(nominal_hz, 200.0) > nominal_hz &&
                   gpioCorrectedPlldFrequency(nominal_hz, -200.0) < nominal_hz,
               "GPIO source-rate PPM bounds must remain valid");

        for (const double invalid_ppm : {
                 200.000001,
                 -200.000001,
                 std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN()})
        {
            bool rejected = false;
            try
            {
                (void)gpioCorrectedPlldFrequency(nominal_hz, invalid_ppm);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            expect(rejected,
                   "invalid GPIO source-rate PPM must fail closed");
        }
    }

    void test_gpio_rf_clock_planning_and_divider_bounds()
    {
        for (const double ppm : {-200.0, 0.0, 2.5, 200.0})
        {
            for (const auto profile : {wsprrypi::LegacyGpioProcessorProfile::Bcm2835,
                                       wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837,
                                       wsprrypi::LegacyGpioProcessorProfile::Bcm2711})
            {
                const auto plan = gpioPlanRfClock(profile, 14097100.0, 14097100.0, ppm);
                const double nominal = profile == wsprrypi::LegacyGpioProcessorProfile::Bcm2711
                    ? 750e6 : 500e6;
                const double intrinsic = profile == wsprrypi::LegacyGpioProcessorProfile::Bcm2835
                    ? -2.5 : 0.0;
                expect(plan.nominal_hz == nominal &&
                           std::fabs(plan.corrected_hz -
                               nominal * (1.0 + (ppm + intrinsic) / 1e6)) < 1e-6,
                       "RF planning must add the chipset offset exactly once at caller PPM bounds");
                expect(gpioCorrectedPlldFrequency(nominal, ppm) ==
                           nominal * (1.0 + ppm * 1e-6),
                       "RF intrinsic offset must not change PWM timing correction");
            }
        }
        for (const double invalid_ppm : {-200.000001, 200.000001,
                 std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::quiet_NaN()})
        {
            bool rejected = false;
            try
            {
                (void)gpioPlanRfClock(wsprrypi::LegacyGpioProcessorProfile::Bcm2835,
                                     14097100.0, 14097100.0, invalid_ppm);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            expect(rejected, "intrinsic offset must not admit invalid caller PPM");
        }
        bool unknown_rejected = false;
        try
        {
            (void)gpioPlanRfClock(static_cast<wsprrypi::LegacyGpioProcessorProfile>(999),
                                 14097100.0, 14097100.0, 0.0);
        }
        catch (const std::invalid_argument&)
        {
            unknown_rejected = true;
        }
        expect(unknown_rejected, "unknown GPIO clock profile must fail closed");
        constexpr double spacing_hz = 12000.0 / 8192.0;
        const auto tone_range = [spacing_hz](double center_hz) {
            return std::pair<double, double>{
                center_hz - 1.5 * spacing_hz,
                center_hz + 1.5 * spacing_hz};
        };

        const auto pi4_2200m_range = tone_range(137500.0);
        const auto pi4_2200m = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2711,
            pi4_2200m_range.first,
            pi4_2200m_range.second,
            0.0);
        expect(pi4_2200m.source == GpioRfClockSource::Oscillator &&
                   pi4_2200m.nominal_hz == 54e6 &&
                   pi4_2200m.corrected_hz == 54e6,
               "Pi 4 2200 m must select the representable 54 MHz oscillator source");

        const auto pi4_630m_range = tone_range(475700.0);
        const auto pi4_630m = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2711,
            pi4_630m_range.first,
            pi4_630m_range.second,
            0.0);
        expect(pi4_630m.source == GpioRfClockSource::PllD &&
                   pi4_630m.nominal_hz == 750e6,
               "Pi 4 630 m must retain the preferred 750 MHz PLLD source");

        const auto legacy_2200m = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837,
            pi4_2200m_range.first,
            pi4_2200m_range.second,
            0.0);
        expect(legacy_2200m.source == GpioRfClockSource::PllD &&
                   legacy_2200m.nominal_hz == 500e6,
               "500 MHz processors must retain PLLD for representable 2200 m output");

        const auto bcm2835 = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2835,
            14097100.0,
            14097100.0,
            1.0);
        expect(
            bcm2835.parent == wsprrypi::LegacyGpioClockParent::PllD &&
                bcm2835.nominal_hz == 500e6 &&
                bcm2835.intrinsic_ppm == -2.5 &&
                bcm2835.additional_ppm == 1.0 &&
                bcm2835.effective_ppm == -1.5 &&
                bcm2835.corrected_hz == 500e6 * (1.0 - 1.5e-6),
            "BCM2835 planning must apply its intrinsic correction exactly once");

        expect(
            gpioHardwareProfileMatchesProcessor(
                wsprrypi::HardwareProfile::BCM2835,
                wsprrypi::LegacyGpioProcessorProfile::Bcm2835) &&
                gpioHardwareProfileMatchesProcessor(
                    wsprrypi::HardwareProfile::BCM2836_BCM2837,
                    wsprrypi::LegacyGpioProcessorProfile::Bcm2836Bcm2837) &&
                gpioHardwareProfileMatchesProcessor(
                    wsprrypi::HardwareProfile::BCM2711,
                    wsprrypi::LegacyGpioProcessorProfile::Bcm2711) &&
                !gpioHardwareProfileMatchesProcessor(
                    wsprrypi::HardwareProfile::BCM2835,
                    wsprrypi::LegacyGpioProcessorProfile::Bcm2711) &&
                !gpioHardwareProfileMatchesProcessor(
                    wsprrypi::HardwareProfile::UNSPECIFIED,
                    wsprrypi::LegacyGpioProcessorProfile::Bcm2835),
            "backend profile agreement must accept exact identity and reject contradictions");

        constexpr double pi4_plld_boundary_hz =
            750e6 / (static_cast<double>(0x00FFFFFFu) / 4096.0);
        const auto below_boundary = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2711,
            pi4_plld_boundary_hz - 100.0,
            pi4_plld_boundary_hz - 100.0,
            0.0);
        const auto above_boundary = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2711,
            pi4_plld_boundary_hz + 100.0,
            pi4_plld_boundary_hz + 100.0,
            0.0);
        expect(below_boundary.source == GpioRfClockSource::Oscillator &&
                   above_boundary.source == GpioRfClockSource::PllD,
               "Pi 4 source selection must switch safely across the PLLD divisor boundary");

        const auto ppm_plan = gpioPlanRfClock(
            wsprrypi::LegacyGpioProcessorProfile::Bcm2711,
            pi4_2200m_range.first,
            pi4_2200m_range.second,
            100.0);
        expect(ppm_plan.source == GpioRfClockSource::Oscillator &&
                   ppm_plan.corrected_hz == 54e6 * 1.0001,
               "GPIO PPM correction must apply to the selected oscillator source");

        const auto lower_word = gpioBuildDividerWord(54e6, 137500.0, false);
        const auto upper_word = gpioBuildDividerWord(54e6, 137500.0, true);
        expect(lower_word <= 0x00FFFFFFu && upper_word == lower_word + 1,
               "representable oscillator tuning words must remain inside the 24-bit field");

        for (const auto& invalid : {
                 std::tuple<double, double, bool>{750e6, 137500.0, false},
                 std::tuple<double, double, bool>{500e6, 100000.0, false},
                 std::tuple<double, double, bool>{500e6, 120000000.0, false}})
        {
            bool rejected = false;
            try
            {
                (void)gpioBuildDividerWord(
                    std::get<0>(invalid),
                    std::get<1>(invalid),
                    std::get<2>(invalid));
            }
            catch (const std::out_of_range&)
            {
                rejected = true;
            }
            expect(rejected,
                   "unrepresentable GPIO divider words must fail closed");
        }
    }

    void test_gpio_continuous_tone_fractional_dither()
    {
        for (const double ratio : {0.0, 0.125, 0.5, 0.876543, 1.0})
        {
            std::int64_t clocks = 0;
            std::int64_t lower_clocks = 0;
            for (int block = 0; block < 1000; ++block)
            {
                const auto selected = gpioDitherLowerClockCount(
                    ratio, 1000, clocks, lower_clocks);
                expect(selected >= 0 && selected <= 1000,
                       "continuous-tone dither must stay inside each DMA block");
                clocks += 1000;
                lower_clocks += selected;
                expect(std::abs(
                           static_cast<double>(lower_clocks) - ratio * clocks) <=
                           0.5,
                       "continuous-tone dither must track the requested cumulative ratio");
            }
        }

        bool rejected = false;
        try
        {
            (void)gpioDitherLowerClockCount(
                std::numeric_limits<double>::quiet_NaN(), 1000, 0, 0);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        expect(rejected, "continuous-tone dither must reject invalid ratios");
    }

    void test_rpi_gpio4_exact_safe_trace()
    {
        TestBridge bridge;
        auto access = std::make_shared<FakeStartupQuiesceAccess>();
        const std::uint32_t original =
            access->registers[RpiStartupQuiesceRegister::GpioFunctionSelect0];
        const std::uint32_t expected = original & ~(std::uint32_t{0x7} << 12);
        WsprRpiBackend backend(bridge, access, 4);

        const auto result = backend.quiesceForStartup();
        expect(result.ok && result.error.empty(),
               "GPIO 4 fake-MMIO startup quiesce must succeed");
        expect(access->trace == successful_gpio_trace("GPFSEL0", original, expected),
               "GPIO 4 startup quiesce must have the exact safe operation trace");
        expect_safe_register_state(
            access,
            RpiStartupQuiesceRegister::GpioFunctionSelect0,
            expected,
            "GPIO 4");
    }

    void test_rpi_gpio20_exact_safe_trace()
    {
        TestBridge bridge;
        auto access = std::make_shared<FakeStartupQuiesceAccess>();
        const std::uint32_t original =
            access->registers[RpiStartupQuiesceRegister::GpioFunctionSelect2];
        const std::uint32_t expected = original & ~std::uint32_t{0x7};
        WsprRpiBackend backend(bridge, access, 20);

        const auto result = backend.quiesceForStartup();
        expect(result.ok && result.error.empty(),
               "GPIO 20 fake-MMIO startup quiesce must succeed");
        expect(access->trace == successful_gpio_trace("GPFSEL2", original, expected),
               "GPIO 20 startup quiesce must have the exact safe operation trace");
        expect_safe_register_state(
            access,
            RpiStartupQuiesceRegister::GpioFunctionSelect2,
            expected,
            "GPIO 20");
    }

    void test_rpi_repeated_call_safety()
    {
        TestBridge bridge;
        auto access = std::make_shared<FakeStartupQuiesceAccess>();
        WsprRpiBackend backend(bridge, access, 4);
        expect(backend.quiesceForStartup().ok,
               "first fake-MMIO startup quiesce must succeed");
        expect(backend.quiesceForStartup().ok,
               "repeated fake-MMIO startup quiesce must succeed");
        expect(access->successful_opens == 2 && access->successful_maps == 2 &&
                   access->close_calls == 2 && access->unmap_calls == 2,
               "repeated startup quiesce must balance each access lifecycle");
        expect(access->no_forbidden_operations(),
               "repeated startup quiesce must remain limited to safe operations");
    }

    void test_rpi_platform_and_gpio_rejection_precede_access()
    {
        TestBridge bridge;
        auto unsupported = std::make_shared<FakeStartupQuiesceAccess>();
        unsupported->failures.insert(FailurePoint::Support);
        WsprRpiBackend unsupported_backend(bridge, unsupported, 4);
        const auto unsupported_result = unsupported_backend.quiesceForStartup();
        expect(!unsupported_result.ok &&
                   unsupported_result.error == "injected support failure",
               "unsupported platforms must return their useful error");
        expect(unsupported->trace == std::vector<std::string>{"support"} &&
                   unsupported->open_calls == 0 && unsupported->map_calls == 0,
               "unsupported platforms must fail before discovery, open, or map");

        auto bad_gpio = std::make_shared<FakeStartupQuiesceAccess>();
        WsprRpiBackend bad_gpio_backend(bridge, bad_gpio, 21);
        const auto bad_gpio_result = bad_gpio_backend.quiesceForStartup();
        expect(!bad_gpio_result.ok &&
                   bad_gpio_result.error ==
                       "Unsupported GPIO backend transmit pin for startup quiesce.",
               "unsupported GPIO must fail closed");
        expect(bad_gpio->trace == std::vector<std::string>{"support"} &&
                   bad_gpio->open_calls == 0 && bad_gpio->map_calls == 0,
               "unsupported GPIO must fail before peripheral access");
    }

    void test_rpi_failure_injection_and_cleanup()
    {
        TestBridge bridge;
        for (const FailurePoint point : {
                 FailurePoint::Discovery,
                 FailurePoint::Open,
                 FailurePoint::Map,
                 FailurePoint::Dma,
                 FailurePoint::Pwm,
                 FailurePoint::Gpclk,
                 FailurePoint::Gpio,
                 FailurePoint::Unmap,
                 FailurePoint::Close})
        {
            auto access = std::make_shared<FakeStartupQuiesceAccess>();
            access->failures.insert(point);
            WsprRpiBackend backend(bridge, access, 4);
            const auto result = backend.quiesceForStartup();
            const std::string expected_error =
                std::string("injected ") + failure_name(point) + " failure";
            expect(!result.ok, expected_error + " must never report success");
            expect(result.error == expected_error,
                   expected_error + " must remain the reported error");
            expect(access->lifecycle_balanced(),
                   expected_error + " must balance every acquired resource");
            expect(access->no_forbidden_operations(),
                   expected_error + " must not trigger forbidden operations");
        }

        auto multiple = std::make_shared<FakeStartupQuiesceAccess>();
        multiple->failures = {
            FailurePoint::Dma, FailurePoint::Unmap, FailurePoint::Close};
        WsprRpiBackend multiple_backend(bridge, multiple, 4);
        const auto multiple_result = multiple_backend.quiesceForStartup();
        expect(!multiple_result.ok &&
                   multiple_result.error == "injected DMA failure",
               "the first useful failure must survive unmap and close failures");
        expect(multiple->unmap_calls == 1 && multiple->close_calls == 1,
               "cleanup must attempt both unmap and close after an earlier failure");
    }

    void test_scheduled_gpio_capability_and_cleanup()
    {
        TestBridge bridge;
        auto access = std::make_shared<FakeStartupQuiesceAccess>();
        WsprRpiBackend backend(bridge, access, 4,
            wsprrypi::LegacyGpioProcessorProfile::Bcm2711);
        const auto caps = backend.capabilities();
        expect(wsprrypi::supports_mode(caps, wsprrypi::TransmissionMode::TONE) &&
                   caps.max_frequency_hz == 150000000.0 && caps.min_frequency_hz < 14000,
               "scheduled GPIO CAPS must use the selected native processor clock model");
        wsprrypi::ScheduledExecutionHooks hooks;
        hooks.confirm_output_disable = true;
        expect(backend.setScheduledExecutionHooks(hooks), "GPIO accepts typed scheduled hooks");
        expect(backend.cleanup().ok && access->lifecycle_balanced(),
               "scheduled cleanup must confirm output-off through fresh quiescence");
        access->failures.insert(FailurePoint::Gpclk);
        expect(!backend.cleanup().ok && access->lifecycle_balanced(),
               "scheduled cleanup must propagate failed GPCLK output-off confirmation");
    }

    void test_fake_rejects_invalid_lifecycle_and_bounds()
    {
        FakeStartupQuiesceAccess access;
        std::string error;
        std::uint32_t value = 0;
        expect(!access.read(
                   RpiStartupQuiesceRegister::Dma0ControlStatus, value, error),
               "fake access must reject register reads before map");

        FakeStartupQuiesceAccess wrong_size;
        error.clear();
        std::uint32_t base = 0;
        expect(wrong_size.supportedPlatform(error) &&
                   wrong_size.discoverPeripheralBase(base, error) &&
                   wrong_size.open(error) &&
                   !wrong_size.map(base, 0x1000, error),
               "fake access must reject any map size other than 0x210000");
        expect(wrong_size.close(error),
               "fake access must allow cleanup after rejecting an invalid map");
    }
}

int main()
{
    test_controller_dispatch();
    test_si5351_quiesce_success_and_repeat();
    test_si5351_reference_source_initialization();
    test_si5351_quiesce_failures_close_handles();
    test_si5351_dry_run_avoids_i2c();
    test_gpio_ppm_sign_and_bounds();
    test_gpio_rf_clock_planning_and_divider_bounds();
    test_gpio_continuous_tone_fractional_dither();
    test_rpi_gpio4_exact_safe_trace();
    test_rpi_gpio20_exact_safe_trace();
    test_rpi_repeated_call_safety();
    test_rpi_platform_and_gpio_rejection_precede_access();
    test_rpi_failure_injection_and_cleanup();
    test_scheduled_gpio_capability_and_cleanup();
    test_fake_rejects_invalid_lifecycle_and_bounds();
    expect(production_platform_support_checks == 0,
           "GPIO startup-quiesce tests must never reach the production adapter");
    std::cout << "startup_quiesce_test: passed\n";
    return 0;
}
