// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <shlobj.h>
#include <cstring>
#include <array>

namespace wsprrypi {
struct NativeWindowsAccount {std::string home;std::wstring sid;};
struct NativeWindowsOwners {NativeWindowsAccount backend,web,http,maintenance;};
inline std::wstring windows_service_sid(const wchar_t *name) {
    DWORD sid_size{},domain_size{};SID_NAME_USE use{};
    ::LookupAccountNameW(nullptr,name,nullptr,&sid_size,nullptr,&domain_size,&use);
    windows_file::require(sid_size>0&&sid_size<=SECURITY_MAX_SID_SIZE&&domain_size<=32768);
    std::vector<unsigned char> bytes(sid_size);std::vector<wchar_t> domain(domain_size);
    windows_file::require(::LookupAccountNameW(nullptr,name,bytes.data(),&sid_size,domain.data(),&domain_size,&use)&&::IsValidSid(bytes.data()));
    SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;
    windows_file::require(std::memcmp(::GetSidIdentifierAuthority(bytes.data()),&nt,sizeof nt)==0&&
        *::GetSidSubAuthorityCount(bytes.data())==6&&*::GetSidSubAuthority(bytes.data(),0)==SECURITY_SERVICE_ID_BASE_RID);
    LPWSTR text{};windows_file::require(::ConvertSidToStringSidW(bytes.data(),&text));
    struct Free {LPWSTR value;~Free(){::LocalFree(value);}} free{text};return text;
}
inline std::filesystem::path windows_known_directory(REFKNOWNFOLDERID folder) {
    const auto initialized=::CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    windows_file::require(SUCCEEDED(initialized)||initialized==RPC_E_CHANGED_MODE);
    struct Apartment {bool owned;~Apartment(){if(owned)::CoUninitialize();}} apartment{SUCCEEDED(initialized)};
    PWSTR text{};
    const auto result=::SHGetKnownFolderPath(folder,KF_FLAG_DONT_VERIFY,nullptr,&text);
    struct Free {PWSTR value;~Free(){if(value)::CoTaskMemFree(value);}} free{text};
    windows_file::require(SUCCEEDED(result)&&text&&*text);
    const auto value=(std::filesystem::path(text)/L"WsprryPi").u8string();
    return windows_file::path_from_utf8(std::string(value.begin(),value.end()));
}
inline std::filesystem::path windows_controller_root(){return windows_known_directory(FOLDERID_ProgramData);}
inline std::filesystem::path windows_program_root(){return windows_known_directory(FOLDERID_ProgramFiles);}
inline NativeWindowsOwners native_windows_controller_owners() {
    const auto root=windows_controller_root();
    const auto account=[&](const wchar_t *name,const wchar_t *directory) {
        const auto value=(root/directory).u8string();
        return NativeWindowsAccount{std::string(value.begin(),value.end()),windows_service_sid(name)};
    };
    NativeWindowsOwners result{
        account(L"NT SERVICE\\WsprryPiController",L"Controller"),
        account(L"NT SERVICE\\WsprryPiWeb",L"Web"),
        account(L"NT SERVICE\\WsprryPiHttp",L"Http"),
        account(L"NT SERVICE\\WsprryPiMaintenance",L"Maintenance")};
    const std::array<std::wstring,4> identities{result.backend.sid,result.web.sid,result.http.sid,result.maintenance.sid};
    for(std::size_t a=0;a<identities.size();++a)
        for(std::size_t b=a+1;b<identities.size();++b)windows_file::require(identities[a]!=identities[b]);
    return result;
}
} // namespace wsprrypi
#endif
