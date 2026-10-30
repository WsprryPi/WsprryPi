// SPDX-License-Identifier: MIT
#pragma once
#include "udp_logging/contract.hpp"
#include "wtp_integration/configuration.hpp"
namespace wsprrypi {
inline PicoHttpResponse
pico_udp_logging_request(const PicoManagementExchange &exchange,
                         const std::string &device, const std::string &boot,
                         const std::string &resource, const std::string &method,
                         const std::string &body, const std::string &revision) {
  const auto failure = [](unsigned status, const char *code) {
    return PicoHttpResponse{
        status, nlohmann::json{{"error", {{"code", code}}}}.dump(), {}};
  };
  if (!wsprry::logging::hex_id(device) || !wsprry::logging::hex_id(boot))
    return failure(409, "member_identity_required");
  const auto control_parse=[](const std::string &document,std::size_t limit) {
    auto value=udp::parse(document,limit);
    udp::require(value.is_object() && value.value("contract","")=="Wsprry-Control/1","unsupported_control_contract");
    value.erase("contract");
    return value;
  };
  try {
    auto caps = exchange("udp-logging/capabilities", "GET", {}, {});
    if (caps.status != 200)
      return caps;
    udp::capabilities(control_parse(caps.body, 2048), device, boot);
    if (resource == "udp-logging/capabilities")
      return method == "GET" ? caps : failure(405, "method_not_allowed");
    if (resource == "udp-logging/test" ||
        resource == "udp-logging/test/capabilities") {
      auto diagnostic =
          exchange("udp-logging/test/capabilities", "GET", {}, {});
      if (diagnostic.status != 200)
        return diagnostic;
      const auto descriptor = control_parse(diagnostic.body, 2048);
      udp::require(udp::fields(descriptor, {"schema", "target", "resource",
                                            "request_schema", "event_code",
                                            "maximum_rate"}) &&
                       descriptor["schema"] ==
                           "udp-logging-test-capabilities" &&
                       descriptor["resource"] == "/api/control/v1/udp-logging/test" &&
                       descriptor["request_schema"] == "udp-logging-test" &&
                       descriptor["event_code"] == "UDP_TEST" &&
                       udp::integer(descriptor["maximum_rate"], 2, 2),
                   "udp_test_capabilities");
      udp::target(descriptor["target"], device, boot);
      if (resource.ends_with("/capabilities"))
        return method == "GET" ? diagnostic
                               : failure(405, "method_not_allowed");
      if (method != "POST")
        return failure(405, "method_not_allowed");
      const auto input = control_parse(body, 2048);
      udp::require(udp::fields(input, {"schema", "target"}) &&
                       input["schema"] == "udp-logging-test",
                   "udp_test_request");
      udp::target(input["target"], device, boot);
      if (revision.empty())
        return failure(428, "revision_required");
      const auto current = exchange("udp-logging", "GET", {}, {});
      if (current.status != 200)
        return current;
      udp::member_readback(control_parse(current.body, 4096), device, boot);
      if (current.etag != revision)
        return failure(412, "revision_conflict");
      auto result = exchange("udp-logging/test", "POST", body, revision);
      if (result.status == 200) {
        const auto v = control_parse(result.body, 2048);
        udp::require(
            udp::fields(v, {"schema", "target", "accepted", "receipt"}) &&
                v["schema"] == "udp-logging-test" &&
                v["accepted"].is_boolean() && v["receipt"] == "unknown",
            "udp_test_reply");
        udp::target(v["target"], device, boot);
      }
      return result;
    }
    if (resource != "udp-logging" || (method != "GET" && method != "PUT"))
      return failure(405, "method_not_allowed");
    if (method == "PUT") {
      const auto input = control_parse(body, 2048);
      udp::require(udp::fields(input, {"schema", "target", "settings"}) &&
                       input["schema"] == "udp-logging",
                   "udp_member_write");
      udp::target(input["target"], device, boot);
      udp::sender(input["settings"]);
      if (revision.empty())
        return failure(428, "revision_required");
      const auto current = exchange("udp-logging", "GET", {}, {});
      if (current.status != 200)
        return current;
      udp::member_readback(control_parse(current.body, 4096), device, boot);
      if (current.etag != revision)
        return failure(412, "revision_conflict");
    }
    auto response = exchange("udp-logging", method, body, revision);
    if (response.status == 200)
      udp::member_readback(control_parse(response.body, 4096), device, boot);
    return response;
  } catch (...) {
    return failure(400, "invalid_udp_logging_contract");
  }
}
} // namespace wsprrypi
