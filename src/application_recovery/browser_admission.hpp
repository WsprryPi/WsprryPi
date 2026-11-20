// SPDX-License-Identifier: MIT
#pragma once
#include "httplib.hpp"
#include "json.hpp"
#include "support_request_guard.hpp"
#include <functional>
#include <memory>
namespace wsprrypi {
// Independent maintenance lease, issued only after the backend's existing
// management issuer authorizes a local mutation. It survives backend restart;
// an archive or job ID never authenticates a browser.
class RecoveryBrowserAdmission {
public:
    using Snapshot=std::function<SupportRequestGuardSnapshot()>;
    RecoveryBrowserAdmission(std::string private_root,std::string identity,Snapshot);
    ~RecoveryBrowserAdmission();
    nlohmann::json issue(const std::string &host,bool secure); // root broker only
    bool permits(const httplib::Request &);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
