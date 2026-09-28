// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/discovery.hpp"
#include "wtp_settings_json.hpp"
#include <cassert>
#include <stdexcept>
#include <unistd.h>

using namespace wsprrypi;
int main() {
  assert(wtp_dns_sd_binding({"txtvers=1", "binding=tls"}) == "tls");
  assert(wtp_dns_sd_binding({"txtvers=1", "binding=plain", "future=x", "feature", "FUTURE=y"}) == "plain");
  for (const auto &txt : {std::vector<std::string>{"binding=tls", "txtvers=1"},
                          {"txtvers=2", "binding=tls"}, {"txtvers=1"},
                          {"txtvers=1", "binding=plain", "BINDING=tls"},
                          {"txtvers=1", "TXTvers=1", "binding=tls"},
                          {"txtvers=1", "binding=unknown"}})
    assert(!wtp_dns_sd_binding(txt));

  WtpDiscovery discovery;
  discovery.available(true);
  const WtpServiceKey one{1, 0, "Shared Pico", "local"};
  const WtpServiceKey two{2, 0, "Shared Pico", "local"};
  discovery.found(one, 1000);
  discovery.found(two, 1000);
  discovery.resolved(one, "pico-one.local", 40123, {"txtvers=1", "binding=tls"}, 1001);
  discovery.resolved(two, "pico-two.local", 31417, {"txtvers=1", "binding=plain"}, 1001);
  auto snapshot = discovery.snapshot(1002);
  assert(snapshot["candidates"].size() == 2);
  assert(snapshot["candidates"][0]["port"] == 40123);
  assert(snapshot["candidates"][0]["id"] != snapshot["candidates"][1]["id"]);
  const auto id = snapshot["candidates"][0]["id"].get<std::string>();
  assert(discovery.candidate(id, 1002).has_value());
  discovery.removed(one, 1003);
  assert(!discovery.candidate(id, 1004));
  assert(discovery.snapshot(1004)["candidates"][0]["state"] == "removed");
  discovery.restart(1005);
  assert(discovery.snapshot(1006)["candidates"][1]["state"] == "stale");
  discovery.found(one, 1007);
  discovery.resolved(one, "pico-one.local", 1, {"txtvers=1", "binding=tls", "BINDING=plain"}, 1008);
  assert(discovery.snapshot(1009)["candidates"][0]["state"] == "invalid");
  discovery.failed(two, "resolver failed", 1010);
  assert(discovery.snapshot(1011)["candidates"][1]["state"] == "failed");
  discovery.available(false, "daemon stopped");
  assert(!discovery.snapshot(1012)["available"].get<bool>());

  char name[] = "/tmp/wsprrypi-catalog-XXXXXX";
  const int fd = mkstemp(name);
  assert(fd >= 0); close(fd); unlink(name);
  WtpCatalog catalog(name);
  WtpSettings legacy;
  legacy.path = "/dev/ttyACM1"; legacy.usb_serial = "123";
  legacy.vendor_id = 0xcafe; legacy.product_id = 0x4012;
  legacy.device_id = "a" + std::string(31, 'a');
  auto [initial, revision] = catalog.snapshot(legacy);
  assert(initial["profiles"].size() == 1 && !initial["active_id"].get<std::string>().empty());
  assert(initial["profiles"][0]["settings"] == wtp_settings_json(legacy));
  auto [again, same_revision] = catalog.snapshot(legacy);
  assert(initial == again && same_revision == revision);
  bool rejected = false;
  try { catalog.mutate({{"operation", "remove"}, {"id", initial["active_id"]}}, "\"stale\"", legacy); }
  catch (const std::exception &e) { rejected = std::string(e.what()) == "revision_conflict"; }
  assert(rejected);
  auto plain = legacy;
  plain.transport = "network_plain"; plain.hostname = "pico.local"; plain.tcp_port = 41234;
  rejected = false;
  try { catalog.mutate({{"operation", "add"}, {"name", "Plain"}, {"method", "manual"},
      {"discovery_id", ""}, {"settings", wtp_settings_json(plain)}, {"consent_plain", false}}, revision, legacy); }
  catch (...) { rejected = true; }
  assert(rejected);
  auto [added, added_revision] = catalog.mutate({{"operation", "add"}, {"name", "Plain"},
      {"method", "manual"}, {"discovery_id", ""}, {"settings", wtp_settings_json(plain)},
      {"consent_plain", true}}, revision, legacy);
  assert(added["profiles"].size() == 2 && added_revision != revision);
  const auto plain_id = added["profiles"][1]["id"].get<std::string>();
  assert(catalog.profile(plain_id, added_revision, legacy).settings == plain);
  auto [renamed, renamed_revision] = catalog.mutate({{"operation", "rename"},
      {"id", plain_id}, {"name", "Nearby bench"}}, added_revision, legacy);
  assert(renamed["profiles"][1]["name"] == "Nearby bench");
  auto [removed, removed_revision] = catalog.mutate({{"operation", "remove"},
      {"id", plain_id}}, renamed_revision, legacy);
  assert(removed["profiles"].size() == 1 && removed_revision != renamed_revision);
  unlink(name);
}
