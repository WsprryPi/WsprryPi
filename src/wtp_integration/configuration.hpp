// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "network_http.hpp"
#include <functional>
namespace wsprrypi {
using PicoManagementExchange = std::function<PicoHttpResponse(
    const std::string &, const std::string &, const std::string &, const std::string &)>;
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
