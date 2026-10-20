#include "si5351_inventory_process.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "descriptor") {
        errno = 0;
        return ::fcntl(std::stoi(argv[2]), F_GETFD) == -1 && errno == EBADF ? 0 : 5;
    }
    if (argc == 2) {
        const std::string mode(argv[1]);
        if (mode == "hang") { ::sleep(10); return 0; }
        if (mode == "slow") { ::usleep(300000); std::cout << "ready"; return 0; }
        if (mode == "large") { std::cout << std::string(5000, 'x'); return 0; }
        if (mode == "fail") return 4;
        if (mode == "success") { std::cout << "ready"; return 0; }
    }
    const std::string self = "/proc/self/exe";
    auto result = si5351_inventory_process::run({self, "success"});
    assert(result.error.empty() && result.output == "ready");
    const int inherited = ::open("/dev/null", O_RDONLY);
    assert(inherited >= 3);
    assert(si5351_inventory_process::run({self, "descriptor", std::to_string(inherited)}).error.empty());
    ::close(inherited);
    assert(!si5351_inventory_process::run({self, "large"}).error.empty());
    assert(!si5351_inventory_process::run({self, "fail"}).error.empty());
    assert(!si5351_inventory_process::run({"/no/such/inventory-worker"}).error.empty());
    const auto started = std::chrono::steady_clock::now();
    assert(si5351_inventory_process::run({self, "hang"}, 100ms).error.find("timed out") != std::string::npos);
    assert(std::chrono::steady_clock::now() - started < 500ms);
    // Allow a normally killable child to exit, then prove the next scan reaps/reuses the slot.
    std::this_thread::sleep_for(20ms);
    assert(si5351_inventory_process::run({self, "success"}).output == "ready");
    assert(!si5351_inventory_process::run({}).error.empty());
    ::close(STDIN_FILENO);
    assert(si5351_inventory_process::run({self, "success"}).output == "ready");
    std::thread worker([&] {
        assert(si5351_inventory_process::run({self, "slow"}).output == "ready");
    });
    std::this_thread::sleep_for(100ms);
    result = si5351_inventory_process::run({self, "success"});
    assert(result.error.find("already running") != std::string::npos);
    worker.join();
    assert(si5351_inventory_process::run({self, "success"}).output == "ready");
    std::cout << "Bounded Si5351 inventory subprocess tests passed\n";
}
