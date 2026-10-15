// SPDX-License-Identifier: MIT
#include "wtp_integration/configuration.hpp"
#include "wtp_integration/browser_api.hpp"
#include <iostream>
using namespace wsprrypi;
using Json = nlohmann::json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
namespace {
const std::string device(32, 'a'), boot(32, '1');
const Json target{{"scope", "member"}, {"device_id", device}, {"boot_id", boot}};
struct Member {
  Json config = Json::parse(R"({"version":1,"enabled":false,"station":{"callsign":"AA0NT","locator":"EM18","power_dbm":20},"wifi":{"ssid":"test-network","password":null,"ntp_ipv4":"192.0.2.1"},"schedules":[{"period_s":120,"phase_s":0}],"allow_unqualified_frequency":true,"allow_non_amateur_frequency":true,"expires_utc_s":1900000000})");
  Json caps{{"api_version", 1}};
  Json job{{"boot_id", boot}, {"output_active", false}, {"owner_id", nullptr}, {"state", "empty"}};
  std::string revision = "\"r1\"", malformed;
  unsigned mutations = 0, reads = 0;
  bool race = false, drop = false, bad_reply = false;
  std::function<void(Json &)> corrupt_application;
  Json emitted;
  std::string resource, method;
  Member() {
    config["pins"] = {{"engine","direct"},{"rf_gp",28},{"i2c_pair",nullptr},{"button_gp",14},
        {"amplifier_gp",nullptr},{"lpf_gps",Json::array()},{"indicator","onboard_led"},
        {"indicator_gp",nullptr},{"indicator_active_high",true}};
  }
  void modern() {
    caps["configuration_updates"] = {{"schema", "transmitter-config-update/1"}, {"path", "/api/v1/config/update"},
        {"method", "POST"}, {"modes", {"merge", "replace"}}, {"max_update_bytes", 2048}, {"max_config_bytes", 1800}};
    caps["application_resources"] = {{"schema", "transmitter-application/1"}, {"read", "/api/v1/application"}};
  }
  PicoHttpResponse exchange(const std::string &path, const std::string &verb, const std::string &body, const std::string &etag) {
    if (verb != "GET") {
      ++mutations; resource = path; method = verb; emitted = strict_browser_json(body);
      CHECK(etag == revision);
      if (drop) return {503, R"({"error":{"code":"management_outcome_unknown"}})", {}};
      return {200, bad_reply ? "{}" : Json{{"config", config}, {"reboot_required", false}}.dump(), "\"r2\""};
    }
    ++reads;
    if (path == "capabilities") return {200, caps.dump(), {}};
    if (path == "status") return {200, Json{{"job", job}}.dump(), {}};
    if (path == "application") {
      Json application{{"schema", "transmitter-application/1"}, {"target", target}, {"reboot_required", false},
          {"station", {{"schema", "transmitter-station/1"}, {"target", target}, {"station", config["station"]},
              {"saved_generation", "1"}, {"storage_healthy", true}}},
          {"hardware", {{"schema", "transmitter-hardware/1"}, {"target", target}, {"saved", config["pins"]},
              {"saved_generation", "1"}, {"active", config["pins"]}, {"active_revision", std::string(64, 'a')},
              {"pending_restart", false}, {"execution_engine", "test"}, {"storage_healthy", true}, {"application_error", nullptr}}},
          {"recurrence", {{"authority", "member"}, {"enabled", false}, {"suspended", false}, {"expires_utc_s", "0"},
              {"schedules", config["schedules"]}, {"schedule_base_frequency_nhz", "3570100000000000"}}},
          {"capabilities", {{"management_carrier", "authenticated_https"}, {"application_read", true},
              {"station_write", true}, {"hardware_write", true}, {"max_update_bytes", 1024}, {"max_config_bytes", 1800},
              {"hardware_activation", "restart"}, {"station_encoding", "wspr_type1"}, {"station_power_meaning", "reported_wspr_dbm"},
              {"recurrence_authorities", {"member"}}, {"autonomous_modes", {"wspr"}}, {"unavailable", Json::array()}}}};
      if (corrupt_application) corrupt_application(application);
      return {200, application.dump(), revision};
    }
    CHECK(path == "config");
    if (race && reads > 3) revision = "\"r2\"";
    return {200, malformed.empty() ? Json{{"config", config}}.dump() : malformed, revision};
  }
  PicoHttpResponse call(Json patch = Json{{"station", {{"callsign", "K1ABC"}}}}, std::string etag = "\"r1\"") {
    return raw(Json{{"schema", "wsprrypi-pico-settings/1"}, {"target", target}, {"operation", "save"}, {"config", patch}}.dump(), etag);
  }
  PicoHttpResponse raw(const std::string &body, const std::string &etag = "\"r1\"") {
    return pico_configuration_request([&](auto p, auto m, auto b, auto e) { return exchange(p,m,b,e); }, device, boot, "POST", body, etag);
  }
};
void checks() {
  Member legacy;
  CHECK(legacy.call().status == 200 && legacy.mutations == 1);
  CHECK(legacy.resource == "config" && legacy.method == "PUT");
  CHECK(legacy.emitted["station"]["callsign"] == "K1ABC");
  CHECK(legacy.emitted["allow_unqualified_frequency"] == true && legacy.emitted["allow_non_amateur_frequency"] == true);
  CHECK(legacy.emitted["expires_utc_s"] == 1900000000 && legacy.emitted["wifi"]["password"].is_null());
  CHECK(legacy.emitted["pins"]["indicator_active_high"] == true);
  Member strict;
  strict.config.erase("allow_unqualified_frequency"); strict.config.erase("allow_non_amateur_frequency");
  strict.config.erase("pins");
  CHECK(strict.call().status == 200 && !strict.emitted.contains("allow_unqualified_frequency"));
  CHECK(strict.call({{"allow_unqualified_frequency", false}}).status == 409 && strict.mutations == 1);
  Member modern; modern.modern();
  CHECK(modern.call().status == 200 && modern.resource == "config/update" && modern.method == "POST");
  CHECK(modern.emitted["target"] == target && modern.emitted["config"].size() == 1 && modern.emitted["mode"] == "merge");
  for (const auto flags : {false, true}) {
    Member m; m.modern();
    CHECK(m.call({{"allow_unqualified_frequency", flags}, {"expires_utc_s", 0}}).status == 200);
    CHECK(m.emitted["config"]["allow_unqualified_frequency"] == flags && m.emitted["config"]["expires_utc_s"] == 0);
  }
  for (unsigned variant = 0; variant < 12; ++variant) {
    Member m; m.modern();
    if (variant == 0) m.config["future"] = true;
    if (variant == 1) m.config["wifi"]["future"] = true;
    if (variant == 2) m.caps["configuration_updates"]["schema"] = "future/1";
    if (variant == 3) m.caps["configuration_updates"]["path"] = "/api/v1/config";
    if (variant == 4) m.caps["configuration_updates"]["modes"] = {"replace"};
    if (variant == 5) m.caps["configuration_updates"]["max_update_bytes"] = 1;
    if (variant == 6) m.job["boot_id"] = std::string(32, '2');
    if (variant == 7) m.job["owner_id"] = device;
    if (variant == 8) m.job["state"] = "loaded";
    if (variant == 9) m.job["output_active"] = true;
    if (variant == 10) m.malformed = R"({"config":null,"config":null})";
    if (variant == 11) m.race = true;
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto &version : {Json(1.1), Json(1.0), Json("1"), Json(true), Json(nullptr)}) {
    Member m; m.modern(); m.caps["api_version"] = version;
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto &modes : {Json::array({"merge", 42}), Json::array({"merge", nullptr})}) {
    Member m; m.modern(); m.caps["configuration_updates"]["modes"] = modes;
    CHECK(m.call().status == 409 && m.mutations == 0);
  }
  Member future_mode; future_mode.modern();
  future_mode.caps["configuration_updates"]["modes"] = {"merge", "future"};
  CHECK(future_mode.call().status == 200 && future_mode.mutations == 1);
  for (const auto key : {"station", "hardware", "recurrence", "capabilities", "reboot_required"}) {
    Member m; m.modern(); m.corrupt_application = [key](Json &j) { j.erase(key); };
    CHECK(m.call().status == 502 && m.mutations == 0);
  }
  for (const auto pointer : {"/reboot_required", "/station/storage_healthy", "/station/station/callsign",
       "/hardware/saved", "/hardware/active", "/hardware/active_revision", "/hardware/pending_restart",
       "/hardware/application_error", "/recurrence/schedules", "/recurrence/expires_utc_s", "/capabilities/application_read"}) {
    Member m; m.modern(); m.corrupt_application = [pointer](Json &j) { j[Json::json_pointer(pointer)] = 42; };
    CHECK(m.call().status == 502 && m.mutations == 0);
  }
  for (const auto pointer : {"/station/target/boot_id", "/hardware/target/device_id"}) {
    Member m; m.modern(); m.corrupt_application = [pointer](Json &j) { j[Json::json_pointer(pointer)] = std::string(32, 'f'); };
    CHECK(m.call().status != 200 && m.mutations == 0);
  }
  for (const auto state : {"loaded", "armed", "running", "faulted", "unconfirmed"}) {
    Member m; m.job["state"] = state;
    CHECK(m.call().status == 409 && m.mutations == 0);
  }
  Member unhealthy; unhealthy.modern();
  unhealthy.corrupt_application = [](Json &j) { j["hardware"]["storage_healthy"] = false; };
  CHECK(unhealthy.call().status == 409 && unhealthy.mutations == 0);
  for (auto patch : {Json{{"future", 1}}, Json{{"wifi", {{"password", false}}}}, Json{{"station", Json::object()}}, Json{{"pins", nullptr}}}) {
    Member m; CHECK(m.call(patch).status == 400 && m.reads == 0 && m.mutations == 0);
  }
  Member stale; CHECK(stale.call({}, "\"old\"").status == 400);
  CHECK(stale.call({{"enabled", false}}, "\"old\"").status == 412 && stale.mutations == 0);
  CHECK(stale.call({{"enabled", false}}, "").status == 428 && stale.mutations == 0);
  CHECK(stale.raw(R"({"schema":1,"schema":2})").status == 400);
  CHECK(stale.raw(std::string(2049, ' ')).status == 413);
  for (const auto bad : {false, true}) {
    Member m; m.modern(); m.drop = !bad; m.bad_reply = bad;
    const auto response = m.call();
    CHECK(response.status != 200 && m.mutations == 1);
    CHECK(Json::parse(response.body)["mutation_attempted"] == true && response.etag.empty());
  }
  Member initial; initial.config = nullptr;
  CHECK(initial.call().status == 400 && initial.mutations == 0);
  std::cout << "Configuration negotiation, strict fallback, closed shapes, revisions, identity and ambiguous-write checks passed\n";
}
struct HttpInput : wtp::ByteStream {
  std::string input;
  explicit HttpInput(std::string raw) : input(std::move(raw)) {}
  wtp::IoResult write(std::span<const std::uint8_t> b) override { return {wtp::IoState::Progress,b.size()}; }
  wtp::IoResult read(std::span<std::uint8_t> b) override {
    const auto count=std::min(b.size(),input.size());
    std::copy_n(input.begin(),count,b.begin()); input.erase(0,count);
    return {count?wtp::IoState::Progress:wtp::IoState::Closed,count};
  }
  void close() noexcept override {}
};
void http_boundaries() {
  for (const auto raw : {"HTTP/1.1 \r\n\r\n", "HTTP/1.1 20x OK\r\n\r\n", "HTTP/1.1 200X\r\n\r\n",
       "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\ncontent-length: 2\r\n\r\n{}",
       "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 32769\r\n\r\n"}) {
    HttpInput stream(raw); std::uint64_t tick=0;
    CHECK(wtp_json_http_exchange(stream,[&]{return ++tick;},"",100,true).status==502);
  }
  HttpInput valid("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
  std::uint64_t tick=0;
  CHECK(wtp_json_http_exchange(valid,[&]{return ++tick;},"",100,true).status==200);
}
} // namespace
int main() { try { checks(); http_boundaries(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
