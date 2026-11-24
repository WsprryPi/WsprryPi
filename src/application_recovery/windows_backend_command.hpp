// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_child_pipe.hpp"
#include "native_windows_process.hpp"
#include "native_windows_owners.hpp"
#include "windows_controller_service.hpp"
#include <optional>
namespace wsprrypi::recovery_windows {
// The installed executable and recovery command grammar are fixed. Only the
// bounded existing operation inputs cross this adapter; it is not a launcher.
class BackendCommand {
public:
    struct Poll {bool done;DWORD code;std::string bytes;};
    BackendCommand(const std::string &action,const std::string &job,const std::string &payload)
        :owners_(native_windows_controller_owners()),paths_(windows_program_root()),
         root_(windows_file::path_from_utf8(windows_file::current_sid()==owners_.backend.sid?owners_.backend.home:owners_.maintenance.home)){
        namespace wf=windows_file;namespace wp=windows_process;
        wf::require(wf::current_sid()==owners_.maintenance.sid||wf::current_sid()==owners_.backend.sid);
        service_detail::unprivileged();
        const bool offline=action=="build-candidate"||action=="seal-candidate";
        wf::require(offline||action=="observe"||action=="capture"||action=="validate"||action=="prepare"||action=="review");
        const bool has_job=action!="observe"&&action!="review";
        wf::require(has_job?(job.size()==32&&job.find_first_not_of("0123456789abcdef")==std::string::npos):job.empty());
        wf::require(payload.size()<=8192&&(action=="review"?!payload.empty():payload.empty()));
        wf::require(!offline||wf::current_sid()==owners_.backend.sid);
        deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(offline?1800:12);
        output_=std::make_unique<wp::ChildPipe>(wp::ChildPipe::Direction::FromChild);
        if(!payload.empty())input_=std::make_unique<wp::ChildPipe>(wp::ChildPipe::Direction::ToChild);
        wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));wf::require(null.get()!=INVALID_HANDLE_VALUE);
        const auto utf8=[](const std::filesystem::path &p){const auto value=p.u8string();return std::string(value.begin(),value.end());};
        const auto program=paths_/L"bin"/L"wsprrypi.exe";
        code_parents_=std::make_unique<wf::ParentChain>(program,L"S-1-5-18");
        image_=wf::Handle(::CreateFileW(wf::api_path(program).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(image_.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(image_.get(),false);wf::verify_security(image_.get(),L"S-1-5-18",false,false);
        child_=std::make_unique<wp::OwnedChild>(utf8(program),utf8(root_),std::array<HANDLE,3>{input_?input_->child():null.get(),output_->child(),null.get()},
            std::vector<HANDLE>{},[&](const auto &){
                std::vector<std::wstring> args{L"--recovery-backend",std::wstring(action.begin(),action.end())};
                if(has_job){args.push_back(L"--job-id");args.emplace_back(job.begin(),job.end());}
                args.push_back(L"--runtime-directory");args.push_back((wf::path_from_utf8(owners_.backend.home)/L"Run").native());return args;
            });
        output_->close_child();
        if(input_){input_->close_child();input_->send(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(payload.data()),payload.size()),deadline_);input_.reset();}
    }
    Poll poll(){
        windows_file::require(child_&&std::chrono::steady_clock::now()<deadline_);auto next=output_->poll(deadline_);
        total_+=next.bytes.size();windows_file::require(total_<=32768);
        if(!next.eof)return {false,0,std::move(next.bytes)};
        const auto code=child_->wait(std::chrono::seconds(5));return {true,code,{}};
    }
private:
    NativeWindowsOwners owners_;std::filesystem::path paths_,root_;
    std::chrono::steady_clock::time_point deadline_;std::size_t total_{};
    std::unique_ptr<windows_file::ParentChain> code_parents_;windows_file::Handle image_;
    std::unique_ptr<windows_process::ChildPipe> input_,output_;
    std::unique_ptr<windows_process::OwnedChild> child_;
};
}
#endif
