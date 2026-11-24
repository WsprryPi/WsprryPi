// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "worker_process.hpp"
#include "worker_protocol.hpp"
#include "native_windows_process.hpp"
#include "native_windows_child_pipe.hpp"
#include "native_windows_arguments.hpp"
#include "native_windows_owners.hpp"
#include "windows_controller_service.hpp"
#include <mutex>
namespace wsprrypi {
namespace {
namespace wf=windows_file;
namespace wp=windows_process;
using Json=nlohmann::json;
void require(bool valid){recovery_worker_protocol::require(valid);}
std::string text(const std::filesystem::path &path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
}
struct RecoveryWorkerProcess::Impl {
    std::mutex mutex;bool poisoned{};
    std::vector<std::unique_ptr<wf::ParentChain>> parents;
    std::vector<wf::Handle> code;
    std::unique_ptr<wp::ChildPipe> input,output;
    std::unique_ptr<wp::OwnedChild> child;
    ~Impl(){stop();}
    void stop()noexcept{input.reset();output.reset();if(child){child->stop();child.reset();}}
    void safe_program(const std::string &name,const std::wstring &owner){
        const auto path=wf::path_from_utf8(name);parents.push_back(std::make_unique<wf::ParentChain>(path,owner));
        wf::Handle file(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        require(file.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(file.get(),false);wf::verify_security(file.get(),owner,false,false);
        code.push_back(std::move(file));
    }
    Json exchange(const std::string &bytes,local_protocol::Deadline deadline){
        input->send(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size()),deadline);
        std::string response;
        while(true){const auto next=output->read(deadline);require(response.size()+next.size()<=1024*1024);response+=next;
            if(response.find('\n')!=std::string::npos)return recovery_worker_protocol::reply(response);}
    }
};
RecoveryWorkerProcess::RecoveryWorkerProcess(const RecoveryPaths &paths,const std::string &identity,const std::string &role)
    :impl_(std::make_unique<Impl>()){
    require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos&&role=="controller");
    require(!paths.native_library.empty()&&!paths.backend_command.empty());
    const bool software=!paths.software_backend_command.empty();
    Json owner=nullptr,lifecycle;
    std::wstring code_owner=wf::current_sid();
    if(software){
        require(!paths.backend_owner);lifecycle={{"kind","software_fixture"},{"start_command",paths.software_backend_command}};
        if(!paths.software_worker_fault_phase.empty())lifecycle["crash_after_phase"]=paths.software_worker_fault_phase;
    }else{
        const auto owners=native_windows_controller_owners();const auto program=windows_program_root();
        const auto backend_home=wf::path_from_utf8(owners.backend.home),maintenance_home=wf::path_from_utf8(owners.maintenance.home);
        require(wf::current_sid()==owners.maintenance.sid&&paths.maintenance_sid==owners.maintenance.sid&&paths.backend_owner.has_value()&&paths.software_worker_fault_phase.empty());
        recovery_windows::service_detail::unprivileged();
        const auto &backend=*paths.backend_owner;
        require(paths.root==owners.maintenance.home&&paths.runtime==text(maintenance_home/L"Run")&&
            paths.application_root==text(backend_home/L"application-state")&&paths.backend_runtime==text(backend_home/L"Run")&&
            paths.python==text(program/L"runtime"/L"python.exe")&&paths.worker==text(program/L"lib"/L"recovery"/L"worker.py")&&
            paths.age==text(program/L"bin"/L"recovery-age.exe")&&paths.native_library==text(program/L"bin"/L"wsprrypi-recovery-native.dll")&&
            paths.backend_command==std::vector<std::string>{text(program/L"bin"/L"wsprrypi.exe")}&&
            backend.uid==0&&backend.gid==0&&backend.sid==owners.backend.sid&&backend.home==owners.backend.home&&
            backend.root==text(backend_home/L".wsprrypi-recovery")&&backend.worker==text(program/L"lib"/L"recovery"/L"state_owner_worker.py"));
        // SCM validates the one fixed image/account before any worker is owned.
        recovery_windows::ControllerService service;(void)service.query();
        owner={{"sid",windows_argument_utf8(backend.sid)},{"home",backend.home},{"root",backend.root},{"worker",backend.worker}};
        lifecycle={{"kind","windows_service"}};code_owner=L"S-1-5-18";
    }
    impl_->safe_program(paths.python,code_owner);impl_->safe_program(paths.worker,code_owner);impl_->safe_program(paths.age,code_owner);impl_->safe_program(paths.native_library,code_owner);
    impl_->safe_program(paths.backend_command.front(),code_owner);
    const auto modules=wf::path_from_utf8(paths.worker).parent_path();
    for(const auto *module:{L"__init__.py",L"archive.py",L"durable.py",L"jobs.py",L"publication.py",L"evidence.py",L"lifecycle.py",L"proxy.py",L"private_acl.py",L"state_owner.py",L"native_files.py"})
        impl_->safe_program(text(modules/module),code_owner);
    if(!software)for(const auto *module:{L"windows_service.py",L"windows_backend.py",L"windows_owner_pipe.py",L"windows_state_owner.py"})impl_->safe_program(text(modules/module),code_owner);
    const Json configuration{{"native_library",paths.native_library},{"backend_owner",owner},{"lifecycle",lifecycle},{"root",paths.root},{"application_root",paths.application_root},
        {"backend_command",paths.backend_command},{"backend_runtime",paths.backend_runtime},{"age",paths.age},{"identity",identity},{"role",role}};
    const auto frame=recovery_worker_protocol::configuration_frame(configuration);
    impl_->input=std::make_unique<wp::ChildPipe>(wp::ChildPipe::Direction::ToChild);
    impl_->output=std::make_unique<wp::ChildPipe>(wp::ChildPipe::Direction::FromChild);wp::ChildPipe config(wp::ChildPipe::Direction::ToChild);
    wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));require(null.get()!=INVALID_HANDLE_VALUE);
    impl_->child=std::make_unique<wp::OwnedChild>(paths.python,paths.root,std::array<HANDLE,3>{impl_->input->child(),impl_->output->child(),null.get()},
        std::vector<HANDLE>{config.child()},[&](const auto &handles){require(handles.size()==1);return std::vector<std::wstring>{L"-I",L"-B",L"-X",L"utf8",wf::path_from_utf8(paths.worker).native(),
            L"--serve",L"--bootstrap-handle",std::to_wstring(reinterpret_cast<std::uintptr_t>(handles[0]))};});
    impl_->input->close_child();impl_->output->close_child();config.close_child();
    try{
        config.send(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(frame.data()),frame.size()),std::chrono::steady_clock::now()+std::chrono::seconds(5));
        config.close_write();
        recovery_worker_protocol::startup_ready(impl_->exchange(recovery_worker_protocol::request_frame({{"action","list.summary"}}),std::chrono::steady_clock::now()+std::chrono::seconds(75)));
    }catch(...){throw LocalControlError(LocalControlError::Code::Unavailable);}
}
RecoveryWorkerProcess::~RecoveryWorkerProcess()=default;
bool RecoveryWorkerProcess::alive(){
    std::lock_guard lock(impl_->mutex);if(impl_->poisoned||!impl_->child)return false;
    try{if(impl_->child->alive())return true;}catch(...){}
    impl_->poisoned=true;impl_->stop();return false;
}
Json RecoveryWorkerProcess::request(const Json &request){
    std::lock_guard lock(impl_->mutex);if(impl_->poisoned)throw LocalControlError(LocalControlError::Code::UnknownOutcome);
    const auto bytes=recovery_worker_protocol::request_frame(request);
    try{
        return impl_->exchange(bytes,std::chrono::steady_clock::now()+std::chrono::seconds(5));
    }catch(...){impl_->poisoned=true;impl_->stop();throw LocalControlError(LocalControlError::Code::UnknownOutcome);}
}
}
#endif
