// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_integration/tls.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace wsprrypi;
std::uint64_t now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int main(int argc, char **argv) {
  if (argc != 8) return 2;
  try {
    TlsSelection selection{argv[1], argv[2], argv[4], argv[5], argv[6], static_cast<unsigned>(std::stoul(argv[3]))};
    auto credentials = std::make_shared<TlsCredentials>(selection);
    TlsStream stream(now, TlsStream::Access::LoopbackTest);
    if (!stream.begin_open(selection, credentials, "http/1.1")) throw std::runtime_error("open failed");
    while (stream.opening()) { stream.poll_open(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    if (!stream.ready()) throw std::runtime_error(stream.observation().diagnostic);
    if (std::string(argv[7]) == "backpressure") {
      std::array<std::uint8_t, 4096> block{};
      std::size_t accepted = 0; bool blocked = false;
      const auto deadline = now() + 20000;
      while (now() < deadline) {
        auto result = stream.write(block);
        if (result.state == wtp::IoState::Progress) accepted += result.count;
        else if (result.state == wtp::IoState::WouldBlock) {
          blocked = true; std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
          if (!accepted || !blocked) throw std::runtime_error("backpressure was not exercised");
          std::cout << "accepted bytes remain ambiguous after bounded write failure: " << accepted << '\n';
          return 0;
        }
      }
      throw std::runtime_error("write deadline not enforced");
    }
    const std::string request = "GET " + std::string(argv[7]) + " HTTP/1.1\r\nHost: 127.0.0.1:" + argv[3] + "\r\n\r\n";
    std::size_t sent{};
    std::string response;
    const auto deadline = now() + 10000;
    while (now() < deadline) {
      if (sent < request.size()) {
        auto result = stream.write(std::span(reinterpret_cast<const std::uint8_t *>(request.data() + sent), std::min<std::size_t>(7, request.size() - sent)));
        if (result.state == wtp::IoState::Progress) sent += result.count;
        else if (result.state != wtp::IoState::WouldBlock) throw std::runtime_error("write failed");
      }
      std::array<std::uint8_t, 13> bytes{};
      const auto result = stream.read(bytes);
      if (result.state == wtp::IoState::Progress) response.append(reinterpret_cast<const char *>(bytes.data()), result.count);
      else if (result.state != wtp::IoState::WouldBlock) {
        if (response.find("HTTP/1.1 200") != 0) throw std::runtime_error("No successful authenticated response");
        std::cout << response; return 0;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("I/O deadline");
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
