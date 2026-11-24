// SPDX-License-Identifier: MIT
#pragma once
#include "native_windows_file.hpp"
#include "native_random.hpp"
#include <array>
struct NativeWindowsFixture {
    std::filesystem::path root;
    bool retain{};
    NativeWindowsFixture() {
        std::array<std::uint8_t,16> random{};wsprrypi::native_random(random);
        std::wstring suffix;
        for(auto byte:random){suffix+=L"0123456789abcdef"[byte>>4];suffix+=L"0123456789abcdef"[byte&15];}
        root=std::filesystem::temp_directory_path()/(L"wsprrypi-native-"+suffix);
        wsprrypi::windows_file::PrivateSecurity security(wsprrypi::windows_file::current_sid());
        wsprrypi::windows_file::require(::CreateDirectoryW(wsprrypi::windows_file::api_path(root).c_str(),security.attributes()));
    }
    ~NativeWindowsFixture(){if(retain)return;std::error_code error;std::filesystem::remove_all(wsprrypi::windows_file::api_path(root),error);}
    NativeWindowsFixture(const NativeWindowsFixture &)=delete;
    NativeWindowsFixture &operator=(const NativeWindowsFixture &)=delete;
    static std::string utf8(const std::filesystem::path &path){const auto value=path.u8string();return {value.begin(),value.end()};}
};
