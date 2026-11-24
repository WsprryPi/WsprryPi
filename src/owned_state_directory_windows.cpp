// SPDX-License-Identifier: MIT
#include "owned_state_directory.hpp"
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <array>

namespace wsprrypi {
namespace {
namespace wf=windows_file;
void require(bool value){if(!value)throw std::runtime_error("owned_state_storage_hold");}
std::string utf8(const std::filesystem::path &path){const auto text=path.u8string();return {text.begin(),text.end()};}
bool path_exists(const std::filesystem::path &path) {
    if(::GetFileAttributesW(wf::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
wf::Handle private_directory(const std::filesystem::path &path,const std::wstring &owner) {
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),owner);wf::verify_security(handle.get(),owner,true,true);return handle;
}
void validate_lock(HANDLE handle,const std::wstring &owner) {
    const auto info=wf::inspect(handle,false);require(info.standard.EndOfFile.QuadPart<=1);
    wf::verify_exact_owner(handle,owner);wf::verify_security(handle,owner,true,false);
}
unsigned marker(HANDLE handle) {
    LARGE_INTEGER zero{};require(::SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN));
    unsigned char value{};DWORD count{};require(::ReadFile(handle,&value,1,&count,nullptr)&&value<=1);return count?value:0;
}
void prepared_marker(HANDLE handle) {
    LARGE_INTEGER zero{};require(::SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN));
    const unsigned char value=1;DWORD count{};
    require(::WriteFile(handle,&value,1,&count,nullptr)&&count==1&&::SetEndOfFile(handle)&&::FlushFileBuffers(handle));
}
void write_image(const std::filesystem::path &path,const std::string &bytes,wf::PrivateSecurity &security,const std::wstring &owner,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint) {
    wf::ParentChain ancestry(path,owner);
    wf::Handle output(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,
        security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
    require(output.get()!=INVALID_HANDLE_VALUE);
    wf::verify_exact_owner(output.get(),owner);wf::verify_security(output.get(),owner,true,false);
    DWORD written{};require(::WriteFile(output.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size());
    if(checkpoint)checkpoint(OwnedStatePublicationPoint::WrittenImage);
    require(::FlushFileBuffers(output.get()));
}
void remove_incomplete(const std::filesystem::path &path,const std::wstring &owner) {
    {
        wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        require(file.get()!=INVALID_HANDLE_VALUE);
        const auto info=wf::inspect(file.get(),false);require(info.standard.EndOfFile.QuadPart<=1048576);
        wf::verify_exact_owner(file.get(),owner);wf::verify_security(file.get(),owner,true,false);
    }
    require(::DeleteFileW(wf::api_path(path).c_str()));
    wf::flush_directory(path.parent_path(),owner);
}
}
OwnedStateDirectory prepare_owned_state_directory(const std::string &requested,
    const std::function<std::string()> &initial,const std::function<void(const std::string &)> &validate,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint) {
    require(bool(initial)&&bool(validate));
    const auto directory=wf::path_from_utf8(requested),parent=directory.parent_path();
    const auto owner=wf::current_sid();
    wf::ParentChain ancestry(directory,owner);
    auto parent_handle=private_directory(parent,owner);
    wf::flush_directory(parent,owner); // Native barrier preflight before provisioning.
    const auto authority=directory/L"authority.json";
    if(path_exists(directory)) {
        auto existing=private_directory(directory,owner);
        return {utf8(authority),true};
    }
    const auto staged=std::filesystem::path(directory.native()+L".prepare");
    const auto lock_path=std::filesystem::path(directory.native()+L".prepare.lock");
    // This persistent sidecar is a lock and prior-publication witness, never a
    // second authority record. A lost established image cannot seed another ID.
    require(!path_exists(lock_path)||path_exists(staged));
    wf::PrivateSecurity security(owner);
    require(::CreateDirectoryW(wf::api_path(staged).c_str(),security.attributes())||::GetLastError()==ERROR_ALREADY_EXISTS);
    auto staged_handle=private_directory(staged,owner);
    wf::Handle lock(::CreateFileW(wf::api_path(lock_path).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
        FILE_SHARE_READ|FILE_SHARE_WRITE,security.attributes(),OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
    require(lock.get()!=INVALID_HANDLE_VALUE);validate_lock(lock.get(),owner);
    OVERLAPPED overlap{};require(::LockFileEx(lock.get(),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap));
    const auto publication_prepared=marker(lock.get());
    wf::flush_directory(parent,owner);
    // Recheck after acquiring the OS lock; a concurrent initializer may have
    // published between the initial absence check and stage-directory creation.
    if(path_exists(directory)) {
        auto existing=private_directory(directory,owner);
        return {utf8(authority),true};
    }
    const auto writing=staged/L"writing",ready=staged/L"authority.json";
    for(const auto &entry:std::filesystem::directory_iterator(std::filesystem::path(wf::api_path(staged))))
        require(entry.path().filename()==L"authority.json"||entry.path().filename()==L"writing");
    if(checkpoint)checkpoint(OwnedStatePublicationPoint::PreparedDirectory);
    // Once a complete image has existed, only that retained image can resume.
    require(!publication_prepared||path_exists(ready));
    if(path_exists(writing))remove_incomplete(writing,owner);
    if(!path_exists(ready)) {
        const auto bytes=initial();require(!bytes.empty()&&bytes.size()<=1048576);validate(bytes);
        write_image(writing,bytes,security,owner,checkpoint);
        require(::MoveFileExW(wf::api_path(writing).c_str(),wf::api_path(ready).c_str(),MOVEFILE_WRITE_THROUGH));
    }
    const auto bytes=wf::read_protected(utf8(ready),true,1048576,owner);
    validate(std::string(bytes.begin(),bytes.end()));
    wf::flush_directory(staged,owner);wf::flush_directory(parent,owner);
    prepared_marker(lock.get());
    if(checkpoint)checkpoint(OwnedStatePublicationPoint::StagedImage);
    // Native directory moves require the contained file handles to be closed.
    // The external LockFileEx lock remains held throughout publication.
    staged_handle=wf::Handle();
    require(::MoveFileExW(wf::api_path(staged).c_str(),wf::api_path(directory).c_str(),MOVEFILE_WRITE_THROUGH));
    if(checkpoint)checkpoint(OwnedStatePublicationPoint::PublishedDirectory);
    wf::flush_directory(parent,owner);
    auto published=private_directory(directory,owner);
    if(checkpoint)checkpoint(OwnedStatePublicationPoint::SyncedDirectory);
    return {utf8(authority),false};
}
} // namespace wsprrypi
#endif
