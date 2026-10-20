// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "tls.hpp"
#include <atomic>
#include <mutex>

namespace wsprrypi {
// Explicit WTP/1 plaintext binding. The network itself is the shared principal.
class PlainTcpStream final : public wtp::ByteStream {
public:
  using Clock = std::function<std::uint64_t()>;
  enum class Access { Production, LoopbackTest };
  PlainTcpStream(Clock, Access = Access::Production,
                 std::unique_ptr<TlsResolver> = system_tls_resolver());
  ~PlainTcpStream() override;
  bool begin_open(const std::string &host, unsigned port);
  void poll_open();
  bool opening() const;
  bool ready() const;
  TlsObservation observation() const;
  wtp::IoResult read(std::span<std::uint8_t>) override;
  wtp::IoResult write(std::span<const std::uint8_t>) override;
  void close() noexcept override;
  static constexpr std::uint64_t resolve_timeout_ms = 3000, connect_timeout_ms = 3000;
private:
  void state(std::string, std::string = {});
  void fail(const char *);
  void connect_next();
  Clock clock_;
  Access access_;
  std::unique_ptr<TlsResolver> resolver_;
  mutable std::mutex mutex_;
  TlsObservation status_;
  int fd_{-1};
  unsigned port_{};
  std::uint64_t deadline_{};
  std::vector<std::string> addresses_;
  std::size_t next_address_{};
};
} // namespace wsprrypi
