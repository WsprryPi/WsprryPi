/* SPDX-License-Identifier: MIT */
#pragma once

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace wsprrypi {
// Fixed production callers supply the argv. The duration seam is test-only.
inline std::string runRp1ProviderProcess(const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout = std::chrono::seconds{3}, bool require_response = true)
{
    if (arguments.empty()) throw std::runtime_error("Missing RP1 provider command.");
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    int output[2];
    if (::pipe(output) != 0) throw std::runtime_error("Could not create RP1 provider pipe.");
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(output[0]); ::close(output[1]);
        throw std::runtime_error("Could not start RP1 provider.");
    }
    if (child == 0) {
        ::setpgid(0, 0);
        ::close(output[0]);
        if (::dup2(output[1], STDOUT_FILENO) < 0 || ::dup2(output[1], STDERR_FILENO) < 0) _exit(126);
        ::close(output[1]);
        ::execv(argv[0], argv.data());
        _exit(127);
    }
    ::setpgid(child, child);
    ::close(output[1]);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool reaped = false;
    try {
        std::string response;
        char buffer[4096];
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) throw std::runtime_error("RP1 provider response timed out.");
            pollfd event{output[0], POLLIN, 0};
            const auto ready = ::poll(&event, 1, static_cast<int>(remaining));
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0) throw std::runtime_error("RP1 provider response timed out or could not be read.");
            const auto count = ::read(output[0], buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) throw std::runtime_error("Could not read RP1 provider response.");
            if (count == 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
            if (response.size() > 1024U * 1024U) throw std::runtime_error("RP1 provider response exceeded its bound.");
        }
        int status = 0;
        for (;;) {
            const auto result = ::waitpid(child, &status, WNOHANG);
            if (result == child) { reaped = true; break; }
            if (result < 0 && errno != EINTR) throw std::runtime_error("Could not collect RP1 provider.");
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("RP1 provider exit timed out.");
            ::poll(nullptr, 0, 1);
        }
        ::close(output[0]);
        output[0] = -1;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error("RP1 provider refused the operation: " + response.substr(0, 8192));
        if (require_response && response.empty()) throw std::runtime_error("RP1 provider returned no response.");
        return response;
    } catch (...) {
        if (output[0] >= 0) ::close(output[0]);
        if (!reaped) {
            ::kill(-child, SIGKILL);
            ::kill(child, SIGKILL);
            int ignored;
            while (::waitpid(child, &ignored, 0) < 0 && errno == EINTR) {}
        }
        throw;
    }
}

// Consumes the connected descriptor. One deadline covers the entire exchange,
// including a peer that continually sends bytes without completing its reply.
inline std::string exchangeRp1ProviderSocket(int fd, const std::string& payload,
    std::chrono::steady_clock::time_point deadline)
{
    struct Close { int fd; ~Close() { ::close(fd); } } close{fd};
    int send_flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
    send_flags |= MSG_NOSIGNAL;
#elif defined(SO_NOSIGPIPE)
    const int no_signal = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_signal, sizeof(no_signal)) != 0)
        throw std::runtime_error("Could not suppress RP1 route socket signals.");
#endif
    const auto ready = [&](short events) {
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) throw std::runtime_error("RP1 route-manager exchange timed out.");
            pollfd event{fd, events, 0};
            const auto count = ::poll(&event, 1, static_cast<int>(remaining));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0 || event.revents & POLLNVAL)
                throw std::runtime_error("RP1 route-manager exchange timed out or could not be polled.");
            return;
        }
    };
    std::size_t offset = 0;
    while (offset < payload.size()) {
        ready(POLLOUT);
        const auto count = ::send(fd, payload.data() + offset, payload.size() - offset,
                                  send_flags);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) throw std::runtime_error("Could not send RP1 route request: " + std::string{std::strerror(errno)});
        offset += static_cast<std::size_t>(count);
    }
    if (::shutdown(fd, SHUT_WR) != 0) throw std::runtime_error("Could not finish RP1 route request.");
    std::string response;
    char buffer[4096];
    for (;;) {
        ready(POLLIN);
        const auto count = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count < 0) throw std::runtime_error("Could not read RP1 route response: " + std::string{std::strerror(errno)});
        if (count == 0) break;
        response.append(buffer, static_cast<std::size_t>(count));
        if (response.size() > 1024U * 1024U) throw std::runtime_error("RP1 route response exceeded its bound.");
    }
    if (response.empty()) throw std::runtime_error("RP1 route manager returned no response.");
    return response;
}
} // namespace wsprrypi
