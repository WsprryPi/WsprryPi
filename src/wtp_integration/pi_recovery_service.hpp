// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "pi_managed_startup.hpp"
namespace wsprrypi {
// Host startup state is supplied by the owning process, never inferred from a
// default configuration or from this read-only management service.
enum class PiHostReadiness {Starting,Ready,ConfigurationRecovery,StorageRecovery,OutputRecovery};
class PiRecoveryService {
public:
    using Observe=std::function<PiHostReadiness()>;
    PiRecoveryService(PiManagedStartup &,std::string self,std::string process_boot,Observe);
    PicoHttpResponse handle(const BrowserRequest &,bool authorized_local);
private:
    nlohmann::json binding() const;
    PiManagedStartup &owner_;
    std::string self_,boot_,capability_revision_;
    Observe observe_;
    std::mutex mutex_;
};
} // namespace wsprrypi
