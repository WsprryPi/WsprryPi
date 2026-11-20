// SPDX-License-Identifier: MIT
#include "paths.hpp"
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
    return value;
}
void testing::set_recovery_paths(RecoveryPaths value){
    std::lock_guard lock(mutex);if(selected||fixture)throw std::logic_error("Recovery paths are already selected");
    for(const auto *path:{&value.root,&value.runtime,&value.backend_runtime,&value.python,&value.worker,&value.age,&value.application_root})
        if(!std::filesystem::path(*path).is_absolute()||path->find('\0')!=std::string::npos)throw std::invalid_argument("Recovery fixture paths must be absolute");
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
