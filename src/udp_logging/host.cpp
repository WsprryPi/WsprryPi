// SPDX-License-Identifier: MIT
#include "udp_logging/host.hpp"
#include "logging.hpp"
#include "udp_logging/io.hpp"
#include "udp_logging/network_policy.hpp"
#include "udp_logging/resource_limits.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/usb_logging.hpp"
#include <chrono>
#include <ctime>
#include <grp.h>
#include <mutex>
#include <net/if.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
namespace wsprrypi {
namespace {
std::mutex policy_mutex;
std::string policy_ini;
bool apply_service(const std::string &ini) {
  const auto pid = ::fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    ::setpgid(0, 0);
    ::execl("/usr/bin/python3", "python3",
            "/usr/local/lib/wsprrypi/udp_log_control.py", "configure", "--ini",
            ini.c_str(), static_cast<char *>(nullptr));
    ::_exit(127);
  }
  ::setpgid(pid, pid);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  int status{};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto n = ::waitpid(pid, &status, WNOHANG);
    if (n == pid)
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (n < 0 && errno != EINTR)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  ::kill(-pid, SIGKILL);
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return false;
}
} // namespace
void start_udp_logging_policy(const std::string &ini) {
  std::lock_guard lock(policy_mutex);
  policy_ini = ini;
}
PicoHttpResponse udp_logging_installation() {
  const auto self = wtp_pi_device_id();
  const auto current = udp_logging_api("GET", {}, {}, false, false);
  std::string policy = "unavailable", service = "inactive", worker = "missing";
  struct stat info{};
  if (::lstat("/usr/local/bin/wsprrypi-log", &info) == 0)
    worker = S_ISREG(info.st_mode) && info.st_uid == 0 &&
                     !(info.st_mode & 0022) && (info.st_mode & 0100)
                 ? "installed"
                 : "unsafe";
  if (current.status == 200) {
    policy = "valid";
    try {
      const auto value = udp::parse(current.body);
      const auto &active = value.at("active");
      if (!active.at("applied_revision").is_null()) {
        service = active.at("collector").at("state") == "bind_failed"
                      ? "bind_error"
                  : (active.at("collector").at("state") == "ready" ||
                     active.at("sender").at("state") == "ready")
                      ? "ready"
                      : "running";
      }
    } catch (...) {
      policy = "invalid";
    }
  } else if (current.status == 400)
    policy = "invalid";
  return {200,
          udp::Json{{"schema", "host-udp-installation/1"},
                    {"target", {{"scope", "host"}, {"collector_id", self}}},
                    {"worker_state", worker},
                    {"native_journal_state",
                     native_journal_available() ? "available" : "unavailable"},
                    {"policy_state", policy},
                    {"service_state", service}}
              .dump(),
          {}};
}
PicoHttpResponse udp_logging_api(const std::string &method,
                                 const std::string &body,
                                 const std::string &revision, bool capabilities,
                                 bool test) {
  std::lock_guard lock(policy_mutex);
  const auto error = [](unsigned n, const char *why) {
    return PicoHttpResponse{
        n, udp::Json{{"error", {{"code", why}}}}.dump(), {}};
  };
  if (policy_ini.empty())
    return error(503, "udp_policy_unavailable");
  const auto self = wtp_pi_device_id();
  const udp::Json target = {{"scope", "host"}, {"collector_id", self}};
  if (capabilities && test) {
    if (method != "GET")
      return error(405, "method_not_allowed");
    return {200,
            udp::Json{{"schema", "udp-logging-test-capabilities/1"},
                      {"target", target},
                      {"resource", "/api/control/v1/host/udp-logging/test"},
                      {"request_schema", "udp-logging-test/1"},
                      {"event_code", "PI_LOG"},
                      {"maximum_rate", 2}}
                .dump(),
            {}};
  }
  if (capabilities) {
    if (method != "GET")
      return error(405, "method_not_allowed");
    return {200,
            udp::Json{{"schema", "udp-logging-capabilities/1"},
                      {"target", target},
                      {"resource", "/api/control/v1/host/udp-logging"},
                      {"settings_schema", "host-udp-logging/1"},
                      {"wire_schema", "wsprry-udp/1"},
                      {"limits",
                       {{"payload_bytes", 480},
                        {"queue_records", 256},
                        {"maximum_sources", 8},
                        {"maximum_peers", 32},
                        {"maximum_rate", 64},
                        {"maximum_burst", 128},
                        {"address_family", "ipv4"}}}}
                .dump(),
            {}};
  }
  try {
    const auto path = policy_ini + ".udp-logs.json";
    const auto bytes = udp::read_file(path, 16384, ::getuid(), true);
    auto saved = udp::defaults();
    std::string etag = "\"udp-host-" + self + "-0\"";
    if (!bytes.empty()) {
      const auto disk = udp::parse(bytes);
      udp::require(udp::fields(disk, {"schema", "collector_id", "revision",
                                      "sender", "collector"}) &&
                       disk["schema"] == "host-udp-logging/1" &&
                       disk["collector_id"] == self &&
                       udp::text(disk["revision"], 128),
                   "udp_policy_binding");
      saved = {{"sender", disk["sender"]}, {"collector", disk["collector"]}};
      udp::settings(saved);
      etag = disk["revision"].get<std::string>();
    }
    if (test) {
      if (method != "POST")
        return error(405, "method_not_allowed");
      if (revision.empty())
        return error(428, "revision_required");
      if (revision != etag)
        return error(412, "revision_conflict");
      const auto input = udp::parse(body, 2048);
      udp::require(udp::fields(input, {"schema", "target"}) &&
                       input["schema"] == "udp-logging-test/1" &&
                       input["target"] == target,
                   "udp_test_target");
      const auto &s = saved["sender"];
      if (!s["enabled"].get<bool>() || !s["native_enabled"].get<bool>() ||
          s["maximum_severity"].get<unsigned>() < 6 ||
          (!s["event_codes"].empty() &&
           std::find(s["event_codes"].begin(), s["event_codes"].end(),
                     "PI_LOG") == s["event_codes"].end()))
        return error(409, "udp_test_filtered_or_disabled");
      static auto last = std::chrono::steady_clock::time_point{};
      const auto now = std::chrono::steady_clock::now();
      if (now - last < std::chrono::milliseconds(500))
        return error(429, "udp_test_rate_limited");
      if (!native_journal_available())
        return error(409, "udp_native_journal_unavailable");
      last = now;
      llog.logS(INFO, "UDP logging test event");
      return {200,
              udp::Json{{"schema", "udp-logging-test/1"},
                        {"target", target},
                        {"accepted", true},
                        {"receipt", "unknown"}}
                  .dump(),
              {}};
    }
    if (method == "PUT") {
      if (revision.empty())
        return error(428, "revision_required");
      if (revision != etag)
        return error(412, "revision_conflict");
      const auto input = udp::parse(body);
      udp::require(udp::fields(input, {"schema", "target", "settings"}) &&
                       input["schema"] == "host-udp-logging/1" &&
                       input["target"] == target,
                   "udp_host_target");
      udp::settings(input["settings"]);
      const auto usb_reply = usb_logging_api("GET", {}, {});
      if (!input["settings"]["sender"]["usb_sources"].empty()) {
        udp::require(usb_reply.status == 200, "udp_usb_policy_unavailable");
        const auto usb = udp::parse(usb_reply.body);
        for (const auto &source : input["settings"]["sender"]["usb_sources"]) {
          bool known = false, collected = false;
          for (const auto &c : usb.at("candidates"))
            known |= c.at("catalog_id") == source["profile_id"] &&
                     c.at("device_id") == source["device_id"];
          for (const auto &c : usb.at("sources"))
            collected |= c.at("catalog_id") == source["profile_id"] &&
                         c.at("device_id") == source["device_id"] &&
                         c.at("enabled") == true;
          bool preserved = false;
          for (const auto &old : saved["sender"]["usb_sources"])
            preserved |= old == source;
          udp::require(
              (known && collected) ||
                  (!input["settings"]["sender"]["enabled"].get<bool>() &&
                   preserved),
              "udp_source_not_collected_in_fleet");
        }
      }
      const auto &candidate = input["settings"];
      if (candidate["sender"]["enabled"].get<bool>() ||
          candidate["collector"]["enabled"].get<bool>())
        udp::require(udp::resource_controllers_available(),
                     "udp_resource_limits_unavailable");
      if (candidate["sender"]["enabled"].get<bool>() &&
          candidate["sender"]["native_enabled"].get<bool>())
        udp::require(native_journal_available(),
                     "udp_native_journal_unavailable");
      const auto &outbound = candidate["sender"];
      if (!outbound["destination_ipv4"].is_null() &&
          (outbound["enabled"].get<bool>() ||
           outbound["destination_ipv4"] !=
               saved["sender"]["destination_ipv4"] ||
           outbound["interface"] != saved["sender"]["interface"])) {
        const auto inventory = udp::interface_inventory(
            outbound["interface"].is_null()
                ? ""
                : outbound["interface"].get<std::string>(),
            udp::ipv4(outbound["destination_ipv4"].get<std::string>()));
        udp::require(inventory.valid && inventory.destination_allowed,
                     "udp_destination_is_local_broadcast_or_wrong_link");
      }
      const auto &collector = candidate["collector"];
      if (collector["enabled"].get<bool>() && collector != saved["collector"]) {
        const auto bound =
            udp::interface_address(collector["interface"].get<std::string>());
        in_addr wanted{};
        inet_pton(AF_INET, collector["bind_ipv4"].get<std::string>().c_str(),
                  &wanted);
        udp::require(bound && bound->s_addr == wanted.s_addr,
                     "udp_bind_address_not_on_selected_interface");
        const auto &old = saved["collector"];
        if (!old["enabled"].get<bool>() ||
            old["bind_ipv4"] != collector["bind_ipv4"] ||
            old["port"] != collector["port"] ||
            old["interface"] != collector["interface"]) {
          const int probe = udp::ipv4_datagram_socket();
          udp::require(probe >= 0, "udp_bind_probe_unavailable");
          sockaddr_in endpoint{};
          endpoint.sin_family = AF_INET;
          endpoint.sin_addr = wanted;
          endpoint.sin_port = htons(collector["port"].get<unsigned>());
          const auto error = bind(
              probe, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint));
          ::close(probe);
          udp::require(error == 0, "udp_bind_endpoint_in_use");
        }
      }
      if (outbound["enabled"].get<bool>() || collector["enabled"].get<bool>()) {
        for (const auto *file :
             {"/usr/local/bin/wsprrypi-log",
              "/usr/local/lib/wsprrypi/udp_log_control.py"}) {
          struct stat info{};
          udp::require(lstat(file, &info) == 0 && S_ISREG(info.st_mode) &&
                           info.st_uid == 0 && !(info.st_mode & 0022),
                       "udp_worker_or_control_not_installed");
        }
      }
      const auto *group = ::getgrnam("wsprrylog");
      udp::require(group ||
                       (!input["settings"]["sender"]["enabled"].get<bool>() &&
                        !input["settings"]["collector"]["enabled"].get<bool>()),
                   "udp_service_not_installed");
      for (auto key : {"sender", "collector"}) {
        const auto &section = input["settings"][key];
        if (!section["interface"].is_null())
          udp::require(
              if_nametoindex(section["interface"].get<std::string>().c_str()) !=
                      0 ||
                  (!section["enabled"].get<bool>() &&
                   section["interface"] == saved[key]["interface"]),
              "udp_interface_unavailable");
      }
      saved = input["settings"];
      etag = "\"udp-host-" + WtpPiBootIdentity{}.new_boot_id() + "\"";
      const udp::Json disk = {{"schema", "host-udp-logging/1"},
                              {"collector_id", self},
                              {"revision", etag},
                              {"sender", saved["sender"]},
                              {"collector", saved["collector"]}};
      udp::atomic_file(path, disk.dump(), 0640,
                       group ? group->gr_gid : static_cast<gid_t>(-1));
      if (!apply_service(policy_ini))
        return error(503, "udp_save_unconfirmed_read_required");
    } else if (method != "GET")
      return error(405, "method_not_allowed");
    udp::Json active = {{"applied_revision", nullptr},
                        {"sender",
                         {{"state", "unavailable"},
                          {"receipt", "unknown"},
                          {"queued", 0},
                          {"counters", udp::counters()}}},
                        {"collector",
                         {{"state", "unavailable"},
                          {"bind_ipv4", nullptr},
                          {"port", nullptr},
                          {"counters", udp::collector_counters()},
                          {"peers", udp::Json::array()}}}};
    const auto *account = ::getpwnam("wsprrylog");
    if (account) {
      try {
        struct stat info{};
        const auto bytes = udp::read_file("/run/wsprrypi-log/status.json",
                                          16384, account->pw_uid, true);
        if (!bytes.empty() &&
            ::lstat("/run/wsprrypi-log/status.json", &info) == 0 &&
            std::time(nullptr) >= info.st_mtime &&
            std::time(nullptr) - info.st_mtime <= 5) {
          const auto value = udp::parse(bytes);
          udp::require(
              udp::fields(value, {"schema", "collector_id", "active"}) &&
                  value["schema"] == "host-udp-logging-status/1" &&
                  value["collector_id"] == self,
              "udp_status_binding");
          udp::host_active(value["active"], saved);
          if (value["active"]["applied_revision"] == etag)
            active = value["active"];
        }
      } catch (...) {
      } // Saved policy remains readable when runtime status is unavailable.
    }
    if (saved["sender"]["enabled"].get<bool>() &&
        saved["sender"]["native_enabled"].get<bool>() &&
        !native_journal_available())
      active["sender"]["state"] = "unavailable";
    return {200,
            udp::Json{{"schema", "host-udp-logging/1"},
                      {"target", target},
                      {"saved", saved},
                      {"active", active}}
                .dump(),
            etag};
  } catch (const std::exception &e) {
    return error(400, e.what());
  }
}
} // namespace wsprrypi
