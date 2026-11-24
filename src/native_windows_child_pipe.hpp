// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "native_random.hpp"
#include "application_control/local_pipe_windows.hpp"
#include <array>
namespace wsprrypi::windows_process {
class ChildPipe {
public:
    enum class Direction {ToChild,FromChild};
    explicit ChildPipe(Direction direction):direction_(direction){
        std::array<std::uint8_t,16> random{};native_random(random);std::wstring name=L"\\\\.\\pipe\\wsprrypi-private-child-";
        for(auto byte:random){name+=L"0123456789abcdef"[byte>>4];name+=L"0123456789abcdef"[byte&15];}
        windows_file::PrivateSecurity security(windows_file::current_sid());
        parent_=windows_file::Handle(::CreateNamedPipeW(name.c_str(),
            (direction==Direction::ToChild?PIPE_ACCESS_OUTBOUND:PIPE_ACCESS_INBOUND)|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,security.attributes()));
        windows_file::require(parent_.get()!=INVALID_HANDLE_VALUE);local_pipe::Pending pending(parent_.get());
        if(::ConnectNamedPipe(parent_.get(),pending.start()))pending.completed();
        else{const auto error=::GetLastError();if(error==ERROR_PIPE_CONNECTED)pending.completed();else if(error!=ERROR_IO_PENDING){pending.completed();windows_file::require(false);}}
        child_=windows_file::Handle(::CreateFileW(name.c_str(),(direction==Direction::ToChild?GENERIC_READ:GENERIC_WRITE)|READ_CONTROL|FILE_READ_ATTRIBUTES,
            0,nullptr,OPEN_EXISTING,SECURITY_SQOS_PRESENT|SECURITY_ANONYMOUS,nullptr));
        windows_file::require(child_.get()!=INVALID_HANDLE_VALUE);
        if(pending.active())(void)pending.wait(std::chrono::steady_clock::now()+std::chrono::seconds(5));
        ULONG client{},server{};
        windows_file::require(::GetNamedPipeClientProcessId(parent_.get(),&client)&&::GetNamedPipeServerProcessId(child_.get(),&server)&&
            client==::GetCurrentProcessId()&&server==::GetCurrentProcessId());
        windows_file::verify_exact_owner(parent_.get(),windows_file::current_sid());
        windows_file::verify_security(parent_.get(),windows_file::current_sid(),true,false);
    }
    HANDLE child()const noexcept{return child_.get();}
    void close_child(){child_=windows_file::Handle();}
    void close_write(){windows_file::require(direction_==Direction::ToChild);parent_=windows_file::Handle();}
    void send(std::span<const std::uint8_t> bytes,local_pipe::Deadline deadline){
        windows_file::require(direction_==Direction::ToChild&&bytes.size()<=144*1024);
        local_pipe::transfer(parent_.get(),const_cast<std::uint8_t *>(bytes.data()),bytes.size(),true,deadline);
    }
    struct Poll {bool eof;std::string bytes;};
    Poll poll(local_pipe::Deadline deadline){
        windows_file::require(direction_==Direction::FromChild);DWORD available{};
        if(!::PeekNamedPipe(parent_.get(),nullptr,0,nullptr,&available,nullptr)){
            windows_file::require(::GetLastError()==ERROR_BROKEN_PIPE);return {true,{}};
        }
        if(!available)return {false,{}};
        return {false,read(deadline)};
    }
    std::string read(local_pipe::Deadline deadline){
        windows_file::require(direction_==Direction::FromChild);std::array<char,8192> buffer{};DWORD count{};local_pipe::Pending pending(parent_.get());
        if(::ReadFile(parent_.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&count,pending.start()))pending.completed();
        else{const auto error=::GetLastError();if(error!=ERROR_IO_PENDING){pending.completed();throw LocalControlError(LocalControlError::Code::Unavailable);}count=pending.wait(deadline);}
        windows_file::require(count>0&&count<=buffer.size());return {buffer.data(),count};
    }
private:
    Direction direction_;windows_file::Handle parent_,child_;
};
}
#endif
