// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_mutex_owner.hpp"
#include <array>
#include <bcrypt.h>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <thread>
namespace wsprrypi {
// The persistent marker is never repaired for established state. Ownership is
// a private Windows mutex bound to the owner SID, NTFS volume, and full directory
// identity. No descendant file remains open solely for ownership: Windows cannot
// rename a directory containing open files, even for stopped publication.
class WindowsSqliteOwnerLock {
public:
    enum class Mobility { Pinned, StoppedCandidate };
    using Busy=WindowsMutexOwner::Busy;
    explicit WindowsSqliteOwnerLock(const std::filesystem::path &root,bool allow_create,Mobility mobility=Mobility::Pinned)
        :owner_(windows_file::current_sid()),parents_(std::make_unique<windows_file::ParentChain>(
            mobility==Mobility::StoppedCandidate?root:root/L"state.sqlite3.owner.lock",owner_)){
        namespace wf=windows_file;
        try{
            wf::require((mobility==Mobility::Pinned||mobility==Mobility::StoppedCandidate)&&(!allow_create||mobility==Mobility::Pinned));
            directory_=wf::Handle(::CreateFileW(wf::api_path(root).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ|FILE_SHARE_WRITE|(mobility==Mobility::StoppedCandidate?FILE_SHARE_DELETE:0),
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
            wf::require(directory_.get()!=INVALID_HANDLE_VALUE);const auto identity=wf::inspect(directory_.get(),true);
            wf::verify_exact_owner(directory_.get(),owner_);wf::verify_security(directory_.get(),owner_,true,true);
            wf::PrivateSecurity security(owner_);
            mutex_=std::make_unique<WindowsMutexOwner>(identity,owner_,WindowsMutexOwner::Family::Sqlite);
            // Abandoned ownership grants admission only. The SQL/journal owner
            // still validates all established records and interrupted evidence.
            const auto path=root/L"state.sqlite3.owner.lock";
            wf::Handle marker(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES|(allow_create?GENERIC_READ|GENERIC_WRITE:0),
                FILE_SHARE_READ,allow_create?security.attributes():nullptr,allow_create?OPEN_ALWAYS:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            wf::require(marker.get()!=INVALID_HANDLE_VALUE);marker_=wf::inspect(marker.get(),false);
            wf::require(marker_.standard.EndOfFile.QuadPart==0);wf::verify_exact_owner(marker.get(),owner_);wf::verify_security(marker.get(),owner_,true,false);
            if(allow_create){wf::require(::FlushFileBuffers(marker.get()));wf::flush_directory(root,owner_);}
            const auto initial=marker_;marker=wf::Handle();marker_=read_marker(root);
            wf::require(same_object(initial,marker_));
        }catch(...){release();throw;}
    }
    ~WindowsSqliteOwnerLock(){release();}
    WindowsSqliteOwnerLock(const WindowsSqliteOwnerLock &)=delete;
    WindowsSqliteOwnerLock &operator=(const WindowsSqliteOwnerLock &)=delete;
    static void initialize_snapshot(const std::filesystem::path &root){
        windows_file::require(std::filesystem::is_empty(windows_file::api_path(root)));
        WindowsSqliteOwnerLock owner(root,true);owner.check();
    }
    // The publication owner must retain installation/generation admission and
    // verify the exact directory and marker identities after a stopped move.
    void verify_at(const std::filesystem::path &root)const{
        namespace wf=windows_file;wf::require(bool(mutex_));
        wf::ParentChain parents(root,owner_);
        wf::Handle directory(::CreateFileW(wf::api_path(root).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        wf::require(directory.get()!=INVALID_HANDLE_VALUE);
        const auto a=wf::inspect(directory_.get(),true),b=wf::inspect(directory.get(),true);
        wf::require(same_object(a,b)&&wf::same_snapshot(marker_,read_marker(root)));
        wf::verify_exact_owner(directory.get(),owner_);wf::verify_security(directory.get(),owner_,true,true);
        mutex_->check();
    }
    void check()const{verify_at(windows_file::normalized_path(directory_.get()));}
private:
    std::wstring owner_;std::unique_ptr<windows_file::ParentChain> parents_;windows_file::Handle directory_;
    std::unique_ptr<WindowsMutexOwner> mutex_;windows_file::Snapshot marker_{};
    static bool same_object(const windows_file::Snapshot &a,const windows_file::Snapshot &b){
        return a.file_id.VolumeSerialNumber==b.file_id.VolumeSerialNumber&&
            std::memcmp(a.file_id.FileId.Identifier,b.file_id.FileId.Identifier,sizeof a.file_id.FileId.Identifier)==0;
    }
    windows_file::Snapshot read_marker(const std::filesystem::path &root)const{
        namespace wf=windows_file;
        wf::Handle marker(::CreateFileW(wf::api_path(root/L"state.sqlite3.owner.lock").c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(marker.get()!=INVALID_HANDLE_VALUE);const auto info=wf::inspect(marker.get(),false);
        wf::require(info.standard.EndOfFile.QuadPart==0);wf::verify_exact_owner(marker.get(),owner_);wf::verify_security(marker.get(),owner_,true,false);return info;
    }
    void release()noexcept{
        // Release filesystem pins before a successor can acquire ownership.
        directory_=windows_file::Handle();parents_.reset();mutex_.reset();
    }

};
}
#endif
