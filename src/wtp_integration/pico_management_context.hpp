// SPDX-License-Identifier: MIT
#pragma once
#include "pico_management_target.hpp"
#include "pico_management_journal.hpp"
#include "browser_api.hpp"
#include <utility>
#include <chrono>
namespace wsprrypi {
// One explicitly enrolled profile/editor context. The owning controller must
// hold its profile binding gate and verify the catalog fingerprint on each call.
class PicoManagementContext {
public:
    class CancellationScope {
    public:
        CancellationScope(const CancellationScope &)=delete;
    private:
        friend class PicoManagementContext;
        static PicoManagementJournal::TargetAdmission acquire(PicoManagementContext &context){
            auto admission=context.journal_.target_admission();
            if(!admission)throw std::runtime_error("management_target_busy");
            return std::move(*admission);
        }
        CancellationScope(PicoManagementContext &context,std::function<bool()> cancelled)
            :admission_(acquire(context)),lock_(context.mutex_),target_(context.target_.scoped_cancellation(std::move(cancelled))){context.sync_target_epoch();}
        PicoManagementJournal::TargetAdmission admission_;
        std::unique_lock<std::recursive_mutex> lock_;
        PicoManagementTarget::CancellationScope target_;
    };
    CancellationScope scoped_cancellation(std::function<bool()> cancelled){return CancellationScope(*this,std::move(cancelled));}
    PicoManagementContext(WtpSettings,std::string controller,std::string directory,
        std::string profile_generation,std::string connection_generation,
        PicoManagementTarget::Fixtures,PicoManagementHistoryAuthority);
    PicoHttpResponse connect(const std::string &carrier,std::string password={});
    PicoHttpResponse disconnect();
    bool authorized();
    bool refresh();
    nlohmann::json management_state();
    nlohmann::json gateway_resources();
    PicoHttpResponse recovery(const std::string &carrier,std::string password={},const nlohmann::json &scope=nullptr);
    PicoHttpResponse request(const BrowserRequest &,const std::string &context,const std::string &operation={});
    PicoHttpResponse request(const BrowserRequest &,const std::string &context,const std::string &operation,
        const PiPairPrincipal &,PiPairHistory &);
    PicoHttpResponse reconcile(const std::string &operation,const std::string &revision);
    PicoHttpResponse reconcile(const std::string &operation,const std::string &revision,const PiPairPrincipal &,PiPairHistory &);
    PicoHttpResponse review(const nlohmann::json &,const PiPairPrincipal &,PiPairHistory &);
    nlohmann::json binding() const;
    nlohmann::json receipt(const std::string &operation);
    nlohmann::json receipt(const std::string &operation,const PiPairPrincipal &,PiPairHistory &);
    std::string receipt_resource(const std::string &operation);
    std::pair<std::string,std::string> recovery_hint() const {std::lock_guard lock(mutex_);return {recovery_operation_,recovery_resource_};}
    std::string id() const {std::lock_guard lock(mutex_);return journal_.target_epoch()==target_epoch_?id_:std::string{};}
    std::string readback_revision() const {std::lock_guard lock(mutex_);return journal_.target_epoch()==target_epoch_?read_revision_:std::string{};}
    void cancel() noexcept {target_.cancel();}
private:
    void sync_target_epoch();
    PicoHttpResponse reconcile_impl(const std::string &,const std::string &,const PiPairPrincipal *,PiPairHistory *);
    PicoHttpResponse request_impl(const BrowserRequest &,const std::string &,const std::string &,
        const PiPairPrincipal *,PiPairHistory *);
    PicoHttpResponse forward(const std::string &,const std::string &,const std::string &,const std::string &,const std::string &);
    PicoManagementTarget target_;
    PicoManagementJournal journal_;
    WtpBrowserApi browser_;
    std::string controller_,device_,profile_,connection_,id_;
    mutable std::recursive_mutex mutex_;
    nlohmann::json binding_;
    std::string operation_,request_digest_,route_,method_,authorization_,capability_,read_revision_,read_resource_;
    std::string recovery_operation_,recovery_resource_;
    std::optional<PicoHttpResponse> gate_error_;
    bool reserved_{},ever_connected_{};
    UsbManagementClient::WriteAdmission paired_admission_;
    bool paired_dispatch_{};
    std::string dispatch_principal_,dispatch_authorization_;
    std::string read_content_,read_scope_;
    std::chrono::steady_clock::time_point read_at_{};
    std::uint64_t read_sequence_{};
    std::uint64_t target_epoch_{};
};
} // namespace wsprrypi
