// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#endif
#include "Singleton/src/installation_lock.hpp"
#include "wtp_endpoint/runtime.hpp"
#include <memory>
namespace wsprrypi {
inline std::unique_ptr<InstallationLock> acquire_controller_installation_lock(bool stopped_backend_maintenance=false){
    // This owner survives generation replacement and covers independent pairing,
    // retirement, and identity owners. No browser, CLI, or environment override.
    const auto owner=controller_installation_owner();
#ifdef _WIN32
    // Native maintenance delegates state work to the backend service identity;
    // it never acquires backend ownership by borrowing an administrator token.
    (void)stopped_backend_maintenance;
    if(owner.sid!=windows_file::current_sid())throw std::runtime_error("Controller commands require the backend owner account");
    auto lock=std::make_unique<InstallationLock>(windows_file::path_from_utf8(owner.home)/L".wsprrypi-controller-instance");
#else
    if(!stopped_backend_maintenance&&owner.uid!=::geteuid())
        throw std::runtime_error("Controller commands require the backend owner account");
    auto lock=std::make_unique<InstallationLock>(std::filesystem::path(owner.home)/".wsprrypi-controller-instance",owner.uid);
#endif
    if(!(*lock)())throw std::runtime_error("Controller installation is already in use");
    return lock;
}
}
