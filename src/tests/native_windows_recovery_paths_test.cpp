// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_recovery/paths.hpp"
#include <cassert>
#include <iostream>
using namespace wsprrypi;
namespace {template<class F>void refuses(F action){bool held=false;try{action();}catch(const std::exception &){held=true;}assert(held);}}
int main(){try{
    NativeWindowsFixture root;const auto base=NativeWindowsFixture::utf8(root.root);RecoveryPaths paths;
    paths.root=base+"/maintenance";paths.runtime=base+"/maintenance/Run";paths.backend_runtime=base+"/controller/Run";
    paths.application_root=base+"/controller/application-state";paths.python=base+"/program/runtime/python.exe";
    paths.worker=base+"/program/lib/recovery/worker.py";paths.age=base+"/program/bin/recovery-age.exe";
    paths.native_library=base+"/program/bin/wsprrypi-recovery-native.dll";
    paths.backend_command={base+"/program/bin/wsprrypi.exe"};
    paths.backend_owner=RecoveryBackendOwner{0,0,base+"/controller",base+"/controller/.wsprrypi-recovery",
        base+"/program/lib/recovery/state_owner_worker.py",windows_file::current_sid()};
    auto invalid=paths;invalid.backend_owner->uid=1000;refuses([&]{testing::set_recovery_paths(invalid);});
    invalid=paths;invalid.backend_owner->sid=L"S-1-5-18";if(invalid.backend_owner->sid!=windows_file::current_sid())refuses([&]{testing::set_recovery_paths(invalid);});
    invalid=paths;invalid.root="C:relative";refuses([&]{testing::set_recovery_paths(invalid);});
    invalid=paths;invalid.maintenance_sid=L"S-1-5-18";if(invalid.maintenance_sid!=windows_file::current_sid())refuses([&]{testing::set_recovery_paths(invalid);});
    testing::set_recovery_paths(paths);const auto actual=recovery_paths();
    assert(actual.root==paths.root&&actual.native_library==paths.native_library&&actual.backend_runtime==paths.backend_runtime&&actual.backend_command==paths.backend_command&&
        actual.maintenance_sid==windows_file::current_sid()&&actual.backend_owner->sid==windows_file::current_sid()&&actual.backend_owner->root==paths.backend_owner->root);
    refuses([&]{testing::set_recovery_paths(paths);});
    std::cout<<"Native recovery path selection, exact SID fixture ownership, relative-path refusal, and immutable selection passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
