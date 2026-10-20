// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "usb_cdc.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif
#include <system_error>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace wsprrypi {
namespace {
void checked(int result, const char *operation) {
  if (result < 0)
    throw std::system_error(errno, std::generic_category(), operation);
}
bool valid_text(const std::string &s, std::size_t maximum) {
  return !s.empty() && s.size() <= maximum &&
         std::none_of(s.begin(), s.end(),
                      [](unsigned char c) { return c < 32 || c == 127; });
}
std::string attribute(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Missing USB metadata: " + path.string());
  char buffer[258];
  in.read(buffer, sizeof(buffer));
  std::string value(buffer, static_cast<std::size_t>(in.gcount()));
  if (!in.eof() || in.bad())
    throw std::runtime_error("Oversized or unreadable USB metadata");
  if (!value.empty() && value.back() == '\n')
    value.pop_back();
  if (!valid_text(value, 256))
    throw std::runtime_error("Malformed USB metadata");
  return value;
}
unsigned hex_attribute(const std::filesystem::path &path, std::size_t digits) {
  auto value = attribute(path);
  unsigned result{};
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result, 16);
  if (value.size() != digits || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size())
    throw std::runtime_error("Malformed USB hexadecimal attribute");
  return result;
}
bool transient(int error) {
  return error == EINTR || error == EAGAIN || error == EWOULDBLOCK;
}
tcflag_t hardware_flow() {
  tcflag_t flags = 0;
#ifdef CRTSCTS
  flags |= CRTSCTS;
#endif
#ifdef CDTR_IFLOW
  flags |= CDTR_IFLOW;
#endif
#ifdef CDSR_OFLOW
  flags |= CDSR_OFLOW;
#endif
#ifdef CCAR_OFLOW
  flags |= CCAR_OFLOW;
#endif
  return flags;
}
} // namespace

CdcIdentity inspect_linux_cdc(const std::string &path,
                              const std::filesystem::path &root) {
  if (!valid_text(path, 4096) || !std::filesystem::path(path).is_absolute())
    throw std::runtime_error("An explicit absolute endpoint path is required");
  const auto canonical = std::filesystem::canonical(path);
  struct stat info{};
  checked(::stat(canonical.c_str(), &info), "stat selected endpoint");
  if (!S_ISCHR(info.st_mode))
    throw std::runtime_error("Selected endpoint is not a character device");
  const auto number = std::to_string(major(info.st_rdev)) + ":" +
                      std::to_string(minor(info.st_rdev));
  auto node = std::filesystem::canonical(root / "dev/char" / number);
  const auto devices = std::filesystem::canonical(root / "devices");
  // Do not infer USB parents from tty names or fixed directory depths.
  for (unsigned depth = 0;
       depth < 16 && node != devices && node.has_parent_path(); ++depth) {
    auto relative = node.lexically_relative(devices);
    if (relative.empty() || *relative.begin() == "..")
      throw std::runtime_error("USB metadata escaped sysfs devices");
    if (std::filesystem::exists(node / "bInterfaceNumber")) {
      const auto interface = hex_attribute(node / "bInterfaceNumber", 2);
      if (hex_attribute(node / "bInterfaceClass", 2) != 2 ||
          hex_attribute(node / "bInterfaceSubClass", 2) != 2 ||
          hex_attribute(node / "bInterfaceProtocol", 2) != 0 ||
          std::filesystem::canonical(node / "driver").filename() != "cdc_acm")
        throw std::runtime_error("Selected USB interface is not CDC ACM");
      const auto usb = node.parent_path();
      return {canonical.string(),
              node.string(),
              attribute(usb / "serial"),
              static_cast<std::uint64_t>(info.st_rdev),
              static_cast<std::uint16_t>(hex_attribute(usb / "idVendor", 4)),
              static_cast<std::uint16_t>(hex_attribute(usb / "idProduct", 4)),
              interface};
    }
    node = node.parent_path();
  }
  throw std::runtime_error(
      "Selected endpoint has no USB control-interface metadata");
}

PicoCdcSelection paired_linux_cdc(const PicoCdcSelection &selected,
    PicoCdcRole role, const std::filesystem::path &root,
    const std::filesystem::path &dev_root) {
  const auto source = inspect_linux_cdc(selected.path, root);
  if (source.control_interface != static_cast<unsigned>(selected.role) ||
      source.usb_serial != selected.usb_serial || source.vendor_id != selected.vendor_id ||
      source.product_id != selected.product_id)
    throw std::runtime_error("Selected USB identity changed before role pairing");
  const auto parent = std::filesystem::path(source.interface_path).parent_path();
  std::optional<PicoCdcSelection> result;
  for (const auto &entry : std::filesystem::directory_iterator(root / "class/tty")) {
    try {
      auto candidate = inspect_linux_cdc((dev_root / entry.path().filename()).string(), root);
      if (std::filesystem::path(candidate.interface_path).parent_path() != parent ||
          candidate.control_interface != static_cast<unsigned>(role) ||
          candidate.usb_serial != source.usb_serial || candidate.vendor_id != source.vendor_id ||
          candidate.product_id != source.product_id) continue;
      if (result) throw std::logic_error("Ambiguous USB management interface");
      result = PicoCdcSelection{candidate.path,candidate.usb_serial,
          candidate.vendor_id,candidate.product_id,role};
    } catch (const std::logic_error &) { throw; }
      catch (const std::exception &) { /* Other tty devices are not candidates. */ }
  }
  if (!result) throw std::runtime_error("Selected firmware has no paired USB management interface");
  return *result;
}

CdcIdentity PosixCdcSystem::inspect(const std::string &path) {
#ifdef __linux__
  return inspect_linux_cdc(path);
#else
  (void)path;
  throw std::runtime_error(
      "Native USB CDC identity resolution is available only on Linux");
#endif
}
int PosixCdcSystem::open(const std::string &path, int flags) {
  const int fd = ::open(path.c_str(), flags);
  checked(fd, "open selected CDC endpoint");
  return fd;
}
void PosixCdcSystem::verify(int fd, const CdcIdentity &identity) {
  struct stat info{};
  checked(::fstat(fd, &info), "fstat CDC descriptor");
  if (!S_ISCHR(info.st_mode) ||
      static_cast<std::uint64_t>(info.st_rdev) != identity.device_number ||
      inspect(identity.path) != identity)
    throw std::runtime_error("Selected USB endpoint changed during open");
}
void PosixCdcSystem::exclusive(int fd) {
  checked(::flock(fd, LOCK_EX | LOCK_NB), "lock CDC endpoint");
  checked(::ioctl(fd, TIOCEXCL), "exclusive CDC endpoint");
}
void PosixCdcSystem::unexclusive(int fd) {
  checked(::ioctl(fd, TIOCNXCL), "release CDC exclusivity");
}
void PosixCdcSystem::raw(int fd) {
  termios settings{};
  checked(::tcgetattr(fd, &settings), "read CDC terminal settings");
  ::cfmakeraw(&settings);
  settings.c_iflag &= ~(IGNBRK | IXON | IXOFF | IXANY);
  const auto flow = hardware_flow();
  settings.c_cflag &= ~(CSIZE | PARENB | CSTOPB | flow);
  settings.c_cflag |= CS8 | CLOCAL | CREAD | HUPCL;
  settings.c_cc[VMIN] = 1;
  settings.c_cc[VTIME] = 0;
  checked(::cfsetispeed(&settings, B115200), "set CDC input baud");
  checked(::cfsetospeed(&settings, B115200), "set CDC output baud");
  checked(::tcsetattr(fd, TCSANOW, &settings), "set CDC raw mode");
  termios actual{};
  checked(::tcgetattr(fd, &actual), "verify CDC raw mode");
  if ((actual.c_iflag & (IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                         ICRNL | IXON | IXOFF | IXANY)) ||
      (actual.c_oflag & OPOST) ||
      (actual.c_lflag & (ECHO | ECHONL | ICANON | ISIG | IEXTEN)) ||
      (actual.c_cflag & (CSIZE | PARENB | CSTOPB | flow | CLOCAL | CREAD |
                         HUPCL)) != (CS8 | CLOCAL | CREAD | HUPCL) ||
      actual.c_cc[VMIN] != 1 || actual.c_cc[VTIME] != 0 ||
      ::cfgetispeed(&actual) != B115200 || ::cfgetospeed(&actual) != B115200)
    throw std::runtime_error(
        "CDC driver did not accept binary nonblocking terminal settings");
}
void PosixCdcSystem::dtr(int fd, bool asserted) {
  int bit = TIOCM_DTR;
  checked(::ioctl(fd, asserted ? TIOCMBIS : TIOCMBIC, &bit), "set CDC DTR");
}
void PosixCdcSystem::discard(int fd) {
  checked(::tcflush(fd, TCIOFLUSH), "discard CDC queues");
}
CdcPoll PosixCdcSystem::poll(int fd, bool writing) {
  pollfd descriptor{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
  const int count = ::poll(&descriptor, 1, 0);
  return {count, descriptor.revents, count < 0 ? errno : 0};
}
CdcTransfer PosixCdcSystem::read(int fd, std::span<std::uint8_t> bytes) {
  const auto count = ::read(fd, bytes.data(), bytes.size());
  return {count, count < 0 ? errno : 0};
}
CdcTransfer PosixCdcSystem::write(int fd, std::span<const std::uint8_t> bytes) {
  const auto count = ::write(fd, bytes.data(), bytes.size());
  return {count, count < 0 ? errno : 0};
}
int PosixCdcSystem::close(int fd) noexcept {
  return ::close(fd) < 0 ? errno : 0;
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
        !selection.product_id || (selection.role != PicoCdcRole::Wtp && selection.role != PicoCdcRole::Management))
      throw std::runtime_error(
          "Explicit endpoint, USB serial and VID/PID are required");
    const auto identity = system_.inspect(selection.path);
    if (identity.usb_serial != selection.usb_serial ||
        identity.vendor_id != selection.vendor_id ||
        identity.product_id != selection.product_id ||
        identity.control_interface != static_cast<unsigned>(selection.role))
      throw std::runtime_error(
          "Selected port does not match the expected Pico USB role");
    fd_ = system_.open(identity.path,
                       O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
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
      (p.events & (POLLERR | POLLHUP | POLLNVAL))) {
    fail("CDC readiness failed or endpoint disconnected");
    return {IoState::Failed};
  }
  if (!p.count || !(p.events & (writing ? POLLOUT : POLLIN)))
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
