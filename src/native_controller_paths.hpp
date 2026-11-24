// SPDX-License-Identifier: MIT
#pragma once
namespace wsprrypi {
struct PosixControllerPaths {
    const char *runtime,*maintenance_runtime,*maintenance_root,*application_root,*web_cache;
    const char *python,*worker,*age,*backend;
};
// Fixed new-installation paths. Existing Pi builds and explicitly selected
// application state/runtime paths retain their existing contracts.
inline constexpr PosixControllerPaths posix_controller_paths(){
#ifdef __APPLE__
    // macOS /var is a symlink, and its physical run directory can be
    // group-writable. Use an installer-owned parent accepted by the same
    // strict ancestor checks, without changing system-directory permissions.
    return {"/Library/Application Support/WsprryPi/Run/Backend","/Library/Application Support/WsprryPi/Run/Maintenance",
        "/Library/Application Support/WsprryPi/Maintenance",
        "/Library/Application Support/WsprryPi/Controller/application-state",
        "/Library/Application Support/WsprryPi/Web/listeners.json",
        "/Library/Application Support/WsprryPi/Program/runtime/bin/python3",
        "/Library/Application Support/WsprryPi/Program/lib/recovery/worker.py",
        "/Library/Application Support/WsprryPi/Program/bin/recovery-age",
        "/Library/Application Support/WsprryPi/Program/bin/wsprrypi"};
#else
    return {"/run/wsprrypi","/run/wsprrypi-maintenance","/var/lib/wsprrypi-maintenance",
        "/var/lib/wsprrypi/application-state","/var/lib/wsprrypi-web/listeners.json",
        "/usr/bin/python3","/usr/local/lib/wsprrypi/recovery/worker.py",
        "/usr/local/lib/wsprrypi/recovery-age","/usr/local/bin/wsprrypi"};
#endif
}
}
