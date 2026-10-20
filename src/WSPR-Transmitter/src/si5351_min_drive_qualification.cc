#include "si5351_min_drive_qualification.hpp"

#include "si5351_planner.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace
{
constexpr const char* acknowledgement =
    "--i-understand-this-enables-one-2m-si5351-burst";
constexpr std::uint8_t output_enable_register = 3;
constexpr std::uint8_t disable_all = 0xff;
constexpr std::uint8_t enable_clk0 = 0xfe;
volatile std::sig_atomic_t stop_requested = 0;

class LinuxAdapter final : public Si5351Device::I2CAdapter
{
public:
    int openDevice(const std::string& path, int flags) override
    { return ::open(path.c_str(), flags); }
    int selectSlave(int fd, std::uint8_t address) override
    { return ::ioctl(fd, I2C_SLAVE, address); }
    ssize_t writeData(int fd, const void* data, std::size_t size) override
    { return ::write(fd, data, size); }
    ssize_t readData(int fd, void* data, std::size_t size) override
    { return ::read(fd, data, size); }
    int closeDevice(int fd) override { return ::close(fd); }
};

std::string error_or(const Si5351Device& device, const char* fallback)
{
    return device.getLastError().empty() ? fallback : device.getLastError();
}
}

namespace si5351_min_drive_qualification
{
double frequency_hz(const Options& options) noexcept
{
    return base_frequency_hz +
        static_cast<double>(options.tone_index) * tone_spacing_hz;
}

bool parse_options(int argc, char** argv, Options& options, std::string& error)
{
    if (argc != 6 || argv == nullptr)
    {
        error = std::string("Refusing RF output: require exactly ") +
            acknowledgement +
            " --tone-index 0..3 --calibration-ppm VALUE.";
        return false;
    }
    bool acknowledged = false;
    bool have_tone = false;
    bool have_ppm = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i] == nullptr ? "" : argv[i];
        if (argument == acknowledgement)
        {
            if (acknowledged) { error = "Duplicate acknowledgement."; return false; }
            acknowledged = true;
        }
        else if (argument == "--tone-index" && i + 1 < argc)
        {
            char* end = nullptr;
            const unsigned long value = std::strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || value > 3)
            { error = "Tone index must be one of 0, 1, 2, or 3."; return false; }
            options.tone_index = static_cast<unsigned>(value);
            have_tone = true;
        }
        else if (argument == "--calibration-ppm" && i + 1 < argc)
        {
            char* end = nullptr;
            const double value = std::strtod(argv[++i], &end);
            if (end == argv[i] || *end != '\0' || !std::isfinite(value) ||
                std::fabs(value) > 100.0)
            { error = "Calibration PPM must be finite and within +/-100."; return false; }
            options.calibration_ppm = value;
            have_ppm = true;
        }
        else
        {
            error = "Unknown or incomplete qualification argument.";
            return false;
        }
    }
    if (!acknowledged || !have_tone || !have_ppm)
    {
        error = "Acknowledgement, tone index, and calibration PPM are required.";
        return false;
    }
    return true;
}

Result run(
    std::shared_ptr<Si5351Device::I2CAdapter> adapter,
    const std::function<bool(unsigned)>& wait_ms,
    const Options& options)
{
    Result result;
    Si5351Planner::Config planner_config;
    planner_config.reference_hz = 27000000;
    planner_config.calibration_ppm = options.calibration_ppm;
    planner_config.tx_output = Si5351Device::Output::CLK0;
    const auto plan = Si5351Planner(planner_config).buildPlan(
        Si5351Planner::Mode::WSPR,
        {Si5351Planner::ToneEntry{frequency_hz(options)}});
    if (plan.tone_sets.size() != 1)
    {
        result.error = "Planner did not produce exactly one tone.";
        return result;
    }
    const auto& tone = plan.tone_sets.front();
    if (!tone.requires_output_inhibit ||
        !tone.pll_retune_candidate.valid ||
        tone.pll_retune_candidate.r_divider != 1 ||
        tone.pll_retune_candidate.multisynth.integer != 6 ||
        tone.writes.size() != 18 ||
        tone.writes[16].address != 16 ||
        (tone.writes[16].value & 0x03) != 0)
    {
        result.error = "Planner did not produce the reviewed 2 mA divide-by-6 plan.";
        return result;
    }
    result.actual_hz = tone.actual_hz;

    Si5351Device::Config device_config;
    device_config.i2c_bus = 1;
    device_config.i2c_address = 0x60;
    Si5351Device device(device_config, std::move(adapter));
    bool opened = device.open();
    auto cleanup = [&]()
    {
        if (!opened)
            return;
        (void)device.disableAllOutputs();
        (void)device.readRegister(output_enable_register, result.after_cleanup);
        device.close();
        opened = false;
    };
    if (!opened)
    {
        result.error = error_or(device, "Could not open Si5351 device.");
        return result;
    }
    if (!device.readRegister(output_enable_register, result.before) ||
        !device.disableAllOutputs() ||
        !device.readRegister(output_enable_register, result.after_inhibit) ||
        result.after_inhibit != disable_all)
    {
        result.error = error_or(device, "Could not establish output inhibit.");
        cleanup();
        return result;
    }
    for (const auto& write : tone.writes)
    {
        if (!device.writeRegister(write.address, write.value))
        {
            result.error = error_or(device, "Could not program reviewed tone plan.");
            cleanup();
            return result;
        }
        result.writes.push_back({write.address, write.value});
    }
    if (!device.writeRegister(output_enable_register, enable_clk0))
    {
        result.error = error_or(device, "Could not enable CLK0.");
        cleanup();
        return result;
    }
    result.writes.push_back({output_enable_register, enable_clk0});
    if (!wait_ms || !wait_ms(duration_ms))
    {
        result.error = "Bounded wait failed or was interrupted.";
        cleanup();
        return result;
    }
    cleanup();
    if (result.after_cleanup != disable_all)
    {
        result.error = "Cleanup readback was not register 3 = 0xFF.";
        return result;
    }
    result.ok = true;
    return result;
}

std::shared_ptr<Si5351Device::I2CAdapter> make_system_adapter()
{
    return std::make_shared<LinuxAdapter>();
}

bool system_wait_ms(unsigned milliseconds)
{
    constexpr unsigned slice_ms = 20;
    unsigned elapsed_ms = 0;
    while (elapsed_ms < milliseconds)
    {
        if (stop_requested != 0)
            return false;
        const unsigned remaining = milliseconds - elapsed_ms;
        const unsigned wait = remaining < slice_ms ? remaining : slice_ms;
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        elapsed_ms += wait;
    }
    return stop_requested == 0;
}

void request_stop() noexcept { stop_requested = 1; }
void clear_stop() noexcept { stop_requested = 0; }
} // namespace si5351_min_drive_qualification
