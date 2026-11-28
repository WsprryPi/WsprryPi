// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "windows_controller_service.hpp"
#include <functional>
#include <mutex>
namespace wsprrypi::recovery_windows {
// SCM hosting only. Installation, service configuration, and credentials are
// outside this boundary. The two callbacks are compiled application entry
// points; neither callback nor an executable is selected by start arguments.
class ControllerServiceHost {
public:
    enum class Role {Backend, Web, Maintenance};
    using Run=std::function<int(const service_detail::Invocation &,HANDLE,const std::function<void()> &)>;
    ControllerServiceHost(Run run,std::function<void()> stop,Role role=Role::Backend)
        :run_(std::move(run)),stop_(std::move(stop)),role_(role){
        namespace wf=windows_file;wf::require(run_&&stop_);
        const auto owners=native_windows_controller_owners();
        const auto &owner=role==Role::Web?owners.web:role==Role::Maintenance?owners.maintenance:owners.backend;
        wf::require(wf::current_sid()==owner.sid);service_detail::unprivileged();
        const auto program=windows_program_root()/L"bin"/L"wsprrypi.exe";std::array<wchar_t,32768> path{};
        const auto length=::GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        wf::require(length&&length<path.size()&&service_detail::equal(std::wstring_view(path.data(),length),program.native()));
        parents_=std::make_unique<wf::ParentChain>(program,L"S-1-5-18");
        image_=wf::Handle(::CreateFileW(wf::api_path(program).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(image_.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(image_.get(),false);wf::verify_security(image_.get(),L"S-1-5-18",false,false);
        stop_event_=wf::Handle(::CreateEventW(nullptr,TRUE,FALSE,nullptr));done_=wf::Handle(::CreateEventW(nullptr,TRUE,FALSE,nullptr));
        wf::require(stop_event_.get()&&done_.get());
    }
    int dispatch(){
        std::lock_guard single(dispatch_mutex_);windows_file::require(active_==nullptr);active_=this;
        struct Reset {~Reset(){active_=nullptr;}} reset;
        SERVICE_TABLE_ENTRYW table[]{{const_cast<wchar_t *>(name()),entry},{nullptr,nullptr}};
        windows_file::require(::StartServiceCtrlDispatcherW(table));return result_;
    }
private:
    inline static std::mutex dispatch_mutex_;inline static ControllerServiceHost *active_{};
    Run run_;std::function<void()> stop_;Role role_;std::unique_ptr<windows_file::ParentChain> parents_;
    windows_file::Handle image_,stop_event_,done_;SERVICE_STATUS_HANDLE status_{};std::mutex status_mutex_;bool stopped_reported_{};int result_=1;
    const wchar_t *name() const noexcept {
        switch(role_){
        case Role::Web:return L"WsprryPiWeb";
        case Role::Maintenance:return L"WsprryPiMaintenance";
        case Role::Backend:return service_detail::name;
        }
        std::terminate();
    }
    void report(DWORD state,DWORD error=NO_ERROR){
        std::lock_guard lock(status_mutex_);if(stopped_reported_)return;if(state==SERVICE_STOPPED)stopped_reported_=true;SERVICE_STATUS value{};value.dwServiceType=SERVICE_WIN32_OWN_PROCESS;value.dwCurrentState=state;
        if(state==SERVICE_RUNNING&&::WaitForSingleObject(stop_event_.get(),0)==WAIT_OBJECT_0){state=SERVICE_STOP_PENDING;value.dwCurrentState=state;}
        value.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;value.dwWin32ExitCode=error;
        value.dwServiceSpecificExitCode=error==ERROR_SERVICE_SPECIFIC_ERROR?1:0;
        value.dwWaitHint=(state==SERVICE_START_PENDING||state==SERVICE_STOP_PENDING)?
            (role_==Role::Maintenance?90000:60000):0;
        value.dwCheckPoint=value.dwWaitHint?1:0;windows_file::require(::SetServiceStatus(status_,&value));
    }
    static DWORD WINAPI control(DWORD code,DWORD,void *,void *context)noexcept{
        auto &self=*static_cast<ControllerServiceHost *>(context);
        if(code==SERVICE_CONTROL_STOP){
            // No application work runs on the SCM handler thread.
            if(!::SetEvent(self.stop_event_.get()))return ERROR_GEN_FAILURE;
            try{self.report(SERVICE_STOP_PENDING);}catch(...){return ERROR_GEN_FAILURE;}
            return NO_ERROR;
        }
        return code==SERVICE_CONTROL_INTERROGATE?NO_ERROR:ERROR_CALL_NOT_IMPLEMENTED;
    }
    static void WINAPI entry(DWORD count,wchar_t **arguments)noexcept{
        if(!active_)return;active_->serve(count,arguments);
    }
    void serve(DWORD count,wchar_t **arguments)noexcept{
        try{
            windows_file::require(count>0&&count<=3&&arguments);std::vector<std::wstring> values;
            for(DWORD index=0;index<count;++index){windows_file::require(arguments[index]);const auto length=::wcsnlen_s(arguments[index],32769);windows_file::require(length<=32768);values.emplace_back(arguments[index],length);}
            service_detail::Invocation invocation{service_detail::Mode::Controller,{}};
            if(role_==Role::Backend)invocation=service_detail::invocation(values);
            else windows_file::require(values.size()==1&&service_detail::equal(values.front(),name()));
            status_=::RegisterServiceCtrlHandlerExW(name(),control,this);windows_file::require(status_!=nullptr);
            report(SERVICE_START_PENDING);
            std::thread monitor([this]{
                const HANDLE events[]{done_.get(),stop_event_.get()};const auto result=::WaitForMultipleObjects(2,events,FALSE,INFINITE);
                if(result==WAIT_OBJECT_0+1){try{stop_();}catch(...){/* Run must observe the stop event as well. */}}
            });
            try{result_=run_(invocation,stop_event_.get(),[this]{report(SERVICE_RUNNING);});}
            catch(...){result_=1;}
            if(!::SetEvent(done_.get()))std::terminate();monitor.join();
            report(SERVICE_STOPPED,result_?ERROR_SERVICE_SPECIFIC_ERROR:NO_ERROR);
        }catch(...){result_=1;if(status_){try{report(SERVICE_STOPPED,ERROR_SERVICE_SPECIFIC_ERROR);}catch(...){}}}
    }
};
}
#endif
