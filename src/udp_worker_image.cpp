// SPDX-License-Identifier: MIT
#include "udp_worker_image.hpp"
#ifndef _WIN32
#include "udp_worker_image.inc"
#include <unistd.h>
#endif
#include <cerrno>
#include <string_view>
std::optional<int> udp_worker_image_main(int argc, char **argv) {
  if (argc != 2 || std::string_view(argv[1]) != "--export-udp-logging-worker")
    return {};
#ifdef _WIN32
  return 3; // No native Windows UDP logging worker is advertised or bundled.
#else
  if (udp_worker_image.empty())
    return 3;
  std::size_t offset = 0;
  while (offset < udp_worker_image.size()) {
    const auto n = ::write(STDOUT_FILENO, udp_worker_image.data() + offset,
                           udp_worker_image.size() - offset);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return 1;
    offset += static_cast<std::size_t>(n);
  }
  return 0;
#endif
}
