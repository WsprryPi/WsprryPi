// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "application_storage/windows_sqlite_owner_lock.hpp"
namespace wsprrypi::recovery_windows {
// Admission for an already closed candidate database. Never replace SQLite's
// persistent owner witness with the removable-directory/job mutex.
class SealedDatabase {
public:
    explicit SealedDatabase(const std::filesystem::path &root):root_(root),owner_(root,false){}
    void check()const{owner_.check();}
    void retire_empty_sidecars(){
        namespace wf=windows_file;
        owner_.check();
        const auto journal=::GetFileAttributesW(wf::api_path(root_/L"state.sqlite3-journal").c_str());
        wf::require(journal==INVALID_FILE_ATTRIBUTES&&::GetLastError()==ERROR_FILE_NOT_FOUND);
        auto wal=sidecar(root_/L"state.sqlite3-wal",0),shm=sidecar(root_/L"state.sqlite3-shm",1048576);
        // Validate both before retiring either. A nonempty WAL always refuses;
        // a failed retirement leaves an inactive candidate, never publication.
        for(auto *handle:{&wal,&shm})if(handle->get()!=INVALID_HANDLE_VALUE){
            FILE_DISPOSITION_INFO remove{TRUE};
            wf::require(::SetFileInformationByHandle(handle->get(),FileDispositionInfo,&remove,sizeof remove));
        }
        wal=wf::Handle();shm=wf::Handle();wf::flush_directory(root_,wf::current_sid());owner_.check();
    }
private:
    std::filesystem::path root_;WindowsSqliteOwnerLock owner_;
    static windows_file::Handle sidecar(const std::filesystem::path &path,std::uint64_t maximum){
        namespace wf=windows_file;
        wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|DELETE|READ_CONTROL,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(file.get()==INVALID_HANDLE_VALUE){wf::require(::GetLastError()==ERROR_FILE_NOT_FOUND);return file;}
        const auto info=wf::inspect(file.get(),false);
        wf::require(info.standard.EndOfFile.QuadPart>=0&&static_cast<std::uint64_t>(info.standard.EndOfFile.QuadPart)<=maximum);
        wf::verify_exact_owner(file.get(),wf::current_sid());wf::verify_security(file.get(),wf::current_sid(),true,false);return file;
    }
};
}
#endif
