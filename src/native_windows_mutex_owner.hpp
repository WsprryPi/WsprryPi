// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <array>
#include <bcrypt.h>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <thread>
namespace wsprrypi {
// A dedicated thread owns each native mutex. This prevents recursive admission
// across independent C++/DLL/Python callers running on the same calling thread.
class WindowsMutexOwner {
public:
    enum class Family { Sqlite, Directory };
    class Busy:public std::runtime_error {public:Busy():std::runtime_error("Native state owner is busy"){};};
    WindowsMutexOwner(const windows_file::Snapshot &identity,const std::wstring &owner,Family family):owner_(owner){
        namespace wf=windows_file;
        try{
            wf::require(family==Family::Sqlite||family==Family::Directory);
            wf::PrivateSecurity security(owner_);const auto selected=name(identity,owner_,family);
            mutex_=wf::Handle(::CreateMutexExW(security.attributes(),selected.c_str(),0,SYNCHRONIZE|MUTEX_MODIFY_STATE|READ_CONTROL));
            wf::require(mutex_.get()!=nullptr&&mutex_.get()!=INVALID_HANDLE_VALUE);check();
            stop_=wf::Handle(::CreateEventW(security.attributes(),TRUE,FALSE,nullptr));wf::require(stop_.get()!=nullptr&&stop_.get()!=INVALID_HANDLE_VALUE);
            thread_=std::thread([this]()noexcept{
                const auto result=::WaitForSingleObject(mutex_.get(),0);
                {std::lock_guard lock(state_);result_=result;ready_=true;}ready_changed_.notify_one();
                if(result==WAIT_OBJECT_0||result==WAIT_ABANDONED){
                    if(::WaitForSingleObject(stop_.get(),INFINITE)!=WAIT_OBJECT_0||!::ReleaseMutex(mutex_.get()))fatal();
                }
            });
            {std::unique_lock lock(state_);ready_changed_.wait(lock,[this]{return ready_;});}
            if(result_==WAIT_TIMEOUT)throw Busy();
            wf::require(result_==WAIT_OBJECT_0||result_==WAIT_ABANDONED);
        }catch(...){stop();throw;}
    }
    ~WindowsMutexOwner(){stop();}
    WindowsMutexOwner(const WindowsMutexOwner &)=delete;
    WindowsMutexOwner &operator=(const WindowsMutexOwner &)=delete;
    void check()const{
        namespace wf=windows_file;PSECURITY_DESCRIPTOR descriptor{};PSID actual{};PACL acl{};
        const auto result=::GetSecurityInfo(mutex_.get(),SE_KERNEL_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&actual,nullptr,&acl,nullptr,&descriptor);
        struct Free{PSECURITY_DESCRIPTOR value;~Free(){if(value)::LocalFree(value);}} free{descriptor};
        wf::require(result==ERROR_SUCCESS&&descriptor&&::IsValidSecurityDescriptor(descriptor)&&actual&&::IsValidSid(actual)&&acl&&::IsValidAcl(acl));
        LPWSTR sid{};wf::require(::ConvertSidToStringSidW(actual,&sid));struct Text{LPWSTR value;~Text(){::LocalFree(value);}} text{sid};wf::require(owner_==sid);
        for(DWORD index=0;index<acl->AceCount;++index){void *raw{};wf::require(::GetAce(acl,index,&raw));const auto *header=static_cast<ACE_HEADER *>(raw);
            if(header->AceFlags&INHERIT_ONLY_ACE)continue;
            if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
            wf::require(header->AceType==ACCESS_ALLOWED_ACE_TYPE);const auto *ace=static_cast<ACCESS_ALLOWED_ACE *>(raw);
            wf::require(wf::trusted_sid(reinterpret_cast<PSID>(const_cast<DWORD *>(&ace->SidStart)),owner_)||ace->Mask==0);
        }
    }
private:
    std::wstring owner_;windows_file::Handle mutex_,stop_;std::thread thread_;
    std::mutex state_;std::condition_variable ready_changed_;DWORD result_{WAIT_FAILED};bool ready_{};
    [[noreturn]]static void fatal()noexcept{::TerminateProcess(::GetCurrentProcess(),ERROR_OPERATION_ABORTED);std::abort();}
    void stop()noexcept{
        if(!thread_.joinable())return;
        if(!::SetEvent(stop_.get()))fatal();
        try{thread_.join();}catch(...){fatal();}
    }
    static std::wstring name(const windows_file::Snapshot &info,const std::wstring &owner,Family family){
        namespace wf=windows_file;std::string key;
        for(const auto character:owner){wf::require(character>0&&character<128);key.push_back(static_cast<char>(character));}
        key+=':';key+=std::to_string(info.file_id.VolumeSerialNumber);key+=':';
        for(const auto byte:info.file_id.FileId.Identifier){key+="0123456789abcdef"[byte>>4];key+="0123456789abcdef"[byte&15];}
        BCRYPT_ALG_HANDLE algorithm{};wf::require(::BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0);
        struct Close{BCRYPT_ALG_HANDLE value;~Close(){::BCryptCloseAlgorithmProvider(value,0);}} close{algorithm};std::array<unsigned char,32> hash{};
        wf::require(::BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(key.data()),static_cast<ULONG>(key.size()),hash.data(),static_cast<ULONG>(hash.size()))==0);
        std::wstring name=family==Family::Sqlite?L"Global\\WsprryPi-SQLite-Owner-":L"Global\\WsprryPi-Recovery-Directory-";for(const auto byte:hash){name+=L"0123456789abcdef"[byte>>4];name+=L"0123456789abcdef"[byte&15];}return name;
    }
};
}
#endif
