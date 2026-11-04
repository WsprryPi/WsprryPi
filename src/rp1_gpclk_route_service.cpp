#include "rp1_gpclk_route_service.hpp"
#include "rp1_provider_process.hpp"
#include "WSPR-Transmitter/src/rp1_gpclk_development_policy.hpp"
#ifndef WSPRRYPI_ROUTE_SERVICE_TEST
#include "config_handler.hpp"
#include "scheduling.hpp"
#endif
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
#ifndef WSPRRYPI_ROUTE_SERVICE_TEST
constexpr const char *kSocket = "/run/rp1-gpclk-dkms/route-manager.sock";
#endif
constexpr const char *kRuntimeContract = "rp1-gpclk-route-manager-runtime";
constexpr const char *kContract = "rp1-gpclk-route-manager";
#ifndef WSPRRYPI_ROUTE_SERVICE_TEST
std::string ownedRuntimeBinding() {
  const char *path = "/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json";
  const int fd = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    throw std::runtime_error("WsprryPi runtime ownership is unavailable.");
  struct stat info {};
  if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
      (info.st_mode & 0077) != 0 || info.st_size <= 0 ||
      info.st_size > 4 * 1024 * 1024) {
    ::close(fd);
    throw std::runtime_error("WsprryPi runtime ownership is unsafe.");
  }
  std::string payload;
  payload.reserve(static_cast<std::size_t>(info.st_size));
  char buffer[4096];
  for (;;) {
    const auto count = ::read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0) {
      ::close(fd);
      throw std::runtime_error("Could not read WsprryPi runtime ownership.");
    }
    if (count == 0)
      break;
    payload.append(buffer, static_cast<std::size_t>(count));
    if (payload.size() > 4U * 1024U * 1024U) {
      ::close(fd);
      throw std::runtime_error("WsprryPi runtime ownership exceeded its bound.");
    }
  }
  ::close(fd);
  const auto record = nlohmann::json::parse(payload);
  if (!record.is_object() ||
      record.value("schema", std::string{}) !=
          "wsprrypi-rp1-gpclk-dkms-ownership-v3" ||
      record.value("repository", std::string{}) !=
          "WsprryPi/RP1-GPCLK-DKMS" ||
      record.value("owner", std::string{}) != "WsprryPi" ||
      !record.contains("runtime") || !record["runtime"].is_object())
    throw std::runtime_error("WsprryPi runtime ownership contract differs.");
  const auto &runtime = record["runtime"];
  const auto digest = runtime.contains("bindingSha256") &&
                              runtime["bindingSha256"].is_string()
                          ? runtime["bindingSha256"].get<std::string>()
                          : std::string{};
  if (runtime.value("readinessContract", std::string{}) !=
          "rp1-gpclk-runtime-readiness-v1" ||
      runtime.value("state", std::string{}) != "neutral_ready" ||
      !runtime.contains("route") || !runtime["route"].is_null() ||
      runtime.value("output", std::string{}) != "disabled" ||
      digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("WsprryPi neutral runtime ownership differs.");
  return digest;
}

nlohmann::json providerCommand(const std::string &operation,
                               const std::string &route,
                               const std::string &digest = {}) {
  if ((route != "gpio4" && route != "gpio20") ||
      (operation != "route-plan" && operation != "route-ensure"))
    throw std::runtime_error("Invalid fixed RP1 runtime-provider request.");
  if (operation == "route-ensure" &&
      (digest.size() != 64 ||
       digest.find_first_not_of("0123456789abcdef") != std::string::npos))
    throw std::runtime_error("Invalid reviewed RP1 route-plan digest.");
  std::vector<std::string> arguments{"/usr/bin/python3", "-I",
      "/usr/lib/rp1-gpclk-dkms/runtime_provider.py", operation, "--route", route};
  if (operation == "route-ensure") {
    arguments.push_back("--plan-sha256");
    arguments.push_back(digest);
  }
  return nlohmann::json::parse(runRp1ProviderProcess(arguments));
}

void launchRuntimeRouteOperation(const std::string &operation,
                                 const std::string &route,
                                 const std::string &digest,
                                 const std::string &request_id) {
  if ((operation != "switch" && operation != "remove" &&
       operation != "recover") ||
      (route != "gpio4" && route != "gpio20") ||
      request_id.empty() || request_id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") !=
          std::string::npos ||
      (operation == "switch" &&
       (digest.size() != 64 ||
        digest.find_first_not_of("0123456789abcdef") != std::string::npos)) ||
      ((operation == "remove" || operation == "recover") && !digest.empty()))
    throw std::runtime_error("Invalid detached RP1 route operation.");

  const std::string unit = "wsprrypi-rp1-route-" +
                           std::to_string(static_cast<long long>(::getpid())) +
                           "-" + request_id;
  std::vector<std::string> arguments{"/usr/bin/systemd-run", "--quiet", "--no-block",
      "--collect", "--unit", unit, "--property=Type=exec",
      "--property=TimeoutStartSec=60s", "--property=RuntimeMaxSec=90s", "/usr/bin/python3"};
  if (operation == "switch") {
    arguments.insert(arguments.end(), {"/usr/lib/rp1-gpclk-dkms/runtime_provider.py",
        "route-ensure", "--route", route, "--plan-sha256", digest});
  } else {
    arguments.insert(arguments.end(), {"/usr/lib/rp1-gpclk-dkms/runtime_route_client.py", operation});
    if (operation == "remove") arguments.push_back(route);
    arguments.push_back("--execute");
  }
  (void)runRp1ProviderProcess(arguments, std::chrono::seconds{3}, false);
}

nlohmann::json socketRequest(const nlohmann::json &request) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  const std::string payload = request.dump() + "\n";
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    throw std::runtime_error(
        "Could not create the RP1 GPCLK route-manager socket.");
  if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    ::close(fd);
    throw std::runtime_error(
        "Could not secure the RP1 GPCLK route-manager socket.");
  }
  const timeval timeout{3, 0};
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
      ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
    ::close(fd);
    throw std::runtime_error("Could not bound the route-manager connection.");
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (std::strlen(kSocket) >= sizeof(address.sun_path)) {
    ::close(fd);
    throw std::runtime_error("The fixed route-manager socket path is invalid.");
  }
  std::strcpy(address.sun_path, kSocket);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) !=
      0) {
    const std::string detail = std::strerror(errno);
    ::close(fd);
    throw std::runtime_error(
        "Could not connect to the RP1 GPCLK route manager: " + detail);
  }
  return nlohmann::json::parse(exchangeRp1ProviderSocket(fd, payload, deadline));
}
#endif
std::string field(const nlohmann::json &value, const char *name) {
  return value.contains(name) && value[name].is_string()
             ? value[name].get<std::string>()
             : std::string{};
}
} // namespace
Rp1GpclkRouteService::Rp1GpclkRouteService(
    Rp1GpclkRouteExecutorOperations operations)
    : operations_(std::move(operations)) {}
std::string Rp1GpclkRouteService::routeForGpio(int gpio) {
  return gpio == 4 ? "GPIO4" : gpio == 20 ? "GPIO20" : std::string{};
}
int Rp1GpclkRouteService::gpioForRoute(const std::string &route) {
  return route == "gpio4" || route == "GPIO4"     ? 4
         : route == "gpio20" || route == "GPIO20" ? 20
                                                  : 0;
}
bool Rp1GpclkRouteService::idle() const {
  return operations_.idle_state && operations_.idle_state().complete();
}
std::string Rp1GpclkRouteService::requestId(const char *operation,
                                            std::uint64_t generation) {
  std::ostringstream value;
  value << "wsprrypi-" << operation << '-' << std::hex << std::setw(8)
        << std::setfill('0') << generation;
  return value.str();
}
nlohmann::json Rp1GpclkRouteService::failure(const std::string &result,
                                             const std::string &message) const {
  return {{"ok", false},
          {"result", result},
          {"state", runtime_profile_ ? "runtime_recovery" : "unavailable"},
          {"profile", runtime_profile_ ? "runtime" : "legacy"},
          {"message", message},
          {"generation", generation_},
          {"requested", "Unavailable"},
          {"persisted", routeForGpio(operations_.persisted_gpio())},
          {"configured", "Unavailable"},
          {"active", "Unavailable"},
          {"reconciled", false},
          {"compatible", false},
          {"eligible", false},
          {"liveQualification", "Not inspected"},
          {"applicationEligibility", "WsprryPi band/mode policy"},
          {"contractIdentity", kContract},
          {"bootOwnership", "unknown"},
          {"journal", "unknown"},
          {"services", nlohmann::json::object()},
          {"endpointOwned", false},
          {"endpointOpen", true},
          {"outputInhibited", "Unknown"},
          {"operationalReady", "Unknown"}};
}
nlohmann::json Rp1GpclkRouteService::withQualification(nlohmann::json result) const {
  result["applicationEligibility"] = "WsprryPi band/mode policy";
  if (!operations_.qualification_status)
    return result;
  try {
    const auto route = field(result, "active");
    const auto evidence = operations_.qualification_status(
        route == "GPIO4" ? "gpio4" : route == "GPIO20" ? "gpio20" : "");
    if (evidence.is_object()) {
      result["qualificationEvidence"] = evidence;
      result["liveQualification"] = evidence.value("state", std::string("Evidence missing"));
      result["structuralCompatibility"] = evidence.value("structuralCompatibility", std::string("Unknown"));
    }
  } catch (const std::exception &) {
    result["liveQualification"] = "Evidence inspection failed";
  }
  // Qualification is observational. Never change compatible, eligible or inhibition here.
  return result;
}
nlohmann::json Rp1GpclkRouteService::request(const nlohmann::json &value) {
  try {
    const auto response = operations_.request(value);
    if (response.is_object() && response.value("schemaVersion", 0) == 3 &&
        response.value("contract", std::string{}) == kRuntimeContract &&
        response.contains("status") &&
        (response.contains("state") || response.contains("error"))) {
      runtime_profile_ = true;
      operations_.set_transmission_inhibited(true, "Runtime route administration is output-disabled");
      return response;
    }
    if (runtime_profile_ || !response.is_object() ||
        response.value("contract", std::string{}) != kContract ||
        !response.contains("status") ||
        (response.contains("state") == response.contains("error")))
      return failure("contract_mismatch",
                     "The RP1 GPCLK route-manager response contract differs.");
    return response;
  } catch (const std::exception &error) {
    return failure("provider_unavailable", error.what());
  }
}
nlohmann::json Rp1GpclkRouteService::render(const nlohmann::json &response,
                                            const std::string &requested) try {
  if (response.contains("ok"))
    return response;
  const std::string status = field(response, "status");
  if (!response.contains("state")) {
    const auto message = response.contains("error")
                             ? field(response["error"], "message")
                             : "Route-manager request failed.";
    return failure("route_manager_error", message);
  }
  const auto &state = response["state"];
  if (runtime_profile_) {
    auto result = failure("runtime_inhibited", "Switching restarts a running Wsprry Pi in idle mode. Transmission does not resume.");
    const bool valid = state.is_object() && state.value("profile", std::string{}) == "runtime" &&
        state.contains("outputEnabled") && state["outputEnabled"] == false &&
        state.contains("qualification") && state["qualification"] == false &&
        state.contains("controller") && state["controller"].is_object();
    result["profile"] = "runtime";
    result["contractIdentity"] = kRuntimeContract;
    result["ok"] = valid && status != "error";
    result["compatible"] = valid;
    result["state"] = status == "error" ? "runtime_recovery" : "runtime_inhibited";
    result["active"] = routeForGpio(gpioForRoute(field(state, "activeRoute")));
    result["requested"] = requested.empty() ? routeForGpio(operations_.persisted_gpio()) : requested;
    const auto runtime_safety = state.value("safety", nlohmann::json::object());
    result["outputInhibited"] =
        runtime_safety.contains("outputInhibited") &&
                runtime_safety["outputInhibited"].is_boolean()
            ? nlohmann::json(runtime_safety["outputInhibited"].get<bool>()
                                 ? "Enabled" : "Disabled")
            : nlohmann::json("Unknown");
    result["operationalReady"] =
        runtime_safety.contains("operationalReady") &&
                runtime_safety["operationalReady"].is_boolean()
            ? nlohmann::json(runtime_safety["operationalReady"].get<bool>()
                                 ? "Ready" : "Not ready")
            : nlohmann::json("Unknown");
    result["bootOwnership"] = "runtime controller";
    result["journal"] = state.contains("pendingTransaction") && state["pendingTransaction"].is_object()
        ? field(state["pendingTransaction"], "phase") : "none";
    result["preflightValidated"] = valid && status == "ok" &&
        state.value("applicationRestoration", false) && field(state, "preflightToken").size() == 64;
    if (!field(state, "preflightToken").empty() && !state.value("applicationRestoration", false)) {
      result["message"] = "Upgrade the route manager before switching: this version cannot restore application availability.";
    }
    result["controller"] = state.value("controller", nlohmann::json::object());
    if (valid) {
      const auto phase = result["journal"].get<std::string>();
      if ((state["controller"].value("flags", 0) & 1) ||
          (phase != "none" && phase != "complete-inhibited" && phase != "recovered-inhibited"))
        result["state"] = "runtime_recovery";
    }
    if (state.contains("application") && state["application"].is_object()) {
      const auto &application = state["application"];
      const auto phase = field(application, "phase");
      result["application"] = application;
      result["services"] = {{"wsprrypi.service", phase}};
      if ((phase == "neutral-restored" || phase == "neutral-stopped" ||
           phase == "neutral-administrator-masked") && valid &&
          state["controller"].value("id", -1) == 0 &&
          state["controller"].value("route", -1) == 0 &&
          state["controller"].value("flags", -1) == 0 &&
          state["controller"].value("error", -1) == 0 &&
          field(application, "boot") == field(state, "bootId") &&
          field(application, "binding") == field(state, "bindingSha256")) {
        result["state"] = phase == "neutral-restored"
                              ? "runtime_neutral_running"
                              : "runtime_neutral_stopped";
        result["configured"] = "None";
        result["active"] = "None";
        result["reconciled"] = true;
        result["message"] = phase == "neutral-restored"
            ? "The RP1 clock route is removed and Wsprry Pi is back online. Transmission remains disabled."
            : phase == "neutral-stopped"
                  ? "The RP1 clock route is removed. Wsprry Pi remains stopped because it was stopped before removal."
                  : "The RP1 clock route is removed. Wsprry Pi remains stopped because the administrator mask is preserved.";
      } else if ((phase == "restored" || phase == "stopped" || phase == "administrator-masked") &&
          valid && application.value("controller", nlohmann::json::object()) == state["controller"] &&
          state["controller"].value("flags", 0) == 6 && state["controller"].value("error", -1) == 0 &&
          routeForGpio(operations_.persisted_gpio()) == field(result, "active") &&
          field(application, "boot") == field(state, "bootId") &&
          field(application, "binding") == field(state, "bindingSha256")) {
        result["state"] = "runtime_ready";
        result["configured"] = result["active"];
        result["reconciled"] = true;
        result["journal"] = "none";
        result["message"] = phase == "restored"
            ? field(result, "active") +
                  " is active and Wsprry Pi is back online in idle mode. "
                  "This does not start or authorize transmission."
            : phase == "stopped" ? "Route switched; Wsprry Pi remains stopped as requested by its prior state."
            : "Route switched; the administrator's service mask is preserved.";
      } else if (phase == "restored" || phase == "stopped" || phase == "administrator-masked") {
        result["state"] = "runtime_recovery";
        result["ok"] = false;
        result["message"] = "The last restoration record does not match the current route and application configuration. Inspect the route before switching.";
      } else if (phase == "restoration-failed") {
        result["state"] = "runtime_restoration_failed";
        result["ok"] = false;
        result["message"] = "Route is installed, but application restoration failed. Run runtime_route_client.py restore --execute. " + field(application, "error");
      } else if (phase == "neutral-restoration-failed") {
        result["state"] = "runtime_neutral_restoration_failed";
        result["ok"] = false;
        result["message"] = "The RP1 clock route was removed, but Wsprry Pi could not be restored. Transmission remains disabled. After correcting the reported service error, retry removal with runtime_route_client.py remove " + field(application, "route") + " --execute. " + field(application, "error");
      } else if (phase == "route-failed" || phase == "route-recovered") {
        result["state"] = "runtime_recovery";
      } else {
        result["state"] = "runtime_restoring";
        result["message"] = "Route administration is in progress. Refresh status after Wsprry Pi reconnects.";
      }
    }
    if (response.contains("error")) {
      result["message"] = field(response["error"], "message");
      result["error"] = response["error"];
      if (response["error"].contains("kernelError") && response["error"].contains("overlayId"))
        result["message"] = field(response["error"], "message") + " Kernel errno: " +
            response["error"]["kernelError"].dump() + "; retained overlay ID: " +
            response["error"]["overlayId"].dump() + ". Transmission remains inhibited.";
    }
    return result;
  }
  const std::string configured = routeForGpio(
                        gpioForRoute(field(state, "configuredRoute"))),
                    active =
                        routeForGpio(gpioForRoute(field(state, "activeRoute"))),
                    persisted = routeForGpio(operations_.persisted_gpio());
  const bool pending = state.contains("pendingTransaction") &&
                       !state["pendingTransaction"].is_null();
  const bool aligned =
      !persisted.empty() && persisted == configured && persisted == active;
  std::string ui = pending ? "pending"
                   : aligned ? "active" : "mismatch";
  if (status == "awaiting-reboot" || status == "reboot-requested")
    ui = "staged";
  else if (status == "recovery-required")
    ui = "rollback_required";
  const nlohmann::json safety =
      state.contains("safety") && state["safety"].is_object()
          ? state["safety"]
          : nlohmann::json::object();
  const bool output_enabled = safety.contains("outputInhibited") &&
                              safety["outputInhibited"].is_boolean() &&
                              !safety["outputInhibited"].get<bool>();
  const bool operational_ready = safety.contains("operationalReady") &&
                                 safety["operationalReady"].is_boolean() &&
                                 safety["operationalReady"].get<bool>();
  const bool preflight_safe = safety.value("endpointOwned", false) &&
                              !safety.value("endpointOpen", true) &&
                              output_enabled && operational_ready;
  const bool accepted = status == "ok" || status == "complete" ||
                        status == "rolled-back" || status == "reboot-requested";
  return {{"ok", accepted},
          {"result", status},
          {"state", ui},
          {"message", ""},
          {"generation", generation_},
          {"requested", requested.empty()
                            ? persisted
                            : routeForGpio(gpioForRoute(requested))},
          {"persisted", persisted},
          {"configured", configured},
          {"active", active},
          {"reconciled", aligned && !pending},
          {"compatible", aligned},
          {"preflightValidated", preflight_safe},
          {"eligible", false},
          {"liveQualification", "Not inspected"},
          {"contractIdentity", kContract},
          {"moduleRoute", active},
          {"bootOwnership", field(state, "bootOwnership")},
          {"bootId", field(state, "bootId")},
          {"journal", pending ? "pending" : "none"},
          {"services", safety.value("services", nlohmann::json::object())},
          {"endpointOwned", safety.value("endpointOwned", false)},
          {"endpointOpen", safety.value("endpointOpen", true)},
          {"outputInhibited",
           safety.contains("outputInhibited") && safety["outputInhibited"].is_boolean()
               ? nlohmann::json(safety["outputInhibited"].get<bool>() ? "Enabled"
                                                                       : "Disabled")
               : nlohmann::json("Unknown")},
          {"operationalReady",
           safety.contains("operationalReady") && safety["operationalReady"].is_boolean()
               ? nlohmann::json(safety["operationalReady"].get<bool>() ? "Ready"
                                                                        : "Not ready")
               : nlohmann::json("Unknown")}};
} catch (const std::exception &error) {
  return failure("provider_response_invalid", error.what());
}

bool Rp1GpclkRouteService::acknowledgeRestoration(const std::string &token,
                                                 bool transmit) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (transmit || !idle() || startup_failure_latched_)
    return false;
  const int gpio = operations_.persisted_gpio();
  if (gpio != 4 && gpio != 20)
    return false;
  const auto reply = request({{"schemaVersion", 3}, {"operation", "application-ready"},
      {"route", gpio == 4 ? "gpio4" : "gpio20"}, {"token", token},
      {"pid", static_cast<int>(::getpid())}, {"transmit", false}});
  return field(reply, "status") == "ok";
}

nlohmann::json Rp1GpclkRouteService::query(bool include_qualification) {
  nlohmann::json result;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    result = render(request({{"operation", "query"}}));
  }
  // Slow evidence inspection belongs to the detailed HTTP query. It must not
  // hold route/configuration locks or delay scheduler status and STOP delivery.
  return include_qualification ? withQualification(std::move(result)) : result;
}
nlohmann::json Rp1GpclkRouteService::operate(const std::string &operation,
                                             const std::string &route,
                                             std::uint64_t generation) {
  std::lock_guard<std::mutex> guard(mutex_);
  try {
  invalidateRp1GpclkDevelopmentOperation();
  const int gpio = gpioForRoute(route);
  const std::string executor_route = gpio == 4    ? "gpio4"
                                     : gpio == 20 ? "gpio20"
                                                  : "";
  if (runtime_profile_) {
    if (!idle())
      return failure("not_idle", "Runtime administration requires the complete application lifecycle to be idle.");
    if (operation == "preflight") {
      runtime_plan_digest_.clear();
      runtime_binding_digest_.clear();
      preflight_route_.clear();
      if (executor_route.empty())
        return failure("invalid_route", "Route must be GPIO4 or GPIO20.");
      if (!operations_.runtime_route_plan)
        return failure("provider_unavailable", "The digest-bound runtime route planner is unavailable.");
      nlohmann::json raw;
      try {
        raw = operations_.runtime_route_plan(executor_route);
      } catch (const std::exception &error) {
        return failure("route_plan_failed", error.what());
      }
      const auto plan = raw.value("routePlan", nlohmann::json::object());
      const auto digest = field(plan, "planSha256");
      const auto binding = field(plan, "bindingSha256");
      const auto safety = raw.value("safety", nlohmann::json::object());
      const auto classification = raw.value("result", std::string{});
      const bool operational_ready_reported =
          safety.contains("operationalReady") &&
          safety["operationalReady"].is_boolean();
      const bool operational_ready =
          operational_ready_reported && safety["operationalReady"].get<bool>();
      const bool already_ready_reported =
          plan.contains("alreadyReady") && plan["alreadyReady"].is_boolean();
      const bool already_ready =
          already_ready_reported && plan["alreadyReady"].get<bool>();
      const auto routes = raw.value("routes", nlohmann::json::object());
      const auto active_route = field(routes, "active");
      const bool exact_route_state =
          (active_route == "gpio4" || active_route == "gpio20") &&
          already_ready == (active_route == executor_route);
      const bool eligibility =
          (classification == "neutral_ready" &&
           !raw.value("routeSelected", true) &&
           !raw.value("executionReady", true) &&
           already_ready_reported && !already_ready) ||
          (classification == "exact_ready" &&
           raw.value("routeSelected", false) &&
           raw.value("executionReady", false) &&
           already_ready_reported && exact_route_state);
      const bool readiness_consistent =
          operational_ready_reported &&
          ((classification == "neutral_ready" && !operational_ready) ||
           (classification == "exact_ready" && operational_ready));
      const bool contract_matches =
          raw.value("contract", std::string{}) ==
          "rp1-gpclk-runtime-readiness-v1";
      const bool state_matches =
          raw.value("state", std::string{}) == classification;
      const bool neutral_idle_evidence =
          contract_matches && state_matches &&
          classification == "neutral_ready" && eligibility &&
          raw.value("administrationEligible", false) &&
          safety.value("outputInhibited", true) == false &&
          safety.value("owner", true) == false &&
          safety.value("lease", true) == false &&
          field(plan, "operation") == "select" &&
          field(plan, "route") == executor_route;
      const bool valid = contract_matches &&
                         state_matches &&
                         eligibility &&
                         raw.value("administrationEligible", false) &&
                         safety.value("outputInhibited", true) == false &&
                         readiness_consistent &&
                         safety.value("owner", true) == false &&
                         safety.value("lease", true) == false &&
                         field(plan, "operation") == "select" &&
                         field(plan, "route") == executor_route &&
                         binding.size() == 64 &&
                         binding
                                 .find_first_not_of("0123456789abcdef") ==
                             std::string::npos &&
                         digest.size() == 64 &&
                         digest.find_first_not_of("0123456789abcdef") ==
                             std::string::npos;
      const auto invalid_message = neutral_idle_evidence
          ? "Route not switched. The provider route plan was rejected before any change began. Transmission remains disabled."
          : "Route not switched. The provider route plan could not be validated. Current route state is unknown; refresh before further administration. Transmission remains disabled.";
      auto result = failure(
          valid ? "route_plan_reviewed" : "route_plan_failed",
          valid ? "Route plan reviewed. No route change has started. Transmission remains disabled until you confirm the switch."
                : invalid_message);
      result["ok"] = valid;
      result["state"] = valid ? "runtime_preflight_ready"
                              : "runtime_preflight_failed";
      result["changeStarted"] = false;
      result["recoveryRequired"] = false;
      result["preflightValidated"] = valid;
      result["requested"] = route;
      if (valid) {
        runtime_plan_digest_ = digest;
        runtime_binding_digest_ = binding;
        preflight_route_ = executor_route;
        result["generation"] = ++generation_;
        result["planSha256"] = digest;
      }
      return result;
    }
    if (operation != "switch" && operation != "remove" && operation != "recover")
      return failure("invalid_operation", "Runtime profile requires explicit switch, remove, or recover; reboot operations are not translated.");
    if (operation == "switch" && (generation == 0 || generation != generation_ ||
        runtime_plan_digest_.empty() || runtime_binding_digest_.empty() ||
        executor_route != preflight_route_))
      return failure("generation_mismatch", "Repeat runtime preflight before switching.");
    operations_.set_transmission_inhibited(true, "Runtime route administration in progress");
    if (operation == "switch") {
      if (!operations_.runtime_route_launch)
        return failure("provider_unavailable", "The detached runtime route executor is unavailable.");
      const auto request_id = requestId("switch", ++generation_);
      try {
        operations_.runtime_route_launch("switch", executor_route,
                                         runtime_plan_digest_,
                                         runtime_binding_digest_, request_id);
      } catch (const std::exception &error) {
        runtime_plan_digest_.clear();
        runtime_binding_digest_.clear();
        preflight_route_.clear();
        return failure("route_ensure_failed", error.what());
      }
      runtime_plan_digest_.clear();
      runtime_binding_digest_.clear();
      preflight_route_.clear();
      auto result = failure("runtime_route_requested",
                            "The reviewed route operation was queued. Wsprry Pi will disconnect briefly; refresh after it reconnects to confirm the route. Transmission remains disabled.");
      result["ok"] = true;
      result["requested"] = route;
      result["state"] = "runtime_switch_queued";
      result["requestId"] = request_id;
      return result;
    }
    if (!operations_.runtime_route_launch)
      return failure("provider_unavailable", "The detached runtime route executor is unavailable.");
    const auto request_id = requestId(operation.c_str(), ++generation_);
    runtime_plan_digest_.clear();
    runtime_binding_digest_.clear();
    preflight_route_.clear();
    try {
      operations_.runtime_route_launch(operation, executor_route, {},
                                       {}, request_id);
    } catch (const std::exception &error) {
      return failure("route_recovery_failed", error.what());
    }
    auto result = failure(
        operation == "remove" ? "runtime_remove_requested"
                              : "runtime_recovery_requested",
        operation == "remove"
            ? "Route removal was queued. A previously running Wsprry Pi will return after the neutral route is verified. Transmission remains disabled."
            : "Route recovery was queued. Wsprry Pi will stop and remain inhibited; use the operator client to confirm recovery. Transmission remains disabled.");
    result["ok"] = true;
    result["state"] = operation == "remove" ? "runtime_remove_queued"
                                              : "runtime_recovery_queued";
    result["requested"] = route;
    result["requestId"] = request_id;
    return result;
  }
  if (operation == "preflight") {
    if (!idle())
      return failure("not_idle",
                     "Route changes require the controller, scheduler, "
                     "provider, drain, and cleanup lifecycle to be idle.");
    if (executor_route.empty())
      return failure("invalid_route", "Route must be exactly GPIO4 or GPIO20.");
    auto raw = request({{"operation", "preflight"},
                        {"route", executor_route}});
    if (!raw.contains("state"))
      return render(raw, route);
    auto rendered = render(raw, route);
    if (!rendered.value("ok", false))
      return rendered;
    if (!rendered.value("compatible", false) ||
        !rendered.value("preflightValidated", false)) {
      rendered["ok"] = false;
      rendered["result"] = "preflight_failed";
      rendered["message"] =
          "Preflight did not confirm the exact packaged identity, endpoint "
          "closure, ownership, output_inhibit=0, and operational readiness.";
      return rendered;
    }
    ++generation_;
    preflight_route_ = executor_route;
    rendered["generation"] = generation_;
    rendered["message"] =
        "External route-manager preflight passed. Qualification evidence and "
        "application band/mode eligibility are reported separately.";
    return rendered;
  }
  if (operation == "apply-and-reboot") {
    if (!idle())
      return failure(
          "not_idle",
          "Route changes require a completely idle application lifecycle.");
    if (generation == 0 || generation != generation_ ||
        executor_route != preflight_route_)
      return failure("generation_mismatch",
                     "The RP1 GPCLK preflight generation is stale.");
    std::string persistence_error;
    if (!operations_.persist_gpio ||
        !operations_.persist_gpio(gpio, &persistence_error))
      return failure("persistence_failed",
                     persistence_error.empty()
                         ? "Could not persist the requested route."
                         : persistence_error);
    const auto raw = request({{"operation", "apply-and-reboot"},
                              {"route", executor_route},
                              {"execute", true},
                              {"requestId", requestId("apply", generation)},
                              {"actor", "wsprrypi.service"}});
    return render(raw, route);
  }
  if (operation == "rollback") {
    const auto raw = request({{"operation", "rollback"},
                              {"execute", true},
                              {"requestId", requestId("rollback", generation)},
                              {"actor", "wsprrypi.service"}});
    return render(raw);
  }
  return failure("invalid_operation",
                 "Operation must be preflight, apply-and-reboot, or rollback.");
  } catch (const std::exception &error) {
    invalidateRp1GpclkDevelopmentOperation();
    startup_failure_latched_ = true;
    operations_.set_transmission_inhibited(true, error.what());
    return failure("provider_response_invalid", error.what());
  }
}
nlohmann::json Rp1GpclkRouteService::reconcileStartup() {
  std::lock_guard<std::mutex> guard(mutex_);
  invalidateRp1GpclkDevelopmentOperation();
  operations_.set_transmission_inhibited(
      true, "RP1 GPCLK route reconciliation is pending");
  if (startup_failure_latched_)
    return failure("startup_failure_latched",
                   "A prior RP1 GPCLK startup reconciliation failure keeps "
                   "transmission inhibited for this process lifetime.");
  const auto raw =
      request({{"operation", "reconcile"},
               {"execute", true},
               {"requestId", requestId("reconcile", ++generation_)},
               {"actor", "wsprrypi.service"}});
  auto rendered = render(raw);
  const bool reconciled = rendered.value("ok", false) &&
                          rendered.value("reconciled", false) &&
                          rendered.value("compatible", false) &&
                          rendered.value("journal", std::string{}) == "none";
  rendered["ok"] = reconciled;
  if (!reconciled) {
    rendered["result"] = "startup_reconciliation_failed";
    rendered["message"] =
        "RP1 GPCLK startup reconciliation is incomplete or mismatched. " + field(rendered, "message");
  }
  startup_failure_latched_ = !reconciled;
  operations_.set_transmission_inhibited(
      !reconciled, reconciled ? "" : "RP1 GPCLK reconciliation is incomplete");
  return rendered;
}
nlohmann::json Rp1GpclkRouteService::reconcileRuntime(
    const std::string &route, bool development) {
  try {
  const int gpio = gpioForRoute(route);
  if (!gpio || gpio != operations_.persisted_gpio())
    return failure("runtime_route_mismatch", "Requested and saved runtime route differ.");
  const auto raw = request({{"schemaVersion",3},
      {"operation",development ? "reconcile-output" : "idle"},
      {"route",gpio == 4 ? "gpio4" : "gpio20"}});
  const auto state = raw.value("state", nlohmann::json::object());
  const auto output = state.value("outputLifecycle", nlohmann::json::object());
  const auto controller = output.value("controller", nlohmann::json::object());
  const bool valid = raw.value("status", std::string{}) == "ok" &&
      output.value("ready", false) && !output.value("executionAuthorized", true) &&
      output.value("route", std::string{}) == (gpio == 4 ? "gpio4" : "gpio20") &&
      output.value("bootId", std::string{}) == state.value("bootId", std::string{}) &&
      !output.value("bootId", std::string{}).empty() &&
      output.value("bindingSha256", std::string{}) == state.value("bindingSha256", std::string{}) &&
      output.value("bindingSha256", std::string{}).size() == 64 &&
      controller == state.value("controller", nlohmann::json::object()) &&
      controller.value("route", 0) == (gpio == 4 ? 1 : 2) &&
      controller.value("flags", 0) == 6 && controller.value("error", -1) == 0 &&
      controller.value("session", 0ULL) != 0 && controller.value("generation", 0ULL) != 0;
  if (!valid) {
    const auto error = raw.value("error", nlohmann::json::object());
    return failure("runtime_reconciliation_failed", error.value("message",
        std::string("Runtime route reconciliation failed; output remains inhibited.")));
  }
  auto result = failure("runtime_reconciled", "Runtime route reconciled; existing operation authorization is still required.");
  result.update({{"ok",true},{"reconciled",true},{"compatible",true},
      {"requested",route},{"persisted",route},{"configured",route},{"active",route},
      {"journal","none"},{"bootOwnership","runtime"},{"endpointOwned",true},
      {"endpointOpen",false},{"outputInhibited","Disabled"},
      {"operationalReady","Ready"},
      {"generation",++generation_},
      {"controllerGeneration",controller["generation"]},
      {"policyDomain",development ? "external-provider" : "startup-idle"},
      {"executionAuthorized",false},{"qualification",false}});
  operations_.set_transmission_inhibited(!development,
      development ? "" : "Runtime idle startup awaits an operation authorization");
  return result;
  } catch (const std::exception &error) {
    invalidateRp1GpclkDevelopmentOperation();
    startup_failure_latched_ = true;
    operations_.set_transmission_inhibited(true, error.what());
    return failure("provider_response_invalid", error.what());
  }
}

nlohmann::json Rp1GpclkRouteService::reconcileIdleStartup(
    const std::string &route) {
  std::lock_guard<std::mutex> guard(mutex_);
  operations_.set_transmission_inhibited(
      true, "RP1 GPCLK idle startup reconciliation is pending");
  if (startup_failure_latched_)
    return failure("startup_failure_latched",
                   "A prior RP1 GPCLK startup reconciliation failure keeps "
                   "transmission inhibited for this process lifetime.");
  const std::string expected = routeForGpio(gpioForRoute(route));
  if (expected.empty()) {
    startup_failure_latched_ = true;
    return failure("invalid_route",
                   "RP1 GPCLK idle startup requires GPIO4 or GPIO20.");
  }

  auto discovered = request({{"operation", "query"}});
  if (runtime_profile_) {
    auto result = reconcileRuntime(expected, false);
    startup_failure_latched_ = !result.value("ok", false);
    return result;
  }
  auto rendered = render(discovered, expected);
  const auto exact_route_state = [&expected](const nlohmann::json &value) {
    return value.value("ok", false) &&
           value.value("reconciled", false) &&
           value.value("requested", std::string{}) == expected &&
           value.value("persisted", std::string{}) == expected &&
           value.value("configured", std::string{}) == expected &&
           value.value("active", std::string{}) == expected &&
           value.value("journal", std::string{}) == "none" &&
           value.value("bootOwnership", std::string{}) == "current";
  };
  if (!exact_route_state(rendered)) {
    startup_failure_latched_ = true;
    rendered["ok"] = false;
    rendered["result"] = "idle_startup_reconciliation_failed";
    rendered["message"] =
        "RP1 GPCLK idle startup route state is incomplete, stale, ambiguous, or mismatched. " + field(rendered, "message");
    return rendered;
  }

  if (!rendered.value("endpointOwned", false) ||
      rendered.value("endpointOpen", true)) {
    startup_failure_latched_ = true;
    rendered["ok"] = false;
    rendered["result"] = "idle_startup_endpoint_unsafe";
    rendered["message"] =
        "RP1 GPCLK idle startup requires the endpoint to be correctly owned and closed. " + field(rendered, "message");
    return rendered;
  }

  rendered["ok"] = true;
  rendered["result"] = "idle_route_reconciled";
  rendered["policyDomain"] = "startup-idle";
  rendered["executionAuthorized"] = false;
  rendered["developmentAuthorizationRequired"] = true;
  rendered["message"] =
      "RP1 GPCLK route is reconciled for safe idle startup; a current "
      "operation-scoped authorization remains required for development output.";
  operations_.set_transmission_inhibited(
      true,
      "RP1 GPCLK development output awaits a bounded authorization");
  return rendered;
}
nlohmann::json Rp1GpclkRouteService::reconcileDevelopmentStartup(
    const std::string &route) {
  std::lock_guard<std::mutex> guard(mutex_);
  operations_.set_transmission_inhibited(
      true, "RP1 GPCLK route reconciliation is pending");
  if (startup_failure_latched_)
    return failure("startup_failure_latched",
                   "A prior RP1 GPCLK startup reconciliation failure keeps "
                   "transmission inhibited for this process lifetime.");
  const std::string expected = routeForGpio(gpioForRoute(route));
  if (expected.empty()) {
    startup_failure_latched_ = true;
    return failure("invalid_route",
                   "Development startup requires GPIO4 or GPIO20.");
  }

  ++generation_;
  auto discovered = request({{"operation", "query"}});
  if (runtime_profile_) return reconcileRuntime(expected, true);
  auto rendered = render(discovered, expected);
  const bool reconciled = rendered.value("ok", false) &&
                          rendered.value("reconciled", false) &&
                          rendered.value("requested", std::string{}) == expected &&
                          rendered.value("persisted", std::string{}) == expected &&
                          rendered.value("configured", std::string{}) == expected &&
                          rendered.value("active", std::string{}) == expected &&
                          rendered.value("journal", std::string{}) == "none" &&
                          rendered.value("bootOwnership", std::string{}) == "current" &&
                          rendered.value("endpointOwned", false) &&
                          !rendered.value("endpointOpen", true);
  rendered["policyDomain"] = "external-provider";
  rendered["ok"] = reconciled;
  rendered["result"] = reconciled ? "development_route_reconciled"
                                    : "development_route_failed";
  rendered["message"] = reconciled
      ? "Externally provisioned route state reconciled; operation authorization and runtime provider checks remain required."
      : "Externally provisioned route state is incomplete, stale, ambiguous, or mismatched. " + field(rendered, "message");
  startup_failure_latched_ = !reconciled;
  operations_.set_transmission_inhibited(
      !reconciled,
      reconciled ? "" : "RP1 GPCLK route reconciliation is incomplete");
  return rendered;
}
#ifndef WSPRRYPI_ROUTE_SERVICE_TEST
Rp1GpclkRouteService &productionRp1GpclkRouteService() {
  static Rp1GpclkRouteService service(
      {socketRequest, rp1_gpclk_application_idle_state,
       [] { return config.gpio_tx_pin; },
       [](int gpio, std::string *error) {
         return persist_rp1_gpclk_route_config(gpio, error);
       },
       [](bool inhibited, const std::string &) {
         set_rp1_route_transaction_inhibited(inhibited);
       },
       [](const std::string &route) {
         auto result = providerCommand("route-plan", route);
         const auto plan = result.value("routePlan", nlohmann::json::object());
         const auto binding = plan.contains("bindingSha256") &&
                                      plan["bindingSha256"].is_string()
                                  ? plan["bindingSha256"].get<std::string>()
                                  : std::string{};
         if (binding != ownedRuntimeBinding())
           throw std::runtime_error(
               "The runtime route plan differs from WsprryPi ownership.");
         return result;
       },
       [](const std::string &operation, const std::string &route,
          const std::string &digest, const std::string &binding,
          const std::string &request_id) {
         const auto owned_binding = ownedRuntimeBinding();
         if (operation == "switch" && binding != owned_binding)
           throw std::runtime_error("WsprryPi runtime ownership is unavailable.");
         launchRuntimeRouteOperation(operation, route, digest, request_id);
       },
       [](const std::string &route) {
         std::vector<std::string> arguments{"/usr/bin/python3", "-I",
             "/usr/local/lib/wsprrypi/rp1_qualification.py", "status"};
         if (!route.empty()) arguments.insert(arguments.end(), {"--route", route});
         return nlohmann::json::parse(runRp1ProviderProcess(arguments, std::chrono::seconds{10}));
       }});
  return service;
}
#endif
} // namespace wsprrypi
