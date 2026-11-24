// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "paths.hpp"
#include "native_windows_owners.hpp"
#include <mutex>
namespace wsprrypi {
namespace {
std::mutex mutex;std::optional<RecoveryPaths> fixture;bool selected{};
std::string text(const std::filesystem::path &path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
void absolute(const std::string &value){(void)windows_file::path_from_utf8(value);}
void command(const std::vector<std::string> &value){
    if(value.empty())throw std::invalid_argument("Recovery command is empty");
    absolute(value.front());
    for(const auto &argument:value)if(argument.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid recovery command argument");
}
}
RecoveryPaths recovery_paths(){
    std::lock_guard lock(mutex);selected=true;if(fixture)return *fixture;
    const auto owners=native_windows_controller_owners();const auto program=windows_program_root();
    RecoveryPaths value;value.root=owners.maintenance.home;value.runtime=text(windows_file::path_from_utf8(owners.maintenance.home)/L"Run");
    value.maintenance_sid=owners.maintenance.sid;
    value.backend_runtime=text(windows_file::path_from_utf8(owners.backend.home)/L"Run");
    value.application_root=text(windows_file::path_from_utf8(owners.backend.home)/L"application-state");
    value.python=text(program/L"runtime"/L"python.exe");value.worker=text(program/L"lib"/L"recovery"/L"worker.py");
    value.native_library=text(program/L"bin"/L"wsprrypi-recovery-native.dll");
    value.age=text(program/L"bin"/L"recovery-age.exe");value.backend_command={text(program/L"bin"/L"wsprrypi.exe")};
    value.backend_owner=RecoveryBackendOwner{0,0,owners.backend.home,text(windows_file::path_from_utf8(owners.backend.home)/L".wsprrypi-recovery"),
        text(program/L"lib"/L"recovery"/L"state_owner_worker.py"),owners.backend.sid};
    if(windows_file::current_sid()==owners.backend.sid)value.root=value.backend_owner->root;
    return value;
}
void testing::set_recovery_paths(RecoveryPaths value){
    std::lock_guard lock(mutex);if(selected||fixture)throw std::logic_error("Recovery paths are already selected");
    if(!value.maintenance_sid.empty()&&value.maintenance_sid!=windows_file::current_sid())
        throw std::invalid_argument("Native Windows recovery fixtures require their actual maintenance SID");
    value.maintenance_sid=windows_file::current_sid();
    for(const auto *path:{&value.root,&value.runtime,&value.backend_runtime,&value.python,&value.worker,&value.age,&value.application_root,&value.native_library})absolute(*path);
    command(value.backend_command);if(!value.software_backend_command.empty())command(value.software_backend_command);
    if(value.backend_owner){
        const auto &owner=*value.backend_owner;absolute(owner.home);absolute(owner.root);absolute(owner.worker);
        if(owner.uid||owner.gid||owner.sid!=windows_file::current_sid()||windows_file::path_from_utf8(owner.root)!=windows_file::path_from_utf8(owner.home)/L".wsprrypi-recovery")
            throw std::invalid_argument("Native Windows recovery fixtures require their actual owner SID");
    }
    if(!value.software_worker_fault_phase.empty()&&(value.software_backend_command.empty()||
        (value.software_worker_fault_phase!="stopping_backend"&&value.software_worker_fault_phase!="retaining_evidence"&&
         value.software_worker_fault_phase!="publishing_generation"&&value.software_worker_fault_phase!="starting_backend"&&
         value.software_worker_fault_phase!="verifying_backend"&&value.software_worker_fault_phase!="complete")))
        throw std::invalid_argument("Unsupported recovery worker crash fixture");
    fixture=std::move(value);
}
}
#endif
