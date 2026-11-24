// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <cstdlib>
#include <thread>
namespace wsprrypi::windows_process {
namespace wf=windows_file;
inline std::mutex launch_mutex;
inline std::wstring quote(const std::wstring &argument){
    wf::require(argument.find(L'\0')==std::wstring::npos);std::wstring result=L"\"";std::size_t slashes=0;
    for(const auto character:argument){
        if(character==L'\\'){++slashes;continue;}
        if(character==L'"'){result.append(2*slashes+1,L'\\');result+=L'"';slashes=0;continue;}
        result.append(slashes,L'\\');slashes=0;result+=character;
    }
    result.append(2*slashes,L'\\');result+=L'"';return result;
}
inline std::wstring system_directory(bool windows){
    std::array<wchar_t,32768> buffer{};
    const auto size=windows?::GetWindowsDirectoryW(buffer.data(),static_cast<UINT>(buffer.size())):
        ::GetSystemDirectoryW(buffer.data(),static_cast<UINT>(buffer.size()));
    wf::require(size>0&&size<buffer.size());return {buffer.data(),size};
}
inline wf::Handle duplicate(HANDLE source){
    wf::require(source&&source!=INVALID_HANDLE_VALUE);HANDLE result{};
    wf::require(::DuplicateHandle(::GetCurrentProcess(),source,::GetCurrentProcess(),&result,0,TRUE,DUPLICATE_SAME_ACCESS));
    return wf::Handle(result);
}
inline wf::Handle private_directory(const std::filesystem::path &path){
    wf::ParentChain parents(path,wf::current_sid());
    wf::Handle handle(::CreateFileW(wf::api_path(path).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),wf::current_sid());wf::verify_security(handle.get(),wf::current_sid(),true,true);return handle;
}
class OwnedChild {
public:
    using Arguments=std::function<std::vector<std::wstring>(const std::vector<HANDLE> &)>;
    OwnedChild(const std::string &program,const std::string &working_directory,
        const std::array<HANDLE,3> &stdio,const std::vector<HANDLE> &extra,const Arguments &arguments){
        const char *stage="arguments";
        try{
        wf::require(bool(arguments)&&extra.size()<=8);const auto executable=wf::path_from_utf8(program),root=wf::path_from_utf8(working_directory);
        stage="program ancestry";
        program_parents_=std::make_unique<wf::ParentChain>(executable,wf::current_sid());
        stage="working ancestry";
        directory_parents_=std::make_unique<wf::ParentChain>(root,wf::current_sid());
        stage="private working directory";directory_=private_directory(root);
        stage="program image";
        program_=wf::Handle(::CreateFileW(wf::api_path(executable).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        wf::require(program_.get()!=INVALID_HANDLE_VALUE);const auto image=wf::inspect(program_.get(),false);
        wf::require(image.standard.EndOfFile.QuadPart>0);wf::verify_security(program_.get(),wf::current_sid(),false,false);
        stage="job policy";job_=wf::Handle(::CreateJobObjectW(nullptr,nullptr));wf::require(job_.get()!=nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        wf::require(::SetInformationJobObject(job_.get(),JobObjectExtendedLimitInformation,&limits,sizeof limits));
        std::lock_guard launch(launch_mutex);
        // Only private duplicates become inheritable, and every launch uses a
        // closed handle list. Job, executable, and state handles never inherit.
        stage="inherited handles";std::vector<wf::Handle> inherited;std::vector<HANDLE> handles,additional;
        for(const auto handle:stdio){inherited.push_back(duplicate(handle));handles.push_back(inherited.back().get());}
        for(const auto handle:extra){inherited.push_back(duplicate(handle));handles.push_back(inherited.back().get());additional.push_back(inherited.back().get());}
        stage="command";auto command=quote(executable.native());const auto values=arguments(additional);wf::require(values.size()<=4096);
        for(const auto &value:values){command+=L' ';command+=quote(value);wf::require(command.size()<32767);}
        stage="environment";const auto windows=system_directory(true),drive=std::filesystem::path(windows).root_name().native();
        wf::require(drive.size()==2&&drive[1]==L':');
        std::vector<std::wstring> environment{
            L"LANG=C",L"PATH="+executable.parent_path().native()+L";"+system_directory(false),
            L"SYSTEMDRIVE="+drive,L"SYSTEMROOT="+windows,L"TEMP="+root.native(),L"TMP="+root.native(),L"WINDIR="+windows};
        std::vector<wchar_t> block;for(const auto &entry:environment){block.insert(block.end(),entry.begin(),entry.end());block.push_back(L'\0');}block.push_back(L'\0');
        stage="handle-list policy";SIZE_T bytes{};(void)::InitializeProcThreadAttributeList(nullptr,1,0,&bytes);wf::require(bytes>0&&bytes<=1048576);
        std::vector<unsigned char> attributes(bytes);auto *list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        wf::require(::InitializeProcThreadAttributeList(list,1,0,&bytes));
        struct Release {LPPROC_THREAD_ATTRIBUTE_LIST list;~Release(){::DeleteProcThreadAttributeList(list);}} release{list};
        wf::require(::UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles.data(),handles.size()*sizeof(HANDLE),nullptr,nullptr));
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=handles[0];startup.StartupInfo.hStdOutput=handles[1];startup.StartupInfo.hStdError=handles[2];startup.lpAttributeList=list;
        stage="process creation";PROCESS_INFORMATION child{};
        wf::require(::CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
            CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,
            block.data(),root.c_str(),&startup.StartupInfo,&child));
        process_=wf::Handle(child.hProcess);wf::Handle thread(child.hThread);
        stage="job assignment";if(!::AssignProcessToJobObject(job_.get(),process_.get())){
            (void)::TerminateProcess(process_.get(),ERROR_OPERATION_ABORTED);
            if(::WaitForSingleObject(process_.get(),5000)!=WAIT_OBJECT_0)fatal();
            throw std::runtime_error("Native recovery child job is unavailable");
        }
        stage="resume";if(::ResumeThread(thread.get())==static_cast<DWORD>(-1)){stop();throw std::runtime_error("Native recovery child cannot start");}
        }catch(...){throw std::runtime_error(std::string("Native recovery child ")+stage+" is unavailable");}
    }
    ~OwnedChild(){stop();}
    OwnedChild(const OwnedChild &)=delete;
    OwnedChild &operator=(const OwnedChild &)=delete;
    bool alive()const {std::lock_guard lock(state_);if(stop_requested_.load()||process_.get()==INVALID_HANDLE_VALUE)return false;
        const auto result=::WaitForSingleObject(process_.get(),0);wf::require(result==WAIT_OBJECT_0||result==WAIT_TIMEOUT);return result==WAIT_TIMEOUT;}
    DWORD id()const {std::lock_guard lock(state_);const auto value=::GetProcessId(process_.get());wf::require(value!=0);return value;}
    DWORD wait(std::chrono::milliseconds timeout){
        wf::require(timeout.count()>0&&timeout.count()<=60000);
        const auto deadline=std::chrono::steady_clock::now()+timeout;wf::Handle observed;
        {
            std::lock_guard lock(state_);wf::require(!stop_requested_.load()&&process_.get()!=INVALID_HANDLE_VALUE);HANDLE copy{};
            wf::require(::DuplicateHandle(::GetCurrentProcess(),process_.get(),::GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS));observed=wf::Handle(copy);
        }
        if(::WaitForSingleObject(observed.get(),static_cast<DWORD>(timeout.count()))!=WAIT_OBJECT_0){stop();throw std::runtime_error("Native recovery child timed out");}
        DWORD code{};
        if(!::GetExitCodeProcess(observed.get(),&code)){stop();throw std::runtime_error("Native recovery child outcome unavailable");}
        // A worker's successful exit cannot hide an outstanding owned child.
        while(true){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION counts{};
            if(stop_requested_.load()){stop();throw std::runtime_error("Native recovery child was cancelled");}
            if(!::QueryInformationJobObject(job_.get(),JobObjectBasicAccountingInformation,&counts,sizeof counts,nullptr)){
                stop();throw std::runtime_error("Native recovery child ownership unavailable");
            }
            if(!counts.ActiveProcesses)return code;
            if(std::chrono::steady_clock::now()>=deadline){stop();throw std::runtime_error("Native recovery descendants did not stop");}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void stop()noexcept{
        std::lock_guard lock(state_);stop_requested_=true;
        if(process_.get()==INVALID_HANDLE_VALUE)return;
        if(!::TerminateJobObject(job_.get(),ERROR_OPERATION_ABORTED))fatal();
        if(::WaitForSingleObject(process_.get(),5000)!=WAIT_OBJECT_0)fatal();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(true){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION counts{};
            if(!::QueryInformationJobObject(job_.get(),JobObjectBasicAccountingInformation,&counts,sizeof counts,nullptr))fatal();
            if(!counts.ActiveProcesses)break;
            if(std::chrono::steady_clock::now()>=deadline)fatal();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        process_=wf::Handle();
    }
private:
    [[noreturn]] static void fatal()noexcept{::TerminateProcess(::GetCurrentProcess(),ERROR_OPERATION_ABORTED);std::abort();}
    mutable std::mutex state_;std::atomic_bool stop_requested_{};
    std::unique_ptr<wf::ParentChain> program_parents_,directory_parents_;
    wf::Handle directory_,program_,job_,process_;
};
}
#endif
