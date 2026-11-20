// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
namespace wsprrypi {
// Exclusive early process role, before backend state/identity/hardware startup.
std::optional<int> web_service_main(int argc,char *argv[]);
}
