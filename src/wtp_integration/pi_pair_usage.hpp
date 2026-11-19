// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_tls.hpp"
#include <chrono>
#include <map>
#include <tuple>
namespace wsprrypi {
// Device-owned RAM accounting only. The history owner serializes access and
// projects counters solely at approved transaction/orderly-shutdown boundaries.
// Reads and Finished accounting perform no file I/O and grant no authority.
class PiPairUsage {
public:
    using Time=std::chrono::steady_clock::time_point;
    using Clock=std::function<Time()>;
    struct Projection {nlohmann::json catalog,peers;};
    static constexpr std::uint64_t service_limit_ms=30ULL*24*60*60*1000;
    static constexpr std::uint64_t connection_limit=1ULL<<20;
    PiPairUsage(std::string self,Clock=[] {return std::chrono::steady_clock::now();});
    void begin(const nlohmann::json &catalog,const nlohmann::json &peers,bool unclean);
    void reconcile(const nlohmann::json &catalog,const nlohmann::json &peers);
    void authenticated(const PiPairTlsIdentity &);
    bool due(const PiPairTlsIdentity &) noexcept;
    Projection project(const nlohmann::json &catalog,const nlohmann::json &peers);
    // An old epoch made due by an unclean start must not lose that hold merely
    // because an offline service subsequently stops in an orderly fashion.
    bool unclean_checkpoint_required() const noexcept;
    void stop() noexcept;
private:
    using Key=std::tuple<std::string,std::string,std::string>;
    struct Entry {std::uint64_t service{},connections{};Time born;bool forced{};};
    Time now();
    void refresh(const nlohmann::json &,const nlohmann::json &,Time,bool startup,bool unclean);
    static Key key(const PiPairTlsIdentity &);
    std::uint64_t service(const Entry &,Time) const;
    std::string self_;
    Clock clock_;
    std::map<Key,Entry> entries_;
    Time last_{};
    bool running_{},failed_{};
};
}
