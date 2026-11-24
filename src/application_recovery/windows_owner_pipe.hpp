// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "application_control/local_pipe_windows.hpp"
#include "native_windows_owners.hpp"
#include "windows_controller_service.hpp"
namespace wsprrypi::recovery_windows {
// The service creates the listening handle itself before giving a duplicate
// to its owned Python child. Clients bind the kernel-reported server PID to
// SCM, even though the child consumes the bounded owner protocol.
inline LocalTransportOptions owner_options(){
    const auto owners=native_windows_controller_owners();LocalTransportOptions value;
    const auto bytes=(windows_file::path_from_utf8(owners.backend.home)/L"Run").u8string();value.directory={bytes.begin(),bytes.end()};
    value.backend_sid=owners.backend.sid;value.administrator_sid=owners.maintenance.sid;value.allow_elevated_administrators=false;
    value.channel=LocalChannel::Recovery;return value;
}
inline std::wstring owner_pipe_name(const LocalTransportOptions &options){return local_pipe::name(options)+L".owner";}
class OwnerListener {
public:
    OwnerListener():options_(owner_options()),security_(options_.backend_sid,{*options_.administrator_sid}){
        windows_file::require(windows_file::current_sid()==options_.backend_sid);
        pipe_=local_pipe::create(owner_pipe_name(options_),security_,1,true);
        local_pipe::verify(pipe_.get(),options_.backend_sid,{*options_.administrator_sid});
    }
    HANDLE handle()const noexcept{return pipe_.get();}
private:
    LocalTransportOptions options_;local_pipe::Security security_;windows_file::Handle pipe_;
};
class OwnerPipe {
public:
    // The inherited listener still reports the SCM service's PID as creator.
    static std::shared_ptr<OwnerPipe> inherited(HANDLE handle,DWORD service_pid){
        const auto options=owner_options();windows_file::require(windows_file::current_sid()==options.backend_sid&&service_pid>0&&service_pid!=::GetCurrentProcessId());
        HANDLE copy{};windows_file::require(::DuplicateHandle(::GetCurrentProcess(),handle,::GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS));
        windows_file::Handle pipe(copy);verify_pid(pipe.get(),service_pid);local_pipe::verify(pipe.get(),options.backend_sid,{*options.administrator_sid});
        return std::shared_ptr<OwnerPipe>(new OwnerPipe(std::move(pipe),options,true));
    }
    static std::shared_ptr<OwnerPipe> connect(DWORD service_pid){
        const auto options=owner_options();windows_file::require(windows_file::current_sid()==*options.administrator_sid&&service_pid>0);
        ControllerService service;const auto status=service.query();windows_file::require(status.state==SERVICE_RUNNING&&status.pid==service_pid);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        windows_file::Handle pipe;
        while(true){
            pipe=windows_file::Handle(::CreateFileW(owner_pipe_name(options).c_str(),local_pipe::client_rights,0,nullptr,OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
            if(pipe.get()!=INVALID_HANDLE_VALUE)break;
            const auto error=::GetLastError();windows_file::require((error==ERROR_FILE_NOT_FOUND||error==ERROR_PIPE_BUSY)&&std::chrono::steady_clock::now()<deadline);
            const auto current=service.query();windows_file::require(current.state==SERVICE_RUNNING&&current.pid==service_pid);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        verify_pid(pipe.get(),service_pid);
        auto reverse=local_pipe::authenticate_server(pipe.get(),options,deadline);
        // The acknowledgement is retained until the owner greeting is read by
        // the caller. It cannot be discarded by an early disconnect.
        return std::shared_ptr<OwnerPipe>(new OwnerPipe(std::move(pipe),options,false,std::move(reverse)));
    }
    void accept(){
        windows_file::require(server_&&!connected_);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        local_pipe::Pending pending(pipe_.get());local_pipe::begin_connect(pipe_.get(),pending);if(pending.active())pending.wait(deadline);
        connected_=true;const auto peer=local_pipe::authenticate_client(pipe_.get(),options_,deadline,nullptr);
        windows_file::require(peer.peer.sid==*options_.administrator_sid&&!peer.peer.elevated_administrator);
    }
    void disconnect(){
        windows_file::require(server_);
        if(connected_){
            // DisconnectNamedPipe discards unread replies. A successful client
            // closes only after consuming its complete response and files.
            // Wait for that closure without an unbounded FlushFileBuffers.
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(std::chrono::steady_clock::now()<deadline){
                DWORD available{};
                if(!::PeekNamedPipe(pipe_.get(),nullptr,0,nullptr,&available,nullptr)){
                    const auto error=::GetLastError();windows_file::require(error==ERROR_BROKEN_PIPE||error==ERROR_PIPE_NOT_CONNECTED||error==ERROR_NO_DATA);break;
                }
                if(available)break; // No second request is admitted on this exchange.
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        if(!::DisconnectNamedPipe(pipe_.get()))windows_file::require(::GetLastError()==ERROR_PIPE_NOT_CONNECTED);connected_=false;
    }
    void transfer(void *bytes,std::size_t size,bool sending,std::chrono::steady_clock::time_point deadline){
        windows_file::require((!server_||connected_)&&size>0&&size<=65536);
        local_pipe::transfer(pipe_.get(),bytes,size,sending,deadline);
        if(!sending)reverse_=windows_file::Handle();
    }
    bool lost(){DWORD available{};return !::PeekNamedPipe(pipe_.get(),nullptr,0,nullptr,&available,nullptr)||available>0;}
private:
    windows_file::Handle pipe_,reverse_;LocalTransportOptions options_;bool server_{},connected_{};
    OwnerPipe(windows_file::Handle pipe,LocalTransportOptions options,bool server,windows_file::Handle reverse=windows_file::Handle())
        :pipe_(std::move(pipe)),reverse_(std::move(reverse)),options_(std::move(options)),server_(server){}
    static void verify_pid(HANDLE pipe,DWORD expected){ULONG pid{};windows_file::require(::GetNamedPipeServerProcessId(pipe,&pid)&&pid==expected);}
};
inline std::string public_identity(bool start_helper){
    ControllerService service;auto status=service.query();
    if(status.state==SERVICE_STOPPED&&start_helper){const auto nonce=local_protocol::token();status=service.start(ControllerService::Start::RecoveryOwner,std::wstring(nonce.begin(),nonce.end()));}
    windows_file::require(status.state==SERVICE_RUNNING);auto pipe=OwnerPipe::connect(status.pid);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    auto io=[&](void *bytes,std::size_t size,bool sending,auto limit){pipe->transfer(bytes,size,sending,limit);};
    const auto value=local_protocol::receive_frame(io,deadline,1024);const auto owners=native_windows_controller_owners();
    const auto sid=owners.backend.sid;std::string encoded_sid;encoded_sid.reserve(sid.size());
    for(const auto character:sid){windows_file::require(character>0&&character<=127);encoded_sid.push_back(static_cast<char>(character));}
    windows_file::require(value.is_object()&&value.size()==6&&value.contains("schema")&&value["schema"]=="native-recovery-owner/1"&&
        value.contains("service_pid")&&value["service_pid"].is_number_unsigned()&&value["service_pid"]==status.pid&&
        value.contains("sid")&&value["sid"]==encoded_sid&&value.contains("mode")&&value["mode"].is_string()&&
        value.contains("helper_nonce")&&value.contains("identity")&&value["identity"].is_string());
    const auto mode=value["mode"].get<std::string>();
    windows_file::require((mode=="controller"&&value["helper_nonce"].is_null())||(mode=="recovery-owner"&&value["helper_nonce"].is_string()&&local_protocol::identity(value["helper_nonce"].get<std::string>())));
    const auto identity=value["identity"].get<std::string>();windows_file::require(local_protocol::identity(identity));return identity;
}

}
#endif
