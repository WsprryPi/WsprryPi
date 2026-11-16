// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "pi_managed_store.hpp"
#include "wtp_endpoint/management_observation.hpp"
#include <optional>
namespace wsprrypi {
// Local protected-browser adapter. The gateway supplies authorization after
// its peer/origin/CSRF checks; it never reads a principal or filesystem path
// from a document. Paired machine access remains unavailable pending its
// approved provider/authorization boundary. Construction does not initialize
// storage, start listeners, or grant output admission.
class PiLocalManagementService {
public:
    using Observe=std::function<std::optional<PiManagementObservation>()>;
    PiLocalManagementService(PiManagedStore &,std::string self,std::string name,Observe);
    PicoHttpResponse handle(const BrowserRequest &,bool authorized_local);
    nlohmann::json target();
private:
    nlohmann::json capabilities(const PiManagementObservation &) const;
    nlohmann::json binding(const PiManagementObservation &) const;
    nlohmann::json output(const PiManagementObservation &) const;
    std::optional<PiManagementObservation> observe();
    void refresh_epoch();
    PiManagedStore &store_;
    std::string self_,name_,connection_;
    Observe observe_;
    std::uint64_t epoch_{};
    std::recursive_mutex mutex_;
};
} // namespace wsprrypi
