// SPDX-License-Identifier: MIT
#pragma once
namespace wsprrypi {
// Coordinates protected recovery. SQL-backed browser listeners belong to the
// separate web service; file-only component fixtures retain their listeners.
// Keep this object alive through normal startup so a saved port change cannot
// strand the browser that performed the repair. No output/backend cleanup runs
// here, including when startup never reaches a validated configuration.
class PiRecoveryRuntime {
public:
    PiRecoveryRuntime()=default;
    ~PiRecoveryRuntime();
    PiRecoveryRuntime(const PiRecoveryRuntime &)=delete;
    PiRecoveryRuntime &operator=(const PiRecoveryRuntime &)=delete;
    bool await_configuration();
    // Restore the saved ordinary network preference only after native startup
    // is verified. Every recovery/output/storage hold remains protected.
    void finish_native_startup();
    bool owns_http() const noexcept{return http_;}
    bool owns_websocket() const noexcept{return websocket_;}
private:
    bool entered_{},http_{},websocket_{},monitor_{},policy_override_{};
};
} // namespace wsprrypi
