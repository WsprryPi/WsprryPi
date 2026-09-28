// SPDX-License-Identifier: MIT
#include "wtp_integration/browser_api.hpp"
#include "privileged_network_policy.hpp"
#include <iostream>
using namespace wsprrypi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
  try {
    int reads = 0, mutations = 0, management = 0;
    WtpBrowserApi api([&] { ++reads; return nlohmann::json{{"transport", "network"}, {"start_utc_ns", "18446744073709551615"}}; },
      [&](auto resource, auto method, auto, auto revision) {
        ++management; CHECK(resource == "config"); CHECK(method == "PUT"); CHECK(revision == "\"revision\"");
        return PicoHttpResponse{412, R"({"error":{"code":"revision_conflict"}})", {}};
      }, [&](auto job) { ++mutations; CHECK(job == std::string(32, 'a')); return PicoHttpResponse{200, R"({"cleanup_ok":true})", {}}; });
    for (auto path : {"/api/v1/status", "/api/v1/jobs", "/api/v1/capabilities"}) CHECK(api.handle({"GET", path, {}, {}, false}).status == 200);
    CHECK(reads == 3 && mutations == 0 && management == 0);
    auto status = nlohmann::json::parse(api.handle({"GET", "/api/v1/status", {}, {}, false}).body);
    CHECK(status["host"]["start_utc_ns"] == "18446744073709551615");
    CHECK(api.handle({"PUT", "/api/v1/config", "{}", {}, false}).status == 403);
    CHECK(api.handle({"PUT", "/api/v1/config", "{}", {}, true}).status == 428);
    CHECK(api.handle({"PUT", "/api/v1/config", R"({"a":1,"a":2})", "\"revision\"", true}).status == 400);
    CHECK(api.handle({"PUT", "/api/v1/config", "{}", "\"revision\"", true}).status == 412);
    nlohmann::json body{{"session_id", std::string(32, '1')}, {"request_id", std::string(32, '2')}, {"operation", "ABORT"}, {"body", {{"job_id", std::string(32, 'a')}}}};
    BrowserRequest request{"POST", "/api/v1/jobs/" + std::string(32, 'a') + "/abort", body.dump(), {}, true};
    const auto first = api.handle(request);
    CHECK(first.status == 200 && api.handle(request).body == first.body && mutations == 1);
    body["body"]["job_id"] = std::string(32, 'b'); request.path = "/api/v1/jobs"; request.body = body.dump();
    CHECK(api.handle(request).status == 409 && mutations == 1);
    request.body = std::string(32769, 'a'); CHECK(api.handle(request).status == 413);
    for (const auto path : {"/api/v1/config", "/api/v1/network", "/api/v1/schedules", "/api/v1/host/config"}) {
      CHECK(classify_privileged_http_operation("GET", path) == PrivilegedOperationClass::protected_operation);
      CHECK(classify_privileged_http_operation("PUT", path) == PrivilegedOperationClass::protected_operation);
      CHECK(classify_privileged_http_operation("DELETE", path) == PrivilegedOperationClass::reject);
    }
    for (const auto path : {"/api/v1/host/devices", "/api/v1/host/discovery"}) {
      CHECK(classify_privileged_http_operation("GET", path) == PrivilegedOperationClass::protected_operation);
      CHECK(classify_privileged_http_operation("PUT", path) == PrivilegedOperationClass::reject);
    }
    for (const auto path : {"/api/v1/host/devices", "/api/v1/host/devices/use", "/api/v1/host/discovery/identify"}) {
      CHECK(classify_privileged_http_operation("POST", path) == PrivilegedOperationClass::protected_operation);
      CHECK(classify_privileged_http_operation("GET", std::string(path) + "/other") == PrivilegedOperationClass::reject);
    }
    CHECK(classify_privileged_http_operation("POST", "/api/v1/jobs/" + std::string(32, 'a') + "/abort") == PrivilegedOperationClass::protected_operation);
    CHECK(classify_privileged_http_operation("GET", "/api/v1/unknown") == PrivilegedOperationClass::reject);
    std::cout << "Shared API shapes, precision, revisions, bounded parsing, cancellation replay and route classification passed\n";
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
