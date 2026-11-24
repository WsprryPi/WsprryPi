// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include <iostream>
// Explicit native NTFS experiment. Successful calls measure API behavior only;
// they do not establish power-loss durability or qualify the journal adapter.
int main(){try{
    namespace wf=wsprrypi::windows_file;
    SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;PSID administrators{};
    wf::require(::AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&administrators));
    struct Free {PSID value;~Free(){::FreeSid(value);}} free{administrators};BOOL elevated{};
    wf::require(::CheckTokenMembership(nullptr,administrators,&elevated));
    if(elevated){std::cout<<"{\"unprivileged_owner_required\":true,\"experiment_complete\":false}\n";return 77;}
    NativeWindowsFixture fixture;const auto own=wf::current_sid();
    wf::ParentChain ancestors(fixture.root,own);
    wf::Handle directory(::CreateFileW(wf::api_path(fixture.root).c_str(),GENERIC_WRITE|READ_CONTROL|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    const auto open_error=directory.get()==INVALID_HANDLE_VALUE?::GetLastError():ERROR_SUCCESS;
    if(open_error){std::cout<<"{\"directory_open_error\":"<<open_error<<",\"experiment_complete\":false}\n";return 77;}
    (void)wf::inspect(directory.get(),true);wf::verify_exact_owner(directory.get(),own);wf::verify_security(directory.get(),own,true,true);
    const auto path=fixture.root/L"barrier-probe";
    {
        wf::PrivateSecurity security(own);
        wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),GENERIC_WRITE|READ_CONTROL|FILE_READ_ATTRIBUTES,0,
            security.attributes(),CREATE_NEW,FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(file.get()!=INVALID_HANDLE_VALUE);wf::verify_exact_owner(file.get(),own);
        DWORD count{};wf::require(::WriteFile(file.get(),"probe",5,&count,nullptr)&&count==5&&::FlushFileBuffers(file.get()));
    }
    const auto published=::FlushFileBuffers(directory.get());const auto publish_error=published?ERROR_SUCCESS: ::GetLastError();
    const auto removed=::DeleteFileW(wf::api_path(path).c_str());const auto remove_error=removed?ERROR_SUCCESS: ::GetLastError();
    const auto retired=::FlushFileBuffers(directory.get());const auto retire_error=retired?ERROR_SUCCESS: ::GetLastError();
    std::cout<<"{\"directory_open_error\":0,\"publish_flush_error\":"<<publish_error<<",\"remove_error\":"<<remove_error
        <<",\"retire_flush_error\":"<<retire_error<<",\"experiment_complete\":true,\"power_loss_qualified\":false}\n";
    return published&&removed&&retired?0:77;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
