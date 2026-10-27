// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_logging.hpp"
#include <cassert>
#include "wtp_settings_json.hpp"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
int main() {
  const std::string device(32, 'a'), boot(32, 'b');
  nlohmann::json value = {{"schema", "wsprry-log/1"},
                          {"origin_kind", "pico"},
                          {"origin_id", device},
                          {"origin_name", "pico.local"},
                          {"boot_id", boot},
                          {"sequence", "18446744073709551615"},
                          {"event", "BOOT"},
                          {"severity", 6},
                          {"facility", 23},
                          {"utc_ns", nullptr},
                          {"monotonic_ns", "10"},
                          {"dropped", "0"},
                          {"message", "Hello <script>"}};
  const auto frame = [&] {
    const auto r = "<190>1 - pico.local WsprryPico - BOOT - " + value.dump();
    return "@SYSLOG " + std::to_string(r.size()) + " " + r;
  };
  assert(parse_usb_log(frame(), device, boot) == value);
  const auto reject = [&](std::string text, std::string id,
                          std::string session) {
    bool failed = false;
    try {
      (void)parse_usb_log(text, id, session);
    } catch (...) {
      failed = true;
    }
    assert(failed);
  };
  reject(frame(), std::string(32, 'c'), boot);
  reject(frame(), device, std::string(32, 'c'));
  reject(frame() + "\n", device, boot);
  reject(std::string(2000, 'x'), device, boot);
  auto duplicate = frame();
  const auto message = duplicate.find("\"message\":");
  duplicate.insert(message, "\"message\":\"first\",");
  const auto count = duplicate.find(' ', 8);
  const auto content = duplicate.substr(count + 1);
  reject("@SYSLOG " + std::to_string(content.size()) + " " + content, device,
         boot);
  value["sequence"] = "18446744073709551616";
  reject(frame(), device, boot);
  value["sequence"] = 1;
  reject(frame(), device, boot);
  value["sequence"] = "01";
  reject(frame(), device, boot);
  value["sequence"] = "1";
  value["message"] = "injected\nline";
  reject(frame(), device, boot);
  value["message"] = "safe";
  value["severity"] = 3;
  reject(frame(), device, boot);
  value["severity"] = 4294967302ULL;
  reject(frame(), device, boot);
  value["severity"] = 6;
  value["unexpected"] = "field";
  reject(frame(), device, boot);
  const nlohmann::json attached = {{"available", true}, {"candidates", {{{"settings", {{"USB Serial", "1545B6EFBF78028C"}}}}, {{"settings", {{"USB Serial", "invalid"}}}}}}};
  const auto observed = usb_logging_attached_sources(attached);
  assert(observed.size() == 1 && observed[0]["device_id"] == "c2a3a2a314c948b50a8562a1aeb69669");
  assert(usb_logging_attached_sources({{"available", false}}).empty());
#if defined(__linux__)
  // Policy authority and persistence are exercised without opening hardware.
  char directory[] = "/tmp/wsprrypi-log-test-XXXXXX";
  assert(mkdtemp(directory));
  const auto ini = std::string(directory) + "/station.ini";
  WtpSettings settings;
  settings.path = "/dev/nonexistent-usb-logging-test";
  settings.usb_serial = "1234567890ABCDEF";
  settings.vendor_id = 0x2e8a;
  settings.product_id = 0xa;
  settings.device_id = device;
  const std::string catalog_id(32, 'c');
  const nlohmann::json catalog = {{"version", 1}, {"profiles", {{{"id", catalog_id},
    {"name", "Test Pico"}, {"method", "manual"}, {"discovery_id", ""},
    {"legacy_unverified", false}, {"settings", wtp_settings_json(settings)}}}}};
  { std::ofstream file(ini + ".wtp-devices.json"); file << catalog.dump(); }
  assert(chmod((ini + ".wtp-devices.json").c_str(), 0600) == 0);
  start_usb_logging(ini);
  auto read = usb_logging_api("GET", "", "");
  assert(read.status == 200);
  const auto initial = nlohmann::json::parse(read.body);
  assert(initial.at("candidates").size() == 1 && initial.at("sources").empty());
  auto policy = nlohmann::json{{"schema", "host-usb-logging/1"},
    {"collector_id", initial.at("collector_id")}, {"sources", {{{"catalog_id", catalog_id},
      {"device_id", device}, {"enabled", false}, {"maximum_severity", 6}}}}};
  assert(usb_logging_api("PUT", policy.dump(), "stale").status == 412);
  auto wrong = policy; wrong["collector_id"] = std::string(32, 'f');
  assert(usb_logging_api("PUT", wrong.dump(), read.etag).status == 409);
  auto saved = usb_logging_api("PUT", policy.dump(), read.etag);
  assert(saved.status == 200 && saved.etag != read.etag);
  struct stat metadata{};
  assert(stat((ini + ".usb-logs.json").c_str(), &metadata) == 0);
  assert((metadata.st_mode & 0777) == 0600);
  stop_usb_logging(); start_usb_logging(ini);
  read = usb_logging_api("GET", "", "");
  assert(read.etag == saved.etag);
  assert(nlohmann::json::parse(read.body).at("sources") == policy.at("sources"));
  auto enabled = policy; enabled["sources"][0]["enabled"] = true;
  assert(usb_logging_api("PUT", enabled.dump(), read.etag).status == 409);
  auto oversized_severity = policy;
  oversized_severity["sources"][0]["maximum_severity"] = 4294967302ULL;
  assert(usb_logging_api("PUT", oversized_severity.dump(), read.etag).status == 409);
  auto unknown = policy; unknown["sources"][0]["catalog_id"] = std::string(32, 'd');
  assert(usb_logging_api("PUT", unknown.dump(), read.etag).status == 409);
  assert(chmod((ini + ".usb-logs.json").c_str(), 0644) == 0);
  assert(usb_logging_api("GET", "", "").status == 409);
  stop_usb_logging();
  std::filesystem::remove_all(directory);
#endif
}
