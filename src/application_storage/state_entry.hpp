// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
#include <string>
#include <vector>
namespace wsprrypi {
struct BackendStateArguments {
    std::optional<std::string> directory;
    std::optional<std::string> runtime_directory;
    std::vector<char *> arguments;
};
BackendStateArguments backend_state_arguments(int argc,char *argv[]);
// Explicit offline initialization/migration only. Ordinary settings clients
// never use this entry point or open persistent state themselves.
std::optional<int> state_command_main(int argc,char *argv[],unsigned short singleton_port);
} // namespace wsprrypi
