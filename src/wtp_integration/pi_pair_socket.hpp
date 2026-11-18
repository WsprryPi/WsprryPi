// SPDX-License-Identifier: MIT
#pragma once
#include "WTP-Client/include/wtp/transport.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
namespace wsprrypi {
struct PiPairEndpoint {std::string host;unsigned port{31418};};
// One admitted worker's pinned private connection. The owner reserves its
// shared connection/mutation slot before opening. TLS and public bootstrap
// are separate callers; this helper never chooses between them or retries.
class PiPairSocket final:public wtp::ByteStream {
public:
    enum class Access {PrivateLan,LoopbackTest};
    using Time=std::chrono::steady_clock::time_point;
    static std::unique_ptr<PiPairSocket> open(const PiPairEndpoint &,Access,Time whole_deadline,std::function<bool()> cancelled);
    ~PiPairSocket() override;
    PiPairSocket(const PiPairSocket &)=delete;
    const std::string &address() const noexcept{return address_;}
    int release_fd() noexcept;
    wtp::IoResult read(std::span<std::uint8_t>) override;
    wtp::IoResult write(std::span<const std::uint8_t>) override;
    void close() noexcept override;
private:
    PiPairSocket(int,std::string,Time,std::function<bool()>);
    bool live() const;
    int fd_;
    std::string address_;
    Time deadline_;
    std::function<bool()> cancelled_;
};
} // namespace wsprrypi
