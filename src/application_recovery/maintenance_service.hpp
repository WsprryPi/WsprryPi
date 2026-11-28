// SPDX-License-Identifier: MIT
#pragma once
#include "application_control/local_transport.hpp"
#include <functional>
#include <optional>
namespace wsprrypi {
enum class MaintenancePeer { Administrator, Browser, BackendSessionIssuer };
LocalTransportOptions maintenance_control_options(MaintenancePeer peer=MaintenancePeer::Administrator);
std::optional<int> maintenance_service_main(int argc,char *argv[]);
std::optional<int> maintenance_service_main(int argc,char *argv[],const std::function<bool()> &stop_requested,
                                           const std::function<void()> &ready);
}
