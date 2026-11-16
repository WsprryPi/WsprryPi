// SPDX-License-Identifier: MIT
#pragma once
#include "pico_management_target.hpp"
#include "pico_management_journal.hpp"
#include "browser_api.hpp"
#include <utility>
namespace wsprrypi {
// One explicitly enrolled profile/editor context. The owning controller must
// hold its profile binding gate and verify the catalog fingerprint on each call.
class PicoManagementContext {
public:
    PicoManagementContext(WtpSettings,std::string controller,std::string directory,
        std::string profile_generation,std::string connection_generation,
        PicoManagementTarget::Fixtures,PicoManagementHistoryAuthority);
    PicoHttpResponse connect(const std::string &carrier,std::string password={});
    PicoHttpResponse disconnect();
    bool authorized();
    bool refresh();
    nlohmann::json management_state();
    PicoHttpResponse recovery(const std::string &carrier,std::string password={},const nlohmann::json &scope=nullptr);
    PicoHttpResponse request(const BrowserRequest &,const std::string &context,const std::string &operation={});
    PicoHttpResponse reconcile(const std::string &operation,const std::string &revision);
    nlohmann::json binding() const;
    nlohmann::json receipt(const std::string &operation);
    std::string receipt_resource(const std::string &operation);
    std::pair<std::string,std::string> recovery_hint() const {std::lock_guard lock(mutex_);return {recovery_operation_,recovery_resource_};}
    std::string id() const {std::lock_guard lock(mutex_);return id_;}
    std::string readback_revision() const {std::lock_guard lock(mutex_);return read_revision_;}
    void cancel() noexcept {target_.cancel();}
private:
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
    bool reserved_{};
};
} // namespace wsprrypi
