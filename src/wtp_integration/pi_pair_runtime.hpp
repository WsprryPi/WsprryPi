// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_owner.hpp"
#include "pi_pair_local_service.hpp"
#include "pi_pair_machine_service.hpp"
namespace wsprrypi {
// Web/process-owned supervisor. Network joins run on its own thread, never the
// RF scheduler or a request/configuration callback. Construction starts nothing.
class PiPairRuntime {
public:
    using Handover=std::function<void(PiPairHistory &,const std::function<bool()> &)>;
    using RemoteFactory=std::function<std::shared_ptr<PiPairRemoteController>(std::shared_ptr<PiPairHistory>)>;
    PiPairRuntime(std::shared_ptr<PiPairOwner>,std::string boot,unsigned web_port,
        std::function<bool()> web_ready,PiPairMachineService::Targets,PiPairMachineService::Target,RemoteFactory = {},Handover = {});
    struct LoopbackTest {};
    PiPairRuntime(std::shared_ptr<PiPairOwner>,std::string boot,unsigned web_port,
        std::function<bool()> web_ready,PiPairMachineService::Targets,PiPairMachineService::Target,LoopbackTest,RemoteFactory = {},Handover = {});
    ~PiPairRuntime();
    PiPairRuntime(const PiPairRuntime &)=delete;
    bool start();
    void request_stop() noexcept;
    void stop() noexcept;
    // Read-only selected owner; never initializes history or starts networking.
    std::shared_ptr<PiPairLocalService> local_service();
    bool ready() const noexcept;
    unsigned port() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
