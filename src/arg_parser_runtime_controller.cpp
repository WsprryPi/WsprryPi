// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include <stdexcept>

void apply_runtime_config_side_effects() {
    throw std::logic_error("Local transmitter configuration is unavailable in the controller build");
}
