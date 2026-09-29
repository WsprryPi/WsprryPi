#include "wtp_endpoint/interface_selection.hpp"

#include <stdexcept>

int main() {
    using namespace wsprrypi;
    const std::vector<WtpStationAddress> one{{"wlan0", "192.168.1.10"}};
    if (!wtp_select_station_address(one, "auto") ||
        !wtp_select_station_address(one, "wlan0") ||
        wtp_select_station_address(one, "eth0"))
        throw std::runtime_error("Single station interface selection failed");
    const std::vector<WtpStationAddress> two{
        {"wlan0", "192.168.1.10"}, {"wlan1", "192.168.1.20"}};
    if (wtp_select_station_address(two, "auto") ||
        !wtp_select_station_address(two, "wlan1") ||
        wtp_select_station_address({}, "auto"))
        throw std::runtime_error("Ambiguous or absent station selection must fail closed");
}
