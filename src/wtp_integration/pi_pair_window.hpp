// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_history.hpp"
#include "pi_pair_http_principals.hpp"
#include "management_browser_session.hpp"
#include <chrono>
#include <functional>
#include <memory>
namespace wsprrypi {
class PiPairWindow;
struct PiPairCommitResult {std::string authorization_generation,receipt_digest;};
class PiPairCommitRefusal final:public std::runtime_error {
public:
    enum class Reason {Expired,Generation,Capacity,Proof,Unavailable};
    explicit PiPairCommitRefusal(Reason reason):std::runtime_error("pairing_commit_refused"),reason_(reason){}
    Reason reason() const noexcept{return reason_;}
private:Reason reason_;
};
// Only the window's actual mutually confirmed PAKE and commit MAC can mint
// this short-lived observation. The storage owner must still check its current
// generation/action and valid() under commit admission, including after WAL
// preparation. No browser boolean/document can construct one.
class PiPairCommitApproval {
public:
    PiPairCommitApproval(const PiPairCommitApproval &)=delete;
    const nlohmann::json &hello() const noexcept{return hello_;}
    const std::string &request_digest() const noexcept{return digest_;}
    const PiPairSecret &tls_key() const noexcept{return tls_;}
    const PiPairSecret &retirement_key() const noexcept{return retirement_;}
    bool valid() const noexcept;
private:
    friend class PiPairWindow;
    friend class PiPairHistory;
    using Time=std::chrono::steady_clock::time_point;
    PiPairCommitApproval(nlohmann::json,std::string,PiPairSecret,PiPairSecret,std::function<Time()>,Time,ManagementBrowserSessions &);
    nlohmann::json hello_;
    std::string digest_;
    PiPairSecret tls_,retirement_;
    std::function<Time()> clock_;
    Time deadline_;
    ManagementBrowserSessions &sessions_;
};
// Ephemeral target-side exchange owner. Construction exposes no routes and
// creates no storage. The production commit callback may do bounded local
// storage only, never DNS/network/hardware or a callback into this window.
// Lock order is window -> pairing history; revocation/cancel uses that order.
// The owner stops/joins every HTTP/crypto worker before destroying this object.
class PiPairWindow {
public:
    using Time=std::chrono::steady_clock::time_point;
    using Clock=std::function<Time()>;
    using CommitResult=PiPairCommitResult;
    using Commit=std::function<CommitResult(const PiPairCommitApproval &)>;
    PiPairWindow(PiPairHistory &,ManagementBrowserSessions &,std::string self,std::string boot,Commit,
        Clock = []{return std::chrono::steady_clock::now();});
    ~PiPairWindow();
    PiPairWindow(const PiPairWindow &)=delete;
    PicoHttpResponse open(const BrowserRequest &,const LocalBrowserPrincipal &);
    PicoHttpResponse state(const BrowserRequest &,const LocalPairingReadPrincipal &);
    PicoHttpResponse bootstrap(const BrowserRequest &,const PiPairBootstrapPrincipal &);
    // Service timer tick, independent of GET/polling requests. Only this tick
    // or an actual failed exchange advances an expired attempt to a fresh offer.
    void advance();
    void cancel() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wsprrypi
