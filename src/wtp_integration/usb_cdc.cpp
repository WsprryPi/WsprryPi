// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "usb_cdc.hpp"
#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <utility>
#ifndef _WIN32
#include <fcntl.h>
#endif
namespace wsprrypi {
namespace {
bool valid_text(const std::string &s,std::size_t maximum){return !s.empty()&&s.size()<=maximum&&
    std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});}
bool transient(int error){return error==EINTR||error==EAGAIN||error==EWOULDBLOCK;}
}
bool UsbCdcStream::begin_open(const PicoCdcSelection &selection,
                              std::uint64_t now) {
  if (fd_ >= 0)
    return false;
  diagnostic_.clear();
  cleanup_failed_ = false;
  identity_.reset();
  reset_started_ms_.reset();
  started_ms_ = observed_ms_ = now;
  try {
    if (!valid_text(selection.path, 4096) ||
        !std::filesystem::path(selection.path).is_absolute() ||
        !valid_text(selection.usb_serial, 256) || !selection.vendor_id ||
        !selection.product_id || (selection.role != PicoCdcRole::Console && selection.role != PicoCdcRole::Wtp && selection.role != PicoCdcRole::Management && selection.role != PicoCdcRole::Logging))
      throw std::runtime_error(
          "Explicit endpoint, USB serial and VID/PID are required");
    const auto identity = system_.inspect(selection.path);
    if (identity.usb_serial != selection.usb_serial ||
        identity.vendor_id != selection.vendor_id ||
        identity.product_id != selection.product_id ||
        identity.control_interface != static_cast<unsigned>(selection.role))
      throw std::runtime_error(
          "Selected port does not match the expected Pico USB role");
#ifdef _WIN32
    fd_=system_.open(identity.path,0); // Native adapter owns handle flags.
#else
    fd_ = system_.open(identity.path,
                       O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
#endif
    if (fd_ < 0)
      throw std::runtime_error("Invalid CDC descriptor");
    system_.verify(fd_, identity);
    system_.exclusive(fd_);
    exclusive_ = true;
    system_.raw(fd_);
    system_.dtr(fd_, false);
    system_.discard(fd_);
    identity_ = identity;
    state_ = CdcState::Resetting;
    return true;
  } catch (const std::exception &e) {
    fail(e.what());
    return false;
  }
}
void UsbCdcStream::poll_open(std::uint64_t now) {
  if (state_ != CdcState::Resetting)
    return;
  if (now < observed_ms_ || now - started_ms_ >= open_timeout_ms) {
    fail("CDC opening clock regressed or deadline expired");
    return;
  }
  observed_ms_ = now;
  // The first poll is after begin_open returned, so setup time cannot count
  // toward the DTR-low interval. The absolute deadline still starts at begin.
  if (!reset_started_ms_)
    reset_started_ms_ = now;
  if (now - *reset_started_ms_ < reset_quiet_ms)
    return;
  try {
    system_.verify(fd_, *identity_);
    system_.discard(fd_);
    system_.dtr(fd_, true);
    state_ = CdcState::Ready;
  } catch (const std::exception &e) {
    fail(e.what());
  }
}
void UsbCdcStream::close() noexcept {
  const int fd = std::exchange(fd_, -1);
  if (fd >= 0) {
    if (exclusive_) {
      try {
        system_.dtr(fd, false);
      } catch (...) {
        cleanup_failed_ = true;
      }
      try {
        system_.discard(fd);
      } catch (...) {
        cleanup_failed_ = true;
      }
      try {
        system_.unexclusive(fd);
      } catch (...) {
        cleanup_failed_ = true;
      }
    }
    // Never retry close, including EINTR: the numeric descriptor may be reused.
    if (system_.close(fd))
      cleanup_failed_ = true;
  }
  exclusive_ = false;
  state_ = CdcState::Closed;
}
void UsbCdcStream::fail(std::string reason) {
  close();
  diagnostic_ = std::move(reason);
  state_ = CdcState::Failed;
}
wtp::IoResult UsbCdcStream::ready(bool writing) {
  using wtp::IoState;
  if (state_ != CdcState::Ready)
    return {IoState::Closed};
  const auto p = system_.poll(fd_, writing);
  if (p.count < 0 && transient(p.error))
    return {IoState::WouldBlock};
  if (p.count < 0 || p.count > 1 || p.error ||
      (p.events & CdcPollFailure)) {
    fail("CDC readiness failed or endpoint disconnected");
    return {IoState::Failed};
  }
  if (!p.count || !(p.events & (writing ? CdcPollWritable : CdcPollReadable)))
    return {IoState::WouldBlock};
  return {IoState::Progress}; // Internal sentinel; no bytes reported to the
                              // caller yet.
}
wtp::IoResult UsbCdcStream::read(std::span<std::uint8_t> bytes) {
  using wtp::IoState;
  if (bytes.empty())
    return {state_ == CdcState::Ready ? IoState::WouldBlock : IoState::Closed};
  try {
    const auto status = ready(false);
    if (status.state != IoState::Progress)
      return status;
    bytes = bytes.first(std::min(bytes.size(), io_chunk_bytes));
    const auto r = system_.read(fd_, bytes);
    if (r.count < 0 && transient(r.error))
      return {IoState::WouldBlock};
    if (r.count < 0 || r.error ||
        static_cast<std::size_t>(r.count) > bytes.size()) {
      fail("CDC read failed or returned an invalid count");
      return {IoState::Failed};
    }
    // POSIX permits zero for a nonblocking tty with no data. Hangup is
    // diagnosed by poll; a zero read alone cannot establish remote EOF.
    return r.count ? wtp::IoResult{IoState::Progress,
                                   static_cast<std::size_t>(r.count)}
                   : wtp::IoResult{IoState::WouldBlock};
  } catch (const std::exception &e) {
    fail(e.what());
    return {IoState::Failed};
  }
}
wtp::IoResult UsbCdcStream::write(std::span<const std::uint8_t> bytes) {
  using wtp::IoState;
  if (bytes.empty())
    return {state_ == CdcState::Ready ? IoState::WouldBlock : IoState::Closed};
  try {
    const auto status = ready(true);
    if (status.state != IoState::Progress)
      return status;
    bytes = bytes.first(std::min(bytes.size(), io_chunk_bytes));
    const auto r = system_.write(fd_, bytes);
    if (r.count < 0 && transient(r.error))
      return {IoState::WouldBlock};
    if (r.count <= 0 || r.error ||
        static_cast<std::size_t>(r.count) > bytes.size()) {
      fail("CDC write failed or returned an invalid count");
      return {IoState::Failed};
    }
    return {IoState::Progress, static_cast<std::size_t>(r.count)};
  } catch (const std::exception &e) {
    fail(e.what());
    return {IoState::Failed};
  }
}
} // namespace wsprrypi
