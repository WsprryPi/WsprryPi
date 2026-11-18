// SPDX-License-Identifier: MIT
#pragma once
#include "wtp_integration/pi_recovery_service.hpp"
#include "wtp_integration/pi_management_service.hpp"
#include <atomic>
namespace wsprrypi {
// Process-owned coordinator, not a backend or a controller-only fallback. The
// caller prepares before parsing, stops all workers before destruction, and
// activates only after normal native quiescence and ownership recovery.
class PiManagedHost {
public:
    PiManagedHost(std::string ini,std::string self,std::string process_boot,
        PiLocalManagementService::Observe,std::function<void(PiPairInitializationProof)> pairing = {});
    void prepare();
    void parsed();
    bool retry_saved_configuration();
    void signal_saved_configuration() noexcept;
    bool consume_configuration_signal() noexcept;
    // Before CLI overrides, retain only valid saved listener values from the
    // current parser observation. This never publishes RF configuration.
    void restore_recovery_listener_ports();
    bool ready_for_native_startup();
    void activate(PiManagedStartup::ConfigurationGate);
    void hold_output();
    PiHostReadiness state();
    PicoHttpResponse recovery(const BrowserRequest &,bool authorized);
    PicoHttpResponse management(const BrowserRequest &,bool authorized);
    PicoHttpResponse management(const BrowserRequest &,const PiPairPrincipal &,PiPairHistory &,std::string *readback_revision=nullptr);
    PicoHttpResponse management(const BrowserRequest &,const LocalBrowserPrincipal &,PiPairHistory &);
    void advance_migration() noexcept;
    void reclaim_pairing_retirements(PiPairHistory &) noexcept;
    void advance_remote_handover(PiPairRetirementClient &,const std::function<bool()> &) noexcept;
    nlohmann::json target();
    WtpCatalog &catalog();
    const std::string &device_id() const noexcept;
private:
    PiManagedStartup owner_;
    std::atomic<PiHostReadiness> state_{PiHostReadiness::Starting};
    std::atomic_bool configuration_signal_{false};
    PiRecoveryService recovery_;
    PiLocalManagementService management_;
    std::string ini_,last_attempt_;
    bool prepared_{},parsed_{};
};
} // namespace wsprrypi
