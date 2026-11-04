/* SPDX-License-Identifier: MIT */
#include "../rp1_provider_process.hpp"
#include <cassert>
#include <fcntl.h>
#include <functional>
#include <iostream>

namespace {
struct ReadinessPipe {
    int descriptors[2]{-1, -1};
    ReadinessPipe()
    {
        if (::pipe(descriptors) != 0) throw std::runtime_error("fixture readiness pipe failed");
        if (::fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0) {
            for (const int fd : descriptors) ::close(fd);
            throw std::runtime_error("fixture readiness close-on-exec failed");
        }
    }
    ~ReadinessPipe() { for (const int fd : descriptors) if (fd >= 0) ::close(fd); }
    void await(char expected, const std::string& stage, pid_t child)
    {
        ::close(descriptors[1]); descriptors[1] = -1;
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds{5};
        const auto fail = [&](const std::string& last) {
            return std::runtime_error("fixture stage=" + stage + "; expected=ack:" + expected +
                "; last=" + last + "; child=" + std::to_string(child) + "; startup_hang_guard=5s; elapsed_ms=" +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count()));
        };
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) throw fail("missing acknowledgment");
            pollfd event{descriptors[0], POLLIN, 0};
            const int result = ::poll(&event, 1, static_cast<int>(remaining));
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) throw fail("missing acknowledgment");
            char observed{};
            const auto count = ::read(descriptors[0], &observed, 1);
            if (count < 0 && errno == EINTR) continue;
            if (count != 1 || observed != expected)
                throw fail("closed or malformed acknowledgment");
            return;
        }
    }
};
struct OwnedPeer {
    pid_t child;
    int socket;
    ~OwnedPeer()
    {
        if (socket >= 0) ::close(socket);
        if (child > 0) {
            ::kill(child, SIGKILL);
            int status{};
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        }
    }
};
}

int main(int argc, char** argv)
{
    if (argc >= 2) {
        const std::string mode{argv[1]};
        if (mode == "success") { std::cout << "{\"status\":\"ok\"}"; return 0; }
        if (mode == "refusal") { std::cerr << "exact provider refusal detail"; return 17; }
        if (mode == "oversize") {
            const std::string block(8192, 'x');
            for (int i = 0; i < 256; ++i) std::cout << block;
            return 0;
        }
        if (mode == "stall" || mode == "closed-pipe") {
            if (argc != 3) return 125;
            if (mode == "closed-pipe") { ::close(STDOUT_FILENO); ::close(STDERR_FILENO); }
            const int ready = std::stoi(argv[2]);
            const char state = mode == "stall" ? 's' : 'c';
            if (::write(ready, &state, 1) != 1) return 126;
            ::close(ready);
            for (;;) ::pause();
        }
        if (mode == "empty") return 0;
        return 2;
    }
    using namespace std::chrono;
    assert(wsprrypi::runRp1ProviderProcess({argv[0], "success"}) == "{\"status\":\"ok\"}");
    const auto fails = [&](const std::vector<std::string>& command, const std::string& detail,
                           milliseconds timeout = seconds{5},
                           const std::function<void(pid_t)>& await_ready = {}) {
        bool failed = false;
        try {
            if (await_ready) (void)wsprrypi::detail::runRp1ProviderProcess(command, timeout, true, await_ready);
            else (void)wsprrypi::runRp1ProviderProcess(command, timeout);
        }
        catch (const std::runtime_error& error) {
            failed = true;
            if (std::string{error.what()}.find(detail) == std::string::npos)
                throw std::runtime_error("expected " + detail + "; observed " + error.what());
        }
        assert(failed);
        int status = 0;
        assert(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    };
    fails({argv[0], "refusal"}, "exact provider refusal detail");
    fails({argv[0], "oversize"}, "exceeded its bound");
    fails({argv[0], "empty"}, "no response");
    fails({"/does-not-exist/rp1-provider"}, "refused");
    for (const std::string mode : {"stall", "closed-pipe"}) {
        ReadinessPipe ready;
        bool acknowledged = false;
        fails({argv[0], mode, std::to_string(ready.descriptors[1])},
              mode == "stall" ? "RP1 provider response timed out" : "RP1 provider exit timed out", milliseconds{50},
              [&](pid_t child) { ready.await(mode == "stall" ? 's' : 'c', mode, child); acknowledged = true; });
        assert(acknowledged);
    }
    for (const bool malformed : {false, true}) {
        ReadinessPipe ready;
        fails({argv[0], malformed ? "stall" : "empty", std::to_string(ready.descriptors[1])},
              "closed or malformed acknowledgment", milliseconds{50},
              [&](pid_t child) { ready.await('!', "negative readiness", child); });
    }
    assert(wsprrypi::runRp1ProviderProcess({argv[0], "empty"}, seconds{5}, false).empty());
    for (const std::string mode : {"success", "drip", "oversize", "empty"}) {
        ReadinessPipe ready;
        int sockets[2];
        assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        const auto child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            ::close(sockets[0]);
            ::close(ready.descriptors[0]);
            if (mode == "drip" && ::send(sockets[1], "x", 1, MSG_NOSIGNAL) != 1) _exit(126);
            if (::write(ready.descriptors[1], "r", 1) != 1) _exit(126);
            ::close(ready.descriptors[1]);
            char request[32];
            while (::read(sockets[1], request, sizeof(request)) > 0) {}
            const std::string block(mode == "oversize" ? 8192 : 1, 'x');
            const int repeats = mode == "oversize" ? 256 : mode == "empty" ? 0 : 1;
            const auto hang_guard = steady_clock::now() + seconds{5};
            for (int i = 0; mode == "drip" || i < repeats; ++i) {
                if (steady_clock::now() >= hang_guard) _exit(125);
                if (::send(sockets[1], block.data(), block.size(), MSG_NOSIGNAL) <= 0) break;
                // Deliberate paced data tests the absolute exchange deadline;
                // peer readiness and completion never depend on this interval.
                if (mode == "drip") ::usleep(5000);
            }
            ::close(sockets[1]);
            _exit(0);
        }
        ::close(sockets[1]);
        OwnedPeer peer{child, sockets[0]};
        ready.await('r', "socket " + mode, child);
        bool failed = false;
        const auto start = steady_clock::now();
        try {
            peer.socket = -1; // The production exchange consumes the descriptor.
            const auto reply = wsprrypi::exchangeRp1ProviderSocket(sockets[0], "{}\n",
                start + (mode == "drip" ? milliseconds{50} : milliseconds{2000}));
            if (mode != "success" || reply != "x") throw std::logic_error("unexpected socket success: " + mode);
        } catch (const std::runtime_error& error) {
            failed = true;
            const std::string detail{error.what()};
            const std::string expected = mode == "drip" ? "timed out" : mode == "oversize" ? "exceeded" : "no response";
            if (detail.find(expected) == std::string::npos)
                throw std::logic_error("socket " + mode + "; expected=" + expected + "; observed=" + detail);
        }
        if (failed != (mode != "success")) throw std::logic_error("socket outcome mismatch: " + mode);
        int status = 0;
        pid_t collected;
        do { collected = ::waitpid(child, &status, 0); } while (collected < 0 && errno == EINTR);
        if (collected == child) peer.child = -1;
        if (collected != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::logic_error("socket " + mode + "; peer failed or fixture hang guard expired");
    }
    std::cout << "RP1 provider subprocess bounds and refusal detail: PASS\n";
}
