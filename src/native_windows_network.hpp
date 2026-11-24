// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "wtp_integration/native_socket.hpp"
#include "support_request_guard.hpp"
#include <iphlpapi.h>
#include <netioapi.h>
namespace wsprrypi {
inline bool eligible_windows_interface(const MIB_IF_ROW2 &row){
    const auto &flags=row.InterfaceAndOperStatusFlags;
    return (row.Type==IF_TYPE_ETHERNET_CSMACD||row.Type==IF_TYPE_IEEE80211)&&row.TunnelType==TUNNEL_TYPE_NONE&&
        row.OperStatus==IfOperStatusUp&&row.AdminStatus==NET_IF_ADMIN_STATUS_UP&&row.MediaConnectState==MediaConnectStateConnected&&
        row.DirectionType==NET_IF_DIRECTION_SENDRECEIVE&&row.AccessType==NET_IF_ACCESS_BROADCAST&&
        flags.HardwareInterface&&flags.ConnectorPresent&&!flags.FilterInterface&&!flags.EndPointInterface&&
        !flags.NotAuthenticated&&!flags.NotMediaConnected&&!flags.Paused&&!flags.LowPower;
}
SupportRequestGuardSnapshot discover_windows_local_networks();
struct WindowsLanAddress {unsigned interface_index{};int family{};std::string address;auto operator<=>(const WindowsLanAddress &)const=default;};
// Same admitted physical interfaces and private subnets as the host guard.
std::optional<std::vector<WindowsLanAddress>> windows_discovery_addresses();
// Self-connection exclusion needs every local unicast address, including VPN,
// virtual, and loopback interfaces that are ineligible for LAN admission.
std::optional<std::vector<std::string>> windows_local_addresses();
}
#endif
