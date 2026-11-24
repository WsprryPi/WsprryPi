// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <memory>
namespace wsprrypi::windows_generation {
namespace wf=windows_file;
class Busy:public std::runtime_error {
public:Busy():std::runtime_error("The application still owns the selected state"){}
};
// Shared by the application and recovery DLL. This is distinct from both the
// installation lock and SQLite's native owner lock; never unlink the witness.
class Admission {
public:
    std::string path;
    explicit Admission(const std::string &input){
        const auto requested=wf::path_from_utf8(input);const auto owner=wf::current_sid();
        parents_=std::make_unique<wf::ParentChain>(requested,owner);
        directory_=wf::Handle(::CreateFileW(wf::api_path(requested).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(directory_.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(directory_.get(),true);
        wf::verify_exact_owner(directory_.get(),owner);wf::verify_security(directory_.get(),owner,true,true);
        const auto root=wf::normalized_path(directory_.get());const auto bytes=root.u8string();path.assign(bytes.begin(),bytes.end());
        wf::flush_directory(root.parent_path(),owner);wf::PrivateSecurity security(owner);
        const auto witness=wf::path_from_utf8(path+".generation.lock");
        lock_=wf::Handle(::CreateFileW(wf::api_path(witness).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
            FILE_SHARE_READ|FILE_SHARE_WRITE,security.attributes(),OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
        wf::require(lock_.get()!=INVALID_HANDLE_VALUE);const auto info=wf::inspect(lock_.get(),false);
        wf::require(info.standard.EndOfFile.QuadPart==0);wf::verify_exact_owner(lock_.get(),owner);wf::verify_security(lock_.get(),owner,true,false);
        OVERLAPPED overlap{};
        if(!::LockFileEx(lock_.get(),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)){
            if(::GetLastError()==ERROR_LOCK_VIOLATION)throw Busy();
            wf::require(false);
        }
        wf::require(::FlushFileBuffers(lock_.get()));wf::flush_directory(root.parent_path(),owner);
    }
    void sync(){wf::flush_directory(wf::path_from_utf8(path),wf::current_sid());}
private:
    std::unique_ptr<wf::ParentChain> parents_;
    wf::Handle directory_,lock_;
};
}
#endif
