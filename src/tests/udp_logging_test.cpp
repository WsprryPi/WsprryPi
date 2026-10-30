// SPDX-License-Identifier: MIT
#include "udp_logging/management.hpp"
#include "udp_logging/network_policy.hpp"
#include "udp_logging/resource_limits.hpp"
#include "udp_logging/udp_event_policy.hpp"
#include <cassert>
#include <cstdlib>
using namespace wsprrypi;
int main() {
  const auto pattern =
      (std::filesystem::temp_directory_path() / "udp-resource-XXXXXX").string();
  std::vector<char> temporary(pattern.begin(), pattern.end());
  temporary.push_back(0);
  const auto created = ::mkdtemp(temporary.data());
  assert(created);
  const std::filesystem::path fixture(created);
  std::filesystem::create_directories(fixture / "proc/self");
  std::filesystem::create_directories(
      fixture / "sys/fs/cgroup/system.slice/wsprrypi-log.service");
  const auto write = [&](const char *path, const char *text) {
    std::ofstream(fixture / path) << text;
  };
  write("sys/fs/cgroup/cgroup.controllers", "cpu io pids\n");
  assert(!udp::resource_controllers_available(fixture));
  write("sys/fs/cgroup/cgroup.controllers", "cpu io memory pids\n");
  assert(udp::resource_controllers_available(fixture));
  write("proc/self/cgroup", "0::/system.slice/wsprrypi-log.service\n");
  write("sys/fs/cgroup/system.slice/wsprrypi-log.service/memory.max", "max\n");
  write("sys/fs/cgroup/system.slice/wsprrypi-log.service/pids.max", "2\n");
  assert(!udp::resource_limits_enforced(fixture));
  write("sys/fs/cgroup/system.slice/wsprrypi-log.service/memory.max",
        "33554432\n");
  assert(udp::resource_limits_enforced(fixture));
  write("sys/fs/cgroup/system.slice/wsprrypi-log.service/pids.max", "3\n");
  assert(!udp::resource_limits_enforced(fixture));
  write("proc/self/cgroup", "0::/../../system.slice/wsprrypi-log.service\n");
  assert(!udp::resource_limits_enforced(fixture));
  std::filesystem::remove_all(fixture);

  const auto route =
      "wlan1\t00000000\t0101A8C0\t0003\t0\t1\t600\t00000000\t0\t0\t0\n";
  auto activity = std::string(route);
  activity.replace(activity.find("\t0\t1\t600"), 8, "\t9\t7\t600");
  assert(udp::route_generation(route, "wlan1") ==
         udp::route_generation(activity, "wlan1"));
  auto changed = std::string(route);
  changed.replace(changed.find("0101A8C0"), 8, "0201A8C0");
  assert(udp::route_generation(route, "wlan1") !=
         udp::route_generation(changed, "wlan1"));
  assert(!udp::route_generation(std::string(65536, 'x'), "wlan1"));
  assert(!udp::route_generation("wlan1\tbroken\n", "wlan1"));

  for (const auto text : {"UTC available", "UTC unavailable", "UTC synchronized", "UTC holdover"})
    assert(wsprry::logging::export_safe_pico("CLOCK_STATE", text));
  assert(!wsprry::logging::export_safe_pico("CLOCK_STATE", "UTC holdover secret"));
  for (const auto text : {"USB control connection changed", "USB WTP port opened",
                          "USB WTP port closed", "USB Management port opened",
                          "USB Management port closed"})
    assert(wsprry::logging::export_safe_pico("USB_STATE", text));
  for (const auto text : {"USB WTP port opened password=secret", "USB Console port opened",
                          "USB Management port changed"})
    assert(!wsprry::logging::export_safe_pico("USB_STATE", text));
  assert(!wsprry::logging::export_safe_pico("UNKNOWN", "USB WTP port opened"));
  assert(wsprry::logging::export_safe_pi("PI_LOG", "UDP logging test event"));
  for (auto text : {"password=secret", "Authorization: Bearer token",
                    "raw HTTP configuration", "UDP logging test event secret"})
    assert(!wsprry::logging::export_safe_pi("PI_LOG", text));
  assert(!wsprry::logging::export_safe_pi("UNKNOWN", "UDP logging test event"));
  auto policy = udp::defaults();
  udp::settings(policy);
  for (auto bad : {udp::Json{}, udp::Json::array(), udp::Json("wrong")}) {
    bool refused = false;
    try {
      udp::settings(bad);
    } catch (...) {
      refused = true;
    }
    assert(refused);
  }
  const auto rejected = [&](const udp::Json &settings) {
    try {
      udp::settings(settings);
      return false;
    } catch (...) {
      return true;
    }
  };
  for (auto address : {"localhost", "127.0.0.1", "0.0.0.0", "224.0.0.1",
                       "192.168.01.1", "255.255.255.255", "192.168.1.1:514"}) {
    auto bad = policy;
    bad["sender"]["destination_ipv4"] = address;
    assert(rejected(bad));
  }
  {
    auto bad = policy;
    bad["sender"]["rate"] = true;
    assert(rejected(bad));
    bad = policy;
    bad["collector"]["extra"] = false;
    assert(rejected(bad));
  }
  bool duplicate = false;
  try {
    udp::parse("{\"sender\":{},\"sender\":{}}");
  } catch (...) {
    duplicate = true;
  }
  assert(duplicate);
  const std::string device(32, 'a'), boot(32, 'b');
  const udp::Json target = {
      {"scope", "member"}, {"device_id", device}, {"boot_id", boot}};
  udp::Json caps = {{"schema", "udp-logging-capabilities/1"},
                    {"target", target},
                    {"resource", "/api/v1/udp-logging"},
                    {"settings_schema", "transmitter-udp-logging/1"},
                    {"wire_schema", "wsprry-udp/1"},
                    {"limits",
                     {{"payload_bytes", 480},
                      {"queue_records", 16},
                      {"maximum_sources", 0},
                      {"maximum_peers", 0},
                      {"maximum_rate", 8},
                      {"maximum_burst", 16},
                      {"address_family", "ipv4"}}}};
  auto saved = policy["sender"];
  for (auto key : {"interface", "native_enabled", "usb_sources"})
    saved.erase(key);
  saved["rate"] = 4;
  saved["burst"] = 8;
  const udp::Json active = {{"state", "disabled"},
                            {"applied_revision", "one"},
                            {"receipt", "unknown"},
                            {"queued", 0},
                            {"counters", udp::counters()}};
  const udp::Json read = {{"schema", "transmitter-udp-logging/1"},
                          {"target", target},
                          {"saved", saved},
                          {"active", active}};
  unsigned writes = 0;
  PicoManagementExchange exchange = [&](auto path, auto method, auto, auto) {
    if (method == "PUT")
      ++writes;
    return PicoHttpResponse{
        200, (path.ends_with("/capabilities") ? caps : read).dump(), "one"};
  };
  assert(pico_udp_logging_request(exchange, device, boot, "udp-logging", "GET",
                                  {}, {})
             .status == 200);
  const auto body = udp::Json{
      {"schema", "transmitter-udp-logging/1"},
      {"target", target},
      {"settings",
       saved}}.dump();
  assert(pico_udp_logging_request(exchange, device, boot, "udp-logging", "PUT",
                                  body, "one")
                 .status == 200 &&
         writes == 1);
  assert(pico_udp_logging_request(exchange, device, boot, "udp-logging", "PUT",
                                  body, "wrong")
                 .status == 412 &&
         writes == 1);
  for (auto version :
       {"udp-logging-capabilities/0", "udp-logging-capabilities/2"}) {
    auto old = caps["schema"];
    caps["schema"] = version;
    assert(pico_udp_logging_request(exchange, device, boot, "udp-logging",
                                    "PUT", body, "one")
                   .status == 400 &&
           writes == 1);
    caps["schema"] = old;
  }
  caps["limits"]["payload_bytes"] = 481;
  assert(pico_udp_logging_request(exchange, device, boot, "udp-logging", "PUT",
                                  body, "one")
                 .status == 400 &&
         writes == 1);
}
