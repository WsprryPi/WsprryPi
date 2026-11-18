// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_service_lifetime.hpp"
#include "pi_pair_maintenance.hpp"
#include "support_request_guard.hpp"
#include "pi_pair_remote_controller.hpp"
namespace wsprrypi {
class PiManagedHost;
// Closed refusal for a selected route when no owner is available.
PicoHttpResponse pi_pair_local_unavailable(const httplib::Request &);
// Owner of the protected local and public bootstrap handlers. Construction
// opens no socket and initializes no history. The process supervisor supplies
// actual transport readiness and joins listeners before stop_and_drain().
class PiPairLocalService {
public:
    PiPairLocalService(std::shared_ptr<PiPairHistory>,ManagementBrowserSessions &,PiPairAdmission &,
        PiPairDiscovery &,std::string self,std::string boot,std::shared_ptr<PiPairRemoteController> remote = {});
    // Explicit software-fixture endpoints, never an operator/config selector.
    struct LoopbackTest {PiPairConnect::LoopbackTest endpoints;};
    PiPairLocalService(std::shared_ptr<PiPairHistory>,ManagementBrowserSessions &,PiPairAdmission &,
        PiPairDiscovery &,std::string self,std::string boot,LoopbackTest,std::shared_ptr<PiPairRemoteController> remote = {});
    ~PiPairLocalService();
    PiPairLocalService(const PiPairLocalService &)=delete;
    bool begin();
    // Supervisor only, after both actual listeners are ready. Repeated calls
    // also start maintenance following an explicit protected history recovery.
    bool transport_ready();
    void request_stop() noexcept;
    void stop_and_drain();
    PicoHttpResponse native(const BrowserRequest &,const LocalBrowserPrincipal &,PiManagedHost &);
    std::optional<PiPairHttpReply> remote(const httplib::Request &,const SupportRequestGuardSnapshot &);
    std::optional<PicoHttpResponse> handle(const httplib::Request &,const SupportRequestGuardSnapshot &);
private:
    std::shared_ptr<PiPairHistory> history_;
    ManagementBrowserSessions &sessions_;
    PiPairDiscovery &discovery_;
    const std::string boot_;
    PiPairServiceLifetime lifetime_;
    PiPairWindow window_;
    PiPairConnect connect_;
    PiPairRenewal renewal_;
    std::shared_ptr<PiPairRemoteController> remote_;
    PiPairMaintenance maintenance_;
    std::mutex owner_;
    bool begun_{},maintaining_{};
    std::atomic_bool transport_{false};
};
}
