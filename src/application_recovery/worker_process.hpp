// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "paths.hpp"
#include <memory>
namespace wsprrypi {
// Application-private pipe owner. Requests have already passed the root broker's
// OS/browser admission; this class exposes no network or generic command API.
class RecoveryWorkerProcess {
public:
    RecoveryWorkerProcess(const RecoveryPaths &,const std::string &identity,const std::string &role);
    ~RecoveryWorkerProcess();
    RecoveryWorkerProcess(const RecoveryWorkerProcess &)=delete;
    nlohmann::json request(const nlohmann::json &);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
