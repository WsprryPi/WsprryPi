// SPDX-License-Identifier: MIT
#include "native_windows_network.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value){if(!value)throw std::runtime_error("Native Windows interface admission failed");}
MIB_IF_ROW2 physical(){
    MIB_IF_ROW2 row{};row.Type=IF_TYPE_ETHERNET_CSMACD;row.TunnelType=TUNNEL_TYPE_NONE;
    row.OperStatus=IfOperStatusUp;row.AdminStatus=NET_IF_ADMIN_STATUS_UP;row.MediaConnectState=MediaConnectStateConnected;
    row.DirectionType=NET_IF_DIRECTION_SENDRECEIVE;row.AccessType=NET_IF_ACCESS_BROADCAST;
    row.InterfaceAndOperStatusFlags.HardwareInterface=TRUE;row.InterfaceAndOperStatusFlags.ConnectorPresent=TRUE;return row;
}
}
int main(){try{
    const auto valid=physical();check(wsprrypi::eligible_windows_interface(valid));auto wifi=valid;wifi.Type=IF_TYPE_IEEE80211;check(wsprrypi::eligible_windows_interface(wifi));
    auto refused=[&](auto change){auto value=valid;change(value);check(!wsprrypi::eligible_windows_interface(value));};
    refused([](auto &r){r.Type=IF_TYPE_SOFTWARE_LOOPBACK;});refused([](auto &r){r.Type=IF_TYPE_TUNNEL;});refused([](auto &r){r.Type=IF_TYPE_PPP;});
    refused([](auto &r){r.InterfaceAndOperStatusFlags.HardwareInterface=FALSE;});refused([](auto &r){r.InterfaceAndOperStatusFlags.ConnectorPresent=FALSE;});
    refused([](auto &r){r.InterfaceAndOperStatusFlags.FilterInterface=TRUE;});refused([](auto &r){r.InterfaceAndOperStatusFlags.EndPointInterface=TRUE;});
    refused([](auto &r){r.InterfaceAndOperStatusFlags.NotAuthenticated=TRUE;});refused([](auto &r){r.InterfaceAndOperStatusFlags.NotMediaConnected=TRUE;});
    refused([](auto &r){r.InterfaceAndOperStatusFlags.Paused=TRUE;});refused([](auto &r){r.InterfaceAndOperStatusFlags.LowPower=TRUE;});
    refused([](auto &r){r.OperStatus=IfOperStatusDown;});refused([](auto &r){r.AdminStatus=NET_IF_ADMIN_STATUS_DOWN;});
    refused([](auto &r){r.MediaConnectState=MediaConnectStateDisconnected;});refused([](auto &r){r.AccessType=NET_IF_ACCESS_POINT_TO_POINT;});
    const auto dns_addresses=wsprrypi::windows_discovery_addresses();
    if(dns_addresses)for(const auto &address:*dns_addresses)check(address.interface_index>0&&(address.family==AF_INET||address.family==AF_INET6)&&!address.address.empty());
    const auto snapshot=SupportRequestGuard::discover_local_networks();
    check(snapshot.discovery_succeeded||snapshot.networks.empty());
    for(const auto &network:snapshot.networks)check(!network.address.empty()&&!network.netmask.empty());
    const auto local=wsprrypi::windows_local_addresses();check(local.has_value()&&!local->empty()&&local->size()<=16384);
    check(std::find(local->begin(),local->end(),"127.0.0.1")!=local->end()||std::find(local->begin(),local->end(),"::1")!=local->end());
    for(const auto &address:*local){in_addr ipv4{};in6_addr ipv6{};check(inet_pton(AF_INET,address.c_str(),&ipv4)==1||inet_pton(AF_INET6,address.c_str(),&ipv6)==1);}
    const SupportRequestGuard unavailable({false,"controller",{}, {}});
    check(!unavailable.evaluate("192.168.50.3","controller",{}).allowed());
    check(unavailable.evaluate("127.0.0.1","localhost",{}).allowed());
    std::cout<<"PASS: Windows physical-interface policy, virtual/tunnel/down/authentication exclusion, native read-only inventory, and unavailable-network refusal\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
