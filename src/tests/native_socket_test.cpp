// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_integration/native_socket.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace ns = wsprrypi::native_socket;
#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
struct Socket {
  ns::Handle value{ns::invalid};
  ~Socket() { ns::close(value); }
};
#ifdef WSPRRYPI_NATIVE_WEBSOCKET_TRANSPORT_TEST
void test_native_windows_websocket_transport();
#endif
int main() {
  try {
    CHECK(ns::initialize());
    Socket listener{ns::open_stream(AF_INET)};
    CHECK(listener.value != ns::invalid);
#ifdef _WIN32
    static_assert(sizeof(ns::Handle) == sizeof(void *));
    DWORD flags = 0;
    CHECK(GetHandleInformation(reinterpret_cast<HANDLE>(listener.value), &flags));
    CHECK(!(flags & HANDLE_FLAG_INHERIT));
#else
    CHECK(fcntl(listener.value, F_GETFD) & FD_CLOEXEC);
    CHECK(fcntl(listener.value, F_GETFL) & O_NONBLOCK);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(listener.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    CHECK(listen(listener.value, 1) == 0);
    ns::Length length = sizeof(address);
    CHECK(getsockname(listener.value, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    Socket client{ns::open_stream(AF_INET)};
    CHECK(client.value != ns::invalid);
    const int started = connect(client.value, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    CHECK(started == 0 || ns::pending(ns::error()));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (ns::poll_write(client.value) == 0 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(ns::connected(client.value));
    Socket peer;
    do {
      peer.value = accept(listener.value, nullptr, nullptr);
      if (peer.value != ns::invalid) break;
      CHECK(ns::retry(ns::error()));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(peer.value != ns::invalid);
    CHECK(ns::configure_stream(peer.value));
#ifdef _WIN32
    CHECK(GetHandleInformation(reinterpret_cast<HANDLE>(peer.value),&flags));
    CHECK(!(flags&HANDLE_FLAG_INHERIT));
#else
    CHECK(fcntl(peer.value,F_GETFD)&FD_CLOEXEC);
    CHECK(fcntl(peer.value,F_GETFL)&O_NONBLOCK);
#endif
    char byte{};
    CHECK(ns::receive(client.value, &byte, 1) < 0);
    CHECK(ns::retry(ns::error()));
    CHECK(ns::send(peer.value, "x", 1) == 1);
    decltype(ns::receive(client.value, &byte, 1)) count;
    do {
      count = ns::receive(client.value, &byte, 1);
      if (count >= 0) break;
      CHECK(ns::retry(ns::error()));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(count == 1 && byte == 'x');
    ns::close(peer.value); peer.value = ns::invalid;
    do {
      count = ns::receive(client.value, &byte, 1);
      if (count >= 0) break;
      CHECK(ns::retry(ns::error()));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(count == 0);
#ifdef WSPRRYPI_NATIVE_WEBSOCKET_TRANSPORT_TEST
    test_native_windows_websocket_transport();
#endif
    std::cout << "Native socket ownership, nonblocking I/O, loopback, and peer close passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
