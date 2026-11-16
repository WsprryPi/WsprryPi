// SPDX-License-Identifier: MIT
#pragma once
#include "network_http.hpp"
#include "json.hpp"
#include "pico_management_history.hpp"
#include <mutex>
namespace wsprrypi {
// Controller-owned recovery authority. Native Pico storage and WTP journals
// remain independent. Callers serialize one target's complete operation.
class PicoManagementJournal {
public:
    PicoManagementJournal(std::string directory,std::string controller,std::string device,
        PicoManagementHistoryAuthority authority={});
    nlohmann::json snapshot();
    // Existing entry is returned without forwarding. New reservations are
    // durable before this method returns; unknown entries inhibit new writes.
    std::optional<nlohmann::json> reserve(const std::string &principal,const std::string &route,
        const std::string &method,const nlohmann::json &binding,const std::string &authorization,
        const std::string &operation,const std::string &digest);
    // True means terminal retention evicted an older receipt; the caller must
    // retire its browser context before accepting another settings request.
    bool finish(const std::string &operation,const PicoHttpResponse &response);
    nlohmann::json receipt(const std::string &operation);
    // Explicit review after the caller has obtained authoritative fresh readback.
    // The historical receipt remains unknown; resolution grants no success claim.
    bool reconcile(const std::string &operation,const nlohmann::json &binding,const std::string &revision);
    // Startup or explicit catalog-mutation housekeeping only, never a settings
    // read. Unresolved records and enrolled targets are never pruned.
    void prune_retired();
    bool authority_bound() const {return history_.complete()||history_.isolated_for_test;}
    static std::string digest(const std::string &);
private:
    nlohmann::json load();
    void save(const nlohmann::json &);
    void validate(const nlohmann::json &) const;
    void check_authority();
    void record_authority(const nlohmann::json &);
    std::string directory_,controller_,device_,path_,initial_epoch_;
    PicoManagementHistoryAuthority history_;
    std::mutex mutex_;
};
} // namespace wsprrypi
