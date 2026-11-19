// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "pi_pair_crypto.hpp"
#include <atomic>
#include <chrono>
namespace wsprrypi {
// Only the real protected request/session boundary can mint this observation.
// No JSON, cookie alone, or caller-supplied local boolean is an authority.
class LocalBrowserPrincipal {
public:
    bool matches(const BrowserRequest &) const;
    // Internal originating-window scope; never serialize or log this value.
    const std::string &session_scope() const noexcept{return session_;}
private:
    friend class ManagementBrowserSessions;
    friend class PiPairHistory;
    LocalBrowserPrincipal(const BrowserRequest &,std::string session,std::weak_ptr<std::atomic_bool>);
    PiPairSecret body_key_;
    std::string method_,path_,revision_,member_context_,session_;
    std::array<unsigned char,32> body_mac_{};
    std::weak_ptr<std::atomic_bool> lease_;
    std::chrono::steady_clock::time_point deadline_;
};
} // namespace wsprrypi
