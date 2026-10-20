#include "si5351_min_drive_qualification.hpp"

#include <iomanip>
#include <iostream>
#include <csignal>

namespace
{
void handle_stop(int) noexcept
{
    si5351_min_drive_qualification::request_stop();
}
}

int main(int argc, char** argv)
{
    using namespace si5351_min_drive_qualification;
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    clear_stop();
    std::signal(SIGINT, handle_stop);
    std::signal(SIGTERM, handle_stop);
    std::cout << std::fixed << std::setprecision(9)
              << "LIVE RF QUALIFICATION: /dev/i2c-1 address=0x60 CLK0"
              << " tone_index=" << options.tone_index
              << " requested_hz=" << frequency_hz(options)
              << " calibration_ppm=" << options.calibration_ppm
              << " drive=2mA duration_ms=" << duration_ms
              << " cycles=1\n";
    const Result result = run(make_system_adapter(), system_wait_ms, options);
    std::cout << "actual_hz=" << result.actual_hz
              << " register3-before=0x" << std::hex
              << static_cast<unsigned>(result.before)
              << " after-inhibit=0x"
              << static_cast<unsigned>(result.after_inhibit)
              << " after-cleanup=0x"
              << static_cast<unsigned>(result.after_cleanup)
              << std::dec << " planned-writes=" << result.writes.size()
              << '\n';
    if (!result.ok)
    {
        std::cerr << "Qualification failed: " << result.error << '\n';
        return 1;
    }
    std::cout << "Qualification passed.\n";
    return 0;
}
