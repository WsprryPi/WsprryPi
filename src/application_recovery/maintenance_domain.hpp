// SPDX-License-Identifier: MIT
#pragma once
#include "application_control/http_domain.hpp"
#include "browser_admission.hpp"
#include "worker_process.hpp"
namespace wsprrypi {
class RecoveryMaintenanceDomain:public ApplicationHttpRouter {
public:
    RecoveryMaintenanceDomain(const RecoveryPaths &,RecoveryWorkerProcess &,RecoveryBrowserAdmission &);
private:
    RecoveryPaths paths_;RecoveryWorkerProcess &worker_;RecoveryBrowserAdmission &admission_;
    void handle(const httplib::Request &,httplib::Response &);
    void download(const std::string &,httplib::Response &);
};
}
