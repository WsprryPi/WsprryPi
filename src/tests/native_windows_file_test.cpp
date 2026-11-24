// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "native_windows_file.hpp"
#include "native_random.hpp"
#include <array>
#include <iostream>
#include <winioctl.h>
#include <cstring>
#include <atomic>
#include <thread>
namespace wf=wsprrypi::windows_file;
#define CHECK(value) do{if(!(value))throw std::runtime_error(#value);}while(false)
template<class F> void refuses(F operation) {
    bool refused=false;try{operation();}catch(const std::runtime_error &){refused=true;}
    CHECK(refused);
}
static std::string utf8(const std::filesystem::path &path) {
    const auto value=path.u8string();return {value.begin(),value.end()};
}
struct Security {
    PSECURITY_DESCRIPTOR value{};
    explicit Security(const std::wstring &sddl) {
        CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&value,nullptr));
    }
    ~Security(){if(value)::LocalFree(value);}
};
int main() {
    try {
        const auto owner=wf::current_sid();
        const auto private_acl=L"O:"+owner+L"D:P(A;OICI;FA;;;"+owner+L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
        Security security(private_acl);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),security.value,FALSE};
        std::array<std::uint8_t,16> random{};wsprrypi::native_random(random);
        std::wstring suffix;
        for(auto byte:random){suffix+=L"0123456789abcdef"[byte>>4];suffix+=L"0123456789abcdef"[byte&15];}
        const auto root=std::filesystem::temp_directory_path()/(L"wsprrypi-private-file-"+suffix);
        CHECK(::CreateDirectoryW(root.c_str(),&attributes));
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{root};
        const auto file=root/L"authority.json";
        {
            wf::Handle output(::CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL|WRITE_DAC,
                FILE_SHARE_READ,&attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
            CHECK(output.get()!=INVALID_HANDLE_VALUE);
            DWORD flags{},count{};
            CHECK(::GetHandleInformation(output.get(),&flags)&&!(flags&HANDLE_FLAG_INHERIT));
            CHECK(::WriteFile(output.get(),"private",7,&count,nullptr)&&count==7&&::FlushFileBuffers(output.get()));
            // A writer held open before the reader cannot be overlooked.
            refuses([&]{(void)wf::read_protected(utf8(file),true,512);});
        }
        const auto bytes=wf::read_protected(utf8(file),true,512);
        CHECK(std::string(bytes.begin(),bytes.end())=="private");
        // Recovery appends portable multi-component archive member names to
        // admitted native roots. Extended Win32 paths require native separators.
        const auto snapshot=root/L"snapshot",application=snapshot/L"application";
        CHECK(::CreateDirectoryW(wf::api_path(snapshot).c_str(),&attributes));
        CHECK(::CreateDirectoryW(wf::api_path(application).c_str(),&attributes));
        const auto member=root/L"snapshot/application/authority.json";
        {
            wf::Handle output(::CreateFileW(wf::api_path(member).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
                FILE_SHARE_READ,&attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
            CHECK(output.get()!=INVALID_HANDLE_VALUE);DWORD count{};
            CHECK(::WriteFile(output.get(),"nested",6,&count,nullptr)&&count==6&&::FlushFileBuffers(output.get()));
        }
        const auto nested=wf::read_protected(utf8(application/L"authority.json"),true,512);
        CHECK(std::string(nested.begin(),nested.end())=="nested");
        refuses([&]{(void)wf::read_protected(utf8(file),true,6);});
        {
            wf::ParentChain chain(file,owner);
            const auto moved=root.wstring()+L"-moved";
            const auto renamed=::MoveFileExW(root.c_str(),moved.c_str(),0);
            if(renamed)CHECK(::MoveFileExW(moved.c_str(),root.c_str(),0));
            CHECK(!renamed);
        }
        const auto moved=root.wstring()+L"-moved";
        CHECK(::MoveFileExW(root.c_str(),moved.c_str(),0));
        CHECK(::MoveFileExW(moved.c_str(),root.c_str(),0));
        const auto transient=root/L"transient-parent",retained_writer=root/L"held-parent";
        CHECK(::CreateDirectoryW(wf::api_path(transient).c_str(),&attributes));
        CHECK(::CreateDirectoryW(wf::api_path(retained_writer).c_str(),&attributes));
        {
            // A raw writer models a different module/process, outside the
            // adapter's process-local metadata mutex. Admission must wait for
            // its closure, not merely switch to write-sharing on a private ACL.
            wf::Handle writer(::CreateFileW(wf::api_path(transient).c_str(),GENERIC_WRITE|READ_CONTROL,
                FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr));
            CHECK(writer.get()!=INVALID_HANDLE_VALUE);std::atomic_bool releasing{};
            std::jthread release([handle=std::move(writer),&releasing]()mutable{
                ::Sleep(100);releasing=true;handle=wf::Handle();
            });
            wf::ParentChain admitted(transient/L"new-file",owner);
            CHECK(releasing.load());
        }
        {
            wf::Handle writer(::CreateFileW(wf::api_path(retained_writer).c_str(),GENERIC_WRITE|READ_CONTROL,
                FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr));
            CHECK(writer.get()!=INVALID_HANDLE_VALUE);const auto started=::GetTickCount64();
            refuses([&]{wf::ParentChain rejected(retained_writer/L"new-file",owner);});
            CHECK(::GetTickCount64()-started<5000);
        }
        {wf::ParentChain admitted_after_close(retained_writer/L"new-file",owner);}
        const auto hard=root/L"alias.json";
        CHECK(::CreateHardLinkW(hard.c_str(),file.c_str(),nullptr));
        refuses([&]{(void)wf::read_protected(utf8(file),true,512);});
        CHECK(::DeleteFileW(hard.c_str()));
        const auto junction=root/L"redirect";
        CHECK(::CreateDirectoryW(junction.c_str(),&attributes));
        struct RemoveJunction {std::filesystem::path path;~RemoveJunction(){::RemoveDirectoryW(path.c_str());}} remove_junction{junction};
        {
            wf::Handle output(::CreateFileW(junction.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            CHECK(output.get()!=INVALID_HANDLE_VALUE);
            const auto substitute=L"\\??\\"+root.native(),display=root.native();
            struct Header {DWORD tag;WORD length,reserved,substitute_offset,substitute_length,display_offset,display_length;};
            static_assert(sizeof(Header)==16);
            std::vector<unsigned char> data(sizeof(Header)+(substitute.size()+display.size()+2)*sizeof(wchar_t));
            const Header header{IO_REPARSE_TAG_MOUNT_POINT,static_cast<WORD>(data.size()-8),0,0,
                static_cast<WORD>(substitute.size()*sizeof(wchar_t)),static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t)),
                static_cast<WORD>(display.size()*sizeof(wchar_t))};
            std::memcpy(data.data(),&header,sizeof(header));
            std::memcpy(data.data()+sizeof(header),substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
            std::memcpy(data.data()+sizeof(header)+(substitute.size()+1)*sizeof(wchar_t),display.c_str(),(display.size()+1)*sizeof(wchar_t));
            DWORD count{};
            CHECK(::DeviceIoControl(output.get(),FSCTL_SET_REPARSE_POINT,data.data(),static_cast<DWORD>(data.size()),nullptr,0,&count,nullptr));
        }
        refuses([&]{(void)wf::read_protected(utf8(junction/L"authority.json"),true,512);});
        CHECK(::RemoveDirectoryW(junction.c_str()));
        auto set_acl=[&](const std::wstring &extra,bool null_dacl=false) {
            Security descriptor(L"O:"+owner+L"D:P(A;;FA;;;"+owner+L")(A;;FA;;;SY)(A;;FA;;;BA)"+extra);
            BOOL present{},defaulted{};PACL dacl{};
            CHECK(::GetSecurityDescriptorDacl(descriptor.value,&present,&dacl,&defaulted)&&present);
            wf::Handle handle(::CreateFileW(file.c_str(),READ_CONTROL|WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            CHECK(handle.get()!=INVALID_HANDLE_VALUE);
            CHECK(::SetSecurityInfo(handle.get(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr,nullptr,null_dacl?nullptr:dacl,nullptr)==ERROR_SUCCESS);
        };
        set_acl(L"(A;;FR;;;WD)");
        refuses([&]{(void)wf::read_protected(utf8(file),true,512);});
        CHECK(wf::read_protected(utf8(file),false,512).size()==7);
        set_acl(L"(A;;FW;;;WD)");
        refuses([&]{(void)wf::read_protected(utf8(file),false,512);});
        set_acl(L"",true);
        refuses([&]{(void)wf::read_protected(utf8(file),true,512);});
        set_acl(L"");
        CHECK(wf::read_protected(utf8(file),true,512).size()==7);
        {
            wf::ParentChain held(file,owner);
            wf::flush_directory(root,owner);
            CHECK(wf::read_protected(utf8(file),true,512).size()==7);
        }
        {
            // Another module/process can still be validating a strict reader
            // before it permits private-directory write sharing.
            HANDLE reader=::CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            CHECK(reader!=INVALID_HANDLE_VALUE);
            {
                wf::Handle conflict(::CreateFileW(root.c_str(),GENERIC_WRITE|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                    nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
                CHECK(conflict.get()==INVALID_HANDLE_VALUE&&::GetLastError()==ERROR_SHARING_VIOLATION);
            }
            std::jthread release([reader]{::Sleep(100);(void)::CloseHandle(reader);});
            try{wf::flush_directory(root,owner);}catch(const std::runtime_error &){
                throw std::runtime_error("Transient strict reader prevented directory flush");
            }
        }
        {
            wf::Handle reader(::CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            CHECK(reader.get()!=INVALID_HANDLE_VALUE);
            refuses([&]{wf::flush_directory(root,owner);});
        }
        wf::flush_directory(root,owner);
        {
            const auto shared=root/L"metadata-parent",child=shared/L"private-child";
            CHECK(::CreateDirectoryW(shared.c_str(),&attributes)&&::CreateDirectoryW(child.c_str(),&attributes));
            auto policy=[&](const std::wstring &extra){
                Security descriptor(private_acl+extra);BOOL present{},defaulted{};PACL dacl{};
                CHECK(::GetSecurityDescriptorDacl(descriptor.value,&present,&dacl,&defaulted)&&present&&dacl);
                wf::Handle directory(::CreateFileW(shared.c_str(),READ_CONTROL|WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE,
                    nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
                CHECK(directory.get()!=INVALID_HANDLE_VALUE&&::SetSecurityInfo(directory.get(),SE_FILE_OBJECT,
                    DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,dacl,nullptr)==ERROR_SUCCESS);
            };
            policy(L"(A;;0x110;;;WD)"); // WRITE_EA | WRITE_ATTRIBUTES, like stock ProgramData.
            refuses([&]{wf::ParentChain unpinned(shared/L"new-file",owner);});
            {
                wf::ParentChain pinned(child/L"new-file",owner);
                CHECK(!::MoveFileExW(child.c_str(),(shared/L"moved-child").c_str(),0));
                wf::Handle metadata(::CreateFileW(shared.c_str(),FILE_WRITE_ATTRIBUTES|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
                    nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
                CHECK(metadata.get()!=INVALID_HANDLE_VALUE);
                const auto substitute=L"\\??\\"+root.native(),display=root.native();
                struct Header {DWORD tag;WORD length,reserved,substitute_offset,substitute_length,display_offset,display_length;};
                std::vector<unsigned char> data(sizeof(Header)+(substitute.size()+display.size()+2)*sizeof(wchar_t));
                const Header header{IO_REPARSE_TAG_MOUNT_POINT,static_cast<WORD>(data.size()-8),0,0,
                    static_cast<WORD>(substitute.size()*sizeof(wchar_t)),static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t)),
                    static_cast<WORD>(display.size()*sizeof(wchar_t))};
                std::memcpy(data.data(),&header,sizeof header);std::memcpy(data.data()+sizeof header,substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
                std::memcpy(data.data()+sizeof header+(substitute.size()+1)*sizeof(wchar_t),display.c_str(),(display.size()+1)*sizeof(wchar_t));
                DWORD count{};CHECK(!::DeviceIoControl(metadata.get(),FSCTL_SET_REPARSE_POINT,data.data(),static_cast<DWORD>(data.size()),nullptr,0,&count,nullptr));
                (void)wf::inspect(metadata.get(),true);
            }
            policy(L"(A;;0x150;;;WD)"); // Adding DELETE_CHILD must still refuse.
            refuses([&]{wf::ParentChain unsafe(child/L"new-file",owner);});
            policy(L"");
        }
        {
            Security inherited(L"O:"+owner+L"D:P(A;OICI;FA;;;"+owner+L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OIIO;FR;;;WD)");
            BOOL present{},defaulted{};PACL dacl{};
            CHECK(::GetSecurityDescriptorDacl(inherited.value,&present,&dacl,&defaulted)&&present&&dacl);
            wf::Handle directory(::CreateFileW(root.c_str(),READ_CONTROL|WRITE_DAC|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            CHECK(directory.get()!=INVALID_HANDLE_VALUE);
            CHECK(::SetSecurityInfo(directory.get(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr,nullptr,dacl,nullptr)==ERROR_SUCCESS);
            refuses([&]{wf::verify_security(directory.get(),owner,true,true);});
            CHECK(::GetSecurityDescriptorDacl(security.value,&present,&dacl,&defaulted)&&present&&dacl);
            CHECK(::SetSecurityInfo(directory.get(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr,nullptr,dacl,nullptr)==ERROR_SUCCESS);
            wf::verify_security(directory.get(),owner,true,true);
        }
        for(const auto &bad:{"relative.key","C:relative.key","C:\\a\\..\\secret.key",
            "C:\\a\\secret.key:stream","\\\\server\\share\\secret.key","C:\\a\\secret.key ","C:\\a\\secret.key.",
            "C:\\a\\NUL","C:\\a\\con.txt","C:\\a\\CON .txt","C:\\a\\AUX\\db","C:\\a\\COM1.key","C:\\a\\LPT9",
            "C:\\a\\CONIN$","C:\\a\\CONOUT$","C:\\a\\bad?name","C:\\a\\bad*name","C:\\a\\bad|name","C:\\a\\bad\"name"})
            refuses([&]{(void)wf::path_from_utf8(bad);});
        std::cout<<"PASS native protected reads, bounded content, ACL refusal, hardlinks, active writers, parent rename exclusion, and path grammar\n";
    } catch(const std::exception &error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
