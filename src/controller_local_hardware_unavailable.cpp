// SPDX-License-Identifier: MIT
// Parent application boundaries retained by shared configuration/status code.
// The controller links no PPS device capture or privileged RP1 route provider.
#include "rp1_pps_calibration.hpp"
#include "rp1_gpclk_route_service.hpp"
#include <stdexcept>

namespace {
[[noreturn]] void unavailable() {
    throw std::logic_error("Local hardware is unavailable in the controller build");
}
}
namespace wsprrypi {
struct Rp1PpsCalibration::Impl {};
Rp1PpsCalibration::Rp1PpsCalibration() = default;
Rp1PpsCalibration::Rp1PpsCalibration(std::function<std::unique_ptr<Rp1PpsCapture>()>) { unavailable(); }
Rp1PpsCalibration::~Rp1PpsCalibration() = default;
SystemClockFrequencyEstimate Rp1PpsCalibration::snapshot(bool enabled, std::chrono::steady_clock::time_point) {
    if(enabled) unavailable();
    return {};
}
bool rp1_pps_reference_ready(const PPMProviderSnapshot &, std::chrono::steady_clock::time_point) { return false; }
Rp1PpsResult evaluate_rp1_pps_measurement(const Rp1PpsMeasurement &) { unavailable(); }

Rp1GpclkRouteService::Rp1GpclkRouteService(Rp1GpclkRouteExecutorOperations) {}
nlohmann::json Rp1GpclkRouteService::query(bool) { unavailable(); }
bool Rp1GpclkRouteService::acknowledgeRestoration(const std::string &, bool) { unavailable(); }
nlohmann::json Rp1GpclkRouteService::operate(const std::string &, const std::string &, std::uint64_t) { unavailable(); }
nlohmann::json Rp1GpclkRouteService::reconcileStartup() { unavailable(); }
nlohmann::json Rp1GpclkRouteService::reconcileIdleStartup(const std::string &) { unavailable(); }
nlohmann::json Rp1GpclkRouteService::reconcileDevelopmentStartup(const std::string &) { unavailable(); }
Rp1GpclkRouteService &productionRp1GpclkRouteService() {
    static Rp1GpclkRouteService absent({});
    return absent;
}
}
