// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_tls.hpp"
#include <chrono>
#include <atomic>
#include <memory>
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
// An explicit protected rollback request bound to the target's current
// incoming manager. Local authority carries its real revocable browser lease;
// paired authority carries the actual channel principal. No JSON can mint it.
class PiPairResumeTargetProof {
public:
    const nlohmann::json &scope() const noexcept{return scope_;}
    const std::string &body_digest() const noexcept{return body_digest_;}
private:
    friend class PiPairHistory;
    PiPairResumeTargetProof(const PiPairHistory *owner,nlohmann::json scope,std::string digest,
        std::weak_ptr<std::atomic_bool> lease,std::chrono::steady_clock::time_point until)
        :owner_(owner),scope_(std::move(scope)),body_digest_(std::move(digest)),lease_(std::move(lease)),until_(until){}
    PiPairResumeTargetProof(const PiPairHistory *owner,nlohmann::json scope,std::string digest,const PiPairPrincipal &principal)
        :owner_(owner),scope_(std::move(scope)),body_digest_(std::move(digest)),principal_(principal),until_(std::chrono::steady_clock::now()+std::chrono::seconds(30)){}
    const PiPairHistory *owner_;
    nlohmann::json scope_;
    std::string body_digest_;
    std::weak_ptr<std::atomic_bool> lease_;
    std::optional<PiPairPrincipal> principal_;
    std::chrono::steady_clock::time_point until_;
};
// Actual current outgoing normal TLS plus its exact grant/readbacks. A
// replacement pair may recover an existing outcome only; it cannot consume the
// old grant. Current-view eligibility never substitutes for final owner/WAL CAS.
class PiPairResumeGrantProof {
public:
    const nlohmann::json &grant() const noexcept{return grant_;}
    const PiPairPrincipal &transport() const noexcept{return principal_;}
    const std::string &profile() const noexcept{return profile_;}
    const std::string &hostname() const noexcept{return hostname_;}
    bool original_scope() const noexcept{return original_scope_;}
    bool current_target() const noexcept{return current_target_;}
private:
    friend class PiPairHistory;
    PiPairResumeGrantProof(const PiPairHistory *owner,const PiPairPrincipal &principal,std::string profile,std::string generation,
        std::string hostname,nlohmann::json grant,bool original,bool current)
        :owner_(owner),principal_(principal),profile_(std::move(profile)),generation_(std::move(generation)),hostname_(std::move(hostname)),
        grant_(std::move(grant)),original_scope_(original),current_target_(current),until_(std::chrono::steady_clock::now()+std::chrono::seconds(30)){}
    const PiPairHistory *owner_;
    PiPairPrincipal principal_;
    std::string profile_,generation_,hostname_;
    nlohmann::json grant_;
    bool original_scope_,current_target_;
    std::chrono::steady_clock::time_point until_;
};
// Final outcome continuity for an existing grant. This does not authorize
// consuming a grant, changing settings, or restoring an old management pair.
class PiPairResumeOutcomeProof {
public:
    const PiPairPrincipal &transport() const noexcept{return principal_;}
    const nlohmann::json &document() const noexcept{return document_;}
private:
    friend class PiPairHistory;
    PiPairResumeOutcomeProof(const PiPairHistory *owner,const PiPairPrincipal &principal,nlohmann::json document)
        :owner_(owner),principal_(principal),document_(std::move(document)),until_(std::chrono::steady_clock::now()+std::chrono::seconds(30)){}
    const PiPairHistory *owner_;
    PiPairPrincipal principal_;
    nlohmann::json document_;
    std::chrono::steady_clock::time_point until_;
};
} // namespace wsprrypi
