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
    const WtpStationAddress original{"eth0", "192.168.1.10", 2};
    const WtpStationAddress recreated{"eth0", "192.168.1.10", 7};
    if (original == recreated ||
        wtp_select_station_address({recreated}, "eth0") != recreated ||
        wtp_select_station_address({original, recreated}, "eth0"))
        throw std::runtime_error("Interface recreation must preserve kernel identity and reject ambiguity");
}
