#pragma once

#include <optional>
#include <string>
#include <vector>

namespace wsprrypi {

struct WtpStationAddress {
    std::string interface_name;
    std::string address;
};

// Only active RFC1918 IPv4 station addresses are candidates for a Plain LAN
// listener. A selection with multiple addresses fails closed.
std::vector<WtpStationAddress> wtp_station_addresses();
std::optional<WtpStationAddress> wtp_select_station_address(
    const std::vector<WtpStationAddress>& addresses,
    const std::string& interface_name);

} // namespace wsprrypi
