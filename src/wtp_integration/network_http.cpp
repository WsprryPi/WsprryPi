// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "network_http.hpp"
#include "json.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <map>
#include <thread>
namespace wsprrypi {
PicoHttpResponse pico_http_request(TlsStream &stream, const TlsSelection &selection,
    std::shared_ptr<TlsCredentials> credentials, TlsStream::Clock clock,
    const std::string &resource, const std::string &method,
    const std::string &body, const std::string &revision) {
  const auto error = [](unsigned status, const char *code) {
    return PicoHttpResponse{status, nlohmann::json{{"error", {{"code", code}}}}.dump(), {}};
  };
  if ((resource != "config" && resource != "schedules" && resource != "network" && resource != "capabilities" && resource != "status") ||
      (method != "GET" && method != "PUT") || (method == "PUT" && (resource == "status" || resource == "capabilities")))
    return error(404, "not_found");
  if (body.size() > 32768 || revision.size() > 128 || revision.find_first_of("\r\n") != std::string::npos)
    return error(400, "invalid_request");
  if (method == "PUT" && revision.empty()) return error(428, "revision_required");
  if (!stream.begin_open(selection, std::move(credentials), "http/1.1")) return error(503, "network_unavailable");
  struct Close { TlsStream &stream; ~Close() { stream.close(); } } close{stream};
  const auto deadline = clock() + 20000;
  while (stream.opening() && clock() < deadline) {
    stream.poll_open(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (!stream.ready()) return error(503, "tls_authentication_or_connection_failed");
  // The successful handshake authenticated the canonical configured reference
  // identity. Resolution chooses only the TCP address, never HTTP authority.
  auto authority = stream.observation().authenticated_identity;
  if (authority.find(':') != std::string::npos) authority = '[' + authority + ']';
  if (selection.port != 443) authority += ':' + std::to_string(selection.port);
  std::string request = method + " /api/v1/" + resource + " HTTP/1.1\r\nHost: " + authority + "\r\n";
  if (method == "PUT") request += "Origin: https://" + authority +
      "\r\nContent-Type: application/json\r\nX-WsprryPico-Request: 1\r\nIf-Match: " + revision +
      "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  request += "\r\n" + body;
  std::size_t sent = 0;
  std::string response;
  while (clock() < deadline) {
    if (sent < request.size()) {
      auto result = stream.write(std::span(reinterpret_cast<const std::uint8_t *>(request.data() + sent), request.size() - sent));
      if (result.state == wtp::IoState::Progress) sent += result.count;
      else if (result.state != wtp::IoState::WouldBlock) return error(503, "management_outcome_unknown");
    }
    std::array<std::uint8_t, 4096> bytes{};
    auto result = stream.read(bytes);
    if (result.state == wtp::IoState::Progress) response.append(reinterpret_cast<const char *>(bytes.data()), result.count);
    if (response.size() > 36864) return error(502, "invalid_remote_response");
    const auto boundary = response.find("\r\n\r\n");
    if (boundary != std::string::npos) {
      if (boundary > 4096 || !response.starts_with("HTTP/1.1 ")) return error(502, "invalid_remote_response");
      unsigned status{};
      auto parsed = std::from_chars(response.data() + 9, response.data() + 12, status);
      if (parsed.ec != std::errc() || status < 200 || status > 599) return error(502, "invalid_remote_response");
      std::map<std::string, std::string> headers;
      auto pos = response.find("\r\n") + 2;
      while (pos < boundary) {
        const auto end = response.find("\r\n", pos), colon = response.find(':', pos);
        if (colon >= end) return error(502, "invalid_remote_response");
        auto name = response.substr(pos, colon - pos);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto value = response.substr(colon + 1, end - colon - 1);
        if (value.starts_with(' ')) value.erase(0, 1);
        if (!headers.emplace(name, value).second) return error(502, "invalid_remote_response");
        pos = end + 2;
      }
      std::size_t length{};
      const auto &size = headers["content-length"];
      auto number = std::from_chars(size.data(), size.data() + size.size(), length);
      if (size.empty() || number.ec != std::errc() || number.ptr != size.data() + size.size() ||
          length > 32768 || headers.contains("transfer-encoding") || headers["content-type"] != "application/json")
        return error(502, "invalid_remote_response");
      const auto received = response.size() - boundary - 4;
      if (received > length) return error(502, "invalid_remote_response");
      if (received == length) {
        auto payload = response.substr(boundary + 4);
        if (nlohmann::json::parse(payload, nullptr, false).is_discarded()) return error(502, "invalid_remote_response");
        return {status, std::move(payload), headers["etag"]};
      }
    }
    if (result.state == wtp::IoState::Closed || result.state == wtp::IoState::Failed)
      return error(503, method == "PUT" ? "management_outcome_unknown" : "network_unavailable");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return error(503, method == "PUT" ? "management_outcome_unknown" : "network_deadline");
}
} // namespace wsprrypi
