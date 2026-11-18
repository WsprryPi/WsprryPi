// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_history.hpp"
#include "pi_pair_client.hpp"
namespace wsprrypi {
// One bounded retirement-purpose exchange for the target-owned handover worker.
// Its owner freezes the request durably and selects Request exactly once; all
// later recovery uses Status. This class never retries or infers nondelivery.
class PiPairRetirementClient {
public:
    enum class Mode {Request,Status};
    enum class State {Refused,Unavailable,Unknown,Pending,Retired};
    struct Result {State state{State::Refused};std::optional<PiPairRetiredProof> proof;};
    using Handoff=std::function<bool(const PiPairMutationGuard &)>;
    PiPairRetirementClient(PiPairHistory &,PiPairAdmission &);
    struct LoopbackTest {PiPairEndpoint endpoint;};
    PiPairRetirementClient(PiPairHistory &,PiPairAdmission &,LoopbackTest);
    Result exchange(const PiPairEndpoint &registered_endpoint,const nlohmann::json &frozen_request,Mode,
        const Handoff &,const std::function<bool()> &cancelled=[] {return false;});
private:
    PiPairHistory &history_;
    PiPairClient client_;
    std::optional<LoopbackTest> test_;
};
}
