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
} // namespace wsprrypi
