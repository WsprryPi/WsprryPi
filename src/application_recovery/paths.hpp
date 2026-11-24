// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
namespace wsprrypi {
struct RecoveryBackendOwner {
    std::uint64_t uid{},gid{};
    std::string home,root,worker;
#ifdef _WIN32
    std::wstring sid;
#endif
};
struct RecoveryPaths {
    std::string root{"/var/lib/wsprrypi-maintenance"};
    std::string runtime{"/run/wsprrypi-maintenance"};
    std::string backend_runtime{"/run/wsprrypi"};
    std::string python{"/usr/bin/python3"};
    std::string worker{"/usr/local/lib/wsprrypi/recovery/worker.py"};
    std::string age{"/usr/local/lib/wsprrypi/recovery-age"};
    std::string application_root{"/var/lib/wsprrypi/application-state"};
    std::string software_worker_fault_phase{}; // Typed crash fixture, never production CLI/env.
    std::vector<std::string> software_backend_command{}; // Typed software fixtures only.
    std::vector<std::string> backend_command{"/usr/local/bin/wsprrypi"};
    std::optional<RecoveryBackendOwner> backend_owner;
#ifdef _WIN32
    std::string native_library;
    std::wstring maintenance_sid;
#endif

};
RecoveryPaths recovery_paths();
namespace testing {
// Actual-main software fixtures only; no production CLI/environment path hook.
void set_recovery_paths(RecoveryPaths);
}
}
