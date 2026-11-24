// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "windows_sqlite_owner_lock.hpp"
#include <memory>

namespace wsprrypi {
// Native namespace admission around SQLite's Win32 VFS. SQLite retains all SQL,
// WAL, transaction, backup, and crash-recovery decisions.
class WindowsSqliteFiles {
public:
    WindowsSqliteFiles(const std::string &requested,bool create)
        :root_(windows_file::path_from_utf8(requested)),file_(root_/L"state.sqlite3"),
         owner_(windows_file::current_sid()),default_owner_(windows_file::token_sid(TokenOwner)) {
        if(owner_.starts_with(L"S-1-5-80-"))windows_file::require(default_owner_==owner_);
        // Windows may select Administrators as an administrator token's object
        // owner. Accept only that token's actual default, never an arbitrary
        // other account. Restricted service tokens select their own user SID.
        if(default_owner_!=owner_) {
            PSID sid{};windows_file::require(::ConvertStringSidToSidW(default_owner_.c_str(),&sid));
            struct Free {PSID value;~Free(){::LocalFree(value);}} free{sid};
            windows_file::require(::IsWellKnownSid(sid,WinBuiltinAdministratorsSid)||::IsWellKnownSid(sid,WinLocalSystemSid));
        }
        ancestors_=std::make_unique<windows_file::ParentChain>(file_,owner_);
        directory_=open_directory(root_);directory_identity_=windows_file::inspect(directory_.get(),true);
        windows_file::require(present(file_)!=create);
        if(!create)verify_named(file_,32*1048576);
        windows_file::flush_directory(root_,owner_); // Refuse unsupported barriers before creating state/locks.
        windows_file::PrivateSecurity security(owner_);
        lock_=std::make_unique<WindowsSqliteOwnerLock>(root_,create);
        database_=windows_file::Handle(::CreateFileW(windows_file::api_path(file_).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
            FILE_SHARE_READ|FILE_SHARE_WRITE,create?security.attributes():nullptr,create?CREATE_NEW:OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
        windows_file::require(database_.get()!=INVALID_HANDLE_VALUE);
        database_identity_=windows_file::inspect(database_.get(),false);verify_file(database_.get(),32*1048576);
        check();
    }
    ~WindowsSqliteFiles(){
        // SQLite itself has closed before this adapter is destroyed. Retire
        // every file/directory pin before releasing the shared owner mutex.
        database_=windows_file::Handle();directory_=windows_file::Handle();ancestors_.reset();lock_.reset();
    }
    std::string directory() const{return text(root_);}
    std::string path() const{return text(file_);}
    void check() const {
        windows_file::ParentChain current(file_,owner_);
        auto named_directory=open_directory(root_);
        windows_file::require(same_object(directory_identity_,windows_file::inspect(named_directory.get(),true)));
        windows_file::verify_exact_owner(directory_.get(),owner_);
        windows_file::verify_security(directory_.get(),owner_,true,true);
        auto named_file=verify_named(file_,32*1048576);
        windows_file::require(same_object(database_identity_,windows_file::inspect(named_file.get(),false)));
        verify_file(database_.get(),32*1048576);
        lock_->check();
        for(const auto *suffix:{L"-wal",L"-shm",L"-journal"}) {
            const auto sidecar=std::filesystem::path(file_.native()+suffix);
            if(present(sidecar))(void)verify_named(sidecar,64*1048576);
        }
    }
    void flush() const {
        check();windows_file::require(::FlushFileBuffers(database_.get()));
        const auto wal=std::filesystem::path(file_.native()+L"-wal");
        if(present(wal)) {
            auto handle=verify_named(wal,64*1048576);
            windows_file::require(::FlushFileBuffers(handle.get()));
        }
        windows_file::flush_directory(root_,owner_);
    }
private:
    std::filesystem::path root_,file_;
    std::wstring owner_,default_owner_;
    std::unique_ptr<windows_file::ParentChain> ancestors_;
    windows_file::Handle directory_;
    std::unique_ptr<WindowsSqliteOwnerLock> lock_;
    windows_file::Handle database_;
    windows_file::Snapshot directory_identity_,database_identity_;
    static std::string text(const std::filesystem::path &path){const auto value=path.u8string();return {value.begin(),value.end()};}
    static bool present(const std::filesystem::path &path) {
        if(::GetFileAttributesW(windows_file::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
        const auto error=::GetLastError();windows_file::require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
    }
    static bool same_object(const windows_file::Snapshot &a,const windows_file::Snapshot &b) {
        return a.file_id.VolumeSerialNumber==b.file_id.VolumeSerialNumber&&
            std::memcmp(a.file_id.FileId.Identifier,b.file_id.FileId.Identifier,sizeof a.file_id.FileId.Identifier)==0;
    }
    windows_file::Handle open_directory(const std::filesystem::path &path) const {
        windows_file::Handle handle(::CreateFileW(windows_file::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        windows_file::require(handle.get()!=INVALID_HANDLE_VALUE);(void)windows_file::inspect(handle.get(),true);
        windows_file::verify_exact_owner(handle.get(),owner_);windows_file::verify_security(handle.get(),owner_,true,true);return handle;
    }
    void verify_file(HANDLE handle,std::uint64_t maximum) const {
        const auto info=windows_file::inspect(handle,false);
        windows_file::require(info.standard.EndOfFile.QuadPart>=0&&static_cast<std::uint64_t>(info.standard.EndOfFile.QuadPart)<=maximum);
        const auto actual_owner=windows_file::file_owner_sid(handle);
        windows_file::require(actual_owner==owner_||actual_owner==default_owner_);
        windows_file::verify_security(handle,owner_,true,false);
    }
    windows_file::Handle verify_named(const std::filesystem::path &path,std::uint64_t maximum) const {
        windows_file::Handle handle(::CreateFileW(windows_file::api_path(path).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        windows_file::require(handle.get()!=INVALID_HANDLE_VALUE);verify_file(handle.get(),maximum);return handle;
    }
};
}
#endif
