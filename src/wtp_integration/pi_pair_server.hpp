// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_admission.hpp"
#include "pi_pair_http.hpp"
#include "pi_pair_tls.hpp"
namespace wsprrypi {
// Process-owned listener. No startup in the constructor, no certificates and
// no HTTP fallback. Every handler receives the actual post-Finished principal;
// its owner still checks current generation/resource under commit admission.
class PiPairServer {
public:
    enum class Access {PrivateLan,LoopbackTest};
    using Finished=std::function<bool(const PiPairPrincipal &)>;
    using Handler=std::function<PiPairHttpReply(const PiPairPrincipal &,const httplib::Request &,
        PiPairAdmission::Connection &,const std::function<bool()> &cancelled)>;
    PiPairServer(PiPairAdmission &,PiPairTlsChannel::Lookup,Finished,Handler,Access = Access::PrivateLan);
    ~PiPairServer();
    PiPairServer(const PiPairServer &)=delete;
    PiPairServer &operator=(const PiPairServer &)=delete;
    bool start();
    void stop() noexcept;
    bool ready() const noexcept;
    unsigned port() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace wsprrypi
