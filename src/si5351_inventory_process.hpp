#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace si5351_inventory_process {
struct Reply {
    std::string output;
    std::string error;
};

// One isolated worker per process, bounded IPC and nonblocking child cleanup.
// The command/deadline arguments also provide a hardware-free test seam.
Reply run(const std::vector<std::string>& command,
          std::chrono::milliseconds deadline = std::chrono::seconds(2));
void reap() noexcept;
std::optional<int> helper_main(int argc, char** argv);
}
