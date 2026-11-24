// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
int main(){try{
    assert(::GetACP()==CP_UTF8);
    NativeWindowsFixture fixture;const auto path=fixture.root/L"migration-\u03a9.ini";
    {std::ofstream output(path,std::ios::binary);assert(output);output<<"[Network]\nEnforced=true\n";}
    const auto raw=path.u8string();const std::string filename(raw.begin(),raw.end());
    std::ifstream input(filename,std::ios::binary);assert(input);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    assert(bytes=="[Network]\nEnforced=true\n");
    std::cout<<"PASS process-local UTF-8 manifest and narrow standard-library Unicode migration path; no host code-page or locale change\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
