// SPDX-License-Identifier: MIT
#pragma once
#include "application_storage/persistent_configuration.hpp"
namespace wsprrypi {
// Explicit portable adapter: logical station/signal intent moves; destination
// hardware, calibration, adapter identities and security policy remain local.
PersistentConfiguration recovery_destination_settings(const PersistentConfiguration &source,const PersistentConfiguration &destination,bool move);
nlohmann::json recovery_configuration_changes(const PersistentConfiguration &before,const PersistentConfiguration &after);
}
