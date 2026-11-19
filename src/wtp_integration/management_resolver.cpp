// SPDX-License-Identifier: MIT
#include "management_resolver.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <array>
#include <cstring>
#include <set>

namespace wsprrypi {
namespace {
std::optional<std::vector<std::string>> self_addresses() {
    ifaddrs *list{};
    if (getifaddrs(&list)) return {};
    struct Release {ifaddrs *p;~Release(){freeifaddrs(p);}} release{list};
    std::vector<std::string> result;
    for (auto *entry=list;entry;entry=entry->ifa_next) {
        if (!entry->ifa_addr) continue;
        const auto family=entry->ifa_addr->sa_family;
        std::array<char,INET6_ADDRSTRLEN> text{};
        const void *address = family == AF_INET ? static_cast<const void *>(&reinterpret_cast<sockaddr_in *>(entry->ifa_addr)->sin_addr)
            : family == AF_INET6 ? static_cast<const void *>(&reinterpret_cast<sockaddr_in6 *>(entry->ifa_addr)->sin6_addr) : nullptr;
        if (address && inet_ntop(family,address,text.data(),text.size())) result.emplace_back(text.data());
    }
    return result;
}
class ManagementResolver final : public TlsResolver {
public:
    ManagementResolver(std::unique_ptr<TlsResolver> resolver,
        std::function<std::optional<std::vector<std::string>>()> local)
        : resolver_(std::move(resolver)), local_(std::move(local)) {}
    bool begin(const std::string &host,unsigned port) override {return resolver_->begin(host,port);}
    void cancel() noexcept override {resolver_->cancel();}
    std::optional<std::vector<std::string>> poll() override {
        auto answers=resolver_->poll();
        if (!answers) return {};
        const auto local=local_();
        if (!local || answers->empty() || answers->size()>16) return std::vector<std::string>{};
        for (const auto &address:*answers)
            if (!private_management_address(address,*local)) return std::vector<std::string>{};
        // One validated address per connection: no hidden address fallback after
        // a failed connect. The next explicit operation resolves afresh.
        return std::vector<std::string>{answers->front()};
    }
private:
    std::unique_ptr<TlsResolver> resolver_;
    std::function<std::optional<std::vector<std::string>>()> local_;
};
}
bool private_management_address(const std::string &text,const std::vector<std::string> &local) {
    in_addr ipv4{};
    in6_addr ipv6{};
    if (inet_pton(AF_INET,text.c_str(),&ipv4)==1) {
        const auto value=ntohl(ipv4.s_addr);
        if (!((value&0xff000000U)==0x0a000000U || (value&0xfff00000U)==0xac100000U ||
              (value&0xffff0000U)==0xc0a80000U)) return false;
        for (const auto &self:local) {in_addr candidate{};if(inet_pton(AF_INET,self.c_str(),&candidate)==1 && candidate.s_addr==ipv4.s_addr)return false;}
        return true;
    }
    if (inet_pton(AF_INET6,text.c_str(),&ipv6)!=1 || (ipv6.s6_addr[0]&0xfe)!=0xfc || IN6_IS_ADDR_V4MAPPED(&ipv6)) return false;
    for (const auto &self:local) {in6_addr candidate{};if(inet_pton(AF_INET6,self.c_str(),&candidate)==1 && !std::memcmp(&candidate,&ipv6,sizeof(ipv6)))return false;}
    return true;
}
std::unique_ptr<TlsResolver> management_tls_resolver() {
    return std::make_unique<ManagementResolver>(system_tls_resolver(),self_addresses);
}
std::unique_ptr<TlsResolver> management_tls_resolver_for_test(std::unique_ptr<TlsResolver> resolver,
    std::function<std::optional<std::vector<std::string>>()> local) {
    return std::make_unique<ManagementResolver>(std::move(resolver),std::move(local));
}
} // namespace wsprrypi
