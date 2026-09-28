// SPDX-License-Identifier: MIT
#include "wtp_integration/plain_tcp.hpp"
#include "wtp_settings_json.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace wsprrypi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Resolver : TlsResolver {
  std::optional<std::vector<std::string>> result;
  std::string host;
  unsigned calls{};
  bool begin(const std::string &name, unsigned) override { host = name; ++calls; return true; }
  auto poll() -> std::optional<std::vector<std::string>> override { return result; }
  void cancel() noexcept override {}
};
int main() {
  try {
    std::uint64_t now{};
    auto resolver = std::make_unique<Resolver>();
    auto *fake = resolver.get();
    PlainTcpStream stream([&] { return now; }, PlainTcpStream::Access::LoopbackTest,
                          std::move(resolver));
    CHECK(!stream.begin_open("bad/path", 31417) && fake->calls == 0);
    CHECK(stream.begin_open("PICO.LOCAL.", 31417));
    CHECK(fake->host == "pico.local");
    now += PlainTcpStream::resolve_timeout_ms;
    stream.poll_open();
    CHECK(!stream.opening() && stream.observation().state == "failed");
    fake->result = std::vector<std::string>{"192.0.2.1"};
    CHECK(stream.begin_open("pico.local", 31417)); stream.poll_open();
    CHECK(stream.observation().diagnostic.find("loopback") != std::string::npos);

    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    CHECK(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0);
    CHECK(listen(listener, 1) == 0);
    fake->result = std::vector<std::string>{"127.0.0.1"};
    CHECK(stream.begin_open("127.0.0.1", ntohs(address.sin_port)));
    for (unsigned i = 0; i < 100 && !stream.ready(); ++i) {
      stream.poll_open();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(stream.ready());
    const int client = accept(listener, nullptr, nullptr);
    CHECK(client >= 0);
    const std::array<std::uint8_t, 4> request{'W', 'T', 'P', 'F'};
    CHECK(stream.write(request).count == request.size());
    std::array<std::uint8_t, 4> received{};
    CHECK(recv(client, received.data(), received.size(), MSG_WAITALL) == 4);
    CHECK(received == request);
    CHECK(send(client, request.data(), request.size(), 0) == 4);
    for (unsigned i = 0; i < 100; ++i) {
      auto result = stream.read(received);
      if (result.state == wtp::IoState::Progress) { CHECK(result.count == 4); break; }
      CHECK(result.state == wtp::IoState::WouldBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      CHECK(i < 99);
    }
    CHECK(received == request);
    CHECK(stream.observation().authenticated_identity.empty());
    stream.close();
    CHECK(!stream.ready());
    ::close(client); ::close(listener);

    WtpSettings settings;
    settings.transport = "network_plain";
    settings.hostname = "pico.local";
    settings.tcp_port = 31417;
    settings.device_id = "";
    CHECK(parse_wtp_settings(wtp_settings_json(settings), true) == settings);
    settings.tcp_port = 0;
    bool rejected = false;
    try { validate_wtp_settings(settings, true); } catch (...) { rejected = true; }
    CHECK(rejected);
    settings.tcp_port = 31417;
    settings.transport = "network";
    rejected = false;
    try { validate_wtp_settings(settings, true); } catch (...) { rejected = true; }
    CHECK(rejected); // Existing TLS selection never falls back to plaintext.
    std::cout << "Plain TCP resolver, loopback I/O, config and TLS isolation passed\n";
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
