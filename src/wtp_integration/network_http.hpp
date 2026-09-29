// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "tls.hpp"
#include "json.hpp"
#include "wtp_settings.hpp"
#include <string>
namespace wsprrypi {
struct PicoHttpResponse {
  unsigned status{503};
  std::string body{R"({"error":{"code":"network_unavailable"}})"}, etag;
};
PicoHttpResponse wtp_json_http_exchange(wtp::ByteStream&, TlsStream::Clock,
    const std::string& request, std::uint64_t deadline, bool mutation);
// One bounded request, already authorized/serialized by the application owner.
// Only standalone management resources; never forwards browser job operations.
PicoHttpResponse pico_http_request(TlsStream &, const TlsSelection &,
    std::shared_ptr<TlsCredentials>, TlsStream::Clock,
    const std::string &resource, const std::string &method,
    const std::string &body = {}, const std::string &revision = {});
nlohmann::json wtp_pi_revocation_status(const WtpSettings&, unsigned management_port);
} // namespace wsprrypi
