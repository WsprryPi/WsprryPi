// SPDX-License-Identifier: MIT
#pragma once
#include "application_control/local_transport.hpp"
#include <optional>
namespace wsprrypi {
LocalTransportOptions maintenance_control_options(bool browser=false);
std::optional<int> maintenance_service_main(int argc,char *argv[]);
}
