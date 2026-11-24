// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "windows_owner_pipe.hpp"
#include "windows_controller_service.hpp"
#include "native_windows_process.hpp"
#include "native_windows_child_pipe.hpp"
#include "native_windows_arguments.hpp"
#include "worker_protocol.hpp"
#include "application_storage/controller_identity.hpp"
#include "wtp_endpoint/identity.hpp"
namespace wsprrypi::recovery_windows {
class OwnerRelay {
public:
    explicit OwnerRelay(const service_detail::Invocation &invocation){
        namespace wf=windows_file;namespace wp=windows_process;
        const auto owners=native_windows_controller_owners();wf::require(wf::current_sid()==owners.backend.sid);
        const auto program=windows_program_root();const auto home=wf::path_from_utf8(owners.backend.home);
        const auto text=[](const std::filesystem::path &p){const auto value=p.u8string();return std::string(value.begin(),value.end());};
        ControllerIdentityStore identity(owners.backend.home,owners.backend.sid);const auto established=identity.read();wf::require(established.has_value());
        const auto modules=program/L"lib"/L"recovery";
        for(const auto *name:{L"__init__.py",L"archive.py",L"durable.py",L"jobs.py",L"publication.py",L"evidence.py",L"lifecycle.py",L"proxy.py",L"private_acl.py",L"state_owner.py",L"native_files.py",
                             L"windows_service.py",L"windows_backend.py",L"windows_owner_pipe.py",L"windows_state_owner.py",L"state_owner_worker.py"})pin(modules/name);
        pin(program/L"bin"/L"wsprrypi-recovery-native.dll");pin(program/L"runtime"/L"python.exe");pin(program/L"bin"/L"wsprrypi.exe");
        const nlohmann::json bootstrap{{"native_library",text(program/L"bin"/L"wsprrypi-recovery-native.dll")},
            {"owner",{{"sid",windows_argument_utf8(owners.backend.sid)},{"home",owners.backend.home},{"root",text(home/L".wsprrypi-recovery")}}},
            {"application_root",text(home/L"application-state")},{"identity",*established},{"role","controller"},
            {"backend_command",nlohmann::json::array({text(program/L"bin"/L"wsprrypi.exe")})},{"backend_runtime",text(home/L"Run")},
            {"service_pid",::GetCurrentProcessId()},{"mode",invocation.mode==service_detail::Mode::Controller?"controller":"recovery-owner"},
            {"helper_nonce",invocation.mode==service_detail::Mode::Controller?nlohmann::json(nullptr):nlohmann::json(windows_argument_utf8(invocation.nonce))}};
        const auto frame=recovery_worker_protocol::configuration_frame(bootstrap);wp::ChildPipe config(wp::ChildPipe::Direction::ToChild),ready(wp::ChildPipe::Direction::FromChild);
        wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));wf::require(null.get()!=INVALID_HANDLE_VALUE);
        child_=std::make_unique<wp::OwnedChild>(text(program/L"runtime"/L"python.exe"),owners.backend.home,
            std::array<HANDLE,3>{null.get(),ready.child(),null.get()},std::vector<HANDLE>{config.child(),listener_.handle()},[&](const auto &handles){
                wf::require(handles.size()==2);return std::vector<std::wstring>{L"-I",L"-B",L"-X",L"utf8",(modules/L"state_owner_worker.py").native(),
                    L"--windows-service-owner",std::to_wstring(reinterpret_cast<std::uintptr_t>(handles[0])),std::to_wstring(reinterpret_cast<std::uintptr_t>(handles[1]))};
            });
        ready.close_child();config.close_child();config.send(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(frame.data()),frame.size()),std::chrono::steady_clock::now()+std::chrono::seconds(5));
        config.close_write(); // The child validates EOF before reporting ready.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);std::string response;
        while(response.find('\n')==std::string::npos){response+=ready.read(deadline);wf::require(response.size()<=6);}
        wf::require(response=="ready\n"&&child_->alive());
    }
    bool alive()const{return child_&&child_->alive();}
private:
    // The service owns the listener and all descendant lifetime. A helper may
    // never outlive the service or acquire another account's credentials.
    OwnerListener listener_;std::vector<std::unique_ptr<windows_file::ParentChain>> parents_;std::vector<windows_file::Handle> images_;
    std::unique_ptr<windows_process::OwnedChild> child_;
    void pin(const std::filesystem::path &path){
        namespace wf=windows_file;parents_.push_back(std::make_unique<wf::ParentChain>(path,L"S-1-5-18"));
        wf::Handle image(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(image.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(image.get(),false);wf::verify_security(image.get(),L"S-1-5-18",false,false);images_.push_back(std::move(image));
    }
};
}
#endif
