// SPDX-License-Identifier: MIT
#include "native_windows_owners.hpp"
#include "native_windows_arguments.hpp"
#include <iostream>
int main() {
    const char *stage="ProgramData known folder";
    try {
        const auto root=wsprrypi::windows_controller_root();
        wsprrypi::windows_file::require(root.is_absolute()&&root.filename()==L"WsprryPi");
        stage="ProgramFiles known folder";const auto program=wsprrypi::windows_program_root();
        wsprrypi::windows_file::require(program.is_absolute()&&program.filename()==L"WsprryPi"&&program!=root);
        stage="TrustedInstaller service SID";const auto installer=wsprrypi::windows_service_sid(L"NT SERVICE\\TrustedInstaller");
        stage="W32Time service SID";const auto clock=wsprrypi::windows_service_sid(L"NT SERVICE\\W32Time");
        wsprrypi::windows_file::require(installer!=clock&&installer.starts_with(L"S-1-5-80-")&&clock.starts_with(L"S-1-5-80-"));
        stage="current user SID";const auto current=wsprrypi::windows_file::current_sid();
        wsprrypi::windows_file::require(!current.empty());
        stage="UTF-16 arguments";wsprrypi::windows_file::require(!wsprrypi::native_windows_arguments().empty());
        stage="Unicode conversion";wsprrypi::windows_file::require(wsprrypi::windows_argument_utf8(L"\u03a9 \U0001f4fb")=="\xce\xa9 \xf0\x9f\x93\xbb");
        bool refused=false;
        try{(void)wsprrypi::windows_argument_utf8(std::wstring(1,static_cast<wchar_t>(0xd800)));}catch(const std::runtime_error &){refused=true;}
        wsprrypi::windows_file::require(refused);
        std::cout<<"PASS native ProgramData selection and distinct OS-resolved service SIDs\n";
    } catch(const std::exception &error) {
        std::cerr<<stage<<": "<<error.what()<<'\n';return 1;
    }
}
