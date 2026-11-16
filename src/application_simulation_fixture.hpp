// SPDX-License-Identifier: MIT
#pragma once
#include "wspr_transmit.hpp"
namespace wsprrypi::testing {
// Set once before starting application threads or selecting any backend.
// This only supplies transient options when the production CLI independently
// selects simulation. No environment, INI, HTTP, or product CLI exposes it.
void set_application_simulation_fixture(const WsprTransmitter::SimulatedRuntimeConfig &);
}
