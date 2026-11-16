// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "network_http.hpp"
#include <functional>
namespace wsprrypi {
using PicoManagementExchange = std::function<PicoHttpResponse(
    const std::string &, const std::string &, const std::string &, const std::string &)>;
// Reuse the current closed capability/descriptor validation during management-
// only negotiation, without constructing or inspecting an execution session.
bool pico_management_capabilities_valid(const nlohmann::json &, const std::string &carrier);
// Existing USB-only native resources, with closed input/readback validation.
// No authentication, WTP ownership, or wire-format translation is introduced.
PicoHttpResponse pico_usb_settings_request(const PicoManagementExchange &,
    const std::string &device,const std::string &boot,const std::string &resource,
    const std::string &method,const std::string &body={},const std::string &revision={});
// Caller holds the existing runtime operation/idle-management authority. Identity
// comes from its fresh WTP negotiation, never from the browser or TLS address.
PicoHttpResponse pico_configuration_request(const PicoManagementExchange &,
    const std::string &device, const std::string &boot, const std::string &method,
    const std::string &body = {}, const std::string &revision = {},
    const std::string &carrier = "engineering_mtls", const std::string &generation = {});
// Caller holds the same operation/application gate as the legacy editor.
// Only the fixed shared-resource mapping is accepted. No Config fallback.
PicoHttpResponse pico_resource_request(const PicoManagementExchange &,
    const std::string &device, const std::string &boot, const std::string &resource,
    const std::string &method, const std::string &body = {}, const std::string &revision = {},
    const std::string &carrier = "engineering_mtls", const std::string &generation = {});
// Additive saved drafts retain Pico's independent field and explicit application
// semantics. The caller holds the same selected-member operation/idle gate.
PicoHttpResponse pico_progress_request(const PicoManagementExchange &,
    const std::string &device, const std::string &boot, const std::string &resource,
    const std::string &method, const std::string &body = {}, const std::string &revision = {},
    const std::string &carrier = "engineering_mtls", const std::string &generation = {});
} // namespace wsprrypi
