// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
namespace wsprrypi::udp {
inline std::optional<std::string>
bounded_kernel_text(const std::filesystem::path &path, std::size_t maximum) {
  std::ifstream input(path);
  if (!input)
    return {};
  std::string value;
  for (char c; input.get(c);) {
    if (value.size() == maximum)
      return {};
    value += c;
  }
  if (!input.eof())
    return {};
  return value;
}
inline bool
resource_controllers_available(const std::filesystem::path &root = "/") {
  const auto unified =
      bounded_kernel_text(root / "sys/fs/cgroup/cgroup.controllers", 4096);
  if (unified) {
    std::istringstream words(*unified);
    std::string word;
    bool memory = false, pids = false;
    while (words >> word) {
      memory |= word == "memory";
      pids |= word == "pids";
    }
    return memory && pids;
  }
  const auto legacy = bounded_kernel_text(root / "proc/cgroups", 4096);
  if (!legacy)
    return false;
  std::istringstream lines(*legacy);
  std::string line;
  bool memory = false, pids = false;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string name;
    unsigned hierarchy{}, groups{}, enabled{};
    if (fields >> name >> hierarchy >> groups >> enabled) {
      memory |= name == "memory" && enabled == 1;
      pids |= name == "pids" && enabled == 1;
    }
  }
  return memory && pids &&
         std::filesystem::is_directory(root / "sys/fs/cgroup/memory") &&
         std::filesystem::is_directory(root / "sys/fs/cgroup/pids");
}
inline bool kernel_limit_at_most(const std::filesystem::path &path,
                                 std::uint64_t maximum) {
  auto text = bounded_kernel_text(path, 32);
  if (!text)
    return false;
  while (!text->empty() && (text->back() == '\n' || text->back() == ' '))
    text->pop_back();
  std::uint64_t value{};
  const auto result =
      std::from_chars(text->data(), text->data() + text->size(), value);
  return result.ec == std::errc{} &&
         result.ptr == text->data() + text->size() && value > 0 &&
         value <= maximum;
}
inline bool resource_limits_enforced(const std::filesystem::path &root = "/") {
  const auto membership = bounded_kernel_text(root / "proc/self/cgroup", 16384);
  if (!membership)
    return false;
  std::istringstream lines(*membership);
  std::string line;
  bool memory = false, pids = false;
  while (std::getline(lines, line)) {
    const auto first = line.find(':'), second = first == line.npos
                                                    ? line.npos
                                                    : line.find(':', first + 1);
    if (second == line.npos)
      return false;
    const auto controllers = line.substr(first + 1, second - first - 1),
               path = line.substr(second + 1);
    if (path.empty() || path.front() != '/' || path.find("..") != path.npos ||
        path.find_first_of("\r\n\0", 0, 3) != path.npos)
      return false;
    const auto relative = path.substr(1);
    if (controllers.empty()) {
      const auto base = root / "sys/fs/cgroup" / relative;
      memory = kernel_limit_at_most(base / "memory.max", 33554432);
      pids = kernel_limit_at_most(base / "pids.max", 2);
    } else {
      const auto list = "," + controllers + ",";
      if (list.find(",memory,") != list.npos)
        memory = kernel_limit_at_most(root / "sys/fs/cgroup/memory" / relative /
                                          "memory.limit_in_bytes",
                                      33554432);
      if (list.find(",pids,") != list.npos)
        pids = kernel_limit_at_most(
            root / "sys/fs/cgroup/pids" / relative / "pids.max", 2);
    }
  }
  return memory && pids;
}
} // namespace wsprrypi::udp
