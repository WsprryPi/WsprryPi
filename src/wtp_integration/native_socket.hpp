// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <climits>
#include <cstddef>

namespace wsprrypi::native_socket {
#ifdef _WIN32
using Handle = SOCKET;
using Length = int;
inline constexpr Handle invalid = INVALID_SOCKET;
inline bool initialize() noexcept {
  // Resolution can outlive its caller on a detached worker. Keep Winsock alive
  // until process teardown, rather than cleaning it up beneath that worker.
  static const bool ready = [] {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ready;
}
inline int error() noexcept { return WSAGetLastError(); }
inline bool interrupted(int value) noexcept { return value == WSAEINTR; }
inline bool pending(int value) noexcept {
  return value == WSAEWOULDBLOCK || value == WSAEINPROGRESS;
}
inline void close(Handle fd) noexcept { if (fd != invalid) closesocket(fd); }
#else
using Handle = int;
using Length = socklen_t;
inline constexpr Handle invalid = -1;
inline bool initialize() noexcept { return true; }
inline int error() noexcept { return errno; }
inline bool interrupted(int value) noexcept { return value == EINTR; }
inline bool pending(int value) noexcept {
  return value == EAGAIN || value == EWOULDBLOCK || value == EINPROGRESS;
}
inline void close(Handle fd) noexcept { if (fd != invalid) ::close(fd); }
#endif
inline bool retry(int value) noexcept { return pending(value) || interrupted(value); }
inline bool configure_stream(Handle fd) noexcept {
  if(fd==invalid)return false;
#ifdef _WIN32
  u_long nonblocking=1;
  return !ioctlsocket(fd,FIONBIO,&nonblocking)&&
      SetHandleInformation(reinterpret_cast<HANDLE>(fd),HANDLE_FLAG_INHERIT,0)!=0;
#else
  const auto flags=fcntl(fd,F_GETFL),descriptor=fcntl(fd,F_GETFD);
  if(flags<0||descriptor<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,descriptor|FD_CLOEXEC))return false;
#ifdef SO_NOSIGPIPE
  int yes=1;if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes))return false;
#endif
  return true;
#endif
}
#ifdef _WIN32
using PollItem=WSAPOLLFD;
#else
using PollItem=pollfd;
#endif
inline constexpr short readable=POLLIN,writable=POLLOUT,failed=POLLERR|POLLHUP|POLLNVAL;
inline int poll(PollItem *items,std::size_t count,int timeout) noexcept {
#ifdef _WIN32
  if(count>static_cast<std::size_t>(ULONG_MAX)){WSASetLastError(WSAEINVAL);return -1;}
  return WSAPoll(items,static_cast<ULONG>(count),timeout);
#else
  return ::poll(items,count,timeout);
#endif
}
inline int option(Handle fd,int level,int name,const void *value,Length length) noexcept {
#ifdef _WIN32
  return setsockopt(fd,level,name,static_cast<const char *>(value),length);
#else
  return setsockopt(fd,level,name,value,length);
#endif
}
inline void shutdown(Handle fd) noexcept {
  if(fd==invalid)return;
#ifdef _WIN32
  (void)::shutdown(fd,SD_BOTH);
#else
  (void)::shutdown(fd,SHUT_RDWR);
#endif
}
inline bool exclusive_listener(Handle fd) noexcept {
  const int yes=1;
#ifdef _WIN32
  return !option(fd,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,&yes,sizeof yes);
#else
  return !option(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
#endif
}
inline bool unsupported_family(int value) noexcept {
#ifdef _WIN32
  return value==WSAEAFNOSUPPORT||value==WSAEPROTONOSUPPORT;
#else
  return value==EAFNOSUPPORT||value==EPROTONOSUPPORT;
#endif
}
inline Handle open_stream(int family) noexcept {
  if (!initialize()) return invalid;
#ifdef _WIN32
  const auto fd = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                            WSA_FLAG_NO_HANDLE_INHERIT);
  if (fd == invalid) return invalid;
  u_long nonblocking = 1;
  if (ioctlsocket(fd, FIONBIO, &nonblocking)) { close(fd); return invalid; }
#else
  const auto fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (fd == invalid) return invalid;
  if (fcntl(fd, F_SETFL, O_NONBLOCK) || fcntl(fd, F_SETFD, FD_CLOEXEC)) {
    close(fd); return invalid;
  }
#ifdef SO_NOSIGPIPE
  int yes = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes))) {
    close(fd); return invalid;
  }
#endif
#endif
  return fd;
}
// Poll readiness separately from SO_ERROR: a failed connection is writable too.
inline int poll_write(Handle fd) noexcept {
#ifdef _WIN32
  WSAPOLLFD descriptor{fd, POLLWRNORM, 0};
  return WSAPoll(&descriptor, 1, 0);
#else
  pollfd descriptor{fd, POLLOUT, 0};
  return ::poll(&descriptor, 1, 0);
#endif
}
inline bool connected(Handle fd) noexcept {
  int value = 0;
  Length length = sizeof(value);
#ifdef _WIN32
  return getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&value), &length) == 0 && !value;
#else
  return getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &length) == 0 && !value;
#endif
}
inline auto receive(Handle fd, void *bytes, std::size_t size) noexcept {
  return recv(fd, static_cast<char *>(bytes), static_cast<int>(std::min(size, static_cast<std::size_t>(INT_MAX))), 0);
}
inline auto send(Handle fd, const void *bytes, std::size_t size) noexcept {
#ifdef MSG_NOSIGNAL
  constexpr int flags = MSG_NOSIGNAL;
#else
  constexpr int flags = 0;
#endif
  return ::send(fd, static_cast<const char *>(bytes), static_cast<int>(std::min(size, static_cast<std::size_t>(INT_MAX))), flags);
}
} // namespace wsprrypi::native_socket
