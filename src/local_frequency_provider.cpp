// SPDX-License-Identifier: MIT
#include "local_frequency_provider.hpp"
#include "backend_capabilities.hpp"
namespace wsprrypi {
PPMProviderSnapshot local_frequency_provider_snapshot(){
#if WSPRRYPI_CONTROLLER_ONLY
    PPMProviderSnapshot unavailable;
    unavailable.provider_name="unavailable";
    unavailable.error_reason="Local frequency calibration is unavailable in the controller build";
    return unavailable;
#else
    return ppmManager.getProviderSnapshot();
#endif
}
bool local_frequency_provider_running(){
#if WSPRRYPI_CONTROLLER_ONLY
    return false;
#else
    return ppmManager.isRunning();
#endif
}
void stop_local_frequency_provider(){
#if !WSPRRYPI_CONTROLLER_ONLY
    ppmManager.stop();
#endif
}
}
