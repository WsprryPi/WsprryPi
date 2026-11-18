// SPDX-License-Identifier: MIT
#pragma once
#include "network_http.hpp"
#include "json.hpp"
#include "pico_management_history.hpp"
#include <mutex>
#include <memory>
#include <optional>
namespace wsprrypi {
struct PicoManagementTargetState;
// Controller-owned recovery authority. Native Pico storage and WTP journals
// remain independent. Callers serialize one target's complete operation.
class PicoManagementJournal {
public:
    class TargetAdmission {
    public:
        TargetAdmission(TargetAdmission &&)=default;
        TargetAdmission(const TargetAdmission &)=delete;
        std::uint64_t epoch() const;
        void retire();
    private:
        friend class PicoManagementJournal;
        explicit TargetAdmission(std::shared_ptr<PicoManagementTargetState>);
        std::shared_ptr<PicoManagementTargetState> state_;
        std::unique_lock<std::recursive_mutex> lock_;
    };
    // One shared process owner per actual directory/device, including direct
    // and remote contexts. Try-only admission never queues an operation.
    static std::optional<TargetAdmission> target_admission(const std::string &directory,const std::string &device);
    std::optional<TargetAdmission> target_admission();
    std::uint64_t target_epoch() const;
    bool remote_enrolled();
    PicoManagementJournal(std::string directory,std::string controller,std::string device,
        PicoManagementHistoryAuthority authority={});
    nlohmann::json snapshot();
    // Existing entry is returned without forwarding. New reservations are
    // durable before this method returns; unknown entries inhibit new writes.
    std::optional<nlohmann::json> reserve(const std::string &principal,const std::string &route,
        const std::string &method,const nlohmann::json &binding,const std::string &authorization,
        const std::string &operation,const std::string &digest);
    // Independent native Stop only: retain every unresolved settings marker.
    // The fixed route/method cannot be selected by a caller's resource string.
    std::optional<nlohmann::json> reserve_native_stop(const std::string &principal,
        const nlohmann::json &binding,const std::string &authorization,const std::string &operation,const std::string &digest);
    // True means terminal retention evicted an older receipt; the caller must
    // retire its browser context before accepting another settings request.
    bool finish(const std::string &operation,const PicoHttpResponse &response);
    // Authenticated exact native-Pi receipt readback only. A missing, malformed,
    // foreign-scope or nonterminal reply cannot clear an unknown-write barrier.
    // The caller supplies the current pair scope from the actual TLS channel.
    bool confirm_native(const std::string &operation,const std::string &principal,
        const std::string &authorization,const PicoHttpResponse &response);
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
    std::optional<nlohmann::json> reserve_impl(const std::string &,const std::string &,const std::string &,
        const nlohmann::json &,const std::string &,const std::string &,const std::string &,bool);
    nlohmann::json load();
    void save(const nlohmann::json &);
    void validate(const nlohmann::json &) const;
    void check_authority();
    void record_authority(const nlohmann::json &);
    std::string directory_,controller_,device_,path_,initial_epoch_;
    PicoManagementHistoryAuthority history_;
    std::shared_ptr<PicoManagementTargetState> target_;
    std::mutex mutex_;
};
} // namespace wsprrypi
