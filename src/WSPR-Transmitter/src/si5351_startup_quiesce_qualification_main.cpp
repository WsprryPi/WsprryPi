#include "si5351_startup_quiesce_qualification.hpp"

#include <iostream>

int main(int argc, char** argv)
{
    using namespace si5351_startup_quiesce_qualification;
    Options options; std::string error;
    if (!parse_options(argc, argv, options, error)) { std::cerr << error << '\n'; return 2; }
    std::cout << "Live Si5351 startup-quiesce qualification: device=" << options.device_path
              << " bus=" << options.bus << " address=0x" << std::hex << static_cast<unsigned>(options.address)
              << std::dec << " count=2 permitted-operation=register-3-write-0xFF-only\n";
    const Result result = run(options, make_system_adapter());
    std::cout << "register3-before=0x" << std::hex << static_cast<unsigned>(result.before)
              << " register3-after-first=0x" << static_cast<unsigned>(result.after_first)
              << " register3-after-second=0x" << static_cast<unsigned>(result.after_second)
              << std::dec << '\n';
    std::cout << "first-quiesce=" << (result.first_quiesce_ok ? "success" : "failure")
              << " error=" << result.first_quiesce_error << '\n';
    std::cout << "second-quiesce=" << (result.second_quiesce_ok ? "success" : "failure")
              << " error=" << result.second_quiesce_error << '\n';
    for (const auto& operation : result.trace) std::cout << operation << '\n';
    if (!result.ok) { std::cerr << "Qualification failed: " << result.error << '\n'; return 1; }
    std::cout << "Qualification passed.\n"; return 0;
}
