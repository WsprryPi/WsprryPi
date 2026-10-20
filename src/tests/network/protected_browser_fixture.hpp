// SPDX-License-Identifier: MIT
#pragma once
#include "backend_http_guard.hpp"
#include "wtp_integration/browser_http.hpp"
inline wsprrypi::PicoHttpResponse protected_browser_fixture(wsprrypi::WtpBrowserApi &api,
    const nlohmann::json &command) {
  httplib::Request request;
  request.method=command.at("method"); request.path=command.at("path"); request.target=command.value("target",request.path);
  request.body=command.value("body","");
  request.headers.emplace("Host","controller.local");
  // Real browsers generate Origin; UI tests forward their actual emitted headers.
  if (request.method != "GET") request.headers.emplace("Origin","http://controller.local");
  if (command.contains("headers")) {
    for (const auto &[key,value] : command["headers"].items()) request.headers.emplace(key,value.get<std::string>());
  } else if (request.method != "GET") {
    request.headers.emplace("Content-Type","application/json"); request.headers.emplace("X-WsprryPico-Request","1");
  }
  if (!command.value("mutation_context",true)) request.headers.erase("X-WsprryPico-Request");
  if (!request.has_header("If-Match") && command.contains("revision")) request.headers.emplace("If-Match",command.at("revision").get<std::string>());
  if (!request.has_header("X-WsprryPi-Member-Context") && command.contains("member_context"))
    request.headers.emplace("X-WsprryPi-Member-Context",command.at("member_context").get<std::string>());
  if (command.contains("extra_headers")) for (const auto &pair : command["extra_headers"])
    request.headers.emplace(pair[0].get<std::string>(),pair[1].get<std::string>());
  const SupportRequestGuardSnapshot snapshot{true,"controller.local",{},{{"192.0.2.1","255.255.255.0"}}};
  const auto origin=request.has_header("Origin") ? std::optional(request.get_header_value("Origin")) : std::nullopt;
  if (evaluate_backend_http_request(request.method,request.path,command.value("peer","192.0.2.10"),
      request.get_header_value("Host"),origin,snapshot) != BackendHttpGuardDecision::allowed)
    return {403,R"({"error":{"code":"protected_route_refused"}})",{}};
  const auto parsed=wsprrypi::wtp_browser_http_request(request);
  if (!parsed) return {400,R"({"error":{"code":"invalid_request"}})",{}};
  return api.handle(*parsed);
}
