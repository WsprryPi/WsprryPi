// SPDX-License-Identifier: MIT
#ifndef _WIN32
#include "paths.hpp"
#include "backend_capabilities.hpp"
#include "native_controller_owners.hpp"
#include "native_controller_paths.hpp"
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <array>
#include <unistd.h>
namespace wsprrypi {
namespace {std::mutex mutex;std::optional<RecoveryPaths> fixture;bool selected{};}
RecoveryPaths recovery_paths(){
    std::lock_guard lock(mutex);selected=true;if(fixture)return *fixture;
    RecoveryPaths value;
#if WSPRRYPI_CONTROLLER_ONLY
    const auto native=posix_controller_paths();
    value.root=native.maintenance_root;value.runtime=native.maintenance_runtime;
    value.backend_runtime=native.runtime;value.application_root=native.application_root;
    value.python=native.python;value.worker=native.worker;value.age=native.age;value.backend_command={native.backend};
#endif
#ifdef __linux__
    // Both installer-supported executable names use the same service. Query
    // the selected image rather than accidentally invoking another installed
    // release when this worker belongs to the debug build.
    std::array<char,4096> executable{};const auto count=::readlink("/proc/self/exe",executable.data(),executable.size());
    if(count>0&&static_cast<std::size_t>(count)<executable.size()){
        const std::string path(executable.data(),static_cast<std::size_t>(count));
        if(path=="/usr/local/bin/wsprrypi"||path=="/usr/local/bin/wsprrypi_debug")value.backend_command={path};
    }
#endif
#if WSPRRYPI_CONTROLLER_ONLY
    if(const auto owners=native_controller_owners()){
        const auto &backend=owners->backend;
        value.backend_owner=RecoveryBackendOwner{backend.uid,backend.gid,backend.home,
            backend.home+"/.wsprrypi-recovery",(std::filesystem::path(value.worker).parent_path()/"state_owner_worker.py").string()};
        if(::geteuid()==backend.uid)value.root=value.backend_owner->root;
    }
#endif
    return value;
}
void testing::set_recovery_paths(RecoveryPaths value){
    std::lock_guard lock(mutex);if(selected||fixture)throw std::logic_error("Recovery paths are already selected");
    for(const auto *path:{&value.root,&value.runtime,&value.backend_runtime,&value.python,&value.worker,&value.age,&value.application_root})
        if(!std::filesystem::path(*path).is_absolute()||path->find('\0')!=std::string::npos)throw std::invalid_argument("Recovery fixture paths must be absolute");
    if(value.backend_owner){
        const auto &owner=*value.backend_owner;
        if(!owner.uid||!owner.gid||owner.uid>=UINT32_MAX||owner.gid>=UINT32_MAX||
            !std::filesystem::path(owner.home).is_absolute()||owner.root!=owner.home+"/.wsprrypi-recovery"||
            !std::filesystem::path(owner.worker).is_absolute()||owner.home.find('\0')!=std::string::npos||owner.worker.find('\0')!=std::string::npos)
            throw std::invalid_argument("Invalid recovery backend owner fixture");
    }
    if(value.backend_command.empty()||!std::filesystem::path(value.backend_command.front()).is_absolute())throw std::invalid_argument("Recovery fixture backend command must be absolute");
    for(const auto &argument:value.backend_command)if(argument.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid recovery fixture command");
    if(!value.software_backend_command.empty()){
        if(!std::filesystem::path(value.software_backend_command.front()).is_absolute())throw std::invalid_argument("Recovery fixture start command must be absolute");
        for(const auto &argument:value.software_backend_command)if(argument.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid recovery fixture start command");
    }
    if(!value.software_worker_fault_phase.empty()&&(value.software_backend_command.empty()||
        (value.software_worker_fault_phase!="stopping_backend"&&value.software_worker_fault_phase!="retaining_evidence"&&value.software_worker_fault_phase!="publishing_generation"&&
         value.software_worker_fault_phase!="starting_backend"&&value.software_worker_fault_phase!="verifying_backend"&&value.software_worker_fault_phase!="complete")))
        throw std::invalid_argument("Unsupported recovery worker crash fixture");
    fixture=std::move(value);
}
}

#endif
