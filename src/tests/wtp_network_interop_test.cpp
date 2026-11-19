// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_integration/network_http.hpp"
#include "json.hpp"
#include <deque>
#include <iostream>
#include <thread>
using namespace wsprrypi;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
namespace {
struct Resolver : TlsResolver {
  std::vector<std::string> addresses{"127.0.0.1"};
  std::string host;
  unsigned calls{};
  bool begin(const std::string &value, unsigned) override { host = value; ++calls; return true; }
  auto poll() -> std::optional<std::vector<std::string>> override { return addresses; }
  void cancel() noexcept override {}
};
struct Clock {
  std::uint64_t now_ms() const { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  std::optional<std::uint64_t> utc_now_ns() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
  void wait_ms(std::uint64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  try {
    Clock clock;
    const std::string directory = argv[1];
    const std::string hostname = "wsprrypico-" + std::string(32, 'a') + ".local";
    TlsSelection selection{"WSPRRYpico-" + std::string(32, 'a') + ".LOCAL.", "", directory + "/client-ca.crt", directory + "/client.crt", directory + "/client.key", 18443};
    auto credentials = std::make_shared<TlsCredentials>(selection);
    auto resolver = std::make_unique<Resolver>(); auto *resolution = resolver.get();
    TlsStream tls([&] { return clock.now_ms(); }, TlsStream::Access::LoopbackTest, std::move(resolver));
    // Actual TLS/server management using explicit injected loopback resolution.
    // This seam neither implements nor qualifies Linux NSS or mDNS.
    const auto http = [&](const std::string &resource, const std::string &method = "GET", const std::string &body = "", const std::string &revision = "") {
      return pico_http_request(tls, selection, credentials, [&] { return clock.now_ms(); }, resource, method, body, revision);
    };
    const auto resolver_calls=resolution->calls;
    const auto transport_state=tls.observation().state;
    for(const auto &[resource,method]:{std::pair{"jobs","POST"}, {"jobs/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/abort","POST"},
        {"restart","POST"},{"hostname","PUT"},{"hostname","GET"},{"jobs","GET"},{"consumer/password","PUT"},
        {"consumer/password","GET"},{"consumer/setup/api/owner/v1/public-status","GET"},
        {"consumer/setup/api/bootstrap/v1/start","POST"},{"consumer/logout","POST"}}) {
      CHECK(http(resource,method,R"({"contract":"Wsprry-Control/1"})","\"r1\"").status==404);
      CHECK(resolution->calls==resolver_calls && tls.observation().state==transport_state);
    }
    const auto restart = [](const char *which) {
      std::cout << "RESTART " << which << std::endl;
      std::string ready; std::getline(std::cin, ready); CHECK(ready == "READY");
    };
    auto config = http("config"); CHECK(config.status == 200 && !config.etag.empty());
    CHECK(resolution->host == hostname && tls.observation().authenticated_identity == hostname);
    selection.host = "127.0.0.1"; selection.expected_identity = hostname;
    CHECK(http("network").status == 200 && tls.observation().authenticated_identity == hostname);
    selection.expected_identity = "127.0.0.1";
    CHECK(http("network").status == 200 && tls.observation().authenticated_identity == "127.0.0.1");
    selection.expected_identity = "127.0.0.2";
    CHECK(http("network").status == 503 && tls.observation().authenticated_identity.empty());
    selection.expected_identity = "wrong.local";
    CHECK(http("network").status == 503 && tls.observation().authenticated_identity.empty());
    selection.host = hostname; selection.expected_identity.clear();
    if (std::getenv("WSPRRY_TEST_SECOND_LOOPBACK")) {
      restart("address2"); resolution->addresses = {"127.0.0.2"};
      CHECK(http("network").status == 200);
      CHECK(tls.observation().address == "127.0.0.2" && tls.observation().authenticated_identity == hostname);
      restart("address1"); resolution->addresses = {"127.0.0.1"};
      CHECK(http("network").status == 200 && tls.observation().address == "127.0.0.1");
      std::cout << "Actual changed IPv4 loopback address with unchanged DNS TLS identity passed\n";
    }
    config = http("config"); CHECK(config.status == 200);

    const auto application=http("application");CHECK(application.status==200);
    const auto target=nlohmann::json::parse(application.body).at("target");
    const std::string body=nlohmann::json{{"contract","Wsprry-Control/1"},{"schema","config-update"},{"target",target},{"mode","merge"},
      {"config",{{"enabled",false},{"station",{{"callsign","AA0NT"},{"locator","EM18"},{"power_dbm",20}}},
      {"wifi",{{"ssid","test"},{"password","never-echo-this"},{"ntp_ipv4","192.0.2.1"}}},{"schedules",nlohmann::json::array({{{"period_s",120},{"phase_s",0}}})}}}}.dump();
    CHECK(http("config/update", "POST", body).status == 428);
    auto saved = http("config/update", "POST", body, config.etag);
    CHECK(saved.status == 200 && saved.body.find("never-echo-this") == std::string::npos);
    CHECK(http("config/update", "POST", body, config.etag).status == 412);
    CHECK(http("schedules").status == 200);
    CHECK(http("network").status == 200);
    const auto caps=http("capabilities");CHECK(caps.status==200);
    const auto capabilities=nlohmann::json::parse(caps.body);
    CHECK(capabilities.contains("execution")&&!capabilities.contains("wtp")&&!capabilities.contains("message_jobs"));
    CHECK(!capabilities.at("features").contains("jobs"));
    // Bypass the production client's route allowlist to challenge the actual
    // Pico handler over certificate-authenticated TLS, including aliases.
    for(const auto method:{"GET","POST","PUT","DELETE","PATCH","OPTIONS","HEAD"}) {
      for(const auto path:{"jobs","jobs/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/abort","jobs/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}) {
        CHECK(tls.begin_open(selection,credentials));
        const auto deadline=clock.now_ms()+20000;
        while(tls.opening()&&clock.now_ms()<deadline){tls.poll_open();clock.wait_ms(1);}
        CHECK(tls.ready());
        const std::string authority=hostname+":18443";
        const std::string request=std::string(method)+" /api/control/v1/"+path+" HTTP/1.1\r\nHost: "+authority+
          "\r\nOrigin: https://"+authority+"\r\nX-WsprryPico-Request: 1\r\n"+
          (std::string(method)=="GET" ? "\r\n" : "Content-Type: application/json\r\nContent-Length: 8\r\n\r\n{invalid");
        const auto response=wtp_json_http_exchange(tls,[&]{return clock.now_ms();},request,deadline,true);
        const bool supported=std::string(method)=="GET"||std::string(method)=="POST"||std::string(method)=="PUT";
        CHECK(response.status==(supported?404:400));
        tls.close();
      }
    }
    auto status=http("status");CHECK(status.status==200);
    auto observed=nlohmann::json::parse(status.body);
    CHECK(observed.at("job").at("state")=="empty"&&observed.at("job").at("owner_id").is_null());
    auto stop=http("schedules/stop");CHECK(stop.status==200&&!stop.etag.empty());
    const auto stop_body=nlohmann::json{{"contract","Wsprry-Control/1"},{"schema","schedule-stop"},{"target",target},{"stop",true}}.dump();
    CHECK(http("schedules/stop","POST",stop_body,"\"stale\"").status==412);
    const auto stopped=http("schedules/stop","POST",stop_body,stop.etag);CHECK(stopped.status==200);
    const auto confirmed=http("schedules/stop");CHECK(confirmed.status==200&&confirmed.etag==stopped.etag);
    CHECK(nlohmann::json::parse(confirmed.body).at("stopped")==true);
    const auto old_boot=target.at("boot_id");restart("boot");
    status=http("status");CHECK(status.status==200);observed=nlohmann::json::parse(status.body);
    CHECK(observed.at("job").at("boot_id")!=old_boot);
    stop=http("schedules/stop");CHECK(stop.status==200);
    CHECK(http("schedules/stop","POST",stop_body,stop.etag).status==409);
    CHECK(nlohmann::json::parse(http("status").body).at("job").at("owner_id").is_null());
    std::cout<<"Actual Pico HTTPS identities, address changes, strict revisions, retired routes, native stop, and stale-boot refusal passed\n";
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
