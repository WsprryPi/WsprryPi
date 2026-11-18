// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_machine_service.hpp"
#include "pico_target_controller.hpp"
#include "../pi_managed_host.hpp"
namespace wsprrypi {
// Fixed application owners behind the authenticated dispatcher. No route,
// profile, INI filename or transport can create another native host/backend.
class PiPairMachineBackend {
public:
    struct Fixtures {
        std::function<nlohmann::json()> usb_inventory;
        std::function<std::shared_ptr<PicoUsbManagementChannel>(const WtpSettings &)> usb_channel;
    };
    using History=std::function<std::shared_ptr<PiPairHistory>()>;
    PiPairMachineBackend(History,WtpCatalog &,std::shared_ptr<PicoTargetController>,PicoTargetController::Active,
        std::shared_ptr<PiManagedHost> native_host = {});
    // Explicit software-fixture sources only; no runtime/operator selector.
    PiPairMachineBackend(History,WtpCatalog &,std::shared_ptr<PicoTargetController>,PicoTargetController::Active,
        std::shared_ptr<PiManagedHost>,Fixtures);
    PicoHttpResponse targets(const PiPairPrincipal &,const PiPairMachineService::Cancellation &);
    PiPairHttpReply request(const PiPairPrincipal &,const httplib::Request &,const PiPairMachineService::Cancellation &);
private:
    struct Discovery {std::string profile;std::shared_ptr<PicoUsbManagementChannel> channel;};
    Discovery discover(const PiPairPrincipal &,PiPairHistory &,const PiPairMachineService::Cancellation &);
    Fixtures fixtures_;
    std::mutex discovery_;
    std::size_t discovery_cursor_{};
    History history_;
    WtpCatalog &catalog_;
    std::shared_ptr<PicoTargetController> picos_;
    PicoTargetController::Active active_;
    std::shared_ptr<PiManagedHost> host_;
    std::mutex inventory_;
    std::size_t cursor_{};
};
}
