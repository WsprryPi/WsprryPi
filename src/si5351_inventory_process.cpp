#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "si5351_inventory_process.hpp"
#ifndef WSPRRYPI_BACKEND_SI5351
#include "backend_capabilities.hpp"
#endif
#include "json.hpp"
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <mutex>

#if defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#if WSPRRYPI_BACKEND_SI5351
#include "si5351_device.hpp"
#endif
#endif

namespace si5351_inventory_process {
#if defined(__linux__)
namespace {
std::mutex worker_mutex;
pid_t outstanding = -1;
}
#endif
void reap() noexcept {
#if defined(__linux__)
    try {
        std::unique_lock lock(worker_mutex, std::try_to_lock);
        if (!lock.owns_lock() || outstanding <= 0) return;
        int status = 0;
        const auto reaped = ::waitpid(outstanding, &status, WNOHANG);
        if (reaped == outstanding || (reaped < 0 && errno == ECHILD)) outstanding = -1;
    } catch (...) {}
#endif
}
Reply run(const std::vector<std::string>& command, std::chrono::milliseconds timeout) {
#if defined(__linux__)
    std::unique_lock lock(worker_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return {{}, "Si5351 address discovery is already running."};
    if (outstanding > 0) {
        int status = 0;
        const auto reaped = ::waitpid(outstanding, &status, WNOHANG);
        if (reaped == 0 || (reaped < 0 && errno != ECHILD))
            return {{}, "Previous Si5351 address discovery is still stopping."};
        outstanding = -1;
    }
    if (command.empty() || command.front().empty() || timeout.count() <= 0)
        return {{}, "Invalid Si5351 discovery command."};
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) != 0)
        return {{}, "Unable to create Si5351 discovery channel."};
    // Preserve the IPC channel even when the caller started with closed stdio.
    for (auto& descriptor : pipefd) {
        if (descriptor >= 3) continue;
        const int replacement = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) {
            ::close(pipefd[0]); ::close(pipefd[1]);
            return {{}, "Unable to create Si5351 discovery channel."};
        }
        ::close(descriptor);
        descriptor = replacement;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    const bool actions_initialized = rc == 0;
    if (rc == 0) {
        rc = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        // Do not retain listener/client sockets or hardware descriptors in a stalled child.
        if (rc == 0) rc = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    }
    std::vector<char*> args;
    for (const auto& argument : command) args.push_back(const_cast<char*>(argument.c_str()));
    args.push_back(nullptr);
    pid_t child = -1;
    if (rc == 0) rc = ::posix_spawn(&child, args[0], &actions, nullptr, args.data(), environ);
    if (actions_initialized) posix_spawn_file_actions_destroy(&actions);
    ::close(pipefd[1]);
    if (rc != 0) {
        ::close(pipefd[0]);
        return {{}, "Unable to start Si5351 discovery worker."};
    }
    outstanding = child;
    const auto expires = std::chrono::steady_clock::now() + timeout;
    Reply result;
    const int flags = ::fcntl(pipefd[0], F_GETFL);
    if (flags < 0 || ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) < 0)
        result.error = "Unable to read Si5351 discovery channel.";
    bool eof = false;
    while (result.error.empty()) {
        char buffer[512];
        const auto count = ::read(pipefd[0], buffer, sizeof(buffer));
        if (count > 0) {
            result.output.append(buffer, static_cast<std::size_t>(count));
            if (result.output.size() > 4096) {
                result.error = "Si5351 discovery reply is too large.";
                break;
            }
        } else if (count == 0) eof = true;
        else if (errno != EAGAIN && errno != EINTR) {
            result.error = "Unable to read Si5351 discovery reply.";
            break;
        }
        int status = 0;
        const auto reaped = ::waitpid(child, &status, WNOHANG);
        if (reaped == child) {
            outstanding = -1;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                result.error = "Si5351 discovery worker failed.";
            // A completed child may have more than one buffer of data queued.
            if (result.error.empty()) {
                while (true) {
                    const auto remaining = ::read(pipefd[0], buffer, sizeof(buffer));
                    if (remaining <= 0) break;
                    result.output.append(buffer, static_cast<std::size_t>(remaining));
                    if (result.output.size() > 4096) {
                        result.error = "Si5351 discovery reply is too large.";
                        break;
                    }
                }
            }
            break;
        }
        if (reaped < 0 && errno != EINTR) {
            outstanding = -1;
            result.error = "Unable to collect Si5351 discovery worker.";
            break;
        }
        if (std::chrono::steady_clock::now() >= expires) {
            result.error = "Si5351 address discovery timed out; configuration remains available.";
            break;
        }
        // Avoid spinning on a closed pipe while the child has not exited yet.
        if (eof) ::poll(nullptr, 0, 5);
        else {
            pollfd descriptor{pipefd[0], POLLIN, 0};
            (void)::poll(&descriptor, 1, 5);
        }
    }
    ::close(pipefd[0]);
    if (outstanding > 0) {
        (void)::kill(child, SIGKILL);
        const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
        do {
            int status = 0;
            const auto reaped = ::waitpid(child, &status, WNOHANG);
            if (reaped == child || (reaped < 0 && errno == ECHILD)) {
                outstanding = -1;
                break;
            }
            ::poll(nullptr, 0, 1);
        } while (std::chrono::steady_clock::now() < cleanup_deadline);
        // A kernel-blocked child may not exit yet. Keep its PID and reject new scans.
        // Never wait synchronously or create a second worker behind it.
    }
    if (!result.error.empty()) result.output.clear();
    return result;
#else
    (void)command; (void)timeout;
    return {{}, "Si5351 address discovery requires Linux."};
#endif
}

std::optional<int> helper_main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "--internal-si5351-inventory") return {};
    int bus = -1, reference = 0, selected = -1;
    const auto integer = [](const char* text, int& value) {
        const std::string input(text);
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
        return !input.empty() && parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size();
    };
    if ((argc != 4 && argc != 5) || !integer(argv[2], bus) || bus < 0 ||
        !integer(argv[3], reference) || reference <= 0 ||
        (argc == 5 && (!integer(argv[4], selected) || selected < 0x60 || selected > 0x6F))) return 2;
    nlohmann::json result = {{"bus", bus}, {"addresses", nlohmann::json::array()}, {"error", ""}};
#if defined(__linux__) && WSPRRYPI_BACKEND_SI5351
    const auto* disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
    if (disabled && std::string(disabled) == "1") {
        result["error"] = "Si5351 address discovery is disabled for hardware-free validation.";
    } else {
        for (int address = selected < 0 ? 0x60 : selected;
             address <= (selected < 0 ? 0x6F : selected); ++address) {
            Si5351Device::Config settings;
            settings.i2c_bus = bus;
            settings.i2c_address = static_cast<std::uint8_t>(address);
            settings.reference_hz = static_cast<std::uint32_t>(reference);
            Si5351Device device(settings);
            if (!device.open()) {
                result["addresses"] = nlohmann::json::array();
                result["error"] = "Unable to inspect Si5351 addresses on I2C bus " +
                    std::to_string(bus) + ": " + device.getLastError();
                break;
            }
            if (device.probe()) result["addresses"].push_back(address);
        }
    }
#else
    result["error"] = "Si5351 address discovery is unavailable because the Si5351 backend was not compiled.";
#endif
    std::cout << result.dump() << std::flush;
    return 0;
}
}
