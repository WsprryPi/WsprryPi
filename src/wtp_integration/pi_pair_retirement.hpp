// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_tls.hpp"
#include <chrono>
namespace wsprrypi {
class PiPairHistory;
// An actual retirement-purpose Finished and its exact closed body MAC. This is
// neither a browser principal nor normal management authority. Only the private
// history owner can construct it; the final owner rechecks before mutation.
class PiPairRetirementPrincipal {
public:
    const PiPairPrincipal &transport() const noexcept{return principal_;}
    const nlohmann::json &request() const noexcept{return request_;}
private:
    friend class PiPairHistory;
    PiPairRetirementPrincipal(const PiPairHistory *owner,const PiPairPrincipal &principal,nlohmann::json request)
        :owner_(owner),principal_(principal),request_(std::move(request)),until_(std::chrono::steady_clock::now()+std::chrono::seconds(30)){}
    const PiPairHistory *owner_;
    PiPairPrincipal principal_;
    nlohmann::json request_;
    std::chrono::steady_clock::time_point until_;
};
// A target accepts only a current authenticated callback and the exact retained
// request/result binding. Matching public IDs alone cannot construct this proof.
class PiPairRetiredProof {
public:
    const nlohmann::json &document() const noexcept{return document_;}
private:
    friend class PiPairHistory;
    PiPairRetiredProof(nlohmann::json document):document_(std::move(document)){}
    nlohmann::json document_;
};
} // namespace wsprrypi
