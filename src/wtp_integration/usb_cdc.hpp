// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once

#include "WTP-Client/include/wtp/transport.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace wsprrypi {
enum class PicoCdcRole : unsigned { Wtp = 2, Management = 4 };
struct PicoCdcSelection {
  std::string path, usb_serial;
  std::uint16_t vendor_id{},
      product_id{}; // Explicit; cafe:4012 is development identity only.
  PicoCdcRole role{PicoCdcRole::Wtp};
};
struct CdcIdentity {
  std::string path, interface_path, usb_serial;
  std::uint64_t device_number{};
  std::uint16_t vendor_id{}, product_id{};
  unsigned control_interface{};
  bool operator==(const CdcIdentity &) const = default;
};
struct CdcTransfer {
  std::ptrdiff_t count{};
  int error{}; // errno captured immediately; zero on success.
};
struct CdcPoll {
  int count{}, events{}, error{};
};

// Typed OS seam for deterministic tests, not a CLI/configuration escape hatch.
// Setup operations throw on failure. I/O returns captured errno; close never
// throws.
class CdcSystem {
public:
  virtual ~CdcSystem() = default;
  virtual CdcIdentity inspect(const std::string &path) = 0;
  virtual int open(const std::string &path, int flags) = 0;
  virtual void verify(int fd, const CdcIdentity &identity) = 0;
  virtual void exclusive(int fd) = 0;
  virtual void unexclusive(int fd) = 0;
  virtual void raw(int fd) = 0;
  virtual void dtr(int fd, bool asserted) = 0;
  virtual void discard(int fd) = 0;
  virtual CdcPoll poll(int fd, bool writing) = 0;
  virtual CdcTransfer read(int fd, std::span<std::uint8_t> bytes) = 0;
  virtual CdcTransfer write(int fd, std::span<const std::uint8_t> bytes) = 0;
  virtual int close(int fd) noexcept = 0;
};

// Metadata-only Linux resolver, with an explicit synthetic sysfs root for
// tests. It never opens the selected character device. Throws on
// missing/invalid metadata.
CdcIdentity inspect_linux_cdc(const std::string &path,
                              const std::filesystem::path &sysfs_root = "/sys");

// Resolve a sibling role only under the exact selected physical USB parent.
// Missing or ambiguous siblings fail closed; tty numbering never selects a role.
PicoCdcSelection paired_linux_cdc(const PicoCdcSelection &, PicoCdcRole,
    const std::filesystem::path &sysfs_root = "/sys",
    const std::filesystem::path &dev_root = "/dev");

class PosixCdcSystem : public CdcSystem {
public:
  CdcIdentity inspect(const std::string &path) override;
  int open(const std::string &path, int flags) override;
  void verify(int fd, const CdcIdentity &identity) override;
  void exclusive(int fd) override;
  void unexclusive(int fd) override;
  void raw(int fd) override;
  void dtr(int fd, bool asserted) override;
  void discard(int fd) override;
  CdcPoll poll(int fd, bool writing) override;
  CdcTransfer read(int fd, std::span<std::uint8_t> bytes) override;
  CdcTransfer write(int fd, std::span<const std::uint8_t> bytes) override;
  int close(int fd) noexcept override;
};

enum class CdcState { Closed, Resetting, Ready, Failed };
class UsbCdcStream final : public wtp::ByteStream {
public:
  explicit UsbCdcStream(CdcSystem &system) : system_(system) {}
  ~UsbCdcStream() override { close(); }
  UsbCdcStream(const UsbCdcStream &) = delete;
  UsbCdcStream &operator=(const UsbCdcStream &) = delete;
  // Single event-loop owner. The system outlives this object. No automatic
  // reopen.
  bool begin_open(const PicoCdcSelection &selection, std::uint64_t now_ms);
  void poll_open(std::uint64_t now_ms);
  wtp::IoResult read(std::span<std::uint8_t> bytes) override;
  wtp::IoResult write(std::span<const std::uint8_t> bytes) override;
  void close() noexcept override;
  CdcState state() const noexcept { return state_; }
  const std::optional<CdcIdentity> &identity() const noexcept {
    return identity_;
  }
  const std::string &diagnostic() const noexcept { return diagnostic_; }
  bool cleanup_failed() const noexcept { return cleanup_failed_; }
  static constexpr std::uint64_t reset_quiet_ms = 100, open_timeout_ms = 2000;
  static constexpr std::size_t io_chunk_bytes = 4096;

private:
  wtp::IoResult ready(bool writing);
  void fail(std::string reason);
  CdcSystem &system_;
  int fd_{-1};
  CdcState state_{CdcState::Closed};
  std::optional<CdcIdentity> identity_;
  std::string diagnostic_;
  std::uint64_t started_ms_{}, observed_ms_{};
  std::optional<std::uint64_t> reset_started_ms_;
  bool exclusive_{}, cleanup_failed_{};
};
} // namespace wsprrypi
