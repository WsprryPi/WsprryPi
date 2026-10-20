#pragma once
#include "WSPR-Transmitter/src/rp1_gpclk_application_idle.hpp"
#include "json.hpp"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
namespace wsprrypi {
struct Rp1GpclkRouteExecutorOperations {
  std::function<nlohmann::json(const nlohmann::json &)> request;
  std::function<Rp1GpclkApplicationIdleState()> idle_state;
  std::function<int()> persisted_gpio;
  std::function<bool(int, std::string *)> persist_gpio;
  std::function<void(bool, const std::string &)> set_transmission_inhibited;
  std::function<nlohmann::json(const std::string &)> runtime_route_plan;
  std::function<void(const std::string &, const std::string &,
                     const std::string &, const std::string &,
                     const std::string &)>
      runtime_route_launch;
  std::function<nlohmann::json(const std::string &)> qualification_status;
};
class Rp1GpclkRouteService {
public:
  explicit Rp1GpclkRouteService(Rp1GpclkRouteExecutorOperations);
  nlohmann::json query(bool include_qualification = true);
  bool acknowledgeRestoration(const std::string &token, bool transmit);
  nlohmann::json operate(const std::string &, const std::string &,
                         std::uint64_t);
  nlohmann::json reconcileStartup();
  nlohmann::json reconcileIdleStartup(const std::string &route);
  nlohmann::json reconcileDevelopmentStartup(const std::string &route);

private:
  nlohmann::json request(const nlohmann::json &);
  nlohmann::json render(const nlohmann::json &,
                        const std::string &requested = {});
  nlohmann::json failure(const std::string &, const std::string &) const;
  nlohmann::json withQualification(nlohmann::json) const;
  nlohmann::json reconcileRuntime(const std::string &, bool);
  bool idle() const;
  static std::string routeForGpio(int);
  static int gpioForRoute(const std::string &);
  static std::string requestId(const char *, std::uint64_t);
  Rp1GpclkRouteExecutorOperations operations_;
  std::mutex mutex_;
  std::uint64_t generation_{0};
  std::string preflight_route_;
  bool runtime_profile_{false};
  std::string runtime_plan_digest_;
  std::string runtime_binding_digest_;
  bool startup_failure_latched_{false};
};
Rp1GpclkRouteService &productionRp1GpclkRouteService();
} // namespace wsprrypi
