// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "native_windows_arguments.hpp"
#include "application_recovery/job_lock.hpp"
#include <cassert>
#include <iostream>
int main(){try{
    const auto arguments=wsprrypi::native_windows_arguments();assert(arguments.size()==3&&arguments[1]=="--library");
    const wsprrypi::RecoveryJobLock::NativeLibrary library{arguments[2]};NativeWindowsFixture fixture;
    {
        wsprrypi::RecoveryJobLock first(fixture.root,true,library);bool busy=false;
        try{wsprrypi::RecoveryJobLock duplicate(fixture.root,true,library);}catch(const wsprrypi::LocalCommandRefused &error){busy=error.reason()==wsprrypi::LocalRefusal::Busy;}
        assert(busy);
    }
    {wsprrypi::RecoveryJobLock reopened(fixture.root,true,library);assert(::RemoveDirectoryW(fixture.root.c_str()));}
    bool absent=false;try{wsprrypi::RecoveryJobLock missing(fixture.root,true,library);}catch(const std::exception &){absent=true;}assert(absent);
    std::cout<<"Native C++ job client loaded only its protected explicit DLL, shared nonrecursive admission, released ownership, and permitted admitted retirement\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
