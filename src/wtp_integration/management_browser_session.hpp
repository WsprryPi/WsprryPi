// SPDX-License-Identifier: MIT
#pragma once
#include "httplib.hpp"
#include "local_browser_principal.hpp"
#include "support_request_guard.hpp"
#include <map>
#include <mutex>
#include <optional>
namespace wsprrypi {
class ManagementBrowserSessions {
public:
    // Called only after the normal protected Host/Origin boundary and JSON
    // request-intent checks. Keys remain in process memory and an HttpOnly cookie.
    bool establish(const httplib::Request &,httplib::Response &);
    bool permits(const httplib::Request &);
    // Repeats the actual enforced LAN/Host/Origin/proxy-client boundary, shape
    // and CSRF checks. No saved network-safety Off value can weaken this mint.
    std::optional<LocalBrowserPrincipal> authorize_local_mutation(const httplib::Request &,
        const SupportRequestGuardSnapshot &);
    // No filesystem/network work. Already minted local observations lose their
    // lease before old cookies are retired; establishing again issues new IDs.
    void retire();
    // Check the issuer and live lease without extending it. Recovery must retire
    // the same session owner that actually minted its protected observation.
    bool owns(const LocalBrowserPrincipal &);
    bool within_rate(const httplib::Request &);
private:
    struct Session {std::string host,token;std::uint64_t started{},seen{};
        std::shared_ptr<std::atomic_bool> alive{std::make_shared<std::atomic_bool>(true)};};
    std::mutex mutex_;
    std::map<std::string,Session> sessions_;
    struct Rate {std::uint64_t seen{},credit{20000};};
    std::map<std::string,Rate> rates_;
};
// One issuer for protected local management routes and their runtime owner.
ManagementBrowserSessions &management_browser_sessions();
} // namespace wsprrypi
