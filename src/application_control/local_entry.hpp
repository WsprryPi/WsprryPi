// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
namespace wsprrypi {
// Non-transmitter entry points run before hardware/identity initialization.
std::optional<int> local_command_main(int,char **);
}
