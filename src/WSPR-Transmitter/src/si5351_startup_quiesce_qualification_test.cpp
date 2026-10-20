#include "si5351_startup_quiesce_qualification.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

class FakeAdapter final : public Si5351Device::I2CAdapter
{
public:
    bool fail_open{false}; bool fail_quiesce_write{false}; bool preserve_register{false}; int opens{0}; int closes{0}; int quiesce_writes{0}; int fd{19}; std::uint8_t selected_register{0}; std::uint8_t register_three{0x00};
    int openDevice(const std::string& path, int) override { ++opens; if (path.rfind("/dev/i2c-", 0) != 0 || fail_open) { errno = ENOENT; return -1; } return fd; }
    int selectSlave(int supplied_fd, std::uint8_t address) override { if (supplied_fd != fd || address != 0x60) { errno = EPERM; return -1; } return 0; }
    ssize_t writeData(int supplied_fd, const void* data, std::size_t size) override { const auto* b = static_cast<const std::uint8_t*>(data); if (supplied_fd != fd || b == nullptr || b[0] != 3 || (size == 2 && b[1] != 0xff) || (size != 1 && size != 2)) { errno = EPERM; return -1; } if (size == 1) selected_register = b[0]; else { ++quiesce_writes; if (fail_quiesce_write) { errno = EIO; return -1; } if (!preserve_register) register_three = b[1]; } return static_cast<ssize_t>(size); }
    ssize_t readData(int supplied_fd, void* data, std::size_t size) override { if (supplied_fd != fd || selected_register != 3 || size != 1) { errno = EPERM; return -1; } *static_cast<std::uint8_t*>(data) = register_three; return 1; }
    int closeDevice(int supplied_fd) override { if (supplied_fd != fd) { errno = EPERM; return -1; } ++closes; return 0; }
};

si5351_startup_quiesce_qualification::Options options()
{
    return {"/dev/i2c-1", 1, 0x60};
}
}

int main()
{
    using namespace si5351_startup_quiesce_qualification;
    Options parsed; std::string error;
    char p0[] = "qualification"; char p1[] = "--device"; char p2[] = "/dev/i2c-1"; char p3[] = "--address"; char p4[] = "0x60"; char p5[] = "--count"; char p6[] = "2"; char p7[] = "--i-understand-this-accesses-live-si5351-hardware"; char* good[] = {p0,p1,p2,p3,p4,p5,p6,p7};
    require(parse_options(8, good, parsed, error), "guarded canonical arguments accepted");
    require(!parse_options(7, good, parsed, error), "missing acknowledgement refused");
    char bad_count[] = "1"; char* wrong_count[] = {p0,p1,p2,p3,p4,p5,bad_count,p7};
    require(!parse_options(8, wrong_count, parsed, error), "count other than two refused");
    char extra[] = "extra"; char* extra_args[] = {p0,p1,p2,p3,p4,p5,p6,p7,extra};
    require(!parse_options(9, extra_args, parsed, error), "extra argument refused");

    auto success_adapter = std::make_shared<FakeAdapter>();
    const Result success = run(options(), success_adapter);
    require(success.ok && success.first_quiesce_ok && success.second_quiesce_ok &&
                success.after_first == 0xff && success.after_second == 0xff &&
                success_adapter->quiesce_writes == 2,
            "permitted trace performs exactly two successful quiesce writes");
    require(success_adapter->closes == success_adapter->opens, "success closes every handle");
    for (const auto& operation : success.trace) require(operation.find("rejected") == std::string::npos, "permitted trace contains no rejected operation");

    auto bad_readback_adapter = std::make_shared<FakeAdapter>(); bad_readback_adapter->preserve_register = true;
    const Result bad_readback = run(options(), bad_readback_adapter);
    require(!bad_readback.ok, "non-FF readback fails qualification");
    require(bad_readback_adapter->closes == bad_readback_adapter->opens, "readback failure closes every handle");

    auto write_failure_adapter = std::make_shared<FakeAdapter>(); write_failure_adapter->fail_quiesce_write = true;
    const Result write_failure = run(options(), write_failure_adapter);
    require(!write_failure.ok && write_failure_adapter->closes == write_failure_adapter->opens,
            "quiesce-write failure closes every handle");

    auto open_failure_adapter = std::make_shared<FakeAdapter>(); open_failure_adapter->fail_open = true;
    const Result open_failure = run(options(), open_failure_adapter);
    require(!open_failure.ok && open_failure_adapter->closes == 0, "open failure does not leak a handle");
    std::cout << "si5351_startup_quiesce_qualification_test passed\n";
}
