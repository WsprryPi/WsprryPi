// SPDX-License-Identifier: MIT
// Network-only fixture: an inhibited engine never operates transmitter hardware.
#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/plain_listener.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>

namespace {
bool address_only_baseline = false;
class TestClock final : public wsprrypico::wtp::Clock {
public:
    wsprrypico::wtp::ClockSnapshot snapshot() const override {
        using namespace std::chrono;
        return {wsprrypico::wtp::ClockState::Synchronized,
                static_cast<std::uint64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()),
                static_cast<std::uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count()),
                1000, 0, wsprrypico::wtp::LeapState::Normal, {}};
    }
};
class TestIdentity final : public wsprrypico::wtp::IdentitySource {
public:
    std::string new_boot_id() override { return std::string(32, 'b'); }
};
}

// Only this test executable can suppress device binding to reproduce the old
// implementation under exactly the same topology and protocol service.
extern "C" int __real_setsockopt(int, int, int, const void*, socklen_t);
extern "C" int __wrap_setsockopt(int fd, int level, int option,
                                const void* value, socklen_t size) {
    if (address_only_baseline && level == SOL_SOCKET && option == SO_BINDTODEVICE) return 0;
    return __real_setsockopt(fd, level, option, value, size);
}

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 2;
    if (argc == 5) {
        if (std::string(argv[4]) != "--baseline-address-only") return 2;
        address_only_baseline = true;
    }
    TestClock clock;
    TestIdentity identity;
    wsprrypico::wtp::InhibitedRfEngine engine;
    wsprrypico::wtp::JobService service(clock, engine, identity);
    wsprrypi::WtpPiAuthority authority(service, true);
    wsprrypi::WtpPlainListener listener(authority, std::string(32, 'c'), "routing-test");
    const auto port = std::stoul(argv[3]);
    if (port > 65535 || !listener.start(argv[1], static_cast<std::uint16_t>(port), argv[2])) {
        std::cerr << listener.error() << '\n';
        return 1;
    }
    std::cout << "READY " << listener.bound_port() << std::endl;
    std::string stop;
    std::getline(std::cin, stop);
    listener.stop();
}
