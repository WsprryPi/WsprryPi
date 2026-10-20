#include "gpio_startup_quiesce_qualification.hpp"

#include "wspr_transmit.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

bool platform_supports_gpio_clock_transmission(std::string *error)
{
    std::ifstream model_file("/proc/device-tree/model", std::ios::binary);
    std::string model(
        (std::istreambuf_iterator<char>(model_file)),
        std::istreambuf_iterator<char>());
    if (!model.empty() && model.back() == '\0')
        model.pop_back();
    for (const char generation : {'1', '2', '3', '4'})
    {
        if (model.find(std::string("Raspberry Pi ") + generation) != std::string::npos)
            return true;
    }
    if (error != nullptr)
        *error = "GPIO startup qualification supports only confirmed Raspberry Pi 1 through 4; detected: " + model;
    return false;
}

void block_signals()
{
}

std::string WsprTransmitter::formatFrequencyMHz(double frequency_hz)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << frequency_hz / 1.0e6;
    return stream.str();
}

namespace
{
void printSnapshot(
    const char *label,
    const gpio_startup_quiesce_qualification::RegisterSnapshot &snapshot)
{
    std::cout << label
              << " dma_cs=0x" << std::hex << snapshot.dma_control_status
              << " dma_cb=0x" << snapshot.dma_control_block_address
              << " pwm_ctl=0x" << snapshot.pwm_control
              << " pwm_dmac=0x" << snapshot.pwm_dma_configuration
              << " gpclk0_ctl=0x" << snapshot.gpclk0_control
              << " gpfsel=0x" << snapshot.gpio_function_select
              << std::dec << '\n';
}
} // namespace

int main(int argc, char **argv)
{
    using namespace gpio_startup_quiesce_qualification;
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error))
    {
        std::cerr << error << '\n';
        return 2;
    }

    std::cout << "Live GPIO startup-quiesce qualification: gpio=" << options.gpio
              << " count=2 map-size=0x210000 permitted=typed-safe-registers-only\n";
    const Result result = run(options, makeProductionRpiStartupQuiesceAccess());
    printSnapshot("before", result.before);
    printSnapshot("after-first", result.after_first);
    printSnapshot("after-second", result.after_second);
    std::cout << "backend-calls=" << result.backend_calls << '\n';
    for (const auto &operation : result.audit.trace)
        std::cout << operation << '\n';
    if (!result.ok)
    {
        std::cerr << "Qualification failed: " << result.error << '\n';
        return 1;
    }
    std::cout << "Qualification passed.\n";
    return 0;
}
