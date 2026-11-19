// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "identity_probe.hpp"
#include "browser_api.hpp"
#include "configuration.hpp"
#include "consumer_radio.hpp"
#include "management_resolver.hpp"
#include "pico_usb_management_channel.hpp"
#include <chrono>
#include <cstdlib>
#include <stdexcept>

namespace wsprrypi {
namespace {
std::uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
void require_probe_allowed() {
  if (const char *disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
      disabled && std::string(disabled)=="1")
    throw std::runtime_error("Device identification is disabled in hardware-free validation");
}
nlohmann::json identify(const WtpSettings &settings,const PicoManagementExchange &exchange,
                        const std::string &carrier,const std::string &binding,bool authenticated) {
  const auto management=exchange("management","GET",{},{});
  if (management.status!=200) throw std::runtime_error("Management identification unavailable");
  const auto document=strict_browser_json(management.body,16);
  if (!document.is_object() || !document.contains("target") || !document["target"].is_object())
    throw std::runtime_error("Invalid management identity");
  const auto &target=document["target"];
  if (!target.contains("device_id") || !target["device_id"].is_string() ||
      !target.contains("boot_id") || !target["boot_id"].is_string())
    throw std::runtime_error("Invalid management identity");
  const auto device=target["device_id"].get<std::string>(),boot=target["boot_id"].get<std::string>();
  if ((!settings.device_id.empty() && settings.device_id!=device) ||
      !valid_management_contract(document,carrier,device,boot))
    throw std::runtime_error("Management identity or carrier mismatch");
  const auto caps=exchange("capabilities","GET",{},{});
  if (caps.status!=200 || !pico_management_capabilities_valid(strict_browser_json(caps.body,16),carrier))
    throw std::runtime_error("Unsupported management capabilities");
  const auto response=exchange("status","GET",{},{});
  if (response.status!=200) throw std::runtime_error("Native output observation unavailable");
  const auto status=strict_browser_json(response.body,16);
  if (!pico_native_status_valid(status,device,boot))
    throw std::runtime_error("Invalid native output observation");
  return {{"device_id",device},{"boot_id",boot},{"binding",binding},
      {"authenticated",authenticated},{"output_active",status["job"]["output_active"]}};
}
}
nlohmann::json wtp_probe_usb_identity(const WtpSettings &settings) {
  require_probe_allowed();
  if (settings.transport!="usb") throw std::runtime_error("USB management endpoint required");
  validate_wtp_settings(settings,true);
  // A temporary read-only owner does not start the periodic time worker.
  auto channel=pico_usb_management_channel(settings,false);
  std::lock_guard lock(channel->mutex);
  if (!channel->ensure(settings.device_id)) throw std::runtime_error("USB management identification unavailable");
  auto selected=settings;selected.device_id=channel->client->device();
  return identify(selected,[&](const auto &resource,const auto &method,const auto &,const auto &){
      return channel->client->request("/api/control/v1/"+resource,method);
    },"usb_physical","usb",false);
}
nlohmann::json wtp_probe_network_identity(const WtpSettings &settings) {
  require_probe_allowed();
  if (settings.transport!="network")
    throw std::runtime_error("Use authenticated HTTPS or the explicit Consumer Wi-Fi login to identify this device");
  validate_wtp_settings(settings,true);
  TlsStream tls(now_ms,TlsStream::Access::Production,management_tls_resolver());
  TlsSelection selection{settings.hostname,settings.tls_identity,settings.tls_ca,
      settings.tls_certificate,settings.tls_key,static_cast<unsigned>(settings.tcp_port)};
  auto credentials=std::make_shared<TlsCredentials>(selection);
  return identify(settings,[&](const auto &resource,const auto &method,const auto &,const auto &){
      return pico_http_request(tls,selection,credentials,now_ms,resource,method);
    },"engineering_mtls","tls",true);
}
} // namespace wsprrypi
