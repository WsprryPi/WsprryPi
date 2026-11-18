// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "pi_managed_store.hpp"
#include "pi_pair_retirement_client.hpp"
#include "wtp_endpoint/management_observation.hpp"
#include <optional>
#include <chrono>
namespace wsprrypi {
class PiPairHistory;
class PiPairPrincipal;
class LocalBrowserPrincipal;
// Protected local-browser and actual paired-principal adapters share native
// resource semantics. Documents supply neither principals nor filesystem paths.
// Construction starts no storage, listener, transmission or output admission.
class PiLocalManagementService {
public:
    using Observe=std::function<std::optional<PiManagementObservation>()>;
    PiLocalManagementService(PiManagedStore &,std::string self,std::string name,Observe);
    PicoHttpResponse handle(const BrowserRequest &,bool authorized_local);
    PicoHttpResponse handle(const BrowserRequest &,const PiPairPrincipal &,PiPairHistory &,std::string *readback_revision=nullptr);
    PicoHttpResponse handle(const BrowserRequest &,const LocalBrowserPrincipal &,PiPairHistory &);
    void advance_migration() noexcept;
    void advance_remote_handover(PiPairRetirementClient &,const std::function<bool()> &cancelled) noexcept;
    nlohmann::json target();
private:
    struct Authorization {
        std::string principal,generation;
        PiPairHistory *history{};
        const PiPairPrincipal *paired{};
        nlohmann::json metadata{nullptr};
        const LocalBrowserPrincipal *local{};
    };
    PicoHttpResponse handle_pair(const BrowserRequest &,const PiPairPrincipal &,PiPairHistory &);
    PicoHttpResponse handle_authorized(const BrowserRequest &,const Authorization &,std::string *readback_revision=nullptr);
    nlohmann::json capabilities(const PiManagementObservation &) const;
    nlohmann::json binding(const PiManagementObservation &) const;
    nlohmann::json output(const PiManagementObservation &) const;
    std::optional<PiManagementObservation> observe();
    void refresh_epoch();
    PiManagedStore &store_;
    std::string self_,name_,connection_,paired_context_;
    Observe observe_;
    std::uint64_t epoch_{};
    struct Readback {
        std::string token,content,resource,scope;
        nlohmann::json binding;
        std::chrono::steady_clock::time_point at;
    };
    std::optional<Readback> readback_;
    std::uint64_t read_sequence_{};
    std::recursive_mutex mutex_;
    std::mutex remote_worker_;
    std::string remote_transition_;
    std::size_t remote_cursor_{};
    struct RemoteBackoff {std::chrono::steady_clock::time_point next{};unsigned failures{};};
    std::vector<RemoteBackoff> remote_backoff_;

};
} // namespace wsprrypi
