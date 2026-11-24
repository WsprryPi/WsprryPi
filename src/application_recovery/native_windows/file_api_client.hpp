// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "file_api.hpp"
#include "native_windows_file.hpp"
#include <array>
#include <bit>
namespace wsprrypi::recovery_windows {
// Load only the explicitly selected protected image. A production executable
// must not import this optional adapter through Windows' default DLL search.
class FileApiClient {
public:
    explicit FileApiClient(const std::string &library):path_(windows_file::path_from_utf8(library)),parents_(path_,windows_file::current_sid()){
        namespace wf=windows_file;wf::require(path_.filename()==L"wsprrypi-recovery-native.dll");
        image_=wf::Handle(::CreateFileW(wf::api_path(path_).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(image_.get()!=INVALID_HANDLE_VALUE);const auto before=wf::inspect(image_.get(),false);
        wf::require(before.standard.EndOfFile.QuadPart>0&&before.standard.EndOfFile.QuadPart<=64*1024*1024);
        wf::verify_security(image_.get(),wf::current_sid(),false,false);
        module_.value=::LoadLibraryExW(wf::api_path(path_).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        wf::require(module_.value!=nullptr);
        std::array<wchar_t,32768> loaded{};const auto size=::GetModuleFileNameW(module_.value,loaded.data(),static_cast<DWORD>(loaded.size()));
        wf::require(size>0&&size<loaded.size());
        // Confirm the image Windows returned is the pinned file, including an
        // already-loaded module, before calling any exported operation.
        wf::Handle actual(::CreateFileW(loaded.data(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(actual.get()!=INVALID_HANDLE_VALUE&&wf::same_snapshot(before,wf::inspect(actual.get(),false)));
        const auto version=resolve<decltype(&wrn_version)>("wrn_version"),info_size=resolve<decltype(&wrn_info_size)>("wrn_info_size");
        wf::require(version()==1&&info_size()==sizeof(WrnInfo));
        acquire=resolve<decltype(&wrn_directory_acquire)>("wrn_directory_acquire");close=resolve<decltype(&wrn_directory_close)>("wrn_directory_close");
    }
    decltype(&wrn_directory_acquire) acquire{};
    decltype(&wrn_directory_close) close{};
private:
    std::filesystem::path path_;windows_file::ParentChain parents_;windows_file::Handle image_;
    struct Module {HMODULE value{};~Module(){if(value)::FreeLibrary(value);}} module_;
    template<class F>F resolve(const char *name){const auto symbol=::GetProcAddress(module_.value,name);windows_file::require(symbol!=nullptr);return std::bit_cast<F>(symbol);}
};
}
#endif
