// SPDX-License-Identifier: MIT
#include "worker_process.hpp"
#include "application_control/local_transport.hpp"
#include <array>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <set>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalControlError(LocalControlError::Code::Protocol);}
struct Descriptor {
    int value{-1};
    ~Descriptor(){if(value>=0)::close(value);}
    Descriptor()=default;Descriptor(const Descriptor &)=delete;
    int release(){const auto result=value;value=-1;return result;}
};
void pipe(Descriptor &read,Descriptor &write){
    int pair[2];if(::pipe(pair))throw LocalControlError(LocalControlError::Code::Unavailable);
    // Move all source descriptors above the child's fixed descriptors before
    // adding dup2 actions. Even a caller with closed stdin cannot alias them.
    read.value=::fcntl(pair[0],F_DUPFD_CLOEXEC,20);write.value=::fcntl(pair[1],F_DUPFD_CLOEXEC,20);
    ::close(pair[0]);::close(pair[1]);require(read.value>=0&&write.value>=0);
}
void safe_program(const std::string &path,bool executable){
    require(std::filesystem::path(path).is_absolute());
    const auto canonical=std::filesystem::canonical(path);struct stat info{};
    require(!::stat(canonical.c_str(),&info)&&S_ISREG(info.st_mode)&&!(info.st_mode&0022)&&
        (info.st_uid==0||info.st_uid==::geteuid())&&(!executable||(info.st_mode&0111)));
    auto parent=canonical.parent_path();
    while(!parent.empty()){
        require(!::lstat(parent.c_str(),&info)&&S_ISDIR(info.st_mode)&&
            ((!(info.st_mode&0022)&&(info.st_uid==0||info.st_uid==::geteuid()))||(info.st_uid==0&&(info.st_mode&S_ISVTX))));
        if(parent==parent.root_path())break;
        parent=parent.parent_path();
    }
}
using Deadline=std::chrono::steady_clock::time_point;
void ready(int fd,short events,Deadline deadline){
    while(true){
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        require(remaining>0);pollfd item{fd,events,0};const auto result=::poll(&item,1,static_cast<int>(std::min<std::int64_t>(remaining,5000)));
        if(result<0&&errno==EINTR)continue;
        require(result>0&&!(item.revents&(POLLERR|POLLNVAL))&&(item.revents&(events|POLLHUP)));return;
    }
}
void send_bytes(int fd,const std::string &bytes,Deadline deadline){
#ifdef F_SETNOSIGPIPE
    require(!::fcntl(fd,F_SETNOSIGPIPE,1));
#endif
    // A dead child must poison this request, not terminate the backend broker.
    sigset_t blocked,previous,pending;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
    require(!::pthread_sigmask(SIG_BLOCK,&blocked,&previous));::sigpending(&pending);
    struct RestoreMask {
        sigset_t previous;bool already_pending;
        ~RestoreMask(){
            sigset_t pending;::sigpending(&pending);
            if(!already_pending&&sigismember(&pending,SIGPIPE)){
                sigset_t only;sigemptyset(&only);sigaddset(&only,SIGPIPE);int signal{};::sigwait(&only,&signal);
            }
            ::pthread_sigmask(SIG_SETMASK,&previous,nullptr);
        }
    } restore{previous,bool(sigismember(&pending,SIGPIPE))};
    std::size_t offset{};
    while(offset<bytes.size()){
        ready(fd,POLLOUT,deadline);const auto size=::write(fd,bytes.data()+offset,bytes.size()-offset);
        if(size<0&&(errno==EINTR||errno==EAGAIN))continue;
        require(size>0);offset+=static_cast<std::size_t>(size);
    }
}
Json parse(const std::string &bytes){
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        require(depth<=32);if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(item.get<std::string>()).second);
        else if(event==Json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
        return true;
    });
}
}
struct RecoveryWorkerProcess::Impl {
    std::mutex mutex;int input{-1},output{-1};pid_t child{-1};bool poisoned{};
    ~Impl(){stop();}
    void stop()noexcept{
        if(input>=0){::close(input);input=-1;}if(output>=0){::close(output);output=-1;}
        if(child>0){
            // The private process group contains only this worker and its owned
            // crypto/backend-query children. No application or service PID is used.
            ::kill(-child,SIGKILL);int status{};while(::waitpid(child,&status,0)<0&&errno==EINTR){}child=-1;
        }
    }
};
RecoveryWorkerProcess::RecoveryWorkerProcess(const RecoveryPaths &paths,const std::string &identity,const std::string &role):impl_(std::make_unique<Impl>()){
    require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos&&(role=="pi"||role=="controller"));
    safe_program(paths.python,true);safe_program(paths.worker,false);safe_program(paths.age,true);
    require(!paths.backend_command.empty());safe_program(paths.backend_command.front(),true);
    const auto python=std::filesystem::canonical(paths.python).string(),worker=std::filesystem::canonical(paths.worker).string();
    for(const auto *module:{"__init__.py","archive.py","durable.py","jobs.py","publication.py","evidence.py","lifecycle.py","proxy.py"})
        safe_program((std::filesystem::path(worker).parent_path()/module).string(),false);
    Descriptor child_input,parent_input,parent_output,child_output,child_config,parent_config;
    pipe(child_input,parent_input);pipe(parent_output,child_output);pipe(child_config,parent_config);
    Descriptor null;null.value=::open("/dev/null",O_WRONLY|O_CLOEXEC);require(null.value>=0);
    posix_spawn_file_actions_t actions;require(!::posix_spawn_file_actions_init(&actions));
    struct Actions {posix_spawn_file_actions_t *value;~Actions(){::posix_spawn_file_actions_destroy(value);}} cleanup_actions{&actions};
    require(!::posix_spawn_file_actions_adddup2(&actions,child_input.value,0)&&!::posix_spawn_file_actions_adddup2(&actions,child_output.value,1)&&
        !::posix_spawn_file_actions_adddup2(&actions,null.value,2)&&!::posix_spawn_file_actions_adddup2(&actions,child_config.value,4));
#ifdef __linux__
    require(!::posix_spawn_file_actions_addclosefrom_np(&actions,5));
#endif
    posix_spawnattr_t attributes;require(!::posix_spawnattr_init(&attributes));
    struct Attributes {posix_spawnattr_t *value;~Attributes(){::posix_spawnattr_destroy(value);}} cleanup_attributes{&attributes};
    short flags=POSIX_SPAWN_SETPGROUP;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags|=POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    require(!::posix_spawnattr_setflags(&attributes,flags)&&!::posix_spawnattr_setpgroup(&attributes,0));
    std::array<char *,5> argv{const_cast<char *>(python.c_str()),const_cast<char *>("-I"),const_cast<char *>(worker.c_str()),const_cast<char *>("--serve"),nullptr};
    char path[]="PATH=/usr/bin:/bin",lang[]="LANG=C";char *environment[]{path,lang,nullptr};
    const auto error=::posix_spawn(&impl_->child,python.c_str(),&actions,&attributes,argv.data(),environment);
    if(error){impl_->child=-1;throw LocalControlError(LocalControlError::Code::Unavailable);}
    impl_->input=parent_input.release();impl_->output=parent_output.release();
    require(!::fcntl(impl_->input,F_SETFL,O_NONBLOCK)&&!::fcntl(impl_->output,F_SETFL,O_NONBLOCK));
    auto lifecycle=paths.software_backend_command.empty()?Json{{"kind","systemd"}}:
        Json{{"kind","software_fixture"},{"start_command",paths.software_backend_command}};
    if(!paths.software_worker_fault_phase.empty())lifecycle["crash_after_phase"]=paths.software_worker_fault_phase;
    const auto config=Json{{"lifecycle",lifecycle},{"root",paths.root},{"application_root",paths.application_root},{"backend_command",paths.backend_command},
        {"backend_runtime",paths.backend_runtime},{"age",paths.age},{"identity",identity},{"role",role}}.dump();
    require(config.size()<=32768);std::string frame(4,'\0');
    for(unsigned index=0;index<4;++index)frame[index]=static_cast<char>((config.size()>>(24-index*8))&255);
    frame+=config;require(!::fcntl(parent_config.value,F_SETFL,O_NONBLOCK));send_bytes(parent_config.value,frame,std::chrono::steady_clock::now()+std::chrono::seconds(5));
}
RecoveryWorkerProcess::~RecoveryWorkerProcess()=default;
Json RecoveryWorkerProcess::request(const Json &request){
    std::lock_guard lock(impl_->mutex);if(impl_->poisoned)throw LocalControlError(LocalControlError::Code::UnknownOutcome);
    const auto bytes=request.dump()+"\n";require(bytes.size()<=144*1024);
    try{
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);send_bytes(impl_->input,bytes,deadline);
        std::string response;std::array<char,8192> buffer{};
        while(true){
            ready(impl_->output,POLLIN,deadline);const auto count=::read(impl_->output,buffer.data(),buffer.size());
            if(count<0&&(errno==EINTR||errno==EAGAIN))continue;
            require(count>0&&response.size()+static_cast<std::size_t>(count)<=1024*1024);response.append(buffer.data(),count);
            const auto newline=response.find('\n');if(newline==std::string::npos)continue;
            require(newline==response.size()-1);auto result=parse(response);
            require(result.is_object()&&result.size()==2&&result.contains("ok")&&result["ok"].is_boolean());
            require((result["ok"]==true&&result.contains("value"))||(result["ok"]==false&&result.contains("error")&&result["error"].is_string()));
            return result;
        }
    }catch(...){impl_->poisoned=true;impl_->stop();throw LocalControlError(LocalControlError::Code::UnknownOutcome);}
}
}
