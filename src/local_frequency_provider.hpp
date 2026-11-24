// SPDX-License-Identifier: MIT
#pragma once
#include "PPM-Manager/src/ppm_manager.hpp"
namespace wsprrypi {
PPMProviderSnapshot local_frequency_provider_snapshot();
bool local_frequency_provider_running();
void stop_local_frequency_provider();
}
