// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
namespace wsprrypi {
inline constexpr std::string_view pi_pair_discovery_type="_wsprry-mgmt._tcp";
// A separate locator cache, not the WTP discovery store and never authority.
// Platform workers own browsing/resolution; reads never start network activity.
class PiPairDiscovery {
public:
    struct Key {
        unsigned interface_index{};
        std::string instance;
        int protocol{};
        auto operator<=>(const Key &) const = default;
    };
    using Clock=std::function<std::chrono::steady_clock::time_point()>;
    explicit PiPairDiscovery(std::string self_id,Clock=[] {return std::chrono::steady_clock::now();});
    void available(bool);
    bool resolved(const Key &,std::string domain,std::string host,unsigned srv_port,
        const std::vector<std::string> &txt,bool wide_area=false);
    void removed(const Key &);
    nlohmann::json snapshot();
    std::optional<nlohmann::json> candidate(const std::string &discovery_id);
private:
    struct Entry {nlohmann::json value;std::chrono::steady_clock::time_point observed;};
    void expire();
    std::string self_;
    Clock clock_;
    std::chrono::steady_clock::time_point last_;
    bool available_{};
    std::mutex mutex_;
    std::map<Key,Entry> entries_;
};
// Exact public P62-06 TXT record. No pair/window or credential state enters it.
std::vector<std::string> pi_pair_discovery_txt(const std::string &self,const std::string &boot,unsigned http_port);
// Read-only native DNS-SD browser. No publisher or connection is started by
// construction. Stop/join before destroying the cache; the owner calls stop.
class PiPairDiscoveryBrowser {
public:
    explicit PiPairDiscoveryBrowser(PiPairDiscovery &);
    ~PiPairDiscoveryBrowser();
    PiPairDiscoveryBrowser(const PiPairDiscoveryBrowser &)=delete;
    bool start();
    void stop() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
struct PiPairAnnouncement {
    std::string device_id,boot_id,name;
    unsigned http_port{};
    // Actual listener capability, not an operator preference or DNS fallback.
    bool bootstrap_ipv6{};
};
// Publishes only the approved private-address locator while its owning runtime
// reports both actual listeners and policy ready. No credential or window is
// consulted. Constructor/start never starts a listener or changes host services.
class PiPairDiscoveryPublisher {
public:
    // usable reads atomic owner readiness/policy flags; it must not acquire a
    // configuration/admission lock that its caller could hold while joining.
    PiPairDiscoveryPublisher(PiPairAnnouncement,std::function<bool()> usable);
    ~PiPairDiscoveryPublisher();
    PiPairDiscoveryPublisher(const PiPairDiscoveryPublisher &)=delete;
    bool start();
    void stop() noexcept;
    bool published() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace wsprrypi
