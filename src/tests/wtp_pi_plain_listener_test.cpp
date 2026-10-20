#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/plain_listener.hpp"

#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "wtp/frame_parser.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <fcntl.h>
#include <unistd.h>

using namespace wsprrypico::wtp;

#ifdef WSPRRYPI_TEST_SOCKET_WRAPPERS
namespace {
std::atomic<int> interface_failure{0}, failed_socket{-1}, bound_accepts{0};
std::atomic<bool> expect_interface{false}, wrong_interface{false};
}
extern "C" int __real_setsockopt(int, int, int, const void*, socklen_t);
extern "C" int __wrap_setsockopt(int fd, int level, int option,
                                const void* value, socklen_t length) {
    if (level == SOL_SOCKET && option == SO_BINDTODEVICE && interface_failure.load()) {
        failed_socket = fd;
        errno = interface_failure.load();
        return -1;
    }
    return __real_setsockopt(fd, level, option, value, length);
}
extern "C" int __real_accept(int, sockaddr*, socklen_t*);
extern "C" int __wrap_accept(int fd, sockaddr* address, socklen_t* length) {
    const int accepted = __real_accept(fd, address, length);
    if (accepted >= 0 && expect_interface.load()) {
        std::array<char, IFNAMSIZ> name{};
        socklen_t size = name.size();
        if (getsockopt(accepted, SOL_SOCKET, SO_BINDTODEVICE, name.data(), &size) != 0 ||
            std::string(name.data()) != "lo") wrong_interface = true;
        ++bound_accepts;
    }
    return accepted;
}
#endif

namespace {
class TestClock final : public Clock {
public:
    ClockSnapshot snapshot() const override {
        using namespace std::chrono;
        const auto steady = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        const auto utc = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        return {ClockState::Synchronized, static_cast<std::uint64_t>(utc),
                static_cast<std::uint64_t>(steady), 1000, 0, LeapState::Normal, {}};
    }
};
class TestIdentity final : public IdentitySource {
public:
    std::string new_boot_id() override { return std::string(32, 'b'); }
};
void check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
struct WireReader {
    wsprrypi::wtp::FrameParser parser;
    std::deque<std::string> frames;
    std::string next(int fd) {
        std::array<std::uint8_t, 4096> buffer{};
        while (frames.empty()) {
            const auto count = recv(fd, buffer.data(), buffer.size(), 0);
            check(count > 0, "response read failed");
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto result = parser.feed(
                std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(count)),
                static_cast<std::uint64_t>(now));
            for (const auto& item : result.events)
                if (item.kind == wsprrypi::wtp::FrameEventKind::Payload)
                    frames.emplace_back(item.payload.begin(), item.payload.end());
        }
        auto result = std::move(frames.front());
        frames.pop_front();
        return result;
    }
};
std::string transact(int fd, WireReader& reader, const std::string& payload) {
    const auto frame = wsprrypi::wtp::encode_frame(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                      payload.size()));
    check(!frame.empty(), "request frame must encode");
    std::size_t sent = 0;
    while (sent < frame.size()) {
        const auto count = send(fd, frame.data() + sent, frame.size() - sent, 0);
        check(count > 0, "request send failed");
        sent += static_cast<std::size_t>(count);
    }
    return reader.next(fd);
}
} // namespace

int main() {
    TestClock clock;
    TestIdentity ids;
    InhibitedRfEngine engine;
    JobService service(clock, engine, ids);
    wsprrypi::WtpPiAuthority authority(service, true);
    wsprrypi::WtpPlainListener listener(authority, std::string(32, 'c'), "test");
    check(!listener.start("localhost", 0), "listener must require numeric bind address");
    check(!listener.start("192.168.1.10", 0), "LAN address must require selected interface");
    check(!listener.start("127.0.0.1", 0, std::string(IFNAMSIZ, 'a')),
          "interface names must not be truncated");
    check(!listener.start("127.0.0.1", 0, std::string("lo\0other", 8)),
          "embedded NUL must not change selected interface");
    std::string selected;
#ifdef WSPRRYPI_TEST_SOCKET_WRAPPERS
    check(!listener.start("127.0.0.1", 0, "wtp_missing0"),
          "missing interface must not fall back to unrestricted listener");
    for (int failure : {EACCES, ENOPROTOOPT}) {
        interface_failure = failure;
        check(!listener.start("127.0.0.1", 0, "lo"), "binding failure must prevent startup");
        check(!listener.running() && listener.bound_port() == 0 &&
                  listener.error().find("interface lo:") != std::string::npos,
              "binding failure must expose error and no listener port");
        check(fcntl(failed_socket.load(), F_GETFD) == -1 && errno == EBADF,
              "failed interface socket must be closed");
    }
    interface_failure = 0;
    selected = "lo";
    expect_interface = true;
#else
    check(!listener.start("127.0.0.1", 0, "lo"),
          "unsupported interface binding must fail closed");
#endif
    check(listener.start("127.0.0.1", 0, selected) && listener.bound_port() && listener.error().empty(),
          "loopback test listener must start");
    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(fd >= 0, "socket creation failed");
    timeval timeout{2, 0};
    check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
          "timeout configuration failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(listener.bound_port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "connection failed");
    const auto ids_json = "\"session_id\":\"" + std::string(32, '1') + "\",";
    const auto hello = "{\"type\":\"request\",\"protocol\":\"WTP/1\"," + ids_json +
        "\"request_id\":\"" + std::string(32, 'a') +
        "\",\"op\":\"HELLO\",\"body\":{\"versions\":[\"WTP/1\"],"
        "\"client_name\":\"test\",\"client_version\":\"1\"}}";
    WireReader first_reader;
    const auto response = transact(fd, first_reader, hello);
    check(response.find("\"ok\":true") != std::string::npos &&
              response.find("\"product\":\"WsprryPi\"") != std::string::npos &&
              response.find(std::string(32, 'c')) != std::string::npos,
          "HELLO must identify the Pi endpoint");
    const auto claim = "{\"type\":\"request\",\"protocol\":\"WTP/1\"," + ids_json +
        "\"request_id\":\"" + std::string(32, 'd') +
        "\",\"op\":\"CLAIM\",\"body\":{\"owner_id\":\"" +
        std::string(32, '2') + "\",\"lease_ms\":10000}}";
    check(transact(fd, first_reader, claim).find("BUSY") != std::string::npos,
          "locally enabled Pi must refuse remote claim over TCP");
    const int replacement = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(replacement >= 0, "replacement socket creation failed");
    check(setsockopt(replacement, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
          "replacement timeout configuration failed");
    check(connect(replacement, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "replacement connection failed");
    WireReader replacement_reader;
    check(transact(replacement, replacement_reader, hello).find("\"ok\":true") != std::string::npos,
          "same-principal session resume should preserve HELLO replay");
    check(first_reader.next(fd).find("SESSION_REPLACED") != std::string::npos,
          "resuming a session must replace the previous connection");
    char byte{};
    check(recv(fd, &byte, 1, 0) == 0, "replaced connection must close");
    close(replacement);
    close(fd);
    listener.stop();

    check(listener.start("127.0.0.1", 0, selected), "listener can restart after shutdown");
    address.sin_port = htons(listener.bound_port());
    auto connect_client = [&] {
        const int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        check(client >= 0, "capacity socket creation");
        check(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
              "capacity socket timeout");
        check(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
              "capacity connection");
        return client;
    };
    std::vector<int> clients;
    for (int index = 0; index < 8; ++index) {
        clients.push_back(connect_client());
        auto distinct_hello = hello;
        distinct_hello.replace(distinct_hello.find(std::string(32, '1')), 32,
                               std::string(32, static_cast<char>('2' + index)));
        WireReader reader;
        check(transact(clients.back(), reader, distinct_hello).find("\"ok\":true") != std::string::npos,
              "each bounded concurrent client can inspect");
    }
    const int excess = connect_client();
    check(recv(excess, &byte, 1, 0) == 0, "excess client closes without displacing admitted clients");
    close(excess);
    for (const int client : clients) close(client);
    listener.stop();
#ifdef WSPRRYPI_TEST_SOCKET_WRAPPERS
    check(bound_accepts.load() >= 11 && !wrong_interface.load(),
          "accepted TCP sockets must inherit the selected interface");
    expect_interface = false;
    check(listener.start("127.0.0.1", 0), "explicit loopback may remain unbound for portable tests");
    listener.stop();
#endif
}
