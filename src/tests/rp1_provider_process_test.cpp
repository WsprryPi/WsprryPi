/* SPDX-License-Identifier: MIT */
#include "../rp1_provider_process.hpp"
#include <cassert>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 2) {
        const std::string mode{argv[1]};
        if (mode == "success") { std::cout << "{\"status\":\"ok\"}"; return 0; }
        if (mode == "refusal") { std::cerr << "exact provider refusal detail"; return 17; }
        if (mode == "oversize") {
            const std::string block(8192, 'x');
            for (int i = 0; i < 256; ++i) std::cout << block;
            return 0;
        }
        if (mode == "closed-pipe") { ::close(STDOUT_FILENO); ::close(STDERR_FILENO); }
        if (mode == "stall" || mode == "closed-pipe") { ::sleep(60); return 0; }
        if (mode == "empty") return 0;
        return 2;
    }
    using namespace std::chrono;
    assert(wsprrypi::runRp1ProviderProcess({argv[0], "success"}) == "{\"status\":\"ok\"}");
    const auto fails = [&](const std::vector<std::string>& command, const std::string& detail,
                           milliseconds timeout = seconds{5}) {
        const auto start = steady_clock::now();
        bool failed = false;
        try { (void)wsprrypi::runRp1ProviderProcess(command, timeout); }
        catch (const std::runtime_error& error) {
            failed = true;
            assert(std::string{error.what()}.find(detail) != std::string::npos);
        }
        assert(failed);
        assert(steady_clock::now() - start < seconds{5});
        int status = 0;
        assert(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    };
    fails({argv[0], "refusal"}, "exact provider refusal detail");
    fails({argv[0], "oversize"}, "exceeded its bound");
    fails({argv[0], "empty"}, "no response");
    fails({"/does-not-exist/rp1-provider"}, "refused");
    fails({argv[0], "stall"}, "timed out", milliseconds{50});
    fails({argv[0], "closed-pipe"}, "timed out", milliseconds{50});
    assert(wsprrypi::runRp1ProviderProcess({argv[0], "empty"}, seconds{5}, false).empty());
    for (const std::string mode : {"success", "drip", "oversize", "empty"}) {
        int sockets[2];
        assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        const auto child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            ::close(sockets[0]);
            char request[32];
            while (::read(sockets[1], request, sizeof(request)) > 0) {}
            const std::string block(mode == "oversize" ? 8192 : 1, 'x');
            const int repeats = mode == "drip" ? 100 : mode == "oversize" ? 256 : mode == "empty" ? 0 : 1;
            for (int i = 0; i < repeats; ++i) {
                if (::send(sockets[1], block.data(), block.size(), MSG_NOSIGNAL) <= 0) break;
                if (mode == "drip") ::usleep(5000);
            }
            ::close(sockets[1]);
            _exit(0);
        }
        ::close(sockets[1]);
        bool failed = false;
        const auto start = steady_clock::now();
        try {
            const auto reply = wsprrypi::exchangeRp1ProviderSocket(sockets[0], "{}\n",
                start + (mode == "drip" ? milliseconds{50} : milliseconds{2000}));
            assert(mode == "success" && reply == "x");
        } catch (const std::runtime_error& error) {
            failed = true;
            const std::string detail{error.what()};
            assert(detail.find(mode == "drip" ? "timed out" : mode == "oversize" ? "exceeded" : "no response") != std::string::npos);
        }
        assert(failed == (mode != "success"));
        if (mode == "drip") assert(steady_clock::now() - start < milliseconds{300});
        int status = 0;
        assert(::waitpid(child, &status, 0) == child && WIFEXITED(status));
    }
    std::cout << "RP1 provider subprocess bounds and refusal detail: PASS\n";
}
