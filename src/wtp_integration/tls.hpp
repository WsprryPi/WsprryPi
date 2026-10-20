// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "WTP-Client/include/wtp/transport.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wsprrypi {
struct TlsSelection {
  std::string host, expected_identity, ca_file, certificate_file, key_file;
  unsigned port{};
  bool operator==(const TlsSelection &) const = default;
};
struct TlsObservation {
  std::string state{"closed"}, address, authenticated_identity, diagnostic;
  std::uint64_t observed_ms{};
};
// System resolution runs outside the calling worker with a process-wide bound
// of one outstanding lookup. A replacement waits for a cancelled lookup to
// leave that slot; cancelling destroys no resolver-owned memory.
class TlsResolver {
public:
  virtual ~TlsResolver() = default;
  virtual bool begin(const std::string &host, unsigned port) = 0;
  virtual std::optional<std::vector<std::string>> poll() = 0;
  virtual void cancel() noexcept = 0;
};
std::unique_ptr<TlsResolver> system_tls_resolver();
// Deterministic host-test seam for the process-wide resolver serialization.
// Production configuration cannot select or replace this lookup function.
using TlsResolverLookup = std::function<std::vector<std::string>(const std::string &, unsigned)>;
std::unique_ptr<TlsResolver> system_tls_resolver_for_test(TlsResolverLookup);

// Opaque, validated immutable credential snapshot. No material is serialized.
class TlsCredentials {
public:
  explicit TlsCredentials(const TlsSelection &);
  ~TlsCredentials();
  TlsCredentials(const TlsCredentials &) = delete;
  bool matches(const TlsCredentials &) const noexcept;
private:
  friend class TlsStream;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class TlsStream final : public wtp::ByteStream {
public:
  using Clock = std::function<std::uint64_t()>;
  // The loopback-only seam bypasses only the hardware execution guard and
  // rejects every non-loopback resolved address. No config/CLI enables it.
  enum class Access { Production, LoopbackTest };
  TlsStream(Clock, Access = Access::Production,
            std::unique_ptr<TlsResolver> = system_tls_resolver());
  ~TlsStream() override;
  bool begin_open(const TlsSelection &, std::shared_ptr<TlsCredentials>,
                  std::string alpn = "wtp/1");
  void poll_open();
  bool ready() const;
  bool opening() const;
  void cancel() noexcept; // notification only; safe across threads
  TlsObservation observation() const; // copied, thread safe
  wtp::IoResult read(std::span<std::uint8_t>) override;
  wtp::IoResult write(std::span<const std::uint8_t>) override;
  void close() noexcept override;
  static constexpr std::uint64_t resolve_timeout_ms = 3000,
      connect_timeout_ms = 3000, handshake_timeout_ms = 10000,
      io_timeout_ms = 8000;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace wsprrypi
