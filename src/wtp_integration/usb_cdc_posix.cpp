// SPDX-License-Identifier: MIT
#ifndef _WIN32
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "usb_cdc.hpp"
#include "usb_cdc_native.hpp"

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
      if (result) throw std::logic_error("Ambiguous USB role interface");
      result = PicoCdcSelection{candidate.path,candidate.usb_serial,
          candidate.vendor_id,candidate.product_id,role};
    } catch (const std::logic_error &) { throw; }
      catch (const std::exception &) { /* Other tty devices are not candidates. */ }
  }
  if (!result) throw std::runtime_error(role == PicoCdcRole::Logging
      ? "dedicated_logging_unsupported" : "Selected firmware has no paired USB management interface");
  return *result;
}

namespace {
PicoCdcSelection resolve_target_role(const PicoCdcSelection &selected, PicoCdcRole role,
    const std::filesystem::path &root, const std::filesystem::path &dev_root) {
  const auto identity = inspect_linux_cdc(selected.path, root);
  if (identity.usb_serial != selected.usb_serial ||
      identity.vendor_id != selected.vendor_id || identity.product_id != selected.product_id)
    throw std::runtime_error("Selected USB identity changed before role resolution");
  auto source = selected;
  if (identity.control_interface == static_cast<unsigned>(PicoCdcRole::Management))
    source.role = PicoCdcRole::Management;
  else if (identity.control_interface == static_cast<unsigned>(PicoCdcRole::Wtp))
    source.role = PicoCdcRole::Wtp;
  else
    throw std::runtime_error("Select a Pico management interface or an existing WTP locator");
  // Resolve even a direct selection through the inventory, retaining duplicate
  // role rejection and the exact physical-parent/serial/VID/PID checks.
  // An existing direct WTP locator need not enumerate a management sibling.
  if (role == PicoCdcRole::Wtp && source.role == role) return source;
  return paired_linux_cdc(source, role, root, dev_root);
}
} // namespace
PicoCdcSelection management_linux_cdc(const PicoCdcSelection &selected,
    const std::filesystem::path &root, const std::filesystem::path &dev_root) {
  return resolve_target_role(selected, PicoCdcRole::Management, root, dev_root);
}
PicoCdcSelection wtp_linux_cdc(const PicoCdcSelection &selected,
    const std::filesystem::path &root, const std::filesystem::path &dev_root) {
  return resolve_target_role(selected, PicoCdcRole::Wtp, root, dev_root);
}
PicoCdcSelection logging_linux_cdc(const PicoCdcSelection &selected,
    const std::filesystem::path &root, const std::filesystem::path &dev_root) {
  return resolve_target_role(selected, PicoCdcRole::Logging, root, dev_root);
}

CdcIdentity PosixCdcSystem::inspect(const std::string &path) {
  return inspect_native_cdc(path);
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
  const auto events=(descriptor.revents&POLLIN?CdcPollReadable:0)|
      (descriptor.revents&POLLOUT?CdcPollWritable:0)|
      (descriptor.revents&(POLLERR|POLLHUP|POLLNVAL)?CdcPollFailure:0);
  return {count,events,count<0?errno:0};
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


} // namespace wsprrypi
#endif
