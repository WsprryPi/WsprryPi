// SPDX-License-Identifier: MIT
#pragma once
#include "udp_logging/contract.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi::udp {
inline std::string read_file(const std::string &path, std::size_t maximum,
                             uid_t owner, bool missing = false) {
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    if (missing && errno == ENOENT)
      return {};
    throw std::runtime_error("udp_file_unavailable");
  }
  struct stat info{};
  if (::fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != owner ||
      (info.st_mode & 0022) || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) > maximum) {
    ::close(fd);
    throw std::runtime_error("udp_unsafe_file");
  }
  std::string result(static_cast<std::size_t>(info.st_size), '\0');
  std::size_t offset = 0;
  while (offset < result.size()) {
    const auto count =
        ::read(fd, result.data() + offset, result.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      ::close(fd);
      throw std::runtime_error("udp_file_read");
    }
    offset += static_cast<std::size_t>(count);
  }
  char extra{};
  const auto tail = ::read(fd, &extra, 1);
  ::close(fd);
  require(tail == 0, "udp_file_changed");
  return result;
}
inline void atomic_file(const std::string &path, const std::string &data,
                        mode_t mode, gid_t group = static_cast<gid_t>(-1)) {
  const auto at = path.rfind('/');
  const auto directory = at == path.npos ? "." : path.substr(0, at);
  auto pattern = path + ".new-XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back(0);
  int fd = ::mkstemp(name.data());
  require(fd >= 0, "udp_file_prepare");
  try {
    require(::fchmod(fd, mode) == 0 && (group == static_cast<gid_t>(-1) ||
                                        ::fchown(fd, ::getuid(), group) == 0),
            "udp_file_permissions");
    std::size_t offset = 0;
    while (offset < data.size()) {
      const auto n = ::write(fd, data.data() + offset, data.size() - offset);
      if (n < 0 && errno == EINTR)
        continue;
      require(n > 0, "udp_file_write");
      offset += static_cast<std::size_t>(n);
    }
    require(::fsync(fd) == 0, "udp_file_sync");
    ::close(fd);
    fd = -1;
    require(::rename(name.data(), path.c_str()) == 0, "udp_file_commit");
    const int dir = ::open(directory.c_str(),
                           O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    require(dir >= 0, "udp_directory_open");
    const auto synced = ::fsync(dir);
    ::close(dir);
    require(synced == 0, "udp_directory_sync");
  } catch (...) {
    if (fd >= 0)
      ::close(fd);
    ::unlink(name.data());
    throw;
  }
}
} // namespace wsprrypi::udp
