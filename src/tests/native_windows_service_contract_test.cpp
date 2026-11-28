// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_recovery/windows_controller_service.hpp"
#include "application_recovery/windows_service_host.hpp"
#include "application_recovery/windows_owner_pipe.hpp"
#include <cassert>
#include <iostream>
namespace service=wsprrypi::recovery_windows::service_detail;
namespace {
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception &){refused=true;}assert(refused);}
}
int main(){try{
    const std::wstring nonce(32,L'a');
    assert(service::invocation(std::array<std::wstring,1>{service::name}).mode==service::Mode::Controller);
    assert(service::invocation(std::array<std::wstring,2>{service::name,service::initialize}).mode==service::Mode::Initialize);
    assert(service::invocation(std::array<std::wstring,2>{service::name,service::inspect_state}).mode==service::Mode::Inspect);
    const auto helper=service::invocation(std::array<std::wstring,3>{service::name,service::helper,nonce});assert(helper.mode==service::Mode::RecoveryOwner&&helper.nonce==nonce);
    for(const auto &arguments:std::vector<std::vector<std::wstring>>{{},{L"OtherService"},{service::name,service::helper},
        {service::name,service::helper,L""},{service::name,service::helper,std::wstring(32,L'A')},
        {service::name,L"--command",nonce},{service::name,service::helper,nonce,L"extra"},
        {service::name,service::initialize,L"C:\\other-state"},
        {service::name,service::inspect_state,L"C:\\other-result"}})refuses([&]{(void)service::invocation(arguments);});
    NativeWindowsFixture root;const auto program=root.root/L"Program Files"/L"wsprrypi.exe";
    const auto command=L"\""+program.native()+L"\" --native-controller-service --controller-only";
    service::definition(SERVICE_WIN32_OWN_PROCESS,service::account,command,program);
    refuses([&]{service::definition(SERVICE_WIN32_SHARE_PROCESS,service::account,command,program);});
    refuses([&]{service::definition(SERVICE_WIN32_OWN_PROCESS,L"LocalSystem",command,program);});
    refuses([&]{service::definition(SERVICE_WIN32_OWN_PROCESS,service::account,command+L" --backend simulated",program);});
    refuses([&]{service::definition(SERVICE_WIN32_OWN_PROCESS,service::account,L"\"C:\\other.exe\" --native-controller-service --controller-only",program);});
    refuses([&]{service::definition(SERVICE_WIN32_OWN_PROCESS,service::account,program.native()+L" --native-controller-service --controller-only",program);});
    // No service accounts are provisioned by this fixture. Their absence must
    // also refuse construction before any SCM operation.
    assert(!wsprrypi::windows_file::current_sid().starts_with(L"S-1-5-80-"));
    refuses([]{wsprrypi::recovery_windows::ControllerService unavailable;});
    refuses([]{wsprrypi::recovery_windows::ControllerServiceHost unavailable([](const auto &,HANDLE,const std::function<void()> &){return 0;},[]{});});
    refuses([]{wsprrypi::recovery_windows::OwnerListener unavailable;});
    std::cout<<"Native fixed service/helper grammar, exact binary/account/role admission, and unprovisioned ordinary-account refusal passed; no SCM mutation performed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
