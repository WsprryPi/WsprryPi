// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <iterator>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <vector>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/acl.h>
#include <membership.h>
#include <cstring>
#endif
#endif

// One owner per installation, independent of replaceable application state.
// Supply a fixed private directory under the installation owner's trusted home.
// The directory and zero-byte lock file persist; never unlink a live lock file.
class InstallationLock {
public:
    explicit InstallationLock(std::filesystem::path directory):directory_(std::move(directory)) {
        require(directory_.is_absolute()&&directory_==directory_.lexically_normal()&&
            directory_.has_filename()&&directory_.native().find(typename std::filesystem::path::value_type{})==std::filesystem::path::string_type::npos);
    }
#ifndef _WIN32
    // A privileged maintenance owner can join an existing backend-owned lock.
    // It cannot provision or repair that owner's directory or file.
    InstallationLock(std::filesystem::path directory,uid_t owner):InstallationLock(std::move(directory)) {
        require(::geteuid()==owner||::geteuid()==0);owner_uid_=owner;
    }
#endif
    InstallationLock(const InstallationLock &)=delete;
    InstallationLock &operator=(const InstallationLock &)=delete;
    InstallationLock(InstallationLock &&)=delete;
    InstallationLock &operator=(InstallationLock &&)=delete;
    ~InstallationLock(){release();}
    bool operator()() {
        if(held_)return true;
        try {return acquire();}catch(...){release();throw;}
    }
private:
    static void require(bool valid){if(!valid)throw std::runtime_error("Unsafe or unavailable installation lock");}
    std::filesystem::path directory_;
    bool held_{};
#ifdef _WIN32
    HANDLE directory_handle_{INVALID_HANDLE_VALUE},file_{INVALID_HANDLE_VALUE};
    std::vector<HANDLE> ancestors_;
    struct Security {
        PSECURITY_DESCRIPTOR descriptor{};
        ~Security(){if(descriptor)::LocalFree(descriptor);}
    };
    static std::wstring owner_sid() {
        HANDLE token{};require(::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&token)!=0);
        DWORD size{};::GetTokenInformation(token,TokenUser,nullptr,0,&size);
        std::vector<unsigned char> bytes(size);
        const auto ok=::GetTokenInformation(token,TokenUser,bytes.data(),size,&size);::CloseHandle(token);require(ok!=0);
        LPWSTR text{};require(::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&text)!=0);
        std::wstring result(text);::LocalFree(text);return result;
    }
    static void private_security(HANDLE handle) {
        Security security;PSID owner{};PACL dacl{};
        require(::GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
            &owner,nullptr,&dacl,nullptr,&security.descriptor)==ERROR_SUCCESS&&owner&&dacl);
        LPWSTR owner_text{};require(::ConvertSidToStringSidW(owner,&owner_text)!=0);
        const bool own=owner_sid()==owner_text;::LocalFree(owner_text);require(own);
        for(DWORD index=0;index<dacl->AceCount;++index) {
            void *raw{};require(::GetAce(dacl,index,&raw)!=0);
            const auto header=static_cast<ACE_HEADER*>(raw);
            if(header->AceFlags&INHERIT_ONLY_ACE)continue;
            if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
            require(header->AceType==ACCESS_ALLOWED_ACE_TYPE);
            auto sid=reinterpret_cast<PSID>(&static_cast<ACCESS_ALLOWED_ACE*>(raw)->SidStart);
            require(::EqualSid(owner,sid)||::IsWellKnownSid(sid,WinLocalSystemSid)||::IsWellKnownSid(sid,WinBuiltinAdministratorsSid));
        }
    }
    bool acquire() {
        // Local drive paths only. Refuse junctions, symlinks, and other reparse
        // points throughout the chain before creating the private directory.
        require(directory_.root_name().native().size()==2&&::GetDriveTypeW(directory_.root_path().c_str())==DRIVE_FIXED);
        auto prefix=directory_.root_path();
        for(const auto &part:directory_.relative_path()) {
            prefix/=part;if(prefix==directory_)break;
            const auto handle=::CreateFileW(prefix.c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            require(handle!=INVALID_HANDLE_VALUE);
            try{ancestors_.push_back(handle);}catch(...){::CloseHandle(handle);throw;}
            BY_HANDLE_FILE_INFORMATION info{};require(::GetFileInformationByHandle(handle,&info)!=0&&
                (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));
        }
        // The direct parent is an existing, private installation-owner root.
        require(!ancestors_.empty());private_security(ancestors_.back());
        Security security;
        const auto sid=owner_sid();
        const auto sddl=L"O:"+sid+L"D:P(A;OICI;FA;;;"+sid+L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
        require(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&security.descriptor,nullptr)!=0);
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),security.descriptor,FALSE};
        require(::CreateDirectoryW(directory_.c_str(),&attributes)||::GetLastError()==ERROR_ALREADY_EXISTS);
        directory_handle_=::CreateFileW(directory_.c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        require(directory_handle_!=INVALID_HANDLE_VALUE);
        BY_HANDLE_FILE_INFORMATION info{};require(::GetFileInformationByHandle(directory_handle_,&info)!=0&&
            (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));
        private_security(directory_handle_);
        file_=::CreateFileW((directory_/L"instance.lock").c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
            FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        require(file_!=INVALID_HANDLE_VALUE);
        require(::GetFileInformationByHandle(file_,&info)!=0&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
            info.nNumberOfLinks==1&&info.nFileSizeHigh==0&&info.nFileSizeLow==0);
        private_security(file_);
        OVERLAPPED overlap{};
        if(!::LockFileEx(file_,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)) {
            const auto error=::GetLastError();release();require(error==ERROR_LOCK_VIOLATION);return false;
        }
        held_=true;return true;
    }
    void release() noexcept {
        if(file_!=INVALID_HANDLE_VALUE){::CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}
        if(directory_handle_!=INVALID_HANDLE_VALUE){::CloseHandle(directory_handle_);directory_handle_=INVALID_HANDLE_VALUE;}
        for(const auto handle:ancestors_)::CloseHandle(handle);
        ancestors_.clear();held_=false;
    }
#else
    int directory_fd_{-1},file_{-1};
    uid_t owner_uid_{::geteuid()};
    static void private_acl(int descriptor,uid_t expected_owner) {
#ifdef __APPLE__
        // Darwin ACLs can grant access beyond the POSIX mode bits.
        auto acl=::acl_get_fd_np(descriptor,ACL_TYPE_EXTENDED);
        if(!acl){require(errno==ENOENT||errno==ENOATTR);return;}
        struct Guard {acl_t value;~Guard(){::acl_free(value);}} guard{acl};
        require(::acl_valid(acl)==0);uuid_t owner{};require(::mbr_uid_to_uuid(expected_owner,owner)==0);
        acl_entry_t entry{};int result=::acl_get_entry(acl,ACL_FIRST_ENTRY,&entry);
        while(result==0){
            acl_tag_t tag{};require(::acl_get_tag_type(entry,&tag)==0);
            if(tag==ACL_EXTENDED_ALLOW){auto who=::acl_get_qualifier(entry);require(who);const bool own=std::memcmp(who,owner,sizeof owner)==0;::acl_free(who);require(own);}
            result=::acl_get_entry(acl,ACL_NEXT_ENTRY,&entry);
        }
        require(result==-1&&errno==EINVAL);
#else
        // A zero group mask in the required 0700/0600 permissions also masks
        // named-user and group grants in a Linux POSIX access ACL.
        (void)descriptor;(void)expected_owner;
#endif
    }
    bool acquire() {
        int parent=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        require(parent>=0);
        struct Guard {int value;~Guard(){if(value>=0)::close(value);}} guard{parent};
        auto parts=directory_.relative_path();
        for(auto iterator=parts.begin();iterator!=parts.end();++iterator) {
            const bool final=std::next(iterator)==parts.end();
            if(final&&::geteuid()==owner_uid_)require(::mkdirat(guard.value,iterator->c_str(),0700)==0||errno==EEXIST);
            const auto next=::openat(guard.value,iterator->c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            require(next>=0);::close(guard.value);guard.value=next;
            struct stat info{};require(::fstat(next,&info)==0&&S_ISDIR(info.st_mode));
            require((info.st_uid==owner_uid_||info.st_uid==0)&&
                (!(info.st_mode&0022)||(info.st_uid==0&&(info.st_mode&S_ISVTX)&&!final)));
            private_acl(next,owner_uid_);
            if(final){require(info.st_uid==owner_uid_&&(info.st_mode&07777)==0700);private_acl(next,owner_uid_);}
        }
        directory_fd_=guard.value;guard.value=-1;
        file_=::openat(directory_fd_,"instance.lock",O_RDWR|(::geteuid()==owner_uid_?O_CREAT:0)|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600);
        require(file_>=0);
        struct stat info{};require(::fstat(file_,&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==owner_uid_&&
            info.st_nlink==1&&(info.st_mode&07777)==0600&&info.st_size==0);private_acl(file_,owner_uid_);
        if(::flock(file_,LOCK_EX|LOCK_NB)!=0){const auto error=errno;release();require(error==EWOULDBLOCK||error==EAGAIN);return false;}
        struct stat path{};require(::fstatat(directory_fd_,"instance.lock",&path,AT_SYMLINK_NOFOLLOW)==0&&path.st_dev==info.st_dev&&path.st_ino==info.st_ino&&path.st_nlink==1);
        held_=true;return true;
    }
    void release() noexcept {
        if(file_>=0){::close(file_);file_=-1;}
        if(directory_fd_>=0){::close(directory_fd_);directory_fd_=-1;}
        held_=false;
    }
#endif
};
