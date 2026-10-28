// SPDX-License-Identifier: MIT
#pragma once
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <net/if.h>
#include <optional>
#include <sstream>
#include <string_view>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
namespace wsprrypi::udp {
// Exclude reference/use counters: traffic does not change route identity.
// The caller reads at most 64 KiB; oversized inventories fail closed.
inline std::optional<std::string>
route_generation(std::string_view table, const std::string &interface) {
  if (table.size() >= 65536)
    return std::nullopt;
  std::istringstream lines{std::string(table)};
  std::string line, result;
  while (std::getline(lines, line)) {
    if (line.size() > 1024)
      return std::nullopt;
    std::istringstream fields(line);
    std::array<std::string, 11> values;
    for (auto &value : values)
      fields >> value;
    if (values[0] != interface)
      continue;
    if (values.back().empty())
      return std::nullopt;
    for (unsigned i : {0U, 1U, 2U, 3U, 6U, 7U, 8U, 9U, 10U}) {
      result += values[i];
      result += '\t';
    }
    result += '\n';
  }
  return result;
}
struct InterfaceInventory {
  std::optional<in_addr> address;
  bool valid = false, destination_allowed = false;
};
inline InterfaceInventory
interface_inventory(const std::string &name,
                    std::optional<std::uint32_t> destination = {}) {
  // getifaddrs normally opens AF_NETLINK, which this service deliberately
  // cannot use. Inventory IPv4 addresses through the permitted ioctl socket.
  const int inventory = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (inventory < 0)
    return {};
  std::array<ifreq, 64> entries{};
  ifconf config{};
  config.ifc_len = sizeof(entries);
  config.ifc_req = entries.data();
  if (ioctl(inventory, SIOCGIFCONF, &config) ||
      config.ifc_len >= static_cast<int>(sizeof(entries))) {
    ::close(inventory);
    return {};
  }
  std::optional<in_addr> result;
  bool forbidden = false;
  const auto count = static_cast<std::size_t>(config.ifc_len) / sizeof(ifreq);
  for (std::size_t i = 0; i < count; ++i) {
    auto entry = entries[i];
    if (entry.ifr_addr.sa_family != AF_INET)
      continue;
    const std::string interface(entry.ifr_name,
                                strnlen(entry.ifr_name, IFNAMSIZ));
    const auto address =
        reinterpret_cast<sockaddr_in *>(&entry.ifr_addr)->sin_addr;
    const auto ip = ntohl(address.s_addr);
    auto query = entry;
    if (ioctl(inventory, SIOCGIFNETMASK, &query)) {
      forbidden = true;
      continue;
    }
    const auto mask = ntohl(
        reinterpret_cast<sockaddr_in *>(&query.ifr_netmask)->sin_addr.s_addr);
    query = entry;
    if (ioctl(inventory, SIOCGIFFLAGS, &query)) {
      forbidden = true;
      continue;
    }
    if (destination &&
        (*destination == ip || (mask && *destination == (ip | ~mask))))
      forbidden = true;
    if (name == interface && (query.ifr_flags & IFF_UP) &&
        !(query.ifr_flags & IFF_LOOPBACK)) {
      if (destination && (*destination & 0xffff0000U) == 0xa9fe0000U &&
          (*destination & mask) != (ip & mask))
        forbidden = true;
      if (result && result->s_addr != address.s_addr)
        forbidden = true;
      else
        result = address;
    }
  }
  ::close(inventory);
  if (destination && (*destination & 0xffff0000U) == 0xa9fe0000U && !result)
    forbidden = true;
  return {result, true, !forbidden};
}
inline std::optional<in_addr>
interface_address(const std::string &name,
                  std::optional<std::uint32_t> destination = {}) {
  const auto result = interface_inventory(name, destination);
  return result.valid && result.destination_allowed ? result.address
                                                    : std::nullopt;
}
} // namespace wsprrypi::udp
