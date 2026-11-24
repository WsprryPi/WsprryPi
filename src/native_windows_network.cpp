// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "native_windows_network.hpp"
#include "privileged_network_policy.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace wsprrypi {
std::optional<std::vector<std::string>> windows_local_addresses(){
    if(!native_socket::initialize())return {};
    try{
        const auto require=[](bool valid){if(!valid)throw std::runtime_error("Native local addresses unavailable");};
        ULONG length=16384;std::vector<unsigned char> bytes;IP_ADAPTER_ADDRESSES *adapters{};bool complete{};
        for(unsigned attempt=0;attempt<4;++attempt){
            require(length>0&&length<=1048576);bytes.resize(length);adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES *>(bytes.data());
            const auto status=::GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,adapters,&length);
            if(status==ERROR_NO_DATA)return std::vector<std::string>{};
            if(status==ERROR_BUFFER_OVERFLOW)continue;
            require(status==NO_ERROR);complete=true;break;
        }
        require(complete);std::vector<std::string> result;unsigned scanned{},addresses{};
        for(auto *adapter=adapters;adapter;adapter=adapter->Next){
            require(++scanned<=4096);
            for(auto *entry=adapter->FirstUnicastAddress;entry;entry=entry->Next){
                require(++addresses<=16384);const auto *socket=entry->Address.lpSockaddr;
                require(socket&&entry->Address.iSockaddrLength>=static_cast<int>(sizeof(sockaddr)));
                const void *address{};
                if(socket->sa_family==AF_INET){require(entry->Address.iSockaddrLength>=static_cast<int>(sizeof(sockaddr_in)));address=&reinterpret_cast<const sockaddr_in *>(socket)->sin_addr;}
                else if(socket->sa_family==AF_INET6){require(entry->Address.iSockaddrLength>=static_cast<int>(sizeof(sockaddr_in6)));address=&reinterpret_cast<const sockaddr_in6 *>(socket)->sin6_addr;}
                else continue;
                std::array<char,INET6_ADDRSTRLEN> text{};require(::inet_ntop(socket->sa_family,address,text.data(),text.size())!=nullptr);result.emplace_back(text.data());
            }
        }
        return result;
    }catch(...){return {};}
}
namespace {
SupportRequestGuardSnapshot discover_networks(std::vector<WindowsLanAddress> *selected){
    SupportRequestGuardSnapshot result;
    if(!native_socket::initialize())return result;
    wchar_t hostname[256]{};DWORD hostname_size=static_cast<DWORD>(std::size(hostname));
    if(::GetComputerNameExW(ComputerNameDnsHostname,hostname,&hostname_size)&&hostname_size&&hostname_size<std::size(hostname)&&
        std::all_of(hostname,hostname+hostname_size,[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'.';}))
        for(DWORD index=0;index<hostname_size;++index)result.hostname+=static_cast<char>(hostname[index]);
    try{
        const auto require=[](bool value){if(!value)throw std::runtime_error("Native interface discovery unavailable");};
        ULONG length=16384;std::vector<unsigned char> bytes;IP_ADAPTER_ADDRESSES *adapters{};bool complete{};
        for(unsigned attempt=0;attempt<4;++attempt){
            require(length>0&&length<=1048576);bytes.resize(length);adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES *>(bytes.data());
            const auto status=::GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,adapters,&length);
            if(status==ERROR_NO_DATA){result.discovery_succeeded=true;return result;}
            if(status==ERROR_BUFFER_OVERFLOW)continue;
            require(status==NO_ERROR);complete=true;break;
        }
        require(complete);unsigned scanned{},addresses{};
        for(auto *adapter=adapters;adapter;adapter=adapter->Next){
            require(++scanned<=4096&&adapter->Length>=offsetof(IP_ADAPTER_ADDRESSES,Luid)+sizeof(adapter->Luid));
            MIB_IF_ROW2 row{};row.InterfaceLuid=adapter->Luid;
            require(::GetIfEntry2(&row)==NO_ERROR);
            if(!eligible_windows_interface(row))continue;
            for(auto *entry=adapter->FirstUnicastAddress;entry;entry=entry->Next){
                require(++addresses<=16384&&entry->Length>=offsetof(IP_ADAPTER_UNICAST_ADDRESS,OnLinkPrefixLength)+sizeof(entry->OnLinkPrefixLength));
                if(entry->DadState!=IpDadStatePreferred&&entry->DadState!=IpDadStateDeprecated)continue;
                const auto *socket=entry->Address.lpSockaddr;require(socket&&entry->Address.iSockaddrLength>=static_cast<int>(sizeof(sockaddr)));
                const void *address{};unsigned width{};
                if(socket->sa_family==AF_INET){
                    require(entry->Address.iSockaddrLength>=static_cast<int>(sizeof(sockaddr_in)));
                    address=&reinterpret_cast<const sockaddr_in *>(socket)->sin_addr;width=32;
                }else if(socket->sa_family==AF_INET6){
                    require(entry->Address.iSockaddrLength>=static_cast<int>(sizeof(sockaddr_in6)));
                    address=&reinterpret_cast<const sockaddr_in6 *>(socket)->sin6_addr;width=128;
                }else continue;
                const auto prefix=entry->OnLinkPrefixLength;require(prefix>0&&prefix<=width);
                std::array<unsigned char,16> mask{};
                for(unsigned bit=0;bit<prefix;++bit)mask[bit/8]|=static_cast<unsigned char>(0x80>>(bit%8));
                std::array<char,INET6_ADDRSTRLEN> address_text{},mask_text{};
                require(::inet_ntop(socket->sa_family,address,address_text.data(),address_text.size())&&
                    ::inet_ntop(socket->sa_family,mask.data(),mask_text.data(),mask_text.size()));
                const auto kind=row.Type==IF_TYPE_IEEE80211?PrivilegedInterfaceKind::wifi:PrivilegedInterfaceKind::ethernet;
                const PrivilegedInterfaceCandidate candidate{true,kind,false,false,false,false,false,false,address_text.data(),mask_text.data()};
                if(is_eligible_privileged_interface(candidate)){
                    result.networks.push_back({candidate.address,candidate.netmask});
                    if(selected){const auto index=socket->sa_family==AF_INET?adapter->IfIndex:adapter->Ipv6IfIndex;
                        require(index>0&&selected->size()<128);selected->push_back({index,socket->sa_family,candidate.address});}
                }
            }
        }
        result.discovery_succeeded=true;
    }catch(...){result.networks.clear();result.discovery_succeeded=false;}
    return result;
}
}
SupportRequestGuardSnapshot discover_windows_local_networks(){return discover_networks(nullptr);}
std::optional<std::vector<WindowsLanAddress>> windows_discovery_addresses(){
    std::vector<WindowsLanAddress> result;const auto snapshot=discover_networks(&result);
    if(!snapshot.discovery_succeeded)return {};
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
#endif
