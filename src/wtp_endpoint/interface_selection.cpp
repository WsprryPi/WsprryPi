#include "wtp_endpoint/interface_selection.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <cstdint>
#include <cstring>
#include <set>
#include <tuple>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/wireless.h>
#include <sys/ioctl.h>
#endif

namespace wsprrypi {
namespace {
bool private_ipv4(std::uint32_t host_order) {
    return (host_order & 0xff000000U) == 0x0a000000U ||
           (host_order & 0xfff00000U) == 0xac100000U ||
           (host_order & 0xffff0000U) == 0xc0a80000U;
}

bool operational_station_interface(const char* name) {
#ifdef __linux__
    if (!name || std::strlen(name) >= IFNAMSIZ) return false;
    const std::string directory = std::string("/sys/class/net/") + name;
    struct stat info{};
    // Listener admission is limited to physical LAN interfaces. Bridges,
    // tunnels and provisioning interfaces need a separately defined policy.
    if (stat((directory + "/device").c_str(), &info) != 0) return false;
    if (stat((directory + "/wireless").c_str(), &info) != 0) return true;
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    iwreq request{};
    std::strncpy(request.ifr_name, name, IFNAMSIZ - 1);
    const bool station = ioctl(fd, SIOCGIWMODE, &request) == 0 &&
                         request.u.mode == IW_MODE_INFRA;
    close(fd);
    return station;
#else
    (void)name;
    return false;
#endif
}
} // namespace

std::vector<WtpStationAddress> wtp_station_addresses() {
    std::vector<WtpStationAddress> result;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (auto* item = list; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_RUNNING) ||
            (item->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) ||
            !operational_station_interface(item->ifa_name))
            continue;
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        if (!private_ipv4(ntohl(ipv4->sin_addr.s_addr))) continue;
        char address[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) continue;
        const auto index = if_nametoindex(item->ifa_name);
        if (index) result.push_back({item->ifa_name, address, index});
    }
    freeifaddrs(list);
    return result;
}

std::optional<WtpStationAddress> wtp_select_station_address(
    const std::vector<WtpStationAddress>& addresses,
    const std::string& interface_name) {
    std::set<std::tuple<std::string, std::string, unsigned int>> matches;
    for (const auto& item : addresses)
        if (interface_name == "auto" || item.interface_name == interface_name)
            matches.emplace(item.interface_name, item.address, item.interface_index);
    if (matches.size() != 1) return std::nullopt;
    const auto& [name, address, index] = *matches.begin();
    return WtpStationAddress{name, address, index};
}
} // namespace wsprrypi
