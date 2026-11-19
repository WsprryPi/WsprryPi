// SPDX-License-Identifier: MIT
#pragma once
#include "pi_managed_host.hpp"
#include <memory>
#include <optional>
namespace wsprrypi {
// Production process selection. No INI, HTTP, or backend can choose another
// role or identity. The returned owner outlives every listener/output worker.
std::optional<std::string> pi_managed_ini_argument(int argc,char *argv[]);
std::shared_ptr<PiManagedHost> prepare_pi_managed_host(const std::string &ini);
std::shared_ptr<PiManagedHost> pi_managed_host();
// The actual pre-parser process boot, independent of transmitter readiness.
std::string pi_managed_host_process_boot();
void poll_pi_managed_migration() noexcept;
bool pi_managed_host_selected();
// These exact native-host routes always require enforced network admission,
// independently of the saved policy used by other management resources.
bool pi_managed_host_protected_recovery_path(std::string_view path);
PiHostReadiness pi_managed_host_state();
PicoHttpResponse pi_managed_host_recovery(const BrowserRequest &,bool authorized);
bool pi_managed_host_target_path(const std::string &path);
// Shared HTTP/WebSocket hold, independent of output backend and CLI role.
bool pi_managed_host_held();
bool pi_managed_host_http_allowed(std::string_view method,std::string_view path);
} // namespace wsprrypi
