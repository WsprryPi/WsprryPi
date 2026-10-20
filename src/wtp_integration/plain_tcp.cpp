// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "plain_tcp.hpp"
#include "identity.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace wsprrypi {
PlainTcpStream::PlainTcpStream(Clock clock, Access access, std::unique_ptr<TlsResolver> resolver)
    : clock_(std::move(clock)), access_(access), resolver_(std::move(resolver)) {}
PlainTcpStream::~PlainTcpStream() { close(); }
void PlainTcpStream::state(std::string value, std::string diagnostic) {
  std::lock_guard lock(mutex_);
  status_.state = std::move(value);
  status_.diagnostic = std::move(diagnostic);
  status_.observed_ms = clock_();
}
void PlainTcpStream::close() noexcept {
  resolver_->cancel();
  std::lock_guard lock(mutex_);
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  addresses_.clear();
  next_address_ = 0;
  if (status_.state != "failed") status_.state = "closed";
}
void PlainTcpStream::fail(const char *why) {
  close();
  state("failed", why);
}
bool PlainTcpStream::begin_open(const std::string &host, unsigned port) {
  close();
  { std::lock_guard lock(mutex_); status_ = {}; }
  if (access_ == Access::Production) {
    const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
    if (disabled && std::string(disabled) == "1") {
      fail("Hardware access disabled; network transmitter access is prohibited");
      return false;
    }
  }
  const auto canonical = canonical_network_identity(host);
  if (!canonical || !port || port > 65535) { fail("Invalid plain TCP endpoint"); return false; }
  port_ = port;
  deadline_ = clock_() + resolve_timeout_ms;
  if (!resolver_->begin(*canonical, port)) { fail("Resolver unavailable"); return false; }
  state("resolving");
  return true;
}
void PlainTcpStream::connect_next() {
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  if (clock_() >= deadline_) { fail("TCP connection deadline exceeded"); return; }
  while (next_address_ < addresses_.size()) {
    const auto address = addresses_[next_address_++];
    sockaddr_storage storage{};
    socklen_t length{};
    bool loopback = false;
    auto &v4 = reinterpret_cast<sockaddr_in &>(storage);
    auto &v6 = reinterpret_cast<sockaddr_in6 &>(storage);
    if (inet_pton(AF_INET, address.c_str(), &v4.sin_addr) == 1) {
      v4.sin_family = AF_INET; v4.sin_port = htons(port_); length = sizeof(v4);
      loopback = (ntohl(v4.sin_addr.s_addr) >> 24) == 127;
    } else if (inet_pton(AF_INET6, address.c_str(), &v6.sin6_addr) == 1) {
      v6.sin6_family = AF_INET6; v6.sin6_port = htons(port_); length = sizeof(v6);
      loopback = IN6_IS_ADDR_LOOPBACK(&v6.sin6_addr);
    } else continue;
    if (access_ == Access::LoopbackTest && !loopback) {
      fail("Test TCP transport requires loopback addresses"); return;
    }
    fd_ = socket(storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) continue;
    if (fcntl(fd_, F_SETFL, O_NONBLOCK) || fcntl(fd_, F_SETFD, FD_CLOEXEC)) {
      ::close(fd_); fd_ = -1; continue;
    }
#ifdef SO_NOSIGPIPE
    int yes = 1;
    (void)setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    { std::lock_guard lock(mutex_); status_.address = address; }
    const auto result = ::connect(fd_, reinterpret_cast<sockaddr *>(&storage), length);
    if (result == 0 || errno == EINPROGRESS) { state("connecting"); return; }
    ::close(fd_); fd_ = -1;
  }
  fail("TCP endpoint connection failed");
}
void PlainTcpStream::poll_open() {
  if (!opening()) return;
  if (clock_() >= deadline_) { fail("TCP resolution or connection deadline exceeded"); return; }
  if (observation().state == "resolving") {
    auto result = resolver_->poll();
    if (!result) return;
    resolver_->cancel();
    addresses_ = std::move(*result);
    next_address_ = 0;
    deadline_ = clock_() + connect_timeout_ms;
    connect_next();
    return;
  }
  pollfd descriptor{fd_, POLLOUT, 0};
  const auto result = ::poll(&descriptor, 1, 0);
  if (!result || (result < 0 && errno == EINTR)) return;
  int error = 0;
  socklen_t size = sizeof(error);
  if (result < 0 || getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
    connect_next(); return;
  }
  state("ready");
}
bool PlainTcpStream::opening() const {
  const auto s = observation().state;
  return s == "resolving" || s == "connecting";
}
bool PlainTcpStream::ready() const { return observation().state == "ready"; }
TlsObservation PlainTcpStream::observation() const {
  std::lock_guard lock(mutex_);
  return status_;
}
wtp::IoResult PlainTcpStream::read(std::span<std::uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  if (status_.state != "ready" || fd_ < 0) return {wtp::IoState::Failed};
  if (bytes.empty()) return {wtp::IoState::WouldBlock};
  const auto count = recv(fd_, bytes.data(), bytes.size(), 0);
  if (count > 0) return {wtp::IoState::Progress, static_cast<std::size_t>(count)};
  if (!count) return {wtp::IoState::Closed};
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return {wtp::IoState::WouldBlock};
  return {wtp::IoState::Failed};
}
wtp::IoResult PlainTcpStream::write(std::span<const std::uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  if (status_.state != "ready" || fd_ < 0) return {wtp::IoState::Failed};
  if (bytes.empty()) return {wtp::IoState::WouldBlock};
#ifdef MSG_NOSIGNAL
  constexpr int flags = MSG_NOSIGNAL;
#else
  constexpr int flags = 0;
#endif
  const auto count = send(fd_, bytes.data(), bytes.size(), flags);
  if (count > 0) return {wtp::IoState::Progress, static_cast<std::size_t>(count)};
  if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return {wtp::IoState::WouldBlock};
  return {wtp::IoState::Failed};
}
} // namespace wsprrypi
