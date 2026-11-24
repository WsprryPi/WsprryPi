// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "Singleton/src/installation_lock.hpp"
#include <memory>
namespace wsprrypi::recovery_windows {
// Recovery may join only the established installation witness. The native
// component still supplies the same persistent byte-range lock as the app;
// an absent or damaged witness is never provisioned or repaired here.
class InstallationAdmission {
public:
    struct Busy {};
    explicit InstallationAdmission(const std::filesystem::path &directory):parents_(directory/L"instance.lock",windows_file::current_sid()){
        namespace wf=windows_file;const auto owner=wf::current_sid();
        directory_=wf::Handle(::CreateFileW(wf::api_path(directory).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(directory_.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(directory_.get(),true);
        wf::verify_exact_owner(directory_.get(),owner);wf::verify_security(directory_.get(),owner,true,true);
        witness_=wf::Handle(::CreateFileW(wf::api_path(directory/L"instance.lock").c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(witness_.get()!=INVALID_HANDLE_VALUE);const auto info=wf::inspect(witness_.get(),false);wf::require(info.standard.EndOfFile.QuadPart==0);
        wf::verify_exact_owner(witness_.get(),owner);wf::verify_security(witness_.get(),owner,true,false);
        lock_=std::make_unique<::InstallationLock>(directory);if(!(*lock_)())throw Busy{};
        wf::require(wf::same_snapshot(info,wf::inspect(witness_.get(),false)));
    }
private:
    windows_file::ParentChain parents_;windows_file::Handle directory_,witness_;std::unique_ptr<::InstallationLock> lock_;
};
}
#endif
