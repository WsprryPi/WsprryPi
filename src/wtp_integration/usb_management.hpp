// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "network_http.hpp"
#include "pi_pair_mutation_guard.hpp"
#include <mutex>
namespace wsprrypi {
struct HostUtcObservation {
  bool qualified{};
  std::uint64_t monotonic_ns{}, utc_ns{}, uncertainty_ns{};
};
HostUtcObservation qualified_host_utc() noexcept;
// Production USB-Management/1 client. One owner serializes a whole
// request/response/ACK, with an absolute deadline and no mutation replay.
class UsbManagementClient {
public:
  using Clock = std::function<std::uint64_t()>;
  using Wait = std::function<void(unsigned)>;
  UsbManagementClient(wtp::ByteStream &, Clock, Wait, std::function<bool()> cancelled = {});
  bool hello(const std::string &expected_device, unsigned timeout_ms = 2000,const std::function<bool()> &cancelled = {});
  PicoHttpResponse request(const std::string &path, const std::string &method,
      const std::string &body = {}, const std::string &revision = {}, unsigned timeout_ms = 10000,const std::function<bool()> &cancelled = {});
  // Paired-Pi gateway admission is held only through the first actual USB
  // request write, never response delivery or its ACK. Zero-progress waits
  // reacquire admission; a partial handoff retains ordinary unknown recovery.
  using WriteAdmission=std::function<std::optional<PiPairMutationGuard>()>;
  PicoHttpResponse request_guarded(const std::string &path,const std::string &method,
      const std::string &body,const std::string &revision,unsigned timeout_ms,const WriteAdmission &,const std::function<bool()> &cancelled = {});
  bool synchronize(const std::function<HostUtcObservation()> & = qualified_host_utc);
  void close() noexcept;
  nlohmann::json observation() const;
  std::string device() const;
  std::string boot() const;
  bool connected() const;
  // Process-local observation only, never a wire or authorization counter.
  // A successful new hello cannot silently rebase an existing editor context.
  std::uint64_t connection_generation() const;
private:
  nlohmann::json exchange(const nlohmann::json &, std::uint64_t deadline,const WriteAdmission & = {},const std::function<bool()> & = {});
  PicoHttpResponse request_locked(const std::string &, const std::string &,
      const std::string &, const std::string &, unsigned, const std::function<void()> & = {},const WriteAdmission & = {},const std::function<bool()> & = {});
  void fail(const char *);
  wtp::ByteStream &stream_;
  Clock now_;
  Wait wait_;
  std::function<bool()> cancelled_;
  mutable std::mutex mutex_;
  nlohmann::json target_, hello_, time_;
  std::string session_, diagnostic_;
  std::uint32_t next_id_{};
  std::uint64_t connection_generation_{};
  bool connected_{};
};
} // namespace wsprrypi
