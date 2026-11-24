// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_owners.hpp"
#include <shellapi.h>
#include <winsvc.h>
#include <array>
#include <chrono>
#include <span>
#include <string_view>
#include <thread>
namespace wsprrypi::recovery_windows {
namespace service_detail {
inline constexpr wchar_t name[]=L"WsprryPiController";
inline constexpr wchar_t account[]=L"NT SERVICE\\WsprryPiController";
inline constexpr wchar_t dispatch[]=L"--native-controller-service";
inline constexpr wchar_t helper[]=L"--recovery-owner-helper";
inline bool equal(std::wstring_view a,std::wstring_view b){
    windows_file::require(a.size()<=32768&&b.size()<=32768);
    return ::CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
inline bool nonce(std::wstring_view value){return value.size()==32&&value.find_first_not_of(L"0123456789abcdef")==std::wstring_view::npos;}
inline void unprivileged(){
    std::array<unsigned char,SECURITY_MAX_SID_SIZE> bytes{};DWORD length=static_cast<DWORD>(bytes.size());BOOL administrator{};
    windows_file::require(::CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,bytes.data(),&length)&&
        ::CheckTokenMembership(nullptr,bytes.data(),&administrator)&&!administrator);
}
enum class Mode {Controller,RecoveryOwner};
struct Invocation {Mode mode;std::wstring nonce;};
// SCM supplies argv[0] itself. No path, command, account, or service name can
// be selected through the one fixed owner-helper start operation.
inline Invocation invocation(std::span<const std::wstring> arguments){
    windows_file::require(!arguments.empty()&&equal(arguments[0],name));
    if(arguments.size()==1)return {Mode::Controller,{}};
    windows_file::require(arguments.size()==3&&arguments[1]==helper&&nonce(arguments[2]));
    return {Mode::RecoveryOwner,arguments[2]};
}
inline void definition(DWORD type,std::wstring_view configured_account,const std::wstring &command,const std::filesystem::path &program){
    windows_file::require(type==SERVICE_WIN32_OWN_PROCESS&&equal(configured_account,account)&&!command.empty()&&command.size()<=32768);
    int count{};LPWSTR *raw=::CommandLineToArgvW(command.c_str(),&count);windows_file::require(raw!=nullptr);
    struct Free {LPWSTR *value;~Free(){::LocalFree(value);}} free{raw};
    windows_file::require(count==3&&equal(raw[0],program.native())&&std::wstring_view(raw[1])==dispatch&&std::wstring_view(raw[2])==L"--controller-only");
}
class Handle {
public:
    explicit Handle(SC_HANDLE value=nullptr):value_(value){}
    ~Handle(){if(value_)::CloseServiceHandle(value_);}
    Handle(const Handle &)=delete;Handle &operator=(const Handle &)=delete;
    Handle(Handle &&other)noexcept:value_(std::exchange(other.value_,nullptr)){}
    Handle &operator=(Handle &&other)noexcept{if(this!=&other){if(value_)::CloseServiceHandle(value_);value_=std::exchange(other.value_,nullptr);}return *this;}
    SC_HANDLE get()const noexcept{return value_;}
private:SC_HANDLE value_;
};
}
// Only maintenance may control the fixed backend service. No credentials are
// acquired or converted, and no caller-supplied command reaches SCM. The
// authenticated owner pipe separately binds its peer SID, service PID, and
// current controller/helper mode before a recovery operation is admitted.
class ControllerService {
public:
    struct Status {DWORD state;DWORD pid;};
    enum class Start {Controller,RecoveryOwner};
    ControllerService():owners_(native_windows_controller_owners()),program_(windows_program_root()/L"bin"/L"wsprrypi.exe"){
        windows_file::require(windows_file::current_sid()==owners_.maintenance.sid);
        service_detail::unprivileged();
        manager_=service_detail::Handle(::OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));windows_file::require(manager_.get()!=nullptr);
    }
    Status query(){auto service=open(SERVICE_QUERY_STATUS);const auto value=status(service.get());return {value.dwCurrentState,value.dwCurrentState==SERVICE_RUNNING?value.dwProcessId:0};}
    Status start(Start mode,const std::wstring &nonce={}){
        windows_file::require((mode==Start::Controller&&nonce.empty())||(mode==Start::RecoveryOwner&&service_detail::nonce(nonce)));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);auto service=open(SERVICE_QUERY_STATUS|SERVICE_START);
        windows_file::require(status(service.get()).dwCurrentState==SERVICE_STOPPED);
        const wchar_t *arguments[]{service_detail::helper,nonce.c_str()};
        windows_file::require(::StartServiceW(service.get(),mode==Start::RecoveryOwner?2:0,mode==Start::RecoveryOwner?arguments:nullptr));
        const auto value=wait(service.get(),SERVICE_RUNNING,deadline);return {value.dwCurrentState,value.dwProcessId};
    }
    void stop(DWORD observed_pid){
        windows_file::require(observed_pid>0&&observed_pid!=::GetCurrentProcessId());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);auto service=open(SERVICE_QUERY_STATUS|SERVICE_STOP);
        const auto value=status(service.get());if(value.dwCurrentState==SERVICE_STOPPED)return;
        windows_file::require(value.dwCurrentState==SERVICE_RUNNING&&value.dwProcessId==observed_pid&&(value.dwControlsAccepted&SERVICE_ACCEPT_STOP));
        SERVICE_STATUS pending{};windows_file::require(::ControlService(service.get(),SERVICE_CONTROL_STOP,&pending));
        (void)wait(service.get(),SERVICE_STOPPED,deadline);
    }
private:
    NativeWindowsOwners owners_;std::filesystem::path program_;service_detail::Handle manager_;
    // Pin the exact configured program while validating/controlling its
    // service. Readable binaries do not require private-state ACLs, but remain
    // protected from untrusted writers and namespace replacement.
    struct Service {
        windows_file::ParentChain parents;
        windows_file::Handle image;
        service_detail::Handle service;
        Service(const std::filesystem::path &program,SC_HANDLE manager,DWORD access)
            :parents(program,L"S-1-5-18"),image(::CreateFileW(windows_file::api_path(program).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)),
             service(::OpenServiceW(manager,service_detail::name,access|SERVICE_QUERY_CONFIG)){
            windows_file::require(image.get()!=INVALID_HANDLE_VALUE&&service.get()!=nullptr);const auto info=windows_file::inspect(image.get(),false);
            windows_file::require(info.standard.EndOfFile.QuadPart>0&&info.standard.EndOfFile.QuadPart<=64LL*1024*1024);windows_file::verify_security(image.get(),L"S-1-5-18",false,false);
            DWORD size{};windows_file::require(!::QueryServiceConfigW(service.get(),nullptr,0,&size)&&::GetLastError()==ERROR_INSUFFICIENT_BUFFER&&size>=sizeof(QUERY_SERVICE_CONFIGW)&&size<=65536);
            std::vector<unsigned char> bytes(size);auto *config=reinterpret_cast<QUERY_SERVICE_CONFIGW *>(bytes.data());
            windows_file::require(::QueryServiceConfigW(service.get(),config,size,&size));
            const auto string=[&](const wchar_t *value){
                const auto address=reinterpret_cast<std::uintptr_t>(value),begin=reinterpret_cast<std::uintptr_t>(bytes.data());
                windows_file::require(address>=begin&&address<begin+bytes.size()&&address%alignof(wchar_t)==0);
                const auto maximum=(begin+bytes.size()-address)/sizeof(wchar_t);const auto end=std::find(value,value+maximum,L'\0');
                windows_file::require(end!=value+maximum);return std::wstring(value,end);
            };
            service_detail::definition(config->dwServiceType,string(config->lpServiceStartName),string(config->lpBinaryPathName),program);
        }
        SC_HANDLE get()const noexcept{return service.get();}
    };
    Service open(DWORD access){return Service(program_,manager_.get(),access);}
    static SERVICE_STATUS_PROCESS status(SC_HANDLE service){
        SERVICE_STATUS_PROCESS value{};DWORD size{};
        windows_file::require(::QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE *>(&value),sizeof value,&size)&&size==sizeof value&&value.dwServiceType==SERVICE_WIN32_OWN_PROCESS);
        windows_file::require(value.dwCurrentState==SERVICE_STOPPED||value.dwCurrentState==SERVICE_START_PENDING||value.dwCurrentState==SERVICE_RUNNING||value.dwCurrentState==SERVICE_STOP_PENDING);
        if(value.dwCurrentState==SERVICE_RUNNING)windows_file::require(value.dwProcessId>0);
        return value;
    }
    static SERVICE_STATUS_PROCESS wait(SC_HANDLE service,DWORD expected,std::chrono::steady_clock::time_point deadline){
        for(;;){const auto value=status(service);if(value.dwCurrentState==expected)return value;
            windows_file::require(std::chrono::steady_clock::now()<deadline&&value.dwCurrentState==static_cast<DWORD>(expected==SERVICE_RUNNING?SERVICE_START_PENDING:SERVICE_STOP_PENDING));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    }
};
}
#endif
