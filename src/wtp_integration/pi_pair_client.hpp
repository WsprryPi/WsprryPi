// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_admission.hpp"
#include "pi_pair_http.hpp"
#include "pi_pair_tls.hpp"
#include "pi_pair_socket.hpp"
#include "pi_pair_mutation_guard.hpp"
namespace wsprrypi {
struct PiPairClientResult {
    enum class State {Refused,Unavailable,Uncertain,Replied};
    State state{State::Refused};
    std::optional<PiPairHttpReply> reply;
    // Actual channel observation for the private response owner, never a
    // browser document. Owners still recheck generation/epoch at publication.
    std::optional<PiPairPrincipal> principal;
};
// A synchronous, bounded worker operation, never a retry queue. Its caller
// owns durable intent and exact operation recovery. No certificate, proxy,
// redirect, alternate address, alternate port or HTTP fallback is consulted.
class PiPairClient {
public:
    enum class Access {PrivateLan,LoopbackTest};
    // Checked after Finished and again before every attempted first write.
    // This callback must be local and bounded, and must recheck current pair
    // authorization. It must not hold owner locks across this exchange.
    using Admit=std::function<bool(const PiPairPrincipal &)>;
    using WriteAdmission=std::function<std::optional<PiPairMutationGuard>(const PiPairPrincipal &)>;
    explicit PiPairClient(PiPairAdmission &,Access = Access::PrivateLan);
    PiPairClientResult exchange(const PiPairEndpoint &,const std::string &target_id,
        PiPairTlsChannel::Lookup,const PiPairTlsIdentity &,const Admit &,
        const std::string &method,const std::string &path,const std::string &body={},
        const httplib::Headers &metadata={},const std::function<bool()> &cancelled=[] {return false;},const WriteAdmission & = {});
private:
    PiPairAdmission &admission_;
    Access access_;
};
} // namespace wsprrypi
