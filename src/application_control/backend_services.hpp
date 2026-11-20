// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <functional>
class SupportBundleJobManager;
namespace wsprrypi {
class PiPairLocalService;
// Process owners. Browser listener stop/restart must not cancel these owners.
// Construction starts no networking; start is called by the backend only.
void start_backend_services(unsigned browser_port, bool loopback_fixture);
void stop_backend_services() noexcept;
// Main owns the servers; runtime teardown drains their requests before the
// domain owners they invoke are stopped. Browser-process exit never calls this.
class BackendRequestDrain {
public:
    explicit BackendRequestDrain(std::function<void()>);
    ~BackendRequestDrain();
    BackendRequestDrain(const BackendRequestDrain &)=delete;
};
void drain_backend_requests() noexcept;
std::shared_ptr<PiPairLocalService> backend_pairing_service();
std::shared_ptr<SupportBundleJobManager> backend_support_jobs();
}
