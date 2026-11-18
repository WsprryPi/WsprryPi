// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "httplib.hpp"
#include "support_request_guard.hpp"
#include <optional>
namespace wsprrypi {
class PiPairBootstrapPrincipal;
class LocalPairingReadPrincipal;
std::optional<PiPairBootstrapPrincipal> authorize_pi_pair_bootstrap(const httplib::Request &,const SupportRequestGuardSnapshot &);
std::optional<LocalPairingReadPrincipal> authorize_local_pairing_read(const httplib::Request &,const SupportRequestGuardSnapshot &);
// These are distinct from protected-browser mutations and authenticated TLS
// principals. Neither type grants settings, window creation, or machine access.
class PiPairBootstrapPrincipal {
public:bool matches(const BrowserRequest &) const;
private:
    friend std::optional<PiPairBootstrapPrincipal> authorize_pi_pair_bootstrap(const httplib::Request &,const SupportRequestGuardSnapshot &);
    explicit PiPairBootstrapPrincipal(const BrowserRequest &);
    std::string method_,path_,digest_;
};
class LocalPairingReadPrincipal {
public:bool matches(const BrowserRequest &) const;
private:
    friend std::optional<LocalPairingReadPrincipal> authorize_local_pairing_read(const httplib::Request &,const SupportRequestGuardSnapshot &);
    explicit LocalPairingReadPrincipal(const BrowserRequest &);
    std::string path_;
};
} // namespace wsprrypi
