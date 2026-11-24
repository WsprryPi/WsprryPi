// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_mutex_owner.hpp"
namespace wsprrypi {
// Private non-database directory ownership shared by application and recovery.
// SQLite always uses its distinct mandatory marker and mutex family.
class WindowsDirectoryOwner {
public:
    enum class Mobility { Pinned, Removable };
    using Busy=WindowsMutexOwner::Busy;
    explicit WindowsDirectoryOwner(const std::filesystem::path &path,Mobility mobility=Mobility::Pinned)
        :path_(path),owner_(windows_file::current_sid()),parents_(std::make_unique<windows_file::ParentChain>(path_,owner_)),caller_(::GetCurrentThreadId()){
        namespace wf=windows_file;
        wf::require(mobility==Mobility::Pinned||mobility==Mobility::Removable);
        directory_=open();identity_=wf::inspect(directory_.get(),true);non_database();
        mutex_=std::make_unique<WindowsMutexOwner>(identity_,owner_,WindowsMutexOwner::Family::Directory);
        check();non_database();
        if(mobility==Mobility::Removable)directory_=wf::Handle();
    }
    ~WindowsDirectoryOwner(){directory_=windows_file::Handle();parents_.reset();mutex_.reset();}
    WindowsDirectoryOwner(const WindowsDirectoryOwner &)=delete;
    WindowsDirectoryOwner &operator=(const WindowsDirectoryOwner &)=delete;
    bool current_thread()const noexcept{return caller_==::GetCurrentThreadId();}
    void check()const{
        namespace wf=windows_file;wf::require(bool(mutex_));auto named=open();const auto current=wf::inspect(named.get(),true);
        wf::require(current.file_id.VolumeSerialNumber==identity_.file_id.VolumeSerialNumber&&
            std::memcmp(current.file_id.FileId.Identifier,identity_.file_id.FileId.Identifier,sizeof current.file_id.FileId.Identifier)==0);
        mutex_->check();
    }
    std::filesystem::path path()const{return windows_file::normalized_path(directory_.get());}
private:
    std::filesystem::path path_;std::wstring owner_;std::unique_ptr<WindowsMutexOwner> mutex_;std::unique_ptr<windows_file::ParentChain> parents_;
    windows_file::Handle directory_;windows_file::Snapshot identity_{};DWORD caller_{};
    windows_file::Handle open()const{
        namespace wf=windows_file;
        wf::Handle handle(::CreateFileW(wf::api_path(path_).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
        wf::verify_exact_owner(handle.get(),owner_);wf::verify_security(handle.get(),owner_,true,true);return handle;
    }
    void non_database()const{
        namespace wf=windows_file;
        for(const auto *name:{L"state.sqlite3",L"state.sqlite3.owner.lock",L"state.sqlite3-wal",L"state.sqlite3-shm",L"state.sqlite3-journal"}){
            const auto attributes=::GetFileAttributesW(wf::api_path(path_/name).c_str());
            wf::require(attributes==INVALID_FILE_ATTRIBUTES&&::GetLastError()==ERROR_FILE_NOT_FOUND);
        }
    }
};
}
#endif
