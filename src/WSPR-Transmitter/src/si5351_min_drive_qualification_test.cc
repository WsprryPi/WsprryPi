#include "si5351_min_drive_qualification.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

class FakeAdapter final : public Si5351Device::I2CAdapter
{
public:
    int openDevice(const std::string&, int) override { ++opens; return 17; }
    int selectSlave(int fd, std::uint8_t address) override
    { return fd == 17 && address == 0x60 ? 0 : -1; }
    ssize_t writeData(int, const void* data, std::size_t size) override
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (size == 1) { selected = bytes[0]; return 1; }
        if (size != 2) { errno = EPERM; return -1; }
        ++write_attempts;
        if (fail_write != 0 && write_attempts == fail_write)
        { errno = EIO; return -1; }
        registers[bytes[0]] = bytes[1];
        writes.push_back({bytes[0], bytes[1]});
        return 2;
    }
    ssize_t readData(int, void* data, std::size_t size) override
    {
        if (size != 1) return -1;
        *static_cast<std::uint8_t*>(data) = registers[selected];
        return 1;
    }
    int closeDevice(int) override { ++closes; return 0; }
    std::array<std::uint8_t, 256> registers{{}};
    std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;
    std::uint8_t selected{0};
    std::size_t write_attempts{0};
    std::size_t fail_write{0};
    int opens{0};
    int closes{0};
};
}

int main()
{
    using namespace si5351_min_drive_qualification;
    std::string error;
    char p0[] = "qualification";
    char p1[] = "--i-understand-this-enables-one-2m-si5351-burst";
    char p2[] = "--tone-index";
    char p3[] = "3";
    char p4[] = "--calibration-ppm";
    char p5[] = "2.246308555";
    char* good[] = {p0, p1, p2, p3, p4, p5};
    Options options;
    require(parse_options(6, good, options, error), "guarded options accepted");
    require(options.tone_index == 3 && options.calibration_ppm == 2.246308555,
        "guarded options retained");
    require(frequency_hz(options) == base_frequency_hz + 3.0 * tone_spacing_hz,
        "tone index selects only an approved WSPR frequency");
    require(!parse_options(1, good, options, error), "missing options refused");

    auto success_adapter = std::make_shared<FakeAdapter>();
    success_adapter->registers[3] = 0xff;
    unsigned waits = 0;
    const Result success = run(success_adapter, [&waits](unsigned milliseconds)
    { ++waits; return milliseconds == duration_ms; }, options);
    require(success.ok && waits == 1 && success.after_inhibit == 0xff &&
            success.after_cleanup == 0xff && success.writes.size() == 19,
        "one bounded burst succeeds and cleans up");
    require(success_adapter->registers[3] == 0xff &&
            success_adapter->opens == success_adapter->closes,
        "success leaves outputs disabled and closes device");
    std::size_t enables = 0;
    for (const auto& write : success_adapter->writes)
        if (write.first == 3 && write.second == 0xfe) ++enables;
    require(enables == 1, "exactly one CLK0 enable is permitted");

    auto wait_failure_adapter = std::make_shared<FakeAdapter>();
    wait_failure_adapter->registers[3] = 0xff;
    const Result wait_failure = run(wait_failure_adapter, [](unsigned)
    { return false; });
    require(!wait_failure.ok && wait_failure_adapter->registers[3] == 0xff &&
            wait_failure_adapter->opens == wait_failure_adapter->closes,
        "interrupted wait fails closed");

    clear_stop();
    request_stop();
    require(!system_wait_ms(duration_ms),
        "signal-aware system wait stops immediately");
    clear_stop();

    auto write_failure_adapter = std::make_shared<FakeAdapter>();
    write_failure_adapter->registers[3] = 0xff;
    write_failure_adapter->fail_write = 7;
    const Result write_failure = run(write_failure_adapter, [](unsigned)
    { return true; });
    require(!write_failure.ok && write_failure_adapter->registers[3] == 0xff &&
            write_failure_adapter->opens == write_failure_adapter->closes,
        "programming failure fails closed");
    std::cout << "si5351_min_drive_qualification_test passed\n";
}
