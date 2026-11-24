// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wsprrypi::windows_file {
// Serialize metadata-handle admission with owned directory flushes. This does
// not replace installation, database, or logical owner/writer locks.
inline std::recursive_mutex namespace_mutex;
inline void require(bool valid) {
    if(!valid)throw std::runtime_error("Native protected file is unsafe or unavailable");
}
class Handle {
public:
    explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):value_(value){}
    ~Handle(){if(value_!=INVALID_HANDLE_VALUE)::CloseHandle(value_);}
    Handle(const Handle &)=delete;
    Handle &operator=(const Handle &)=delete;
    Handle(Handle &&other) noexcept:value_(std::exchange(other.value_,INVALID_HANDLE_VALUE)){}
    Handle &operator=(Handle &&other) noexcept {
        if(this!=&other){if(value_!=INVALID_HANDLE_VALUE)::CloseHandle(value_);value_=std::exchange(other.value_,INVALID_HANDLE_VALUE);}
        return *this;
    }
    HANDLE get() const noexcept{return value_;}
private:
    HANDLE value_;
};
inline std::wstring token_sid(TOKEN_INFORMATION_CLASS field) {
    require(field==TokenUser||field==TokenOwner);
    HANDLE raw{};require(::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&raw)!=0);
    Handle token(raw);DWORD size{};
    ::GetTokenInformation(token.get(),field,nullptr,0,&size);
    require(size>0&&size<=65536);
    std::vector<unsigned char> bytes(size);
    require(::GetTokenInformation(token.get(),field,bytes.data(),size,&size)!=0);
    LPWSTR text{};
    const auto sid=field==TokenUser?reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid:
        reinterpret_cast<TOKEN_OWNER *>(bytes.data())->Owner;
    require(::ConvertSidToStringSidW(sid,&text)!=0);
    struct Release {LPWSTR value;~Release(){::LocalFree(value);}} release{text};
    return text;
}
inline std::wstring current_sid(){return token_sid(TokenUser);}
inline bool trusted_installer(PSID sid) {
    // Windows owns some system-prefix directories through this fixed service.
    // It is trusted for ancestry only, never as a private authority-file owner.
    static const auto principal=[] {
        DWORD sid_size{},domain_size{};SID_NAME_USE use{};
        ::LookupAccountNameW(nullptr,L"NT SERVICE\\TrustedInstaller",nullptr,&sid_size,nullptr,&domain_size,&use);
        require(sid_size>0&&sid_size<=SECURITY_MAX_SID_SIZE&&domain_size<=32768);
        std::vector<unsigned char> value(sid_size);std::vector<wchar_t> domain(domain_size);
        require(::LookupAccountNameW(nullptr,L"NT SERVICE\\TrustedInstaller",value.data(),&sid_size,
            domain.data(),&domain_size,&use)!=0&&::IsValidSid(value.data()));
        return value;
    }();
    return ::EqualSid(sid,const_cast<unsigned char *>(principal.data()))!=0;
}
inline bool trusted_sid(PSID sid,const std::wstring &owner,bool system_directory=false) {
    require(sid&&::IsValidSid(sid));
    if(::IsWellKnownSid(sid,WinLocalSystemSid)||::IsWellKnownSid(sid,WinBuiltinAdministratorsSid))return true;
    LPWSTR text{};require(::ConvertSidToStringSidW(sid,&text)!=0);
    struct Release {LPWSTR value;~Release(){::LocalFree(value);}} release{text};
    return owner==text||(system_directory&&trusted_installer(sid));
}
inline std::wstring file_owner_sid(HANDLE handle) {
    PSID owner{};PSECURITY_DESCRIPTOR descriptor{};
    const auto result=::GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,
        &owner,nullptr,nullptr,nullptr,&descriptor);
    struct Release {PSECURITY_DESCRIPTOR value;~Release(){if(value)::LocalFree(value);}} release{descriptor};
    require(result==ERROR_SUCCESS&&owner&&::IsValidSid(owner));
    LPWSTR text{};require(::ConvertSidToStringSidW(owner,&text));
    struct FreeText {LPWSTR value;~FreeText(){::LocalFree(value);}} free_text{text};
    return text;
}
inline void verify_exact_owner(HANDLE handle,const std::wstring &expected){require(file_owner_sid(handle)==expected);}
class PrivateSecurity {
public:
    explicit PrivateSecurity(const std::wstring &owner) {
        // Provisioning belongs to the current owner, never maintenance acting
        // as an implicit credential-repair or ownership-conversion service.
        require(owner==current_sid());
        const auto sddl=L"O:"+owner+L"D:P(A;OICI;FA;;;"+owner+L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
        require(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&value_,nullptr));
        attributes_={sizeof(attributes_),value_,FALSE};
    }
    ~PrivateSecurity(){if(value_)::LocalFree(value_);}
    PrivateSecurity(const PrivateSecurity &)=delete;
    PrivateSecurity &operator=(const PrivateSecurity &)=delete;
    SECURITY_ATTRIBUTES *attributes() noexcept{return &attributes_;}
private:
    PSECURITY_DESCRIPTOR value_{};
    SECURITY_ATTRIBUTES attributes_{};
};
// Inspect the opened object, not a second name lookup. Directory create-child
// grants cannot replace an already-open child while its handle denies deletion.
inline void verify_security(HANDLE handle,const std::wstring &owner,bool private_data,bool directory,bool pinned_parent=false) {
    PSECURITY_DESCRIPTOR descriptor{};PSID actual_owner{};PACL dacl{};
    const auto result=::GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &actual_owner,nullptr,&dacl,nullptr,&descriptor);
    struct Release {PSECURITY_DESCRIPTOR value;~Release(){if(value)::LocalFree(value);}} release{descriptor};
    require(result==ERROR_SUCCESS&&descriptor&&::IsValidSecurityDescriptor(descriptor)&&dacl&&::IsValidAcl(dacl));
    const bool system_prefix=directory&&!private_data;
    require(trusted_sid(actual_owner,owner,system_prefix));
    constexpr DWORD file_mutation=FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|
        DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
    const DWORD directory_mutation=FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL|
        (pinned_parent?0:FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES);
    for(DWORD index=0;index<dacl->AceCount;++index) {
        void *raw{};require(::GetAce(dacl,index,&raw)!=0);
        const auto *header=static_cast<ACE_HEADER *>(raw);
        // SQLite creates its own WAL/SHM files. Private directory admission
        // must include child-only grants, before a native writer inherits them.
        if((header->AceFlags&INHERIT_ONLY_ACE)&&!(private_data&&directory))continue;
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        require(header->AceType==ACCESS_ALLOWED_ACE_TYPE);
        const auto *ace=static_cast<ACCESS_ALLOWED_ACE *>(raw);
        auto sid=reinterpret_cast<PSID>(const_cast<DWORD *>(&ace->SidStart));
        if(trusted_sid(sid,owner,system_prefix))continue;
        require(private_data?ace->Mask==0:(ace->Mask&(directory?directory_mutation:file_mutation))==0);
    }
}
inline std::filesystem::path path_from_utf8(const std::string &text) {
    require(!text.empty()&&text.size()<=32760&&text.find('\0')==std::string::npos);
    const int length=::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    require(length>0);std::wstring wide(static_cast<std::size_t>(length),L'\0');
    require(::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),wide.data(),length)==length);
    std::filesystem::path path(wide);
    require(path.is_absolute()&&path==path.lexically_normal()&&path.root_name().native().size()==2&&
        path.root_name().native()[1]==L':'&&path.has_filename()&&
        ((wide[0]>=L'A'&&wide[0]<=L'Z')||(wide[0]>=L'a'&&wide[0]<=L'z')));
    for(const auto &component:path.relative_path()) {
        const auto value=component.native();
        require(!value.empty()&&value.back()!=L'.'&&value.back()!=L' '&&value.find(L':')==std::wstring::npos&&
            std::none_of(value.begin(),value.end(),[](wchar_t c){return c<32||c==127||c==L'/'||c==L'\\'||c==L'<'||c==L'>'||c==L'"'||c==L'|'||c==L'?'||c==L'*';}));
        auto stem=value.substr(0,value.find(L'.'));
        while(!stem.empty()&&stem.back()==L' ')stem.pop_back();
        for(auto &c:stem)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
        require(stem!=L"CON"&&stem!=L"PRN"&&stem!=L"AUX"&&stem!=L"NUL"&&stem!=L"CLOCK$"&&stem!=L"CONIN$"&&stem!=L"CONOUT$");
        if(stem.size()==4&&(stem.starts_with(L"COM")||stem.starts_with(L"LPT")))
            require(!((stem[3]>=L'0'&&stem[3]<=L'9')||stem[3]==L'\u00b9'||stem[3]==L'\u00b2'||stem[3]==L'\u00b3'));
    }
    return path.make_preferred();
}
inline std::wstring api_path(std::filesystem::path path){return L"\\\\?\\"+path.make_preferred().native();}
// Resolve existing short-name/case aliases through the opened object. The
// normalized local-drive result still passes the strict native path grammar.
inline std::filesystem::path normalized_path(HANDLE handle){
    const auto length=::GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    require(length>0&&length<=32760);std::vector<wchar_t> value(static_cast<std::size_t>(length)+1);
    const auto written=::GetFinalPathNameByHandleW(handle,value.data(),static_cast<DWORD>(value.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    require(written>0&&written<value.size());std::wstring path(value.data(),written);
    require(path.starts_with(L"\\\\?\\"));path.erase(0,4);
    const auto bytes=std::filesystem::path(path).u8string();return path_from_utf8(std::string(bytes.begin(),bytes.end()));
}
struct Snapshot {
    BY_HANDLE_FILE_INFORMATION identity{};
    FILE_ID_INFO file_id{};
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
};
inline Snapshot inspect(HANDLE handle,bool directory) {
    Snapshot result;
    require(::GetFileType(handle)==FILE_TYPE_DISK&&
        ::GetFileInformationByHandle(handle,&result.identity)&&
        ::GetFileInformationByHandleEx(handle,FileIdInfo,&result.file_id,sizeof(result.file_id))&&
        ::GetFileInformationByHandleEx(handle,FileBasicInfo,&result.basic,sizeof(result.basic))&&
        ::GetFileInformationByHandleEx(handle,FileStandardInfo,&result.standard,sizeof(result.standard)));
    require(!(result.basic.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
        static_cast<bool>(result.standard.Directory)==directory&&!result.standard.DeletePending&&
        (directory||result.standard.NumberOfLinks==1));
    return result;
}
inline bool same_snapshot(const Snapshot &a,const Snapshot &b) {
    return a.file_id.VolumeSerialNumber==b.file_id.VolumeSerialNumber&&
        std::memcmp(a.file_id.FileId.Identifier,b.file_id.FileId.Identifier,sizeof a.file_id.FileId.Identifier)==0&&
        a.basic.FileAttributes==b.basic.FileAttributes&&a.basic.LastWriteTime.QuadPart==b.basic.LastWriteTime.QuadPart&&
        a.basic.ChangeTime.QuadPart==b.basic.ChangeTime.QuadPart&&a.standard.EndOfFile.QuadPart==b.standard.EndOfFile.QuadPart&&
        a.standard.NumberOfLinks==b.standard.NumberOfLinks&&a.standard.DeletePending==b.standard.DeletePending;
}
class ParentChain {
public:
    ParentChain(const std::filesystem::path &file,const std::wstring &owner) {
        require(::GetDriveTypeW(file.root_path().c_str())==DRIVE_FIXED);
        DWORD flags{};wchar_t filesystem[32]{};
        require(::GetVolumeInformationW(file.root_path().c_str(),nullptr,0,nullptr,nullptr,&flags,filesystem,static_cast<DWORD>(std::size(filesystem)))&&
            (flags&FILE_PERSISTENT_ACLS)&&std::wstring(filesystem)==L"NTFS");
        auto current=file.root_path();
        keep(current,owner);
        for(const auto &part:file.relative_path().parent_path()) {current/=part;keep(current,owner);}
        for(std::size_t index=0;index<metadata_parents_.size();++index)if(metadata_parents_[index]){
            // Stock ProgramData allows ordinary users to change directory
            // metadata. Such an ancestor is admitted only with an existing,
            // trusted immediate child pinned against deletion. It cannot be
            // emptied or turned into a reparse point (Windows rejects setting
            // a reparse point on a nonempty directory). The final parent and
            // every private directory retain the strict metadata-write policy.
            require(index+1<handles_.size());(void)inspect(handles_[index].get(),true);
        }
    }
private:
    std::vector<Handle> handles_;
    std::vector<bool> metadata_parents_;
    void keep(const std::filesystem::path &path,const std::wstring &owner) {
        std::lock_guard lock(namespace_mutex);
        // Metadata-only opens do not participate in Windows sharing checks.
        // Directory-data access makes the omitted delete share pin this name.
        Handle handle;
        const auto started=::GetTickCount64();
        for(;;){
            handle=Handle(::CreateFileW(api_path(path).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            if(handle.get()!=INVALID_HANDLE_VALUE)break;
            const auto error=::GetLastError();
            if(error!=ERROR_SHARING_VIOLATION||::GetTickCount64()-started>=500){::SetLastError(error);break;}
            // A flush or namespace update in another module/process can retain
            // an incompatible handle briefly. Wait without relaxing sharing
            // or bypassing any identity, owner, ACL, or reparse check.
            ::Sleep(5);
        }
        require(handle.get()!=INVALID_HANDLE_VALUE);
        const auto original=inspect(handle.get(),true);verify_security(handle.get(),owner,false,true,true);
        bool metadata_parent=false;
        try{verify_security(handle.get(),owner,false,true);}catch(const std::runtime_error &){metadata_parent=true;}
        bool private_directory=false;
        try{verify_security(handle.get(),owner,true,true);private_directory=true;}catch(const std::runtime_error &){}
        if(private_directory){
            // Initial read-only sharing excludes any preexisting writer. Only
            // a fully private DACL can then permit owned metadata/flush handles;
            // public ancestry retains read-only sharing. Deletion stays denied.
            Handle shared(::CreateFileW(api_path(path).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            require(shared.get()!=INVALID_HANDLE_VALUE);const auto observed=inspect(shared.get(),true);
            require(original.file_id.VolumeSerialNumber==observed.file_id.VolumeSerialNumber&&
                std::memcmp(original.file_id.FileId.Identifier,observed.file_id.FileId.Identifier,sizeof original.file_id.FileId.Identifier)==0);
            verify_security(shared.get(),owner,true,true);handle=std::move(shared);
        }
        handles_.push_back(std::move(handle));
        metadata_parents_.push_back(metadata_parent);
    }
};
// The selected Windows profile is fixed-volume NTFS. Unsupported access or
// directory flushing fails closed; no volume privilege or no-op fallback.
inline void flush_directory(const std::filesystem::path &path,const std::wstring &owner){
    std::lock_guard lock(namespace_mutex);ParentChain ancestors(path,owner);
    Handle handle;const auto started=::GetTickCount64();
    for(;;){
        handle=Handle(::CreateFileW(api_path(path).c_str(),GENERIC_WRITE|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(handle.get()!=INVALID_HANDLE_VALUE)break;
        const auto error=::GetLastError();
        if(error!=ERROR_SHARING_VIOLATION||::GetTickCount64()-started>=500){::SetLastError(error);break;}
        // A strict reader can still be verifying the private directory before
        // allowing write sharing. Wait before admitting a flush; never retry
        // the flush itself or relax its owner, ACL, or namespace checks.
        ::Sleep(5);
    }
    require(handle.get()!=INVALID_HANDLE_VALUE);(void)inspect(handle.get(),true);
    verify_exact_owner(handle.get(),owner);verify_security(handle.get(),owner,true,true);
    require(::FlushFileBuffers(handle.get())!=0);
}
inline std::vector<unsigned char> read_protected(const std::string &requested,bool private_data,
    std::size_t maximum,const std::wstring &expected_owner={}) {
    require(maximum>0&&maximum<=static_cast<std::size_t>(std::numeric_limits<DWORD>::max()));
    const auto path=path_from_utf8(requested);
    const auto owner=expected_owner.empty()?current_sid():expected_owner;
    ParentChain parents(path,owner);
    Handle input(::CreateFileW(api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    require(input.get()!=INVALID_HANDLE_VALUE);
    const auto before=inspect(input.get(),false);verify_security(input.get(),owner,private_data,false);
    require(before.standard.EndOfFile.QuadPart>0&&static_cast<std::uint64_t>(before.standard.EndOfFile.QuadPart)<=maximum);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(before.standard.EndOfFile.QuadPart));
    DWORD count{};
    require(::ReadFile(input.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size());
    require(same_snapshot(before,inspect(input.get(),false)));
    verify_security(input.get(),owner,private_data,false);
    return bytes;
}
} // namespace wsprrypi::windows_file
#endif
