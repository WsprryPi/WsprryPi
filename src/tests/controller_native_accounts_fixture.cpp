// SPDX-License-Identifier: MIT
// Production controller entry and native account/IPC selection, with only the
// service lifecycle replaced by the existing typed software-fixture adapter.
#include "backend_capabilities.hpp"
#include "application_recovery/paths.hpp"
#include "native_controller_owners.hpp"
#include "native_controller_paths.hpp"
#include <filesystem>
#include <string_view>
#include <vector>
#include <unistd.h>
#if !WSPRRYPI_CONTROLLER_ONLY
#error Native account qualification requires the explicit controller profile
#endif
int pi_managed_production_main(int,char **);
int main(int argc,char *argv[]){
    try{
        const auto owners=wsprrypi::native_controller_owners();if(!owners)return 2;
        const auto executable=std::filesystem::canonical(argv[0]);
        const auto program=executable.parent_path();const auto native=wsprrypi::posix_controller_paths();
        wsprrypi::RecoveryPaths paths;paths.root=native.maintenance_root;paths.runtime=native.maintenance_runtime;
        paths.backend_runtime=native.runtime;paths.application_root=native.application_root;
        paths.python="/usr/bin/python3";paths.worker=(program/"application_recovery/worker.py").string();
        paths.age=(program/"recovery-age").string();paths.backend_command={executable.string()};
        paths.backend_owner=wsprrypi::RecoveryBackendOwner{owners->backend.uid,owners->backend.gid,
            owners->backend.home,owners->backend.home+"/.wsprrypi-recovery",(program/"application_recovery/state_owner_worker.py").string()};
        if(::geteuid()==owners->backend.uid)paths.root=paths.backend_owner->root;
        paths.software_backend_command={executable.string(),"--controller-only","--state-directory",paths.application_root,
            "--runtime-directory",paths.backend_runtime,"--backend","simulated"};
        std::vector<char *> forwarded{argv[0]};
        for(int index=1;index<argc;++index){
            if(std::string_view(argv[index])=="--fixture-fault-phase"){
                if(!paths.software_worker_fault_phase.empty()||index+1>=argc)return 2;
                paths.software_worker_fault_phase=argv[++index];continue;
            }
            // This marker is required by the existing software lifecycle's
            // closed admission. It never reaches the zero-backend application.
            if(std::string_view(argv[index])=="--backend"&&index+1<argc&&std::string_view(argv[index+1])=="simulated"){
                ++index;continue;
            }
            forwarded.push_back(argv[index]);
        }
        forwarded.push_back(nullptr);wsprrypi::testing::set_recovery_paths(std::move(paths));
        return pi_managed_production_main(static_cast<int>(forwarded.size())-1,forwarded.data());
    }catch(...){return 2;}
}
