// SPDX-License-Identifier: MIT
// Hardware-free launcher for the actual controller startup, HTTP, WebSocket,
// Fleet storage, and shutdown paths. Only identity acquisition is injected;
// production main still derives it from the machine ID.
#include "../controller_runtime.hpp"
#include "../wtp_endpoint/runtime.hpp"
#include "../config_handler.hpp"
#include "../scheduling.hpp"
#include "../version.hpp"
#include "../web_server.hpp"
#include <atomic>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping{};
void stop(int) { stopping = 1; }
}
int main(int argc, char *argv[]) {
    set_hardware_platform_detection_enabled(false);
    webServer.bind_loopback_for_test();
    try { configure_controller_only(argc, argv); }
    catch (const std::exception &) { return 2; }
    wsprrypi::testing::set_wtp_pi_private_state_path(config.ini_filename+".fixture-ledger");
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    std::atomic_bool done{false};
    std::thread monitor([&] {
        while (!done.load() && !stopping) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (stopping) request_wspr_shutdown("controller fixture shutdown");
    });
    const bool result = controller_only_loop("0123456789abcdef0123456789abcdef");
    done = true;
    monitor.join();
    return result ? 0 : 1;
}
