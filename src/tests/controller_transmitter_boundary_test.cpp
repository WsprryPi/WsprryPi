// SPDX-License-Identifier: MIT
#include "backend_capabilities.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "rp1_pps_calibration.hpp"
#include "rp1_gpclk_route_service.hpp"
#include "local_frequency_provider.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

template<class Action> void refused(Action action) {
    bool failed = false;
    try { action(); } catch (const std::logic_error &) { failed = true; }
    assert(failed);
}
int main() {
    static_assert(WSPRRYPI_CONTROLLER_ONLY && !WSPRRYPI_ANCILLARY_GPIO);
    static_assert(!WSPRRYPI_BACKEND_RPI_GPIO && !WSPRRYPI_BACKEND_RP1_GPCLK &&
                  !WSPRRYPI_BACKEND_SI5351 && !WSPRRYPI_BACKEND_SIMULATED);
    assert(!wsprrypi::local_frequency_provider_running());
    const auto frequency=wsprrypi::local_frequency_provider_snapshot();
    assert(!frequency.synchronized&&!frequency.frequency_ppm.has_value());
    wsprrypi::stop_local_frequency_provider();
    refused([] { transmitter_start_async(); });
    refused([] { transmitter_quiesce_for_startup(); });
    refused([] { transmitter_set_thread_scheduling(1, 40); });
    refused([] { transmitter_set_callbacks({}); });
    refused([] { transmitter_runtime_status(); });
    refused([] { transmitter_format_frequency_mhz(14097000); });
    refused([] { transmitter_configure_execution(TransmissionRequest{}); });
    assert(transmitter_state() == WsprTransmitState::DISABLED);
    assert(!transmitter_active_execution_is_tone());
    assert(transmitter_output_inactive_confirmed());
    assert(transmitter_stop_and_join().ok);
    transmitter_shutdown_for_process_exit();
    assert(!transmitter_callbacks());
    wsprrypi::Rp1PpsCalibration pps;
    refused([&] { pps.snapshot(true); });
    refused([] { wsprrypi::productionRp1GpclkRouteService().query(); });
    refused([] { wsprrypi::productionRp1GpclkRouteService().reconcileStartup(); });
    std::cout << "PASS controller boundary refuses local configuration, start, callbacks, scheduling, and output status without a transmitter instance\n";
}
